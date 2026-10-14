#!/usr/bin/env python3
"""Per-family skin styling for the four drum ports (6w6, 8w8, cw78, 9w9): palette, fonts, faceplate art.

    gen_drum_skins.py [kit ...]     writes ports/<kit>/skin.css, ports/<kit>/images/plate_<n>.svg and ports/<kit>/fonts/;
                                    then run tools/make_layout.py (it imports STYLES for theme, header and art lines).

Reference hardware (Roland TR-606 Drumatix, TR-808, CR-78 CompuRhythm, TR-909): palettes and typography per kit in STYLES.
Faceplate art is shapes only (SVG drawn as an <image>, so custom fonts do not apply inside it); all lettering is live
frame titles / text widgets set in the kit's fonts through skin.css (@font-face) and `fontfile=`.

Fonts come from the sibling mpc-vst checkout (Assets/fonts, FONT_SRC) and are copied into ports/<kit>/fonts/. They are
commercial/unknown-licence typefaces: ports/*/fonts/ is gitignored, so they never get published from this repo.
"""
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
FONT_SRC = os.environ.get("FONT_SRC", os.path.join(ROOT, "..", "mpc-vst", "Assets", "fonts"))
W, H = 1280, 628
Y_OFF = 86
X0, PANEL_W, TOP, BOT = 8, 316, 136, 708      # must match make_layout.py
HEAD_Y0, HEAD_Y1 = 92, 136
HEADER_CY = 116                                # layout y of the logo's centre line

FONTS = {   # family -> (source file under FONT_SRC, copied name)
    "Earth": ("Earth Normal.ttf", "Earth.ttf"),
    "Filmotype": ("FilmotypeFord.ttf", "FilmotypeFord.ttf"),
    "Eurostile": ("EuroStyle Normal.ttf", "EuroStyle.ttf"),
    "Univers": ("Univers 53 Extended Regular.otf", "UniversExt.otf"),
    "Helvetica": ("helvetica-255/Helvetica-Bold.ttf", "HelveticaBold.ttf"),
}

STYLES = {
    # TR-606: silver-grey aluminium, near-black printing, red LEDs/outline, cobalt accents; Helvetica titles, Univers logo
    "6w6": dict(
        theme="bg=b9bbb4 ink=1a1d24 ink_dim=4c4f55 accent=dd0000 accent_hi=ff3b30 seg_active=1a1d24 seg_inactive=d9dad4 "
              "seg_active_tx=ecd5aa lcd=1a1d24 line=6f7279 btn_bg=d9dad4 btn_text=1a1d24 box=cacbc4 display_ink=dd0000 "
              "knob_face=e4e5e1 knob_ring=7b7e84 knob_dot=1a1d24",
        knob_look="metal", logo=("DRUMATIX", "Eurostile"), logo_color="1a1d24", logo_size=3.0,
        tag="TR-606  COMPUTER CONTROLLED", tag_color="dd0000", fonts=("Helvetica", "Helvetica"),
        title_color="1a1d24", title_size="17px", title_spacing="0.1em"),
    # TR-808: charcoal panel, white lettering, red / orange / yellow stripes
    "8w8": dict(
        theme="bg=202020 ink=ffffff ink_dim=a8a49a accent=f8a125 accent_hi=f1f827 seg_active=f8a125 seg_inactive=3a3a3a "
              "seg_active_tx=202020 lcd=141414 line=6b6b6b btn_bg=3a3a3a btn_text=ffffff box=141414 display_ink=f8a125 "
              "knob_face=121212 knob_ring=3c3c3c knob_dot=111111",
        knob_look="cap", logo=("8W8", "Eurostile"), logo_color="e72e2e", logo_size=3.0,
        tag="RHYTHM COMPOSER  TR-808 STYLE", tag_color="f8a125", fonts=("Eurostile", "Helvetica"),
        title_color="111111", title_size="18px", title_spacing="0.1em",
        css=".look-cap-ring { fill: #121212; } .look-cap-top { fill: #f1f1ec; } .look-line { stroke: #111; }\n"
            ".look-metal { fill: #e8b030; stroke: #5a4310; } .look-metal-top { fill: #f6cf55; } .look-notch { stroke: #111; }\n"),
    # CR-78: black panel, orange headings, cream lettering, red / avocado / yellow / blue pattern buttons, wood cheeks
    "cw78": dict(
        theme="bg=1c1d21 ink=ecd5aa ink_dim=9c9378 accent=ff5a00 accent_hi=ff8a3d seg_active=ff5a00 seg_inactive=2a2b30 "
              "seg_active_tx=1c1d21 lcd=121316 line=7a735f btn_bg=2a2b30 btn_text=ecd5aa box=121316 display_ink=ff5a00 "
              "knob_face=101112 knob_ring=5a5547 knob_dot=dd0000",
        knob_look="cap", logo=("CompuRhythm", "Filmotype"), logo_color="ff5a00", logo_size=3.0,
        tag="CW-78  CR-78 STYLE", tag_color="ecd5aa", fonts=("Helvetica", "Helvetica"),
        title_color="ff5a00", title_size="16px", title_spacing="0.08em",
        css=".look-cap-ring { fill: #0d0d0e; } .look-cap-top { fill: #1a1a1c; } .look-line { stroke: #dd0000; }\n"),
    # TR-909: warm grey chassis, charcoal section bars with white lettering, red-orange logo, blue rules
    "9w9": dict(
        theme="bg=e6e2d6 ink=1f1f1f ink_dim=4b5566 accent=ef6c1f accent_hi=ff8a3d seg_active=2b2c30 seg_inactive=d9d5c8 "
              "seg_active_tx=ef6c1f lcd=2b2c30 line=8a8678 btn_bg=d9d5c8 btn_text=1f1f1f box=cfcbbd display_ink=e4002b "
              "knob_face=151515 knob_ring=8b8880 knob_dot=ef6c1f",
        knob_look="cap", logo=("9W9", "Earth"), logo_color="2b2c30", logo_size=3.4,
        tag="RHYTHM COMPOSER", tag_color="4b5566", fonts=("Helvetica", "Helvetica"),
        title_color="ef6c1f", title_size="15px", title_spacing="0.14em",
        css=".look-cap-ring { fill: #101010; } .look-cap-top { fill: #1b1b1b; } .look-line { stroke: #ef6c1f; }\n"
            ".box { fill: #2b2c30; } .box-label { fill: #4b5566; }\n"),
}


