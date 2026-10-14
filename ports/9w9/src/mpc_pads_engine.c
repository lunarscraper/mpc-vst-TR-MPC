/* Schwung plugin_api_v2 DSP -> mpc_engine_t, with the MPC drum-pad note remap and STEMS.
 *
 * Same glue as mpc-vst-plugins' adapters/schwung/schwung_engine.c (44.1 kHz, 128-frame int16 blocks, MIDI as source 2),
 * plus: with the MPC OS drum-pad patch (mpc-vst-machinedrum release/mpc_patch) a plugin's drum program sends pad n as
 * MIDI note n-1 (notes 0-15). The kits' own drum-rack maps start at note 36 (pad n = note 35+n in voice order), so notes
 * 0-15 are moved up by 36. Notes 16 and above pass through unchanged, so the melodic layout still works.
 *
 * STEMS (1.2.0): each voice, the reverb and delay returns and the mix as WAV files on the card, from PLAY to STOP
 * (stemrec.h, written for Percolator). The kit's engine writes its voices into a buffer this file hands it
 * (trstem_attach() in the port's *_plugin.c(pp)); everything else (the switch, the take, the files) is here, the same
 * for every kit. The parameters "stems" and "stems_status" are the last two in module.mpc.json, so no VST parameter
 * index moves; STEMS is kept in the project (a tail on the engine's state blob) like a control.
 *
 * One copy of this file lives in ports/common/; each port's src/ holds an identical copy (a port builds in a container
 * that only sees its own folder). tools/sync_common.py refreshes them, and stemrec.h with them.
 */
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "engine.h"
#include "host/plugin_api_v1.h"
#include "stemrec.h"

extern plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host);

/* The port's side of STEMS (its *_plugin.c(pp)): the kit's name, its voices, and the hook into its engine. With a
 * buffer attached, the engine's render writes, for frame i of the block (TRSTEM_FRAMES apart):
 *   row v < voices:  voice v as it goes into the mix (after its drive, level, velocity and choke: post-fader, dry);
 *                    a voice that is silent writes nothing, so the rows are cleared before each block
 *   row voices:      the reverb and delay returns (what they add to the mix, before the master stage)
 *   row voices + 1:  the master compressor's gain on this frame (1 when it is off)
 *   row voices + 2:  the master VOLUME */
#define TRSTEM_FRAMES 128
const char *trstem_kit(void);
int trstem_voices(void);
const char *trstem_voice(int v);        /* short name, in the file name: "BD" */
const char *trstem_voice_info(int v);   /* for take.txt: "bass drum" */
void trstem_attach(void *dsp, float *buf);   /* NULL detaches */

#define MIDI_SOURCE_EXTERNAL 2
#define DRUM_PAD_BASE_NOTE 36
#define DRUM_PAD_COUNT 16

static plugin_api_v2_t *api;

/* The kits read the host transport through Schwung's host_api_v1 (get_clock_status for "is the transport running",
 * get_bpm for the synced delay and the CW-78 preset rhythms). MPC has one transport for every plugin instance and these
 * callbacks take no instance, so the state is process-wide. vst2_wrap.c (vst.json "defines": HAS_TRANSPORT, HAS_LFO_BPM)
 * delivers it as the params "transport" ("1"/"0"; "1" again = the song position jumped back) and "lfo_bpm". */
static volatile int g_playing;
static volatile int g_restart;          /* transport restarted while playing: report "stopped" for one block so the DSP resets its clock */
static volatile float g_bpm = 120.0f;
static host_api_v1_t g_host;

static int host_clock(void) { return (g_playing && !g_restart) ? MOVE_CLOCK_STATUS_RUNNING : MOVE_CLOCK_STATUS_STOPPED; }
static float host_bpm(void) { return g_bpm; }
static double host_beat(void) { return -1.0; }   /* the DSPs fall back to counting samples from the transport start */

