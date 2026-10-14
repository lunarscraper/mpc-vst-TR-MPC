#!/usr/bin/env python3
"""Write ports/trkit/params.json: 16 slots (src, level, tune, decay, drive, dist, x1-x3, pan, rev, dly) plus the shared FX and master stage.

The voice lists must match the engines' own voice order (sd606_voice_t, sc808_voice_t, cr78_voice_t, er99_trigger_t);
they are the flat index that trkit.cpp's `sNN_src` takes. Append only: saved projects store these by VST index.
"""
import json
import os

KITS = [
    ("606", "bd sd lt ht ch oh cy cp"),
    ("808", "bd sd lt mt ht lc mc hc rs cl ma cp cb ch oh cy"),
    ("CR78", "bd sd rs hh cy ma cl hb lb lc cb tb gu mb"),
    ("909", "bd sd lt mt ht rs hc ohh chh rc cr"),
]
NAMES = {   # the voices' own names: a slot's menu and title read "SNARE", the faceplate says which kit
    "bd": "BASS DRUM", "sd": "SNARE", "lt": "LOW TOM", "mt": "MID TOM", "ht": "HIGH TOM", "ch": "CLOSED HAT", "oh": "OPEN HAT",
    "cy": "CYMBAL", "cp": "HAND CLAP", "lc": "LOW CONGA", "mc": "MID CONGA", "hc": "HI CONGA", "rs": "RIM SHOT", "cl": "CLAVES",
    "ma": "MARACAS", "cb": "COWBELL", "hh": "HI-HAT", "hb": "HI BONGO", "lb": "LOW BONGO", "tb": "TAMBOURINE", "gu": "GUIRO",
    "mb": "METAL BEAT", "ohh": "OPEN HAT", "chh": "CLOSED HAT", "rc": "RIDE", "cr": "CRASH",
}
NAMES909 = {"hc": "HAND CLAP", "lt": "LOW TOM", "mt": "MID TOM", "ht": "HI TOM"}
SRC = [(NAMES909.get(v) if k == "909" and v in NAMES909 else NAMES[v]) for k, ids in KITS for v in ids.split()]
DEFAULT_FLAT = []
off = [0]
for _, ids in KITS:
    off.append(off[-1] + len(ids.split()))
for b, v in [(1, 0), (1, 1), (1, 2), (1, 4), (0, 4), (0, 5), (3, 6), (3, 5),
             (1, 11), (1, 12), (2, 10), (2, 6), (0, 6), (3, 9), (2, 9), (1, 7)]:
    DEFAULT_FLAT.append(off[b] + v)

KITS_ORDER = [("6W6", 0), ("8W8", 1), ("CW-78", 2), ("9W9", 3)]
EXTRA_KNOBS = [("attack", "Attack", 64), ("tone", "Tone", 64), ("snappy", "Snappy", 64), ("noise", "Noise", 64), ("rate", "Rate", 64),
               ("sweep", "Sweep", 64), ("pmod", "Pitch Mod", 64), ("ndecay", "Noise Decay", 64), ("sat", "Saturate", 64)]
DIST = ["Diode", "Clip", "SAT", "BFZ", "PDIST", "Fold", "Crush"]   # a voice's distortion character (0..6)
params, sections = [], []
for s in range(1, 17):
    keys = []
    sk = "s%02d_" % s
    params.append({"key": sk + "src", "name": "Slot %d voice" % s, "options": SRC, "default": DEFAULT_FLAT[s - 1]})
    keys.append(sk + "src")
    # the voice's kit (read-only, derived in trkit.cpp): the skin swaps each slot's faceplate on it
    params.append({"key": sk + "fam", "name": "Kit", "options": [k for k, _ in KITS_ORDER] + ["9W9 BD"], "default": 1})
    keys.append(sk + "fam")
    for k, label, dflt in ([("level", "Level", 64), ("tune", "Tune", 64), ("decay", "Decay", 64), ("drive", "Drive", 64),
                           ("dist", "Dist", 0)] + EXTRA_KNOBS + [
                           ("pan", "Pan", 64), ("rev", "Rev", 0), ("dly", "Dly", 0)]):
        p = {"key": sk + k, "name": label, "default": dflt}
        if k == "dist": p["options"] = DIST
        else: p.update({"min": 0, "max": 127})
        params.append(p)
        keys.append(sk + k)
    sections.append({"label": "Slot %d" % s, "keys": keys})