_FONTS = {}


def _font(fam):
    if fam not in _FONTS:
        from fontTools.ttLib import TTFont
        f = TTFont(os.path.join(FONT_SRC, FONTS[fam][0]))
        cmap = f.getBestCmap()
        os2 = f["OS/2"]
        cap = getattr(os2, "sCapHeight", 0) or 0.7 * f["head"].unitsPerEm
        _FONTS[fam] = (f, cmap, f.getGlyphSet(), f["head"].unitsPerEm, cap)
    return _FONTS[fam]


def text_path(fam, s, x, cy, px, fill, spacing=0.0, opacity=1.0, anchor="start", bold=0.035):
    """Text as outline paths (no font file needed at build time). cy is the vertical centre of the capitals."""
    from fontTools.pens.svgPathPen import SVGPathPen
    f, cmap, gs, upem, cap = _font(fam)
    k = px / float(upem)
    names = [cmap[ord(c)] for c in s if ord(c) in cmap]
    adv = [f["hmtx"][n][0] * k + spacing for n in names]
    width = sum(adv) - spacing
    x0 = x - (width / 2 if anchor == "middle" else width if anchor == "end" else 0)
    base = cy + cap * k / 2
    out, cx = [], x0
    for n, a in zip(names, adv):
        pen = SVGPathPen(gs)
        gs[n].draw(pen)
        if pen.getCommands():
            out.append('<path transform="translate(%.2f %.2f) scale(%.5f %.5f)" d="%s"/>' % (cx, base, k, -k, pen.getCommands()))
        cx += a
    sw = bold * upem      # the renderer's text was synthetic-bold; a stroke in glyph units gives the same weight
    return '<g fill="#%s" stroke="#%s" stroke-width="%g" stroke-linejoin="round" opacity="%g">%s</g>' % (fill, fill, sw, opacity, "".join(out)), width


def panel_xs(n):
    return [X0 + i * PANEL_W + 2 for i in range(n)]


def stripes(x, y, w, colors, h=3, gap=2):
    return "".join('<rect x="%g" y="%g" width="%g" height="%d" fill="#%s"/>' % (x, y + i * (h + gap), w, h, c)
                   for i, c in enumerate(colors))


def screw(cx, cy):
    return ('<circle cx="%g" cy="%g" r="6" fill="url(#screw)" stroke="#000" stroke-opacity="0.6"/>'
            '<rect x="%g" y="%g" width="8" height="1.6" fill="#222" transform="rotate(35 %g %g)"/>' % (cx, cy, cx - 4, cy - 0.8, cx, cy))


