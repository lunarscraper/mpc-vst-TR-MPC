#!/usr/bin/env python3
"""One-off helper that produced ports/trkit/patches/*.patch (kept for reference; the build only applies the patches).

TR-MPC taps: each engine gets set_tap(); with a tap set, render writes every voice's post-fader dry sample to
tap[voice * 128 + i] and skips the engine's own reverb, delay, master stage and volume. 8w8 additionally gets
sc808_fx_stereo(), its post-voice stage (reverb, delay, master distortion, glue, volume) run once for the whole kit.
"""
import os, re, shutil, subprocess, sys
P = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ports")
T = os.path.join(P, "trkit")
def rep(s, old, new, cnt=1):
    assert s.count(old) == cnt, (old, s.count(old))
    return s.replace(old, new)
def rd(p): return open(p).read()
def wr(p, s): open(p, "w", newline="\n").write(s)

FX = '''
/* TR-MPC patch: the post-voice stage (reverb, delay, master distortion, glue, volume) run once on summed buses. */
void %(p)s_fx_stereo(%(p)s_engine_t *e, const float *dl, const float *dr, const float *send_r, const float *send_d,
                     float *ol, float *orr, int frames)
{
    const int   mdist  = e->env[e->e_master_dist];
    const float mdrive = e->potv[e->p_master_drive];
    const float vol    = e->potv[e->p_volume];
    const float comp   = e->potv[e->p_comp];
    for(int i = 0; i < frames; ++i)
    {
        const float wet = %(p)s_verb_tick(&e->verb, send_r[i]) + %(p)s_dly_tick(&e->dly, send_d[i], e->sample_rate);
        float l = dl[i] + wet, r = dr[i] + wet;
        if(mdist > 0)
        {
            l = %(p)s_shape_st(l, mdrive, mdist - 1, e->crush_master);
            r = %(p)s_shape_st(r, mdrive, mdist - 1, e->crush_master_r);
        }
        if(comp > 0.001f)
        {
            const float m = 0.5f * (l + r);
            const float g = %(p)s_glue_tick(&e->glue, m, comp, e->sample_rate);
            const float k = fabsf(m) > 1e-9f ? g / m : 1.0f;
            l *= k; r *= k;
        }
        l *= vol; r *= vol;
        ol[i]  = (l > -8.0f && l < 8.0f) ? l : 0.0f;
        orr[i] = (r > -8.0f && r < 8.0f) ? r : 0.0f;
    }
}
'''
def hdr(p, T_, fx):
    s = '''
/* TR-MPC patch: per-voice tap. With a tap set (NUM_VOICES x %(p)s_TAP_FRAMES floats, zeroed by the caller before each
 * render) render() writes each voice's post-fader dry sample there and skips the reverb, delay, master stage and
 * volume; the output stays silent. */
#define %(P)s_TAP_FRAMES 128
void %(p)s_set_tap(%(p)s_engine_t *e, float *tap);
''' % dict(p=p, P=p.upper())
    if fx: s += '''/* The post-voice stage on summed buses (dry L/R, reverb and delay send buses), as render() does it per kit. */
void %(p)s_fx_stereo(%(p)s_engine_t *e, const float *dl, const float *dr, const float *send_r, const float *send_d,
                     float *ol, float *orr, int frames);
''' % dict(p=p)
    return s

def cpp_engine(kit, p, cpp, h, structline, sendadd, loopmark, fx):
    d = os.path.join(T, "engines", kit)
    c = rd(os.path.join(d, cpp)); hh = rd(os.path.join(d, h))
    c = rep(c, structline, structline + "\n    float *tap; int tap_i;           /* TR-MPC patch: per-voice tap, see the header */"
            + ("\n    float crush_master_r[%s_CRUSH_STATE];" % p.upper() if fx else ""))
    c = rep(c, sendadd[0], sendadd[1])
    c = rep(c, loopmark[0], loopmark[1])
    c = rep(c, "/* ---- parameters ------------------------------------------------------- */",
            ("void %s_set_tap(%s_engine_t *e, float *tap) { e->tap = tap; }\n" % (p, p)) + (FX % dict(p=p) if fx else "")
            + "\n/* ---- parameters ------------------------------------------------------- */")
    wr(os.path.join(d, cpp), c)
    anchor = re.search(r"void\s+%s_render\([^;]*;\n" % p, hh).group(0)
    hh = rep(hh, anchor, anchor + hdr(p, None, fx))
    wr(os.path.join(d, h), hh)