# the editor page: edit_slot picks a slot; edit_voice and e_<knob> are that slot's voice and knobs (proxied by trkit.cpp)
ed = []
params.append({"key": "edit_slot", "name": "Slot", "options": [str(i) for i in range(1, 17)], "default": 0})
ed.append("edit_slot")
params.append({"key": "edit_voice", "name": "Voice", "options": SRC, "default": DEFAULT_FLAT[0]})
ed.append("edit_voice")
for k, label, dflt in ([("level", "Level", 64), ("tune", "Tune", 64), ("decay", "Decay", 64), ("drive", "Drive", 64),
                       ("dist", "Dist", 0)] + EXTRA_KNOBS + [
                       ("pan", "Pan", 64), ("rev", "Rev", 0), ("dly", "Dly", 0)]):
    p = {"key": "e_" + k, "name": label, "default": dflt}
    if k == "dist": p["options"] = DIST
    else: p.update({"min": 0, "max": 127})
    params.append(p)
    ed.append("e_" + k)
sections.append({"label": "Editor", "keys": ed})

# the randomise module (last page): which slots, how far, whether the voice may change, and the button
rn = []
params.append({"key": "rnd_amount", "name": "Random Amount", "min": 0, "max": 127, "default": 64}); rn.append("rnd_amount")
params.append({"key": "rnd_voice", "name": "Random Voice", "options": ["OFF", "ON"], "default": 0}); rn.append("rnd_voice")
params.append({"key": "rnd_go", "name": "Randomise", "momentary": True}); rn.append("rnd_go")
params.append({"key": "rnd_all", "name": "Select All", "momentary": True}); rn.append("rnd_all")
params.append({"key": "rnd_none", "name": "Clear", "momentary": True}); rn.append("rnd_none")
for sl in range(1, 17):
    params.append({"key": "rnd_s%02d" % sl, "name": "Slot %d" % sl, "options": ["OFF", "ON"], "default": 0}); rn.append("rnd_s%02d" % sl)
sections.append({"label": "Randomise", "keys": rn})

# the shared FX and master stage: 8W8's own keys (defaults are 8W8's), prefixed fx_
DLY_TIME = ["1/32", "1/16T", "1/16", "1/8T", "1/16.", "1/8", "1/4T", "1/8.", "1/4", "1/2T", "1/4.", "1/2", "1/2."]
MASTER_DIST = ["Off", "Diode", "Clip", "SAT", "BFZ", "PDIST", "Fold", "Crush"]
fx = []
for key, name, dflt, opts in (
        ("rev_decay", "Rev Decay", 73, None), ("rev_tone", "Rev Tone", 57, None), ("rev_hpf", "Rev HPF", 62, None),
        ("rev_level", "Rev Level", 85, None), ("dly_time", "Dly Time", 7, DLY_TIME), ("dly_fdbk", "Dly Fdbk", 52, None),
        ("dly_tone", "Dly Tone", 51, None), ("dly_hpf", "Dly HPF", 62, None), ("dly_level", "Dly Level", 85, None),
        ("master_dist", "Master Dist", 0, MASTER_DIST), ("master_drive", "Master Drive", 0, None),
        ("comp", "Comp", 0, None), ("volume", "Volume", 100, None)):
    p = {"key": "fx_" + key, "name": name, "default": dflt}
    if opts: p["options"] = opts
    else: p.update({"min": 0, "max": 127})
    params.append(p)
    fx.append("fx_" + key)
sections.append({"label": "FX and master", "keys": fx})

# appended last (the list is append-only): the kit's own reverb, delay and compressor can be switched off so that the sends
# only leave through TR-MPC Tap FX; and a second randomise button that only swaps voices
ix = []
for key, name in (("int_rev", "Internal Reverb"), ("int_dly", "Internal Delay"), ("int_comp", "Internal Comp")):
    params.append({"key": key, "name": name, "options": ["OFF", "ON"], "default": 1}); ix.append(key)
sections.append({"label": "Internal FX", "keys": ix})
params.append({"key": "rnd_go_voice", "name": "Random Voices", "momentary": True})
sections[[s["label"] for s in sections].index("Randomise")]["keys"].append("rnd_go_voice")

for p in params:   # the jog wheel: a click moves a knob three steps (wrapper "nudge_gain", mpc-vst-plugins)
    if "options" not in p and not p.get("momentary") and "min" in p and "max" in p:
        p["nudge_gain"] = 3

out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ports", "trkit", "params.json")
json.dump({"name": "TR-MPC", "params": params, "sections": sections}, open(out, "w", newline="\n"), indent=1)
print(len(params), "params,", len(SRC), "voices")