/* ---- STEMS ---------------------------------------------------------------------------------------------------- *
 * OFF: nothing recorded, nothing written, the engine untouched. DRY: each voice post-fader, before the master stage
 * (no master distortion, no compressor), times VOLUME. AS HEARD: the same with the compressor's gain on it, so the
 * voices and FX add up to MIX when MASTER DIST is off. A take starts with the first block after PLAY and ends after
 * STOP once the sound has died away (half a second of silence, at most 15 s); PLAY during that tail starts the next
 * one. A voice that played nothing in a take gets no file (the take.txt says so). */
enum { ST_IDLE, ST_REC, ST_TAIL };
#define ST_MAXV 16
#define ST_STATE_TAG " #stems="   /* the tail on the state blob: " #stems=<0..2>" */

typedef struct {
    void *dsp;
    char dir[512];
    int mode;               /* 0 OFF, 1 DRY, 2 AS HEARD */
    int nv;
    stemrec_t rec;
    float *buf;             /* (nv + 3) x TRSTEM_FRAMES: what the engine writes */
    float *frames;          /* TRSTEM_FRAMES x (nv + 2): the block, interleaved, for the recorder */
    int state, attached;
    int tail, quiet;
    char info[ST_MAXV + 2][96];
} inst_t;

static void stems_base(const inst_t *I, char *buf, int len) {
    struct stat st;
    const char *env = getenv("TRSTEM_DIR");   /* offline tests */
    if (env && *env) snprintf(buf, len, "%s", env);
    else if (!stat("/sdcard/Force Documents", &st) && S_ISDIR(st.st_mode)) snprintf(buf, len, "/sdcard/Force Documents/%s Stems", trstem_kit());
    else if (!stat("/sdcard/MPC Documents", &st) && S_ISDIR(st.st_mode)) snprintf(buf, len, "/sdcard/MPC Documents/%s Stems", trstem_kit());
    else snprintf(buf, len, "%.440s/stems", I->dir[0] ? I->dir : ".");
}

/* STEMS set (a touch, a project loading): the recorder starts the first time, and wakes or sleeps (not the audio
 * thread: the wrapper sets parameters from MPC's UI thread) */
static void stems_set(inst_t *I, int mode) {
    I->mode = mode < 0 ? 0 : mode > 2 ? 2 : mode;
    if (I->mode && !I->rec.ok && I->nv > 0) {
        const char *names[ST_MAXV + 2], *info[ST_MAXV + 2];
        int n = I->nv;
        for (int v = 0; v < n; v++) {
            names[v] = trstem_voice(v);
            snprintf(I->info[v], sizeof I->info[v], "%.60s, after its drive and level", trstem_voice_info(v));
            info[v] = I->info[v];
        }
        names[n] = "FX";
        info[n] = "the reverb and delay returns";
        names[n + 1] = "MIX";
        info[n + 1] = "this track's output as heard (master stage, VOLUME), for reference";
        char base[480];
        stems_base(I, base, sizeof base);
        I->rec.drop_silent = 1;
        sr_init(&I->rec, trstem_kit(), n + 2, names, info, base);
    }
    sr_arm(&I->rec, I->mode > 0);
}

static void stems_stop(inst_t *I) {
    sr_stop(&I->rec);
    I->state = ST_IDLE;
}

/* at the start of a block: start, carry on or end the take; 1 = this block is recorded */
static int stems_begin(inst_t *I) {
    stemrec_t *S = &I->rec;
    if (!S->ok) return 0;
    sr_poll(S);
    int want = I->mode > 0 && g_playing;
    if (I->state == ST_REC && !want) {
        if (I->mode > 0) {   /* STOP: keep the tail (decays, echoes) */
            I->state = ST_TAIL;
            I->tail = I->quiet = 0;
        } else stems_stop(I);
    } else if (I->state == ST_TAIL && (want || I->mode == 0)) stems_stop(I);   /* PLAY again: a new take */
    if (I->state == ST_IDLE && want) {
        sr_meta_t m;
        memset(&m, 0, sizeof m);
        m.transport = 1;
        m.bpm = g_bpm;
        m.ppq = -1;   /* the wrapper tells the kits play / stop, not where */
        char md[8];
        int dist = api->get_param(I->dsp, "master_dist", md, sizeof md) > 0 && atoi(md) > 0;
        snprintf(m.mode, sizeof m.mode, "%s%s", I->mode == 1 ? "DRY" : "AS HEARD", dist ? ", MASTER DIST on" : "");
        if (!sr_start(S, &m)) I->state = ST_REC;
    }
    return I->state != ST_IDLE;
}

