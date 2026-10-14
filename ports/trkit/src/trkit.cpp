/*
 * TR-MPC: sixteen slots, each playing any voice from any of the four ported kits (6W6, 8W8, CW-78, 9W9).
 *
 * The four engines are the vendored DSP plus a small per-voice tap (ports/trkit/patches, applied by
 * tools/apply_trkit_patches.sh): with a tap set, an engine renders only its voices and hands each voice's
 * post-fader dry sample to this file. So a slot owns its pan and its reverb and delay sends, and ONE reverb,
 * delay and master stage (8W8's own, run through sc808_fx_stereo) serves the whole kit.
 *   - a slot's Level, Tune, Decay and Drive are the voice's own pots in its engine (get/set proxy to the engine);
 *   - Pan, Rev and Dly are the slot's, mixed here: dry goes to L/R by pan, the sends feed the shared FX buses;
 *   - two slots that pick the SAME voice of the same kit share that voice (one engine voice, retriggered);
 *   - an engine that has been silent for a few seconds is not rendered until the next trigger.
 *
 * Pads: MIDI notes 36..51 -> slots 1..16 (MPC drum-pad default, C1 upward), or 0..15 with the drum-pad patch.
 * GPL-3.0, as the kits it links.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <algorithm>
#include <chrono>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif

extern "C" {
#include "engine.h"   /* vendored: mpc-vst-plugins/wrapper/engine.h */
}

#include "trkit_tap.h"
#include "sd606_engine.h"
#include "sc808_engine.h"
#include "cr78_engine.h"
extern "C" {
#include "er99_engine.h"
}

