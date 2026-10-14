#!/usr/bin/env python3
"""Draft layout.conf for a port: a header with the plugin name, then tall voice panels, as many per page as fit.

    make_layout.py            regenerate every port's ports/<kit>/layout.conf (overwrites hand edits)
    make_layout.py 6w6        one port

Each voice panel is 2 knob columns x up to 4 rows (8 controls; an enum such as Distortion takes one cell as a popup).
On the device the first draft used one column of 8 horizontal sliders per voice; MPC draws each slider's name and value
around it, so 8 of them in 570 px overlapped and clipped. A knob row needs about 130 px (arc, name, value box), so 4 rows
fit under a 44 px header, 4 panels (4 x 300 px) per page.

Panels come from the kit's page hierarchy (ports/<kit>/module.mpc.json, built by extract_module.py): one per voice, then
the send effects, then a MASTER panel with whatever no voice page lists. Palettes are the CSS variables at the top of each
ports/<kit>/src/web_ui.html, mapped onto the renderer's theme_<name> keys (tools/shadow_skin.py THEME_KEYS).
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_drum_skins as skins

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# plugin area, in the skin's own coordinates (the grid studio.py's auto-layout uses): x 0-1280, y 92-708
X0 = 8
Y_TOP = 92
HEADER_H = 44
PANEL_W = skins.PANEL_W   # fills the 1280 px width with 4 panels
PER_PAGE = 4
PANEL_H = 708 - Y_TOP - HEADER_H
TITLE_H = 44             # frame title inside the panel
ROWS = 4
KNOB_R = 26
SKIP = {"ui_focus", "mutes", "note_map"}   # engine-internal, not for the panel
FX_LEVELS = {"rev", "dly", "fxrev", "fxdly", "rhy"}   # page-hierarchy levels that are not a drum voice

THEMES = {k: v["theme"] for k, v in skins.STYLES.items()}
TITLES = {   # (big name, tagline)
    "6w6": ("6W6", "DRUMATIX - TR-606 STYLE DRUM MACHINE"),
    "8w8": ("8W8", "TR-808 STYLE DRUM MACHINE - 16 VOICES"),
    "cw78": ("CW-78", "COMPURHYTHM - CR-78 STYLE DRUM MACHINE"),
    "9w9": ("9W9", "TR-909 STYLE DRUM MACHINE"),
}


def label(s, n=12):
    t = "".join(c if c.isalnum() or c in " .-/%+:#" else " " for c in s.upper())
    return " ".join(t.split())[:n]


def panels(mod):
    cp = {p["key"]: p for p in mod["chain_params"]}
    levels = mod["ui_hierarchy"]["levels"]
    cols, used, labels = [], set(), {}
    for name, lv in levels.items():
        if name == "root":
            continue
        keys = []
        for p in lv.get("params", []):
            if isinstance(p, dict) and p.get("key") in cp and p["key"] not in SKIP:
                keys.append(p["key"])
                labels[p["key"]] = p.get("label")
        if keys:
            fx = name in FX_LEVELS
            abbr = {"rev": "REV", "fxrev": "REV", "dly": "DLY", "fxdly": "DLY", "rhy": "RHY"}.get(name) or keys[0].split("_")[0].upper()
            cols.append((label(lv.get("name") or name, 14), keys, not fx, abbr))
            used.update(keys)
    rest = [k for k in cp if k not in used and k not in SKIP and not k.endswith("__open")]
    if rest:
        cols.append(("MASTER", rest, False, "MASTER"))
    return cp, cols, labels


def cells(keys, voice):
    """key -> cell 0-7 (row = cell // 2, column = cell % 2). In a voice panel the top row is Distortion + Level, then the voice's own
    controls and Drive fill the next cells, and Rev and Dly keep the bottom row."""
    if not voice:
        return {k: i for i, k in enumerate(keys[:ROWS * 2])}
    def tail(k):
        if k.endswith("_dist_type") or k.endswith("_dist"):
            return 0
        if k.endswith("_level") or k.endswith("_volume"):
            return 1
        if k.endswith("_rev"):
            return 6
        if k.endswith("_dly"):
            return 7
        return None
    pos = {k: tail(k) for k in keys if tail(k) is not None}
    taken = set(pos.values())
    free = [c for c in range(ROWS * 2) if c not in taken]
    for k in sorted(keys, key=lambda k: k.endswith("_drive")):    # the voice's own controls first, Drive after them
        if k not in pos and free:
            pos[k] = free.pop(0)
    return pos


def build(kit):
    port = os.path.join(ROOT, "ports", kit)
    mod = json.load(open(os.path.join(port, "module.mpc.json")))
    cp, cols, labels = panels(mod)
    head = ["# Draft skin. Palette from the Schwung web UI (src/web_ui.html); header, then 2-column knob panels. "
            "Edit in Skin Studio."]
    head += ["theme_%s=%s" % tuple(kv.split("=")) for kv in THEMES[kit].split()]
    st = skins.STYLES[kit]
    head += ["art_css=skin.css", "knob_look=%s" % st["knob_look"]]
    npages = (len(cols) + PER_PAGE - 1) // PER_PAGE
    per = (len(cols) + npages - 1) // npages            # balance the pages instead of 4 + 4 + 1
    name, tagline = TITLES[kit]
    th = dict(kv.split('=') for kv in THEMES[kit].split())
    acc, dim = th['accent'], th['ink_dim']
    body = []
    pitch = (PANEL_H - TITLE_H - 6) // ROWS
    for pg in range(npages):
        group = cols[pg * per:(pg + 1) * per]
        rng = group[0][3] + (" - " + group[-1][3] if len(group) > 1 else "")
        plate = "images/plate_pg%d.svg" % (pg + 1)
        os.makedirs(os.path.join(port, "images"), exist_ok=True)
        open(os.path.join(port, plate), "w", newline="\n").write(skins.plate_svg(kit, len(group), [g[0] for g in group], name))
        body += ["", "[tab %s]" % rng, "art file=%s fit=stretch" % plate]
        qsets = []
        top = Y_TOP + HEADER_H
        overview = []
        for i, (ptitle, keys, voice, abbr) in enumerate(group):
            fx = X0 + i * PANEL_W
            body.append('frame x=%d y=%d w=%d h=%d title=""' % (fx + 2, top, PANEL_W - 6, PANEL_H))
            pos = cells(keys, voice)
            for k in keys:
                if k not in pos:
                    continue
                row, col = divmod(pos[k], 2)
                cx = fx + 80 + col * 154
                ry = top + TITLE_H + row * pitch
                nm = label(labels.get(k) or cp[k].get("name") or k, 12)
                if cp[k].get("type") == "enum":
                    body.append('popup cx=%d cy=%d w=130 h=48 label="%s" key=%s' % (cx, ry + 66, nm, k))
                else:
                    yl = " look=metal" if kit == "8w8" and k.endswith("_level") else ""   # 808: yellow level knobs
                    body.append('knob cx=%d cy=%d r=%d label="%s" key=%s%s' % (cx, ry + 38, KNOB_R, nm, k, yl))
            qsets.append((ptitle, keys[:16]))
            # the page's first Q-Link set (it names the tab): this panel's first two controls, drive and level
            pick = [k for k in keys[:2]] + [k for k in keys if k.endswith(("_drive", "_level", "_volume"))][:2]
            overview += list(dict.fromkeys(pick))[:4]
        body.append('qlinks "%s" = %s' % (rng, ",".join(overview[:16])))
        for ptitle, keys in qsets:
            body.append('qlinks "%s" = %s' % (ptitle, ",".join(keys)))
    out = os.path.join(port, "layout.conf")
    open(out, "w", newline="\n").write("\n".join(head + body) + "\n")
    vp = os.path.join(port, "vst.json")
    v = json.load(open(vp))
    if v.get("layout") != "layout.conf":
        v["layout"] = "layout.conf"
        json.dump(v, open(vp, "w", newline="\n"), indent=2)
    over = [c[0] for c in cols if len(c[1]) > ROWS * 2]
    print(kit, "panels:", len(cols), "pages:", npages, "max controls:", max(len(c[1]) for c in cols), "over 8:", over)


for k in (sys.argv[1:] or THEMES):
    build(k)
