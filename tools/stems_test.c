/* STEMS, offline checks for one kit (6W6, 8W8, CW-78, 9W9), against the kit's engine and ports/common/mpc_pads_engine.c,
 * without the VST wrapper. Built and run by tools/test_stems.sh <kit>. Prints PASSED or the failures.
 *
 *   stems_test <out dir> <module dir> <a send key> <the compressor's key>
 *
 * Checks: the output is the same, bit for bit, with STEMS OFF, DRY and AS HEARD; PLAY ... STOP makes one take with a
 * file per voice played (none for a silent one), FX, MIX and take.txt, every file the same length; AS HEARD, the voices
 * and FX add up to MIX; the screen's status; STOP keeps the tail; STEMS OFF ends a take at once; STEMS rides on the
 * state blob and a blob without it leaves STEMS as it is. */
#include <dirent.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "engine.h"

static const mpc_engine_t *E;
static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void set(void *I, const char *k, const char *v) { E->set_param(I, k, v); }
static void status(void *I, char *b) { b[0] = 0; E->get_param(I, "stems_status", b, 96); }

/* a groove: every voice note (36 up) in turn, one every 2048 frames, velocities 70..127 */
#define BLOCK 128
static int16_t *render(void *I, int blocks, int *note_i) {
    int16_t *o = malloc(sizeof(int16_t) * 2 * BLOCK * blocks);
    for (int b = 0; b < blocks; b++) {
        if (note_i && (b % 16) == 0) {
            int n = (*note_i)++;
            uint8_t m[3] = {0x90, (uint8_t)(36 + n % 16), (uint8_t)(70 + (n * 13) % 58)};
            E->midi(I, m, 3);
        }
        E->render(I, o + 2 * BLOCK * b, BLOCK);
    }
    return o;
}

/* the stems' take folders under dir (one level), newest last */
static int takes(const char *dir, char out[][700], int max) {
    DIR *d = opendir(dir);
    int n = 0;
    if (!d) return 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < max)
        if (e->d_name[0] != '.') snprintf(out[n++], 700, "%s/%s", dir, e->d_name);
    closedir(d);
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (strcmp(out[i], out[j]) > 0) { char t[700]; strcpy(t, out[i]); strcpy(out[i], out[j]); strcpy(out[j], t); }
    return n;
}

typedef struct { char name[300]; float *x; long n; } wav_t;

static int read_wav(const char *path, wav_t *w) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    unsigned char h[58];
    if (fread(h, 1, 58, f) != 58 || memcmp(h, "RIFF", 4) || memcmp(h + 50, "data", 4) || h[20] != 3 || h[22] != 1) { fclose(f); return -1; }
    uint32_t bytes = h[54] | h[55] << 8 | h[56] << 16 | (uint32_t)h[57] << 24;
    w->n = bytes / 4;
    w->x = malloc(bytes + 4);
    long got = (long)fread(w->x, 4, w->n, f);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fclose(f);
    return got == w->n && size == 58 + (long)bytes ? 0 : -1;
}

static int read_take(const char *dir, wav_t *w, int max) {
    DIR *d = opendir(dir);
    int n = 0;
    struct dirent *e;
    while (d && (e = readdir(d)) && n < max) {
        size_t l = strlen(e->d_name);
        if (l < 5 || strcmp(e->d_name + l - 4, ".wav")) continue;
        char p[1400];
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        snprintf(w[n].name, sizeof w[n].name, "%s", e->d_name);
        if (read_wav(p, &w[n]) == 0) n++;
        else printf("FAIL: %s unreadable\n", p), fails++;
    }
    if (d) closedir(d);
    return n;
}