namespace {

trtap::Shared g_tap;   /* see trkit_tap.h; the first Inst to claim g_tap.owner publishes into it */

constexpr int kSlots = 16;
constexpr int kBlock = 128;
constexpr float kSr = 44100.0f;

struct Backend {
    const char *tag;
    int nvoices;
    const char *(*voice_id)(int);
    void *(*create)(const char *dir);
    void (*destroy)(void *);
    void (*trigger)(void *, int voice, int vel);
    void (*render)(void *, float *mono, int n);
    void (*set_tap)(void *, float *tap);   /* nvoices x 128 floats, see the patches */
    int (*set)(void *, const char *key, int pot);
    int (*get)(void *, const char *key, int *pot);
};

/* ---- 6W6 / 8W8 / CW-78 share a C API shape; 9W9 has its own ---- */
#define STRING_API(P, T)                                                                            \
    void *P##_c(const char *) { return P##_create(kSr); }                                           \
    void P##_d(void *e) { P##_destroy((T *)e); }                                                    \
    void P##_t(void *e, int v, int vel) { P##_trigger((T *)e, v, vel); }                            \
    void P##_r(void *e, float *o, int n) { P##_render((T *)e, o, n); }                              \
    void P##_tp(void *e, float *t) { P##_set_tap((T *)e, t); }                                      \
    int P##_s(void *e, const char *k, int p) { char b[16]; snprintf(b, sizeof b, "%d", p); return P##_set_param((T *)e, k, b); } \
    int P##_g(void *e, const char *k, int *p) { char b[32]; if (P##_get_param((T *)e, k, b, sizeof b) <= 0) return 0; *p = atoi(b); return 1; }

STRING_API(sd606, sd606_engine_t)
STRING_API(sc808, sc808_engine_t)
STRING_API(cr78, cr78_engine_t)

const char *kEr99Ids[ER99_NUM_TRIGGERS] = { "bd", "sd", "lt", "mt", "ht", "rs", "hc", "ohh", "chh", "rc", "cr" };
const char *er99_vid(int v) { return (v >= 0 && v < ER99_NUM_TRIGGERS) ? kEr99Ids[v] : ""; }
void *er99_c(const char *dir) {
    er99_engine_t *e = (er99_engine_t *)calloc(1, sizeof(er99_engine_t));
    if (e) er99_engine_init(e, kSr, dir);
    return e;
}
void er99_d(void *e) { if (e) { er99_engine_free((er99_engine_t *)e); free(e); } }
void er99_tp(void *e, float *t) { er99_engine_set_tap((er99_engine_t *)e, t); }
void er99_t(void *e, int v, int vel) { er99_engine_trigger((er99_engine_t *)e, (er99_trigger_t)v, vel); }
void er99_r(void *e, float *o, int n) { er99_engine_render((er99_engine_t *)e, o, n); }
int er99_s(void *e, const char *k, int p) { return er99_engine_set_param((er99_engine_t *)e, k, (float)p); }
int er99_g(void *e, const char *k, int *p) { float f; if (!er99_engine_get_param((er99_engine_t *)e, k, &f)) return 0; *p = (int)lrintf(f); return 1; }

const Backend kBackends[4] = {
    { "606",  SD606_NUM_VOICES, sd606_voice_id, sd606_c, sd606_d, sd606_t, sd606_r, sd606_tp, sd606_s, sd606_g },
    { "808",  SC808_NUM_VOICES, sc808_voice_id, sc808_c, sc808_d, sc808_t, sc808_r, sc808_tp, sc808_s, sc808_g },
    { "CR78", CR78_NUM_VOICES,  cr78_voice_id,  cr78_c,  cr78_d,  cr78_t,  cr78_r,  cr78_tp, cr78_s,  cr78_g },
    { "909",  ER99_NUM_TRIGGERS, er99_vid,      er99_c,  er99_d,  er99_t,  er99_r,  er99_tp, er99_s,  er99_g },
};
constexpr int kNumBackends = 4;
constexpr int kTotalVoices = SD606_NUM_VOICES + SC808_NUM_VOICES + CR78_NUM_VOICES + ER99_NUM_TRIGGERS;

/* flat voice index <-> (backend, voice): backends in the order above, voices in each engine's own order */
void flat_to_bv(int flat, int *b, int *v) {
    for (int i = 0; i < kNumBackends; ++i) {
        if (flat < kBackends[i].nvoices) { *b = i; *v = flat; return; }
        flat -= kBackends[i].nvoices;
    }
    *b = 0; *v = 0;
}

/* which of the slot's knobs forward to which engine key suffix (first key that the engine accepts wins) */
struct Knob { const char *name; const char *suffixes[3]; };
const Knob kKnobs[] = {
    { "level", { "level", "volume", nullptr } },
    { "tune",  { "tune", "pitch", nullptr } },
    { "decay", { "decay", nullptr, nullptr } },
    { "drive", { "drive", nullptr, nullptr } },
    { "dist",  { "dist_type", nullptr, nullptr } },   /* the voice's distortion character, an enum (0..6) */
    /* the voice-specific pots, one control each; a voice without that pot ignores it (the skin only shows the ones it has) */
    { "attack",  { "attack", nullptr, nullptr } },
    { "tone",    { "tone", nullptr, nullptr } },
    { "snappy",  { "snappy", nullptr, nullptr } },
    { "noise",   { "noise", nullptr, nullptr } },
    { "rate",    { "rate", nullptr, nullptr } },
    { "sweep",   { "sweep_depth", nullptr, nullptr } },
    { "pmod",    { "pitch_mod", nullptr, nullptr } },
    { "ndecay",  { "noise_decay", nullptr, nullptr } },
    { "sat",     { "saturation", nullptr, nullptr } },
};
constexpr int kNumKnobs = 14;

/* Default flat voice per slot (808 kick/snare/toms, 606 hats, 909 clap/rim/cymbals, CR78 percussion) */
int default_flat(int slot) {
    static const struct { int b, v; } d[kSlots] = {
        {1, 0}, {1, 1}, {1, 2}, {1, 4}, {0, 4}, {0, 5}, {3, 6}, {3, 5},
        {1, 11}, {1, 12}, {2, 10}, {2, 6}, {0, 6}, {3, 9}, {2, 9}, {1, 7},
    };
    int flat = d[slot].v;
    for (int i = 0; i < d[slot].b; ++i) flat += kBackends[i].nvoices;
    return flat;
}

constexpr int kFxBackend = 1;                 /* 8W8's reverb, delay and master stage serve the whole kit */
constexpr int kSleepAfter = 5 * 44100;        /* samples of silence before an engine or the FX stage stops rendering */
constexpr float kSilence = 1e-5f;

struct Inst {
    char dir[512];
    void *eng[kNumBackends];
    float *tap[kNumBackends];  /* per engine: nvoices x kBlock dry samples */
    int quiet[kNumBackends];   /* samples of near-silence since the engine last sounded */
    int fx_quiet;              /* same for the shared FX stage's output */
    int slot_flat[kSlots];
    int edit_slot;             /* 0..15: the slot the editor page's e_* controls (and edit_voice) act on */
    int rnd_sel[kSlots];       /* the randomise module: which slots it acts on, how far, and whether it may change the voice */
    int rnd_amount, rnd_voice;
    int int_rev, int_dly, int_comp;   /* the kit's own reverb / delay / compressor on (1) or off (0: the sends then only leave through a tap) */
    int voice_owner[kNumBackends][32];   /* per engine voice: the slot it was last triggered from; only that slot gets its sound */
    int comp_saved;                   /* the Comp value while int_comp is off (the engine's own is set to 0) */
    uint32_t rng;
    int pan[kSlots], rev[kSlots], dly[kSlots];   /* 0..127; pan 64 = centre */
    bool tap_owner;            /* this Inst publishes the slot planes for the Tap plugins */
    int16_t pending[kBlock * 2];   /* the owner outputs one block behind what it publishes, so a tap called before it in a period still finds its block */
    bool have_pending;
};

void *engine_for(Inst *in, int b) {
    if (!in->eng[b]) {
        in->eng[b] = kBackends[b].create(in->dir);
        in->tap[b] = (float *)calloc((size_t)kBackends[b].nvoices * kBlock, sizeof(float));
        if (in->eng[b] && in->tap[b]) kBackends[b].set_tap(in->eng[b], in->tap[b]);
    }
    return in->eng[b];
}

/* try "<id>_<suffix>", then "<id>_c_<suffix>" (9W9 kick/snare/toms) */
bool forward(Inst *in, int slot, const Knob &k, int *val, bool set) {
    int b, v;
    flat_to_bv(in->slot_flat[slot], &b, &v);
    void *e = engine_for(in, b);
    if (!e) return false;
    const char *id = kBackends[b].voice_id(v);
    const char *const *sfx = k.suffixes;
    char key[48];
    for (int s = 0; s < 3 && sfx[s]; ++s) {
        const char *infix[2] = { "", "c_" };
        for (int c = 0; c < 2; ++c) {
            snprintf(key, sizeof key, "%s_%s%s", id, infix[c], sfx[s]);
            if (set ? kBackends[b].set(e, key, *val) : kBackends[b].get(e, key, val)) return true;
        }
    }
    return false;
}

void *create(const char *dir) {
    Inst *in = (Inst *)calloc(1, sizeof(Inst));
    if (!in) return nullptr;
    snprintf(in->dir, sizeof in->dir, "%s", dir ? dir : "");
    for (int s = 0; s < kSlots; ++s) in->slot_flat[s] = default_flat(s);
    for (int s = 0; s < kSlots; ++s) { in->pan[s] = 64; in->rev[s] = 0; in->dly[s] = 0; }
    in->int_rev = in->int_dly = in->int_comp = 1;
    in->rnd_amount = 64; in->rng = 0x9e3779b9u ^ (uint32_t)(uintptr_t)in;
    { const void *none = nullptr; in->tap_owner = g_tap.owner.compare_exchange_strong(none, in); }
    engine_for(in, kFxBackend);
    for (int s = 0; s < kSlots; ++s) { int b, v; flat_to_bv(in->slot_flat[s], &b, &v); engine_for(in, b); }
    return in;
}

void destroy(void *p) {
    Inst *in = (Inst *)p;
    if (!in) return;
    { const void *me = in; g_tap.owner.compare_exchange_strong(me, nullptr); }
    for (int b = 0; b < kNumBackends; ++b) { if (in->eng[b]) kBackends[b].destroy(in->eng[b]); free(in->tap[b]); }
    free(in);
}

void midi(void *p, const uint8_t *m, int len) {
    Inst *in = (Inst *)p;
    if (len < 3 || (m[0] & 0xF0) != 0x90 || m[2] == 0) return;   /* one-shots: note-offs ignored */
    /* notes 36-51, or 0-15 (what the MPC OS drum-pad patch sends: pad n = note n-1) */
    const int slot = m[1] < kSlots ? (int)m[1] : (int)m[1] - 36;
    if (slot < 0 || slot >= kSlots) return;
    int b, v;
    flat_to_bv(in->slot_flat[slot], &b, &v);
    if (void *e = engine_for(in, b)) { in->voice_owner[b][v] = slot; kBackends[b].trigger(e, v, m[2]); in->quiet[b] = 0; }
}

/* keys: sNN_src (flat voice index), sNN_level/tune/decay/drive (the voice's pots), sNN_pan/rev/dly (the slot's),
 * fx_<key> (8W8's reverb, delay and master keys, applied to the shared stage) */
bool parse_slot(const char *key, int *slot, const char **rest) {
    if (key[0] != 's' || key[1] < '0' || key[1] > '9' || key[2] < '0' || key[2] > '9' || key[3] != '_') return false;
    *slot = (key[1] - '0') * 10 + (key[2] - '0') - 1;
    *rest = key + 4;
    return *slot >= 0 && *slot < kSlots;
}

int *owned(Inst *in, int slot, const char *rest) {
    if (!strcmp(rest, "pan")) return &in->pan[slot];
    if (!strcmp(rest, "rev")) return &in->rev[slot];
    if (!strcmp(rest, "dly")) return &in->dly[slot];
    return nullptr;
}

void set_state(Inst *in, const char *st);

uint32_t next_rand(Inst *in) {   /* xorshift32 */
    uint32_t x = in->rng ? in->rng : 0x1234567u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return in->rng = x;
}
float unit(Inst *in) { return (next_rand(in) >> 8) * (1.0f / 16777216.0f); }   /* 0..1 */

/* Randomise the selected slots around where they are: each control moves by up to its range x the amount (0..127),
 * Level is left alone, Drive and the sends stay on the quiet side. With the voice option on, a slot also swaps to another voice (of any kit). */
void randomise(Inst *in, bool voices_only = false) {
    const float amt = in->rnd_amount / 127.0f;
    for (int sl = 0; sl < kSlots; ++sl) {
        if (!in->rnd_sel[sl]) continue;
        if (in->rnd_voice || voices_only) {   /* any voice of any kit (the slot then reskins to that kit), never the one it already has */
            int nv = (int)(unit(in) * kTotalVoices) % kTotalVoices;
            if (nv == in->slot_flat[sl]) nv = (nv + 1 + (int)(unit(in) * (kTotalVoices - 1)) % (kTotalVoices - 1)) % kTotalVoices;
            in->slot_flat[sl] = nv;
            int b, v;
            flat_to_bv(nv, &b, &v);
            engine_for(in, b);
        }
        if (voices_only) continue;
        auto wander = [&](const char *name, float range, float lo, float hi, float bias) {
            for (int i = 0; i < kNumKnobs; ++i) {
                if (strcmp(kKnobs[i].name, name)) continue;
                int cur = 0;
                if (!forward(in, sl, kKnobs[i], &cur, false)) return;   /* the voice has no such control */
                float nv = cur + (unit(in) * 2.0f - 1.0f) * range * amt + bias * amt;
                nv = nv < lo ? lo : (nv > hi ? hi : nv);
                int iv = (int)(nv + 0.5f);
                forward(in, sl, kKnobs[i], &iv, true);
            }
        };
        wander("tune", 50, 0, 127, 0); wander("decay", 60, 0, 127, 0); wander("drive", 70, 0, 100, 10);
        wander("attack", 60, 0, 127, 0); wander("tone", 60, 0, 127, 0); wander("snappy", 60, 0, 127, 0);
        wander("noise", 60, 0, 127, 0); wander("rate", 60, 0, 127, 0); wander("sweep", 60, 0, 127, 0);
        wander("pmod", 60, 0, 127, 0); wander("ndecay", 60, 0, 127, 0); wander("sat", 60, 0, 127, 0);
        if (amt > 0.25f) {   /* a different distortion character */
            int d = (int)(unit(in) * 7) % 7;
            for (int i = 0; i < kNumKnobs; ++i) if (!strcmp(kKnobs[i].name, "dist")) forward(in, sl, kKnobs[i], &d, true);
        }
        auto nudge = [&](int &val, float range, float lo, float hi) {
            float nv = val + (unit(in) * 2.0f - 1.0f) * range * amt;
            val = (int)((nv < lo ? lo : (nv > hi ? hi : nv)) + 0.5f);
        };
        nudge(in->pan[sl], 64, 0, 127); nudge(in->rev[sl], 45, 0, 100); nudge(in->dly[sl], 45, 0, 100);
    }
}

/* The editor page's controls act on the edit slot: edit_voice -> sNN_src, e_<knob> -> sNN_<knob> */
bool editor_key(const Inst *in, const char *key, char *out, size_t n) {
    if (!strcmp(key, "edit_voice")) { snprintf(out, n, "s%02d_src", in->edit_slot + 1); return true; }
    if (key[0] == 'e' && key[1] == '_') { snprintf(out, n, "s%02d_%s", in->edit_slot + 1, key + 2); return true; }
    return false;
}

void set_param(void *p, const char *key, const char *val) {
    Inst *in = (Inst *)p;
    int slot; const char *rest;
    const int x = atoi(val);
    if (!strcmp(key, "state")) { set_state(in, val); return; }
    if (!strcmp(key, "edit_slot")) { in->edit_slot = x < 0 ? 0 : (x >= kSlots ? kSlots - 1 : x); return; }
    if (!strncmp(key, "rnd_", 4)) {
        if (!strcmp(key, "rnd_go")) { if (x) randomise(in); }
        else if (!strcmp(key, "rnd_go_voice")) { if (x) randomise(in, true); }
        else if (!strcmp(key, "rnd_all") || !strcmp(key, "rnd_none")) { if (x) for (int sl = 0; sl < kSlots; ++sl) in->rnd_sel[sl] = key[4] == 'a'; }
        else if (!strcmp(key, "rnd_amount")) in->rnd_amount = x < 0 ? 0 : (x > 127 ? 127 : x);
        else if (!strcmp(key, "rnd_voice")) in->rnd_voice = x != 0;
        else if (key[4] == 's' && key[5] >= '0' && key[5] <= '9' && key[6] >= '0' && key[6] <= '9') {
            const int sl = (key[5] - '0') * 10 + (key[6] - '0') - 1;
            if (sl >= 0 && sl < kSlots) in->rnd_sel[sl] = x != 0;
        }
        return;
    }
    if (!strncmp(key, "int_", 4)) {
        const int on = x != 0;
        if (!strcmp(key, "int_rev")) in->int_rev = on;
        else if (!strcmp(key, "int_dly")) in->int_dly = on;
        else if (!strcmp(key, "int_comp") && on != in->int_comp) {
            in->int_comp = on;
            void *e = in->eng[kFxBackend];
            if (!on) { int v = 0; kBackends[kFxBackend].get(e, "comp", &v); in->comp_saved = v; kBackends[kFxBackend].set(e, "comp", 0); }
            else kBackends[kFxBackend].set(e, "comp", in->comp_saved);
        }
        return;
    }
    char ek[24];
    if (editor_key(in, key, ek, sizeof ek)) { set_param(p, ek, val); return; }
    if (parse_slot(key, &slot, &rest)) {
        if (!strcmp(rest, "fam")) return;   /* derived from the voice; the skin only reads it */
        if (!strcmp(rest, "src")) {
            if (x >= 0 && x < kTotalVoices) { in->slot_flat[slot] = x; int b, v; flat_to_bv(x, &b, &v); engine_for(in, b); }
            return;
        }
        if (int *o = owned(in, slot, rest)) { *o = x < 0 ? 0 : (x > 127 ? 127 : x); return; }
        for (int i = 0; i < kNumKnobs; ++i)
            if (!strcmp(rest, kKnobs[i].name)) { int v = x; forward(in, slot, kKnobs[i], &v, true); return; }
    } else if (!strncmp(key, "fx_", 3)) {
        if (!in->int_comp && !strcmp(key + 3, "comp")) { in->comp_saved = x; return; }   /* held until the compressor is switched back on */
        kBackends[kFxBackend].set(in->eng[kFxBackend], key + 3, x);
    } else if (!strcmp(key, "lfo_bpm")) {   /* host tempo, for the synced delay */
        sc808_set_param((sc808_engine_t *)in->eng[kFxBackend], "dly_bpm", val);
    }
}

const char *const kFxKeys[] = { "rev_decay", "rev_tone", "rev_hpf", "rev_level", "dly_time", "dly_fdbk", "dly_tone",
                                "dly_hpf", "dly_level", "master_dist", "master_drive", "comp", "volume" };
const char *const kSlotKeys[] = { "src", "level", "tune", "decay", "drive", "dist", "attack", "tone", "snappy", "noise", "rate", "sweep", "pmod", "ndecay", "sat", "pan", "rev", "dly" };

int get_param(void *p, const char *key, char *buf, int n);

/* The project chunk (the wrapper stores whatever "state" returns): every slot and FX value as key=value; pairs,
 * slot keys first within a slot so that src is applied before the voice's own pots. */
int get_state(Inst *in, char *buf, int n) {
    int len = snprintf(buf, n, "trmpc1;edit_slot=%d;rnd_amount=%d;rnd_voice=%d;", in->edit_slot, in->rnd_amount, in->rnd_voice);
    for (int sl = 0; sl < kSlots; ++sl) len += snprintf(buf + len, n - len, "rnd_s%02d=%d;", sl + 1, in->rnd_sel[sl]);
    char k[24], v[16];
    for (int s = 1; s <= kSlots; ++s)
        for (const char *sk : kSlotKeys) {
            snprintf(k, sizeof k, "s%02d_%s", s, sk);
            if (get_param(in, k, v, sizeof v) < 0) continue;
            len += snprintf(buf + len, n - len, "%s=%s;", k, v);
            if (len >= n - 32) return len;
        }
    for (const char *fk : kFxKeys) {
        snprintf(k, sizeof k, "fx_%s", fk);
        if (get_param(in, k, v, sizeof v) < 0) continue;
        len += snprintf(buf + len, n - len, "%s=%s;", k, v);
        if (len >= n - 32) break;
    }
    /* after the fx values: switching the compressor off saves the Comp value set above */
    len += snprintf(buf + len, n - len, "int_rev=%d;int_dly=%d;int_comp=%d;", in->int_rev, in->int_dly, in->int_comp);
    return len;
}

void set_param(void *p, const char *key, const char *val);

void set_state(Inst *in, const char *st) {
    if (strncmp(st, "trmpc1;", 7)) return;
    st += 7;
    char k[24], v[16];
    while (*st) {
        const char *eq = strchr(st, '=');
        const char *end = strchr(st, ';');
        if (!eq || !end || eq > end) break;
        snprintf(k, sizeof k, "%.*s", (int)(eq - st) < 23 ? (int)(eq - st) : 23, st);
        snprintf(v, sizeof v, "%.*s", (int)(end - eq - 1) < 15 ? (int)(end - eq - 1) : 15, eq + 1);
        set_param(in, k, v);
        st = end + 1;
    }
}

int get_param(void *p, const char *key, char *buf, int n) {
    Inst *in = (Inst *)p;
    int slot; const char *rest;
    if (!strcmp(key, "state")) return get_state(in, buf, n);
    if (!strcmp(key, "edit_slot")) return snprintf(buf, n, "%d", in->edit_slot);
    if (!strncmp(key, "rnd_", 4)) {
        if (!strcmp(key, "rnd_go") || !strcmp(key, "rnd_go_voice") || !strcmp(key, "rnd_all") || !strcmp(key, "rnd_none")) return snprintf(buf, n, "0");
        if (!strcmp(key, "rnd_amount")) return snprintf(buf, n, "%d", in->rnd_amount);
        if (!strcmp(key, "rnd_voice")) return snprintf(buf, n, "%d", in->rnd_voice);
        if (key[4] == 's' && key[5] >= '0' && key[5] <= '9' && key[6] >= '0' && key[6] <= '9') {
            const int sl = (key[5] - '0') * 10 + (key[6] - '0') - 1;
            if (sl >= 0 && sl < kSlots) return snprintf(buf, n, "%d", in->rnd_sel[sl]);
        }
        return -1;
    }
    if (!strcmp(key, "int_rev")) return snprintf(buf, n, "%d", in->int_rev);
    if (!strcmp(key, "int_dly")) return snprintf(buf, n, "%d", in->int_dly);
    if (!strcmp(key, "int_comp")) return snprintf(buf, n, "%d", in->int_comp);
    char ek[24];
    if (editor_key(in, key, ek, sizeof ek)) return get_param(p, ek, buf, n);
    if (parse_slot(key, &slot, &rest)) {
        if (!strcmp(rest, "src")) return snprintf(buf, n, "%d", in->slot_flat[slot]);
        if (!strcmp(rest, "fam")) { int b, v; flat_to_bv(in->slot_flat[slot], &b, &v); return snprintf(buf, n, "%d", (b == 3 && v == 0) ? 4 : b); }   /* read-only: the voice's kit; the 909 kick (three extra controls) is 4 */
        if (int *o = owned(in, slot, rest)) return snprintf(buf, n, "%d", *o);
        for (int i = 0; i < kNumKnobs; ++i)
            if (!strcmp(rest, kKnobs[i].name)) { int v = 0; if (!forward(in, slot, kKnobs[i], &v, false)) v = 0; return snprintf(buf, n, "%d", v); }
    } else if (!strncmp(key, "fx_", 3)) {
        if (!in->int_comp && !strcmp(key + 3, "comp")) return snprintf(buf, n, "%d", in->comp_saved);
        int v = 0;
        if (kBackends[kFxBackend].get(in->eng[kFxBackend], key + 3, &v)) return snprintf(buf, n, "%d", v);
    }
    return -1;
}

inline float peak_of(const float *x, int n, float pk) {
    for (int i = 0; i < n; ++i) { const float a = x[i] < 0 ? -x[i] : x[i]; if (a > pk) pk = a; }
    return pk;
}

/* denormals are slow on the Force's ARM core and the reverb and delay tails decay into them */
inline void flush_denormals() {
#if defined(__arm__)
    unsigned fpscr;
    asm volatile("vmrs %0, fpscr" : "=r"(fpscr));
    fpscr |= 1u << 24;   /* FZ */
    asm volatile("vmsr fpscr, %0" : : "r"(fpscr));
#elif defined(__aarch64__)
    unsigned long fpcr;
    asm volatile("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= 1ul << 24;   /* FZ */
    asm volatile("msr fpcr, %0" : : "r"(fpcr));
#elif defined(__SSE__)
    _mm_setcsr(_mm_getcsr() | 0x8040);
#endif
}

/* Output limiter: sixteen pads add up (a kick, a snare and a clap together exceed full scale), and a hard clip is what
 * crackles. Below kKnee the signal is untouched; above it the peak is rounded off toward 1.0 (the slope is 1 at the
 * knee and it never exceeds full scale). */
constexpr float kKnee = 0.5f;
inline float soft_limit(float x) {
    const float a = x < 0 ? -x : x;
    if (a <= kKnee) return x;
    const float u = (a - kKnee) * (1.0f / (1.0f - kKnee));
    const float y = kKnee + (1.0f - kKnee) * (u / (1.0f + u));
    return x < 0 ? -y : y;
}

void render(void *p, int16_t *out, int frames) {
    Inst *in = (Inst *)p;
    flush_denormals();
    static thread_local float scratch[kBlock], dl[kBlock], dr[kBlock], sr[kBlock], sd[kBlock], ol[kBlock], orr[kBlock];
    for (int done = 0; done < frames; done += kBlock) {
        const int n = frames - done < kBlock ? frames - done : kBlock;
        memset(dl, 0, sizeof(float) * n); memset(dr, 0, sizeof(float) * n);
        memset(sr, 0, sizeof(float) * n); memset(sd, 0, sizeof(float) * n);
        const bool pub = in->tap_owner && n == kBlock;
        const uint32_t wr = g_tap.written.load(std::memory_order_relaxed);
        int16_t (*planes)[trtap::kFrames] = g_tap.data[wr % trtap::kRing];
        /* the planes are only filled while some tap reads something (the latency scheme runs regardless) */
        bool tapping = false;
        if (pub) {
            tapping = g_tap.tapsRev.load(std::memory_order_relaxed) > 0 || g_tap.tapsDel.load(std::memory_order_relaxed) > 0;
            for (int s = 0; s < kSlots && !tapping; ++s) tapping = g_tap.tapped[s].load(std::memory_order_relaxed) > 0;
            if (tapping) memset(planes, 0, sizeof(g_tap.data[0]));
        }
        auto q = [](float x) { x *= 32767.0f; return (int16_t)(x > 32767.0f ? 32767.0f : (x < -32768.0f ? -32768.0f : x)); };
        bool awake = false;
        for (int b = 0; b < kNumBackends; ++b) {
            if (!in->eng[b] || !in->tap[b] || in->quiet[b] >= kSleepAfter) continue;
            awake = true;
            memset(in->tap[b], 0, sizeof(float) * kBlock * kBackends[b].nvoices);
            kBackends[b].render(in->eng[b], scratch, n);
            float pk = 0.0f;
            for (int s = 0; s < kSlots; ++s) {
                int sb, v;
                flat_to_bv(in->slot_flat[s], &sb, &v);
                if (sb != b || in->voice_owner[b][v] != s) continue;   /* two slots on one voice: the one that was hit */
                const float *src = in->tap[b] + v * kBlock;
                pk = peak_of(src, n, pk);
                /* constant-power pan, unity at the centre */
                const float ang = (float)in->pan[s] * (1.5707963f / 127.0f);
                const float gl = cosf(ang) * 1.41421356f, gr = sinf(ang) * 1.41421356f;
                const float rv = (float)in->rev[s] * (1.0f / 127.0f), dy = (float)in->dly[s] * (1.0f / 127.0f);
                const bool tapped = tapping && g_tap.tapped[s].load(std::memory_order_relaxed) > 0;   /* its dry goes out through a tap, not the main mix */
                for (int i = 0; i < n; ++i) {
                    const float x = src[i];
                    if (tapping) { planes[2 * s][i] = q(x * gl); planes[2 * s + 1][i] = q(x * gr); }
                    if (!tapped) { dl[i] += x * gl; dr[i] += x * gr; }
                    sr[i] += x * rv; sd[i] += x * dy;
                }
            }
            in->quiet[b] = pk > kSilence ? 0 : in->quiet[b] + n;
        }
        if (tapping) {
            for (int i = 0; i < n; ++i) { planes[trtap::kPlaneRev][i] = q(sr[i]); planes[trtap::kPlaneDel][i] = q(sd[i]); }
            /* a send that leaves through a tap no longer feeds the kit's own reverb / delay */
            if (g_tap.tapsRev.load(std::memory_order_relaxed) > 0) memset(sr, 0, sizeof(float) * n);
            if (g_tap.tapsDel.load(std::memory_order_relaxed) > 0) memset(sd, 0, sizeof(float) * n);
        }
        if (!in->int_rev) memset(sr, 0, sizeof(float) * n);
        if (!in->int_dly) memset(sd, 0, sizeof(float) * n);
        if (awake || in->fx_quiet < kSleepAfter) {
            sc808_fx_stereo((sc808_engine_t *)in->eng[kFxBackend], dl, dr, sr, sd, ol, orr, n);
            const float pk = peak_of(orr, n, peak_of(ol, n, 0.0f));
            in->fx_quiet = (awake || pk > kSilence) ? 0 : in->fx_quiet + n;
        } else {
            memset(ol, 0, sizeof(float) * n); memset(orr, 0, sizeof(float) * n);
        }
        int16_t *dst = out + done * 2;
        int16_t cur[kBlock * 2];
        for (int i = 0; i < n; ++i) {
            cur[2 * i] = q(soft_limit(ol[i]));
            cur[2 * i + 1] = q(soft_limit(orr[i]));
        }
        if (pub) {
            g_tap.written.store(wr + 1, std::memory_order_release);
            /* one block of latency: output the block published last time, keep this one */
            if (in->have_pending) memcpy(dst, in->pending, sizeof(int16_t) * n * 2); else memset(dst, 0, sizeof(int16_t) * n * 2);
            memcpy(in->pending, cur, sizeof(int16_t) * n * 2);
            in->have_pending = true;
            g_tap.hostRead.store(wr, std::memory_order_release);   /* blocks the host has taken (the one just output is wr - 1) */
        } else {
            memcpy(dst, cur, sizeof(int16_t) * n * 2);
        }
    }
    if (in->tap_owner)
        g_tap.hostCallUs.store(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(), std::memory_order_release);
}

const mpc_engine_t kEngine = { create, destroy, midi, set_param, get_param, render, nullptr };

}  // namespace

extern "C" const mpc_engine_t *mpc_engine(void) { return &kEngine; }
extern "C" __attribute__((visibility("default"))) trtap::Shared *trmpc_tap_shared(void) { return &g_tap; }