DEFS = (
    '<defs>'
    # brushed metal: stretched noise, overlaid on a base gradient (SVG filters render fine in an <image>)
    '<filter id="brush" x="0" y="0" width="100%" height="100%"><feTurbulence type="fractalNoise" baseFrequency="0.0008 1.1" numOctaves="4" seed="7"/>'
    '<feColorMatrix type="matrix" values="1.9 0 0 0 -0.45  1.9 0 0 0 -0.45  1.9 0 0 0 -0.45  0 0 0 0 1"/></filter>'
    '<filter id="fine" x="0" y="0" width="100%" height="100%"><feTurbulence type="fractalNoise" baseFrequency="0.9" numOctaves="2" seed="3"/>'
    '<feColorMatrix type="matrix" values="1.6 0 0 0 -0.3  1.6 0 0 0 -0.3  1.6 0 0 0 -0.3  0 0 0 0 1"/></filter>'
    '<filter id="grainv" x="0" y="0" width="100%" height="100%"><feTurbulence type="fractalNoise" baseFrequency="0.02 0.6" numOctaves="4" seed="11"/>'
    '<feColorMatrix type="matrix" values="1.5 0 0 0 -0.25  1.2 0 0 0 -0.25  0.8 0 0 0 -0.25  0 0 0 0 1"/></filter>'
    '<linearGradient id="lit" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#fff" stop-opacity="0.22"/><stop offset="0.45" stop-color="#fff" stop-opacity="0"/><stop offset="1" stop-color="#000" stop-opacity="0.18"/></linearGradient>'
    '<radialGradient id="vig" cx="0.5" cy="0.45" r="0.8"><stop offset="0.6" stop-color="#000" stop-opacity="0"/><stop offset="1" stop-color="#000" stop-opacity="0.35"/></radialGradient>'
    '<radialGradient id="screw" cx="0.35" cy="0.3" r="0.8"><stop offset="0" stop-color="#e2e4e8"/><stop offset="1" stop-color="#6a6d74"/></radialGradient>'
    '<linearGradient id="woodh" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#7a5232"/><stop offset="1" stop-color="#4a2f1b"/></linearGradient>'
    '<linearGradient id="wood" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="#5a3a22"/><stop offset="0.5" stop-color="#7a5232"/><stop offset="1" stop-color="#4a2f1b"/></linearGradient>'
    '<filter id="woodg" x="0" y="0" width="100%" height="100%"><feTurbulence type="fractalNoise" baseFrequency="0.02 0.4" numOctaves="3" seed="5"/>'
    '<feColorMatrix type="matrix" values="0.9 0 0 0 0.1  0.6 0 0 0 0.02  0.4 0 0 0 0  0 0 0 0 1"/></filter>'
    '</defs>')


def base(kit):
    """The full-canvas chassis with its photographic texture."""
    full = 'x="0" y="0" width="%d" height="%d"' % (W, H)
    if kit == "6w6":     # brushed aluminium
        g = ('<linearGradient id="b6" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#d5d7d2"/><stop offset="0.5" stop-color="#bfc1bb"/><stop offset="1" stop-color="#a9aca5"/></linearGradient>'
             '<rect %s fill="url(#b6)"/><rect %s filter="url(#brush)" style="mix-blend-mode:overlay" opacity="0.5"/>'
             '<rect %s fill="url(#lit)"/><rect %s fill="url(#vig)"/>')
        return g % (full, full, full, full)
    if kit == "9w9":     # cream powder-coated steel
        g = ('<linearGradient id="b9" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#ece8dc"/><stop offset="1" stop-color="#dcd8ca"/></linearGradient>'
             '<rect %s fill="url(#b9)"/><rect %s filter="url(#fine)" style="mix-blend-mode:overlay" opacity="0.28"/>'
             '<rect %s fill="url(#lit)" opacity="0.5"/><rect %s fill="url(#vig)" opacity="0.6"/>')
        cheek = ('<linearGradient id="silv" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="#8f9096"/><stop offset="0.45" stop-color="#e9eaec"/><stop offset="1" stop-color="#a3a4aa"/></linearGradient>'
                 '<rect x="0" y="0" width="8" height="%d" fill="url(#silv)"/><rect x="%d" y="0" width="8" height="%d" fill="url(#silv)"/>'
                 '<rect x="8" y="0" width="1" height="%d" fill="#000" opacity="0.35"/><rect x="%d" y="0" width="1" height="%d" fill="#000" opacity="0.35"/>') % (H, W - 8, H, H, W - 9, H)
        return g % (full, full, full, full) + cheek
    if kit == "8w8":     # black powder-coated steel
        return ('<rect %s fill="#1f1f1f"/><rect %s filter="url(#fine)" style="mix-blend-mode:overlay" opacity="0.35"/>'
                '<rect %s fill="url(#lit)" opacity="0.5"/><rect %s fill="url(#vig)"/>') % (full, full, full, full)
    # cw78: satin black inside a thin wood bezel on all four sides
    wood = '<path clip-rule="evenodd" d="M0 0H%d V%d H0Z M8 6H%d V%d H8Z"' % (W, H, W - 8, H - 6)
    return ('<rect %(f)s fill="#1c1d21"/><rect %(f)s filter="url(#fine)" style="mix-blend-mode:overlay" opacity="0.3"/>'
            '<rect %(f)s fill="url(#lit)" opacity="0.45"/><rect %(f)s fill="url(#vig)"/>'
            '<clipPath id="bez">%(w)s/></clipPath>'
            '<g clip-path="url(#bez)"><rect %(f)s fill="url(#wood)"/><rect %(f)s filter="url(#woodg)" style="mix-blend-mode:overlay" opacity="0.8"/></g>'
            '<rect x="8" y="%(by)d" width="%(bw)d" height="1" fill="#000" opacity="0.5"/>') % dict(f=full, w=wood, by=H - 7, bw=W - 16)