static const wav_t *find(const wav_t *w, int n, const char *suffix) {
    for (int i = 0; i < n; i++) {
        size_t a = strlen(w[i].name), b = strlen(suffix);
        if (a >= b && !strcmp(w[i].name + a - b, suffix)) return &w[i];
    }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc < 5) { fprintf(stderr, "usage: stems_test <out dir> <module dir> <send key> <comp key>\n"); return 2; }
    const char *out = argv[1], *mod = argv[2], *send = argv[3], *comp = argv[4];
    char cmd[800];
    snprintf(cmd, sizeof cmd, "rm -rf '%s' && mkdir -p '%s'", out, out);
    if (system(cmd)) return 2;
    setenv("TRSTEM_DIR", out, 1);
    E = mpc_engine();
    if (!E) { printf("FAIL: no engine\n"); return 1; }
    char st[96], buf[1 << 16];

    /* 1. bit for bit: OFF, DRY, AS HEARD (one instance each, the same groove, transport playing) */
    int16_t *ref = NULL;
    for (int mode = 0; mode < 3; mode++) {
        void *I = E->create(mod);
        set(I, send, "90");
        set(I, comp, "80");
        set(I, "rev_level", "100");
        char m[4];
        snprintf(m, sizeof m, "%d", mode);
        set(I, "stems", m);
        set(I, "transport", "1");
        int ni = 0;
        int16_t *o = render(I, 400, &ni);
        set(I, "transport", "0");
        if (!ref) ref = o;
        else {
            CHECK(!memcmp(ref, o, sizeof(int16_t) * 2 * BLOCK * 400), "STEMS mode %d changes the sound", mode);
            free(o);
        }
        E->destroy(I);
    }
    free(ref);
    snprintf(cmd, sizeof cmd, "rm -rf '%s' && mkdir -p '%s'", out, out);
    if (system(cmd)) return 2;

    /* 2. a take, AS HEARD, compressor on, master distortion off */
    void *I = E->create(mod);
    set(I, send, "90");
    set(I, "rev_level", "100");
    set(I, comp, "80");
    status(I, st);
    CHECK(!strcmp(st, "OFF"), "status off: %s", st);
    set(I, "stems", "2");
    status(I, st);
    CHECK(!strcmp(st, "ARMED: PLAY RECORDS"), "status armed: %s", st);
    int ni = 0;
    free(render(I, 50, &ni));   /* stopped: nothing recorded */
    char t[8][700];
    usleep(100000);
    CHECK(takes(out, t, 8) == 0, "a take without PLAY");
    set(I, "transport", "1");
    free(render(I, 2000, &ni));   /* 5.8 s */
    status(I, st);
    CHECK(!strncmp(st, "REC 0:05", 8), "status recording: %s", st);
    set(I, "transport", "0");
    free(render(I, 10, NULL));
    status(I, st);
    CHECK(strstr(st, "ENDING") != NULL, "status in the tail: %s", st);
    free(render(I, 6000, NULL));   /* the tail dies away (at most 15 s) */
    status(I, st);
    for (int k = 0; k < 100 && strncmp(st, "SAVED", 5); k++) { usleep(20000); status(I, st); }
    CHECK(!strncmp(st, "SAVED", 5), "status saved: %s", st);
    printf("status after the take: %s\n", st);
    int nt = takes(out, t, 8);
    CHECK(nt == 1, "%d takes, wanted 1", nt);
    if (nt == 1) {
        wav_t w[24];
        int nw = read_take(t[0], w, 24);
        const wav_t *mix = find(w, nw, " MIX.wav"), *fx = find(w, nw, " FX.wav");
        CHECK(mix != NULL, "no MIX");
        CHECK(fx != NULL, "no FX (a send is up)");
        long n = mix ? mix->n : 0;
        CHECK(n >= 2000L * BLOCK && n < 2000L * BLOCK + 15 * 44100, "take length %ld", n);
        for (int i = 0; i < nw; i++) CHECK(w[i].n == n, "%s: %ld frames, MIX %ld", w[i].name, w[i].n, n);
        printf("take: %d files, %.2f s\n", nw, n / 44100.0);
        /* the voices and FX add up to MIX (MIX is the int16 output; away from the clip) */
        double err = 0, peak = 0;
        long cmpd = 0;
        for (long k = 0; mix && k < n; k++) {
            double s = 0;
            for (int i = 0; i < nw; i++) if (&w[i] != mix) s += w[i].x[k];
            if (fabs(mix->x[k]) > 0.98) continue;
            double e = fabs(s - mix->x[k]);
            if (e > err) err = e;
            if (fabs(s) > peak) peak = fabs(s);
            cmpd++;
        }
        printf("sum of stems vs MIX: max error %.6f (peak %.3f, %ld frames)\n", err, peak, cmpd);
        CHECK(peak > 0.01, "the take is silent");
        CHECK(err < 2e-4, "AS HEARD: the stems don't add up to MIX (max error %g)", err);
        char p[1400];
        snprintf(p, sizeof p, "%s/take.txt", t[0]);
        FILE *f = fopen(p, "r");
        CHECK(f != NULL, "no take.txt");
        if (f) {
            size_t r = fread(buf, 1, sizeof buf - 1, f);
            buf[r] = 0;
            fclose(f);
            CHECK(strstr(buf, "AS HEARD") != NULL, "take.txt: no mode");
        }
        for (int i = 0; i < nw; i++) free(w[i].x);
    }

    /* 3. one voice played: only its file and MIX; STEMS OFF during a take ends it at once */
    set(I, send, "0");
    free(render(I, 1000, NULL));   /* the reverb dies away */
    set(I, "transport", "1");
    {
        uint8_t m[3] = {0x90, 36, 100};
        E->midi(I, m, 3);
    }
    free(render(I, 100, NULL));
    set(I, "stems", "0");
    free(render(I, 2, NULL));
    set(I, "transport", "0");
    status(I, st);
    CHECK(!strcmp(st, "OFF"), "status off: %s", st);
    usleep(300000);
    nt = takes(out, t, 8);
    CHECK(nt == 2, "%d takes after the second PLAY, wanted 2", nt);
    if (nt == 2) {
        wav_t w[24];
        int nw = read_take(t[1], w, 24);
        CHECK(nw == 2 && find(w, nw, " BD.wav") && find(w, nw, " MIX.wav"), "one voice played: %d files, wanted BD and MIX", nw);
        CHECK(nw > 0 && w[0].n == 100L * BLOCK, "STEMS OFF: the take is %ld frames, wanted %d", nw ? w[0].n : 0, 100 * BLOCK);
        for (int i = 0; i < nw; i++) free(w[i].x);
        char p[1400];
        snprintf(p, sizeof p, "%s/take.txt", t[1]);
        FILE *f = fopen(p, "r");
        size_t r = f ? fread(buf, 1, sizeof buf - 1, f) : 0;
        buf[r] = 0;
        if (f) fclose(f);
        CHECK(strstr(buf, "silent all take, no file") != NULL, "take.txt: the silent voices aren't listed");
    }

    /* 4. the state blob carries STEMS */
    set(I, "stems", "1");
    int len = E->get_param(I, "state", buf, sizeof buf);
    CHECK(len > 0 && strstr(buf, " #stems=1") && (int)strlen(buf) == len, "state without the STEMS tail (%d)", len);
    void *J = E->create(mod);
    set(J, "state", buf);
    E->get_param(J, "stems", st, sizeof st);
    CHECK(!strcmp(st, "1"), "STEMS not restored: %s", st);
    char *tail = strstr(buf, " #stems=");
    if (tail) *tail = 0;   /* a blob from before 1.2.0 */
    set(J, "stems", "2");
    set(J, "state", buf);
    E->get_param(J, "stems", st, sizeof st);
    CHECK(!strcmp(st, "2"), "an old blob changed STEMS: %s", st);
    char b2[1 << 16];
    E->get_param(J, "state", b2, sizeof b2);
    tail = strstr(b2, " #stems=");
    if (tail) *tail = 0;
    CHECK(!strcmp(buf, b2), "the kit's own state doesn't round-trip");
    E->destroy(J);
    E->destroy(I);

    printf("%d checks, %d failed\n%s\n", checks, fails, fails ? "FAILED" : "PASSED");
    return fails != 0;
}