/* at the end of a block: the tail after STOP ends after half a second of silence, or 15 s */
static void stems_end(inst_t *I, float peak, int frames) {
    if (I->state != ST_TAIL) return;
    I->tail += frames;
    I->quiet = peak < 1e-5f ? I->quiet + frames : 0;   /* -100 dB */
    if (I->quiet >= SR_RATE / 2 || I->tail >= 15 * SR_RATE) stems_stop(I);
}

static int stems_status(const inst_t *I, char *buf, int len) {
    const stemrec_t *S = &I->rec;
    int err = S->ok ? SR_LOAD(S->err) : SR_OK, saved = S->ok ? SR_LOAD(S->saved) : 0;
    if (!I->mode) return snprintf(buf, len, "OFF");
    if (!S->ok) return snprintf(buf, len, "CAN'T START THE RECORDER");
    if (I->state != ST_IDLE) {
        unsigned s = __atomic_load_n(&S->take_frames, __ATOMIC_RELAXED) / SR_RATE;
        if (err == SR_ERR_OPEN) return snprintf(buf, len, "CAN'T WRITE TO THE CARD");
        if (err == SR_ERR_WRITE) return snprintf(buf, len, "CARD FULL? NOT SAVING");
        return snprintf(buf, len, "REC %u:%02u%s", s / 60, s % 60, I->state == ST_TAIL ? "  ENDING" : "");
    }
    if (err == SR_ERR_OPEN) return snprintf(buf, len, "CAN'T WRITE TO THE CARD");
    if (err == SR_ERR_WRITE) return snprintf(buf, len, "CARD FULL? TAKE CUT SHORT");
    if (saved) return snprintf(buf, len, "SAVED %02d:%02d:%02d, %d FILES", (saved - 1) / 10000, (saved - 1) / 100 % 100,
                               (saved - 1) % 100, SR_LOAD(S->kept));
    return snprintf(buf, len, "ARMED: PLAY RECORDS");
}

/* the block the engine just rendered, into the recorder's frames; returns the loudest sample */
static float stems_collect(inst_t *I, const int16_t *out, int frames) {
    const int nv = I->nv, nc = nv + 2, dry = I->mode == 1;
    const float *b = I->buf;
    float peak = 0.0f;
    for (int i = 0; i < frames; i++) {
        const float vol = b[(nv + 2) * TRSTEM_FRAMES + i];
        const float g = dry ? vol : vol * b[(nv + 1) * TRSTEM_FRAMES + i];
        float *f = I->frames + (size_t)i * nc;
        for (int v = 0; v <= nv; v++) f[v] = b[v * TRSTEM_FRAMES + i] * g;   /* the voices, then FX */
        f[nv + 1] = out[2 * i] * (1.0f / 32768.0f);
        for (int k = 0; k < nc; k++) { float a = fabsf(f[k]); if (a > peak) peak = a; }
    }
    return peak;
}

/* ---- the engine ------------------------------------------------------------------------------------------------- */

static void *create(const char *dir) {
    inst_t *I = calloc(1, sizeof *I);
    if (!I) return NULL;
    I->dsp = api->create_instance(dir ? dir : "", NULL);
    if (!I->dsp) { free(I); return NULL; }
    snprintf(I->dir, sizeof I->dir, "%s", dir ? dir : "");
    I->nv = trstem_voices();
    if (I->nv < 1 || I->nv > ST_MAXV) I->nv = 0;
    else {
        I->buf = calloc((size_t)(I->nv + 3) * TRSTEM_FRAMES, sizeof(float));
        I->frames = calloc((size_t)(I->nv + 2) * TRSTEM_FRAMES, sizeof(float));
        if (!I->buf || !I->frames) I->nv = 0;
    }
    return I;
}