def plate_svg(kit, n, titles=(), name=""):
    """Faceplate for one page: n voice panels, then blank plates up to 4 (shapes and textures only)."""
    y0, y1 = TOP - Y_OFF, BOT - Y_OFF
    hy0, hy1 = HEAD_Y0 - Y_OFF, HEAD_Y1 - Y_OFF
    pw = PANEL_W - 6
    xs = panel_xs(4)
    st = STYLES[kit]
    dim = dict(kv.split("=") for kv in st["theme"].split())["ink_dim"]
    d = [DEFS, base(kit)]
    lg, lf = st["logo"]
    hy = HEADER_CY + (3 if kit == "8w8" else 0) - Y_OFF
    g, _ = text_path(lf, lg, 24, hy, st["logo_size"] * 10, st["logo_color"])
    d.append(g)
    g, _ = text_path(st["fonts"][1], st["tag"], 24 + int(st["logo_size"] * 15 * len(lg)) + 40, hy + 4, 13, st["tag_color"], spacing=2, bold=0.02)
    d.append(g)
    if kit == "6w6":
        d.append('<rect x="0" y="%d" width="%d" height="3" fill="#1a1d24"/>' % (hy1 - 2, W))
        cy = (hy0 + hy1) / 2
        d.append('<circle cx="1232" cy="%g" r="5" fill="#dd0000"/><circle cx="1232" cy="%g" r="9" fill="none" stroke="#1a1d24" stroke-width="2"/>' % (cy, cy))
    elif kit == "8w8":   # the 808's four-colour rule sits across the top of the header only; step keys below it, right
        d.append(stripes(0, hy0 - 4, W, ["e72e2e", "f8a125", "f1f827", "ffffff"], 2, 1))
        cols = ["e72e2e"] * 4 + ["f8a125"] * 4 + ["f1f827"] * 4 + ["ffffff"] * 4
        for i, c in enumerate(cols):
            d.append('<rect x="%d" y="%d" width="16" height="14" rx="2" fill="#%s"/>' % (1004 + i * 15 + (i // 4) * 4, hy0 + 22, c))
    elif kit == "cw78":
        d.append('<rect x="8" y="%d" width="%d" height="2" fill="#ff5a00" fill-opacity="0.9"/>' % (hy1 - 2, W - 16))
    tpx = float(st["title_size"].rstrip("px"))
    title_w = [text_path(st["fonts"][0], s, 0, 0, tpx, "000", spacing=tpx * float(st["title_spacing"].rstrip("em")))[1] for s in titles]
    for i, x in enumerate(xs):
        if i >= n:       # blank plate: screws, no title bar; the model name is a text widget in the layout
            d.append('<rect x="%g" y="%d" width="%d" height="%d" rx="3" fill="#000" fill-opacity="0.08" stroke="#000" stroke-opacity="0.35" stroke-width="1.5"/>' % (x, y0, pw, y1 - y0))
            for sx, sy in ((x + 14, y0 + 14), (x + pw - 14, y0 + 14), (x + 14, y1 - 14), (x + pw - 14, y1 - 14)):
                d.append(screw(sx, sy))
            bl = lg if len(lg) <= 8 else name       # a long logo (CompuRhythm) gives way to the model name
            mid = (y0 + y1) // 2
            g, _ = text_path(lf, bl, x + pw / 2, mid - 10, min(st["logo_size"] * 1.3, 230 / (15.0 * len(bl))) * 10, st["logo_color"], opacity=0.55, anchor="middle")
            d.append(g)
            g, _ = text_path(st["fonts"][1], st["tag"], x + pw / 2, mid + 34, 10, dim, spacing=2, opacity=0.6, anchor="middle", bold=0.02)
            d.append(g)
            continue
        if kit == "6w6":     # recessed panel, thin black frame, red/cobalt tick under the title
            d.append('<rect x="%g" y="%d" width="%d" height="%d" rx="3" fill="#000" fill-opacity="0.07" stroke="#1a1d24" stroke-width="1.5"/>' % (x, y0, pw, y1 - y0))
            d.append('<rect x="%g" y="%d" width="%d" height="3" fill="#1a1d24"/>' % (x + 14, y0 + 36, pw - 28))
        elif kit == "8w8":   # one thin white rule under the title; the colour stripes stay in the header
            d.append('<rect x="%g" y="%d" width="%d" height="%d" rx="2" fill="#000" fill-opacity="0.22" stroke="#4a4a4a" stroke-width="1.2"/>' % (x, y0, pw, y1 - y0))
            d.append('<rect x="%g" y="%d" width="%d" height="26" rx="13" fill="#f3efdc"/>' % (x + 10, y0 + 7, title_w[i] + 28))
        elif kit == "cw78":
            d.append('<rect x="%g" y="%d" width="%d" height="%d" rx="12" fill="#000" fill-opacity="0.25" stroke="#ecd5aa" stroke-width="2" stroke-opacity="0.7"/>' % (x, y0, pw, y1 - y0))
            for k, c in enumerate(["dd0000", "cacb9c", "f2ff2c", "2f3bff"]):
                d.append('<rect x="%g" y="%d" width="20" height="8" rx="2" fill="#%s"/>' % (x + pw - 14 - (4 - k) * 24, y0 + 20, c))
            d.append('<rect x="%g" y="%d" width="%d" height="1.5" fill="#ff5a00"/>' % (x + 14, y0 + 40, pw - 28))
        else:
            d.append('<rect x="%g" y="%d" width="%d" height="%d" fill="#000" fill-opacity="0.06" stroke="#7d7a72" stroke-width="1"/>' % (x, y0, pw, y1 - y0))
            d.append('<rect x="%g" y="%d" width="%d" height="30" fill="#4b5566"/>' % (x, y0, pw))
        if i < len(titles):
            g, _ = text_path(st["fonts"][0], titles[i], x + (24 if kit == "8w8" else 18), y0 + 19, tpx, st["title_color"],
                             spacing=tpx * float(st["title_spacing"].rstrip("em")))
            d.append(g)
    return '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" width="%d" height="%d">%s</svg>\n' % (W, H, W, H, "".join(d))


def css(kit):
    """Only knob/box restyling: all lettering is outlined into the plate art, so no font files are needed."""
    s = STYLES[kit]
    return ("/* %s skin: generated by tools/gen_drum_skins.py. Faceplate art and lettering are images/plate_pg*.svg. */\n"
            ":root { --label-size: 14px; --sheen: 0.06; }\n"
            ".frame-border { fill: none; stroke: none; }\n.frame-rule { stroke: none; }\n"
            ".knob-face, .look-metal, .look-body, .look-cap-ring, .look-fader { filter: none; }\n"
            ".look-metal, .look-cap-ring { stroke: #000; stroke-opacity: 0.55; }\n"
            ".box-label { letter-spacing: 0.06em; }\n" % kit) + s.get("css", "")


def build(kit):
    port = os.path.join(ROOT, "ports", kit)
    os.makedirs(os.path.join(port, "images"), exist_ok=True)
    shutil.rmtree(os.path.join(port, "fonts"), ignore_errors=True)
    for f in os.listdir(os.path.join(port, "images")):
        if f.startswith("plate_"):
            os.remove(os.path.join(port, "images", f))
    open(os.path.join(port, "skin.css"), "w", newline="\n").write(css(kit))
    print(kit, "skin.css (plates and lettering are written by make_layout.py; needs fontTools and FONT_SRC)")


if __name__ == "__main__":
    for k in (sys.argv[1:] or STYLES):
        build(k)