def prepare():
    shutil.rmtree(os.path.join(T, "engines"), ignore_errors=True)
    for kit in ("6w6", "8w8", "cw78", "9w9"):
        shutil.copytree(os.path.join(P, kit, "src", "dsp"), os.path.join(T, "engines", kit))

prepare()
lane_tail = "    float mix = 0.0f"
cpp_engine("6w6", "sd606", "sd606_engine.cpp", "sd606_engine.h", "struct sd606_engine {",
    ("             mix   += sv;                                                     \\\n",
     "             mix   += sv;                                                     \\\n"
     "             if(e->tap) e->tap[(vid) * SD606_TAP_FRAMES + e->tap_i] = sv;     \\\n"),
    ("        float mix = 0.0f, send_r = 0.0f, send_d = 0.0f;\n        SD606_LANE(SD606_BD",
     "        float mix = 0.0f, send_r = 0.0f, send_d = 0.0f;\n        e->tap_i = i;\n        SD606_LANE(SD606_BD"), False)
c = rd(os.path.join(T, "engines/6w6/sd606_engine.cpp"))
# 6W6 renders every lane (the metal voices sum 47 sines) whether or not it sounds; with a tap set, only lanes
# that were triggered and have not been silent for 50 ms are rendered.
c = rep(c, "    float *tap; int tap_i;           /* TR-MPC patch: per-voice tap, see the header */",
        "    float *tap; int tap_i;           /* TR-MPC patch: per-voice tap, see the header */\n    int tap_live[SD606_NUM_VOICES], tap_quiet[SD606_NUM_VOICES];")
c = rep(c, "    if(e->mutes & (1u << voice)) return;\n", "    if(e->mutes & (1u << voice)) return;\n    e->tap_live[voice] = 1; e->tap_quiet[voice] = 0;   /* TR-MPC patch */\n")
c = rep(c, "    do { if(e->rt[vid].choke_gain > 0.0f) {                                   \\\n",
        "    do { if(e->rt[vid].choke_gain > 0.0f && (!e->tap || e->tap_live[vid])) {  \\\n")
c = rep(c, "             if(e->tap) e->tap[(vid) * SD606_TAP_FRAMES + e->tap_i] = sv;     \\\n",
        "             if(e->tap) {                                                     \\\n"
        "                 e->tap[(vid) * SD606_TAP_FRAMES + e->tap_i] = sv;            \\\n"
        "                 if(sv > 1e-6f || sv < -1e-6f) e->tap_quiet[vid] = 0;         \\\n"
        "                 else if(++e->tap_quiet[vid] > 2205) e->tap_live[vid] = 0;    \\\n"
        "             }                                                                \\\n")
c = rep(c, "        /* The wet returns BEFORE the master distortion, so that stage works",
        "        if(e->tap) { out[i] = 0.0f; continue; }   /* TR-MPC patch: voices only */\n\n        /* The wet returns BEFORE the master distortion, so that stage works")
wr(os.path.join(T, "engines/6w6/sd606_engine.cpp"), c)

for kit, p, add_anchor in (("8w8", "sc808", "sc808"), ("cw78", "cr78", "cr78")):
    cpp, h = p + "_engine.cpp", p + "_engine.h"
    cpp_engine(kit, p, cpp, h, "struct %s_engine {" % p,
        ("    *mix += sv;\n", "    *mix += sv;\n    if(e->tap) e->tap[v * %s_TAP_FRAMES + e->tap_i] = sv;\n" % p.upper()),
        ("        float mix = 0.0f, send_r = 0.0f, send_d = 0.0f;\n",
         "        float mix = 0.0f, send_r = 0.0f, send_d = 0.0f;\n        e->tap_i = i;\n"), True)
    c = rd(os.path.join(T, "engines", kit, cpp))
    m = re.search(r"\n        /\*\n         \* The wet returns join the bus BEFORE the master stages", c)
    c = c[:m.start()] + "\n        if(e->tap) { out[i] = 0.0f; continue; }   /* TR-MPC patch: voices only */\n" + c[m.start():]
    wr(os.path.join(T, "engines", kit, cpp), c)