static void destroy(void *p) {
    inst_t *I = p;
    if (I->state != ST_IDLE) stems_stop(I);
    sr_free(&I->rec);   /* closes the take: the files are complete */
    trstem_attach(I->dsp, NULL);
    api->destroy_instance(I->dsp);
    free(I->buf);
    free(I->frames);
    free(I);
}

static void midi(void *p, const uint8_t *m, int n) {
    void *i = ((inst_t *)p)->dsp;
    const uint8_t type = m[0] & 0xF0;
    if (n >= 2 && (type == 0x80 || type == 0x90 || type == 0xA0) && m[1] < DRUM_PAD_COUNT) {
        uint8_t t[4] = { m[0], (uint8_t)(m[1] + DRUM_PAD_BASE_NOTE), n > 2 ? m[2] : 0, n > 3 ? m[3] : 0 };
        api->on_midi(i, t, n > 4 ? 4 : n, MIDI_SOURCE_EXTERNAL);
        return;
    }
    api->on_midi(i, m, n, MIDI_SOURCE_EXTERNAL);
}

static void set_param(void *p, const char *k, const char *v) {
    inst_t *I = p;
    if (!strcmp(k, "transport")) {
        int on = v[0] == '1';
        if (on && g_playing) g_restart = 1;
        g_playing = on;
        return;
    }
    if (!strcmp(k, "lfo_bpm")) { float b = (float)atof(v); if (b > 20.0f) g_bpm = b; return; }
    if (!strcmp(k, "stems")) { stems_set(I, atoi(v)); return; }
    if (!strcmp(k, "stems_status")) return;
    if (!strcmp(k, "state")) {   /* the engine's blob, then (since 1.2.0) the STEMS tail; a blob without one leaves STEMS */
        const char *t = strstr(v, ST_STATE_TAG);
        if (t) {
            size_t n = (size_t)(t - v);
            char *s = malloc(n + 1);
            if (s) {
                memcpy(s, v, n);
                s[n] = 0;
                api->set_param(I->dsp, k, s);
                free(s);
                stems_set(I, atoi(t + strlen(ST_STATE_TAG)));
                return;
            }
        }
    }
    api->set_param(I->dsp, k, v);
}

static int get_param(void *p, const char *k, char *b, int n) {
    inst_t *I = p;
    if (!strcmp(k, "stems")) return snprintf(b, n, "%d", I->mode);
    if (!strcmp(k, "stems_status")) return stems_status(I, b, n);
    if (!strcmp(k, "state")) {
        int len = api->get_param(I->dsp, k, b, n);
        if (len <= 0 || len >= n) return len;
        int t = snprintf(b + len, n - len, ST_STATE_TAG "%d", I->mode);
        return t > 0 && len + t < n ? len + t : (b[len] = 0, len);   /* no room: the blob alone */
    }
    return api->get_param(I->dsp, k, b, n);
}

static void render(void *p, int16_t *out, int frames) {
    inst_t *I = p;
    int rec = I->nv > 0 && frames <= TRSTEM_FRAMES && stems_begin(I);
    if (rec != I->attached) { trstem_attach(I->dsp, rec ? I->buf : NULL); I->attached = rec; }
    if (rec) memset(I->buf, 0, sizeof(float) * (size_t)I->nv * TRSTEM_FRAMES);
    api->render_block(I->dsp, out, frames);
    if (rec) {
        float peak = stems_collect(I, out, frames);
        sr_write(&I->rec, I->frames, frames);
        stems_end(I, peak, frames);
    }
    g_restart = 0;
}

static const mpc_engine_t engine = { create, destroy, midi, set_param, get_param, render, NULL };

const mpc_engine_t *mpc_engine(void) {
    if (!api) {
        g_host.api_version = 1;
        g_host.sample_rate = 44100;
        g_host.frames_per_block = 128;
        g_host.get_clock_status = host_clock;
        g_host.get_bpm = host_bpm;
        g_host.get_beat_position = host_beat;
        api = move_plugin_init_v2(&g_host);
    }
    return api ? &engine : NULL;
}
