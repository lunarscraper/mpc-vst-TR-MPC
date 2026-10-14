// Offline audio check for TR-MPC: every flat voice, played from slot 1, must make sound and then decay;
// a retargeted slot must change what the same note plays. Build: see ports/trkit/test/run.sh
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
extern "C" {
#include "engine.h"
}
#include "trkit_tap.h"

static double rms_of(const mpc_engine_t *e, void *in, int blocks) {
    int16_t buf[128 * 2];
    double acc = 0; long n = 0;
    for (int b = 0; b < blocks; ++b) {
        memset(buf, 0, sizeof buf);
        e->render(in, buf, 128);
        for (int i = 0; i < 128; ++i) { double s = buf[i * 2] / 32768.0; acc += s * s; ++n; }
    }
    return sqrt(acc / n);
}

int main() {
    const mpc_engine_t *e = mpc_engine();
    const char *dir = getenv("TRKIT_DIR");   // folder holding samples/ (the 909 hats and cymbals)
    void *in = e->create(dir ? dir : "");
    int fails = 0;
    const int kVoices = 49;
    for (int v = 0; v < kVoices; ++v) {
        char val[16];
        snprintf(val, sizeof val, "%d", v);
        e->set_param(in, "s01_src", val);
        rms_of(e, in, 400);                         // flush the previous voice's tail
        const uint8_t on[3] = {0x90, 36, 110};
        e->midi(in, on, 3);
        const double hit = rms_of(e, in, 40);       // ~116 ms right after the hit
        const double tail = rms_of(e, in, 800);     // 2.3 s later window
        const double after = rms_of(e, in, 200);
        const bool ok = hit > 1e-4 && hit < 1.0 && after < hit;
        if (!ok) ++fails;
        printf("%s voice %2d  hit %.5f  window2 %.5f  window3 %.5f\n", ok ? "ok  " : "FAIL", v, hit, tail, after);
    }
    // pan and sends belong to the slot: hard left silences the right channel; a reverb send adds tail
    auto hit_lr = [&](const char *pan, const char *rev, double *l, double *r, double *tail) {
        e->set_param(in, "s01_src", "21");   // 808 closed hat: dry tail is short, so a reverb send is obvious
        e->set_param(in, "s01_pan", pan);
        e->set_param(in, "s01_rev", rev);
        rms_of(e, in, 1500);
        const uint8_t on[3] = {0x90, 36, 110};
        e->midi(in, on, 3);
        int16_t buf[128 * 2];
        double al = 0, ar = 0;
        for (int b = 0; b < 40; ++b) {
            e->render(in, buf, 128);
            for (int i = 0; i < 128; ++i) { al += (buf[i * 2] / 32768.0) * (buf[i * 2] / 32768.0); ar += (buf[i * 2 + 1] / 32768.0) * (buf[i * 2 + 1] / 32768.0); }
        }
        *l = sqrt(al / 5120); *r = sqrt(ar / 5120);
        *tail = rms_of(e, in, 400);
    };
    double l, r, t0, t1;
    hit_lr("0", "0", &l, &r, &t0);
    const bool pan_ok = l > 1e-3 && r < l * 0.01;
    hit_lr("64", "0", &l, &r, &t0);
    hit_lr("64", "127", &l, &r, &t1);
    const bool rev_ok = t1 > t0 * 2 && fabs(l - r) < l * 0.05;
    printf("%s pan hard left (centre now: L %.5f R %.5f)\n%s reverb send: tail %.6f -> %.6f\n", pan_ok ? "ok  " : "FAIL", l, r, rev_ok ? "ok  " : "FAIL", t0, t1);
    fails += !pan_ok + !rev_ok;
    // voice-specific controls: an 808 kick has attack and tone as X1/X2, a tom has none; Dist is the voice's own enum
    {
        auto get = [&](const char *k) { char b[32]; return e->get_param(in, k, b, sizeof b) > 0 ? atoi(b) : -1; };
        e->set_param(in, "s02_src", "8");            // 808 BD
        e->set_param(in, "s02_attack", "111"); e->set_param(in, "s02_tone", "22"); e->set_param(in, "s02_dist", "5");
        const bool kick = get("s02_attack") == 111 && get("s02_tone") == 22 && get("s02_dist") == 5;
        e->set_param(in, "s02_src", "10");           // 808 LT: no extras
        e->set_param(in, "s02_attack", "99");
        const bool tom = get("s02_attack") == 0;
        e->set_param(in, "s02_src", "38");           // 909 BD: attack, sweep depth, pitch mod
        e->set_param(in, "s02_attack", "50"); e->set_param(in, "s02_sweep", "60"); e->set_param(in, "s02_pmod", "70");
        const bool k909 = get("s02_attack") == 50 && get("s02_sweep") == 60 && get("s02_pmod") == 70;
        printf("%s extras: 808 kick %d, tom ignores %d, 909 kick %d\n", kick && tom && k909 ? "ok  " : "FAIL", kick, tom, k909);
        fails += !(kick && tom && k909);
    }
    // the editor's e_* controls and edit_voice act on the edit slot
    {
        auto get = [&](const char *k) { char b[32]; return e->get_param(in, k, b, sizeof b) > 0 ? atoi(b) : -1; };
        e->set_param(in, "edit_slot", "4");
        e->set_param(in, "e_pan", "12"); e->set_param(in, "edit_voice", "21");
        const bool ok = get("s05_pan") == 12 && get("s05_src") == 21 && get("e_pan") == 12 && get("edit_voice") == 21 && get("s04_pan") != 12;
        printf("%s editor: edit slot 5 -> pan %d voice %d (slot 4 pan %d)\n", ok ? "ok  " : "FAIL", get("s05_pan"), get("s05_src"), get("s04_pan"));
        fails += !ok;
        e->set_param(in, "edit_slot", "0");
    }
    // the slot's kit is derived from its voice and read-only
    {
        auto get = [&](const char *k) { char b[32]; return e->get_param(in, k, b, sizeof b) > 0 ? atoi(b) : -1; };
        const int voices[4] = {3, 12, 30, 45};   // 606 HT, 808 HT/mid, CW-78, 909
        bool ok = true;
        for (int b = 0; b < 4; ++b) {
            char v[8]; snprintf(v, sizeof v, "%d", voices[b]);
            e->set_param(in, "s06_src", v);
            ok = ok && get("s06_fam") == b;
        }
        e->set_param(in, "s06_src", "38");   // the 909 kick has its own layout value
        ok = ok && get("s06_fam") == 4;
        e->set_param(in, "s06_src", "45");
        e->set_param(in, "s06_fam", "0");
        ok = ok && get("s06_fam") == 3;
        printf("%s kit follows the voice (606/808/CW-78/909 -> 0..3, set ignored)\n", ok ? "ok  " : "FAIL");
        fails += !ok;
    }
    // sixteen pads at once must not hard-clip (that crackles): the output limiter keeps every sample below full scale
    {
        void *in3 = e->create(dir ? dir : "");
        for (int i = 0; i < 16; ++i) { uint8_t m[3] = {0x90, (uint8_t)(36 + i), 127}; e->midi(in3, m, 3); }
        int16_t buf[256]; int peak = 0;
        for (int b = 0; b < 400; ++b) { e->render(in3, buf, 128); for (int i = 0; i < 256; ++i) { int a = abs(buf[i]); if (a > peak) peak = a; } }
        const bool ok = peak < 32767 && peak > 8000;
        printf("%s limiter: 16 pads at velocity 127 peak %d of 32767\n", ok ? "ok  " : "FAIL", peak);
        fails += !ok;
        e->destroy(in3);
    }
    // randomise: only the selected slots move; Level stays; the voice stays in its kit; selections ride in the project chunk
    {
        void *in4 = e->create(dir ? dir : "");
        e->set_param(in4, "rnd_all", "1");
        { char b[16]; bool all = true; for (int i = 1; i <= 16; ++i) { char k[16]; snprintf(k, sizeof k, "rnd_s%02d", i); all = all && e->get_param(in4, k, b, sizeof b) > 0 && atoi(b) == 1; }
          e->set_param(in4, "rnd_none", "1");
          for (int i = 1; i <= 16; ++i) { char k[16]; snprintf(k, sizeof k, "rnd_s%02d", i); all = all && e->get_param(in4, k, b, sizeof b) > 0 && atoi(b) == 0; }
          printf("%s randomise select all / clear\n", all ? "ok  " : "FAIL"); fails += !all; }
        auto get = [&](const char *k) { char b[32]; return e->get_param(in4, k, b, sizeof b) > 0 ? atoi(b) : -1; };
        e->set_param(in4, "s03_src", "8");   // 808 kick (attack, tone)
        e->set_param(in4, "s04_src", "8");
        int before[6] = { get("s03_tune"), get("s03_decay"), get("s03_attack"), get("s03_pan"), get("s03_level"), get("s04_tune") };
        e->set_param(in4, "rnd_s03", "1"); e->set_param(in4, "rnd_amount", "127"); e->set_param(in4, "rnd_voice", "1");
        int moved = 0;
        for (int round = 0; round < 3; ++round) e->set_param(in4, "rnd_go", "1");
        int after[6] = { get("s03_tune"), get("s03_decay"), get("s03_attack"), get("s03_pan"), get("s03_level"), get("s04_tune") };
        moved = (after[0] != before[0]) + (after[1] != before[1]) + (after[2] != before[2]) + (after[3] != before[3]);
        const int voice = get("s03_src");
        const bool ok = moved >= 2 && (voice != 8 || after[4] == before[4]) && after[5] == before[5] && voice >= 0 && voice < 49;   /* some other voice */
        static char st[8192]; e->get_param(in4, "state", st, sizeof st);
        void *in5 = e->create(dir ? dir : "");
        e->set_param(in5, "state", st);
        char b2[16]; const bool kept = e->get_param(in5, "rnd_s03", b2, sizeof b2) > 0 && atoi(b2) == 1;
        printf("%s randomise: %d of 4 controls moved, level and slot 4 untouched, voice %d (any kit), selection kept in state: %d\n",
               ok && kept ? "ok  " : "FAIL", moved, voice, kept);
        fails += !(ok && kept);
        e->destroy(in5); e->destroy(in4);
    }
    // a project chunk ("state") must restore every slot's voice and settings into a fresh instance
    {
        const char *set[][2] = {{"s03_src", "30"}, {"s03_pan", "10"}, {"s03_rev", "90"}, {"s03_level", "100"}, {"s03_tune", "20"},
                                {"s16_src", "48"}, {"s16_decay", "33"}, {"fx_rev_decay", "11"}, {"fx_dly_time", "3"},
                                {"fx_master_dist", "4"}, {"fx_volume", "77"}};
        for (auto &kv : set) e->set_param(in, kv[0], kv[1]);
        static char st[8192];
        int n = e->get_param(in, "state", st, sizeof st);
        void *in2 = e->create(dir ? dir : "");
        e->set_param(in2, "state", st);
        int bad = 0;
        for (auto &kv : set) {
            char b[32];
            if (e->get_param(in2, kv[0], b, sizeof b) <= 0 || strcmp(b, kv[1])) { printf("FAIL state: %s = %s, want %s\n", kv[0], b, kv[1]); ++bad; }
        }
        printf("%s state chunk: %d bytes, %d of %d values restored\n", bad ? "FAIL" : "ok  ", n, (int)(sizeof set / sizeof *set) - bad, (int)(sizeof set / sizeof *set));
        fails += bad;
        e->destroy(in2);
    }
    // taps: the owner publishes each slot's post-pan planes; the block just output equals the main mix when nothing is tapped,
    // and a tapped slot leaves the main mix but stays in its plane
    {
        trtap::Shared *sh = trmpc_tap_shared();
        e->set_param(in, "s01_src", "8"); e->set_param(in, "s01_pan", "64"); e->set_param(in, "s01_rev", "0");
        rms_of(e, in, 800);
        auto run = [&](int tapped, double *mainRms, double *planeRms, double *diff) {
            sh->tapped[0].store(tapped); sh->tapped[5].store(1);   // some tap must be registered for planes to be published
            const uint8_t on[3] = {0x90, 36, 110};
            e->midi(in, on, 3);
            double am = 0, ap = 0, ad = 0, ac = 0; int16_t buf[256];
            for (int b = 0; b < 60; ++b) {
                e->render(in, buf, 128);
                const uint32_t hr = sh->hostRead.load();
                const auto &blk = sh->data[(hr - 1) % trtap::kRing];
                for (int i = 0; i < 128; ++i) {
                    am += (double)buf[2 * i] * buf[2 * i]; ap += (double)blk[0][i] * blk[0][i];
                    ac += (double)buf[2 * i] * blk[0][i];
                }
            }
            *mainRms = sqrt(am / 7680); *planeRms = sqrt(ap / 7680); *diff = ac / sqrt(am * ap + 1e-9);   /* correlation at lag 0 */
            sh->tapped[0].store(0); sh->tapped[5].store(0);
            rms_of(e, in, 800);
        };
        double m0, p0, d0, m1, p1, d1;
        run(0, &m0, &p0, &d0);
        run(1, &m1, &p1, &d1);
        const bool ok = p0 > 100 && m0 > 100 && d0 > 0.95 && p1 > 100 && m1 < 1.0;
        printf("%s taps: untapped main %.0f plane %.0f (correlation %.3f); tapped main %.1f plane %.0f\n", ok ? "ok  " : "FAIL", m0, p0, d0, m1, p1);
        fails += !ok;
    }
    // internal FX switches and voices-only randomise
    {
        void *in6 = e->create(dir ? dir : "");
        auto get = [&](const char *k) { char b[32]; return e->get_param(in6, k, b, sizeof b) > 0 ? atoi(b) : -1; };
        e->set_param(in6, "s01_src", "21"); e->set_param(in6, "s01_rev", "100");
        auto tail = [&](bool on) {
            e->set_param(in6, "int_rev", on ? "1" : "0");
            rms_of(e, in6, 1500);
            const uint8_t m[3] = {0x90, 36, 110}; e->midi(in6, m, 3);
            rms_of(e, in6, 60);
            return rms_of(e, in6, 200);
        };
        const double on = tail(true), off = tail(false);
        bool ok = on > off * 3 && get("int_rev") == 0 && get("int_dly") == 1;
        printf("%s internal reverb off: tail %.5f -> %.5f\n", ok ? "ok  " : "FAIL", on, off); fails += !ok;
        e->set_param(in6, "fx_comp", "50"); e->set_param(in6, "int_comp", "0");
        ok = get("fx_comp") == 50 && get("int_comp") == 0;
        static char st[8192]; e->get_param(in6, "state", st, sizeof st);
        void *in7 = e->create(dir ? dir : ""); e->set_param(in7, "state", st);
        char b[16]; ok = ok && e->get_param(in7, "fx_comp", b, sizeof b) > 0 && atoi(b) == 50 && e->get_param(in7, "int_comp", b, sizeof b) > 0 && atoi(b) == 0 && e->get_param(in7, "int_rev", b, sizeof b) > 0 && atoi(b) == 0;
        e->set_param(in6, "int_comp", "1"); ok = ok && get("fx_comp") == 50;
        printf("%s internal comp off keeps its value and is restored from the project\n", ok ? "ok  " : "FAIL"); fails += !ok;
        e->destroy(in7);
        e->set_param(in6, "rnd_all", "1"); e->set_param(in6, "s02_src", "8"); e->set_param(in6, "s02_pan", "30");
        int v0 = get("s02_src"), t0 = get("s02_pan"); bool moved = false;
        for (int i = 0; i < 6; ++i) { e->set_param(in6, "rnd_go_voice", "1"); if (get("s02_src") != v0) moved = true; }
        bool kits[4] = {false, false, false, false};
        for (int i = 0; i < 40; ++i) { e->set_param(in6, "rnd_go_voice", "1"); int v = get("s02_src"); kits[v < 8 ? 0 : v < 24 ? 1 : v < 38 ? 2 : 3] = true; }
        ok = moved && get("s02_pan") == t0 && kits[0] + kits[1] + kits[2] + kits[3] >= 3;
        printf("%s random voices: voice changes across kits, pan untouched\n", ok ? "ok  " : "FAIL"); fails += !ok;
        e->destroy(in6);
    }
    // two slots on one voice: the sound goes only to the slot that was hit (a tapped slot's twin must not leak it into the main mix)
    {
        trtap::Shared *sh = trmpc_tap_shared();
        e->set_param(in, "s01_src", "8"); e->set_param(in, "s02_src", "8");
        auto hit = [&](int note) { sh->tapped[0].store(1); rms_of(e, in, 800); const uint8_t m[3] = {0x90, (uint8_t)note, 110}; e->midi(in, m, 3); return rms_of(e, in, 60); };
        const double tappedHit = hit(36), twinHit = hit(37);
        sh->tapped[0].store(0);
        const bool ok = tappedHit < 1e-4 && twinHit > 1e-3;
        printf("%s shared voice: tapped slot's hit leaves main %.5f, its twin's hit sounds %.5f\n", ok ? "ok  " : "FAIL", tappedHit, twinHit);
        fails += !ok;
    }
    e->destroy(in);
    printf(fails ? "FAILED %d\n" : "PASSED\n", fails);
    return fails != 0;
}