# 9W9 (C, public struct)
d = os.path.join(T, "engines", "9w9")
h = rd(d + "/er99_engine.h"); c = rd(d + "/er99_engine.c")
h = rep(h, "    wa_noise_t    noise;\n", "    wa_noise_t    noise;\n    float        *tap;      /* TR-MPC patch: per-voice tap, see er99_engine_set_tap */\n    int           tap_i;\n")
h = rep(h, "void er99_engine_render(er99_engine_t *e, float *out, int frames);\n",
        "void er99_engine_render(er99_engine_t *e, float *out, int frames);\n\n"
        "/* TR-MPC patch: per-voice tap. With a tap set (ER99_NUM_TRIGGERS x 128 floats, zeroed by the caller before each\n"
        " * render) render writes each voice's dry sample to tap[trigger * 128 + i] and skips send FX and master stage. */\n"
        "#define ER99_TAP_FRAMES 128\nvoid er99_engine_set_tap(er99_engine_t *e, float *tap);\n")
c = rep(c, "        float mix = 0.0f, rbus = 0.0f, dbus = 0.0f;\n", "        float mix = 0.0f, rbus = 0.0f, dbus = 0.0f;\n        e->tap_i = n;\n")
c = rep(c, "            mix += v;\n            rbus += v * e->send_rev[i];\n",
        "            mix += v;\n            if(e->tap) e->tap[i * ER99_TAP_FRAMES + n] = v;\n            rbus += v * e->send_rev[i];\n")
c = rep(c, "            mix += v; rbus += v * e->send_rev[ER99_RS];", "            if(e->tap) e->tap[ER99_RS * ER99_TAP_FRAMES + n] = v;\n            mix += v; rbus += v * e->send_rev[ER99_RS];")
c = rep(c, "            mix += v; rbus += v * e->send_rev[ER99_HC];", "            if(e->tap) e->tap[ER99_HC * ER99_TAP_FRAMES + n] = v;\n            mix += v; rbus += v * e->send_rev[ER99_HC];")
c = rep(c, "                mix += v;\n                rbus += v * e->send_rev[strig[i]];",
        "                mix += v;\n                if(e->tap) e->tap[strig[i] * ER99_TAP_FRAMES + n] = v;\n                rbus += v * e->send_rev[strig[i]];")
c = rep(c, "        /* FX returns join the bus before the master stages, so master",
        "        if(e->tap) { out[n] = 0.0f; continue; }   /* TR-MPC patch: voices only */\n\n        /* FX returns join the bus before the master stages, so master")
c = rep(c, "/* ===================================================================== */\n/* Parameters ",
        "void er99_engine_set_tap(er99_engine_t *e, float *tap) { e->tap = tap; }\n\n/* ===================================================================== */\n/* Parameters ")
wr(d + "/er99_engine.h", h); wr(d + "/er99_engine.c", c)

# write patches
shutil.rmtree(os.path.join(T, "patches"), ignore_errors=True); os.makedirs(os.path.join(T, "patches"))
for kit in ("6w6", "8w8", "cw78", "9w9"):
    r = subprocess.run(["diff", "-ruN", "a", "b"], cwd=None, capture_output=True, text=True) if False else None
    tmp = os.path.join(T, "_diff"); shutil.rmtree(tmp, ignore_errors=True); os.makedirs(tmp)
    shutil.copytree(os.path.join(P, kit, "src", "dsp"), os.path.join(tmp, "a"))
    shutil.copytree(os.path.join(T, "engines", kit), os.path.join(tmp, "b"))
    out = subprocess.run(["diff", "-ruN", "a", "b"], cwd=tmp, capture_output=True, text=True).stdout
    wr(os.path.join(T, "patches", kit + ".patch"), out)
    shutil.rmtree(tmp)
print("ok")
