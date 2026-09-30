#!/usr/bin/env python3
"""Builds the Punch2Pen marks as SVG from one set of geometry.

    pip install fonttools brotli   # only needed for the wordmark
    python3 design/brand/build_marks.py

Marks (drawn from the founder's sketches, 2026-09):
  fist      2PEN knuckle fist with the lightning bolt behind it (index-card sketch)
  pen       retractable pen whose clip is a "2" (green-marker sketch)
  two       the outlined "2" on its own: app icon, favicon, plugin corner
  bolt      the lightning bolt glyph
  wordmark  PUNCH2PEN set in Archivo 75/900, outlined to paths, green 2

Each mark is written for two grounds:
  -booth  graphite ground (plugin, site)   outline ink, fill graphite, green-400
  -pad    paper ground (lyric sheet, print) outline marker black, fill paper, marker green
"""
from __future__ import annotations

import io
import json
import os
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
TOKENS = json.loads((HERE.parent / "tokens" / "tokens.json").read_text())


def tok(path: str) -> str:
    node = TOKENS
    for key in path.split("."):
        node = node[key]
    value = node["$value"]
    if value.startswith("{"):
        return tok(value.strip("{}"))
    return value


GROUNDS = {
    "booth": {
        "line": tok("color.ink.base"),
        "fill": tok("color.graphite.850"),
        "accent": tok("color.green.400"),
        "letter": tok("color.ink.base"),
    },
    "pad": {
        "line": tok("color.pad.ink"),
        "fill": tok("color.pad.paper"),
        "accent": tok("color.green.700"),
        "letter": tok("color.pad.ink"),
    },
}

# --- Geometry --------------------------------------------------------------

# Monoline marker letters, 18 x 26 box, origin top-left. Same hand as the knuckles.
LETTERS = {
    "2": "M1.5 7.2C1.5 3 4.8 .6 9 .6s7.6 2.6 7.6 6.6c0 3.4-2.2 5.4-5.8 8.6L1.6 25.4H17",
    "P": "M2 25.6V.6h8c4.6 0 7 2.7 7 6.7s-2.4 6.9-7 6.9H2",
    "E": "M16.4.6H2v24.8h14.4M2 13h11.4",
    "N": "M2 25.8V.6l14.2 25V.2",
}

# Knuckles: four capsules, left to right, carrying 2 / P / E / N.
FINGERS = [  # x, width, top, bottom
    (34, 46, 78, 178),
    (80, 46, 66, 184),
    (126, 46, 70, 181),
    (172, 42, 84, 172),
]
THUMB = (100, 156, 126, 46)  # x, y, width, height
# Bolt behind the fist: the straight top shows above the knuckles, the jag and
# tip show below, like the index-card sketch.
BOLT = [(122, 6), (172, 6), (99, 208), (136, 208), (70, 276), (76, 236), (39, 236)]
# The standalone glyph keeps the classic single-jag proportions.
BOLT_GLYPH = [(120, 8), (166, 8), (137, 100), (171, 100), (80, 272), (106, 146), (76, 146)]


def bolt_path(points, dx=0.0, dy=0.0, scale=1.0) -> str:
    pts = [((x * scale) + dx, (y * scale) + dy) for x, y in points]
    return "M" + " L".join(f"{x:.1f} {y:.1f}" for x, y in pts) + " Z"


def capsule(x, y, w, h) -> str:
    r = w / 2
    return (
        f"M{x} {y + r}A{r} {r} 0 0 1 {x + w} {y + r}"
        f"V{y + h - r}A{r} {r} 0 0 1 {x} {y + h - r}Z"
    )


def svg(view_w, view_h, body, title) -> str:
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{view_w if str(view_w).count(" ") else f"0 0 {view_w}"} {view_h}" '
        f'role="img" aria-label="{title}">\n<title>{title}</title>\n{body}\n</svg>\n'
    )


def fist(c) -> str:
    def stroke_w(w):
        return f'stroke="{c["line"]}" stroke-width="{w}" stroke-linejoin="round" stroke-linecap="round"'

    stroke = stroke_w(7)
    parts = [f'<path d="{bolt_path(BOLT)}" fill="{c["accent"]}" {stroke}/>']
    # back of the hand fills the notches between knuckles
    parts.append(f'<rect x="40" y="100" width="170" height="72" rx="18" fill="{c["fill"]}" {stroke}/>')
    for x, w, top, bottom in reversed(FINGERS):
        parts.append(f'<path d="{capsule(x, top, w, bottom - top)}" fill="{c["fill"]}" {stroke}/>')
    tx, ty, tw, th = THUMB
    r = th / 2
    parts.append(
        f'<path d="M{tx + r} {ty}H{tx + tw - r}A{r} {r} 0 0 1 {tx + tw - r} {ty + th}H{tx + r}'
        f'A{r} {r} 0 0 1 {tx + r} {ty}Z" fill="{c["fill"]}" {stroke}/>'
    )
    for (x, w, top, _), ch in zip(FINGERS, "2PEN"):
        lx = x + w / 2 - 9.3
        ly = top + 22
        parts.append(
            f'<path d="{LETTERS[ch]}" transform="translate({lx:.1f} {ly})" fill="none" '
            f'stroke="{c["letter"]}" stroke-width="5" stroke-linecap="round" stroke-linejoin="round"/>'
        )
    return svg(240, 280, "\n".join(parts), "Punch2Pen knuckle mark")


# The clip 2: one centreline, drawn twice (outline under, fill over) like the marker sketch.
TWO_CLIP = "M214 22C214 10 224 4 236 4C249 4 258 12 258 23C258 33 250 39 238 46L214 60H326"


def pen(c) -> str:
    line = c["line"]
    def stroke_w(w):
        return f'stroke="{line}" stroke-width="{w}" stroke-linejoin="round" stroke-linecap="round"'

    stroke = stroke_w(5)
    parts = [
        # plunger + end cap
        f'<rect x="330" y="62" width="20" height="16" rx="4" fill="{c["fill"]}" {stroke}/>',
        f'<rect x="312" y="57" width="22" height="26" rx="6" fill="{c["fill"]}" {stroke}/>',
        # barrel
        f'<rect x="124" y="58" width="192" height="24" rx="3" fill="{c["fill"]}" {stroke}/>',
        # grip with three rings
        f'<rect x="64" y="54" width="64" height="32" rx="9" fill="{c["fill"]}" {stroke}/>',
        f'<path d="M78 55v30M90 55v30M102 55v30" fill="none" {stroke_w(3.5)}/>',
        # cone and nib
        f'<path d="M66 60L24 66V74L66 80Z" fill="{c["fill"]}" {stroke}/>',
        f'<path d="M24 66L8 70L24 74" fill="{line}" {stroke_w(4)}/>',
        # clip "2": outline then fill, same path
        f'<path d="{TWO_CLIP}" fill="none" stroke="{line}" stroke-width="17" stroke-linecap="round" stroke-linejoin="round"/>',
        f'<path d="{TWO_CLIP}" fill="none" stroke="{c["accent"]}" stroke-width="8" stroke-linecap="round" stroke-linejoin="round"/>',
    ]
    return svg("0 -12 360", 112, "\n".join(parts), "Punch2Pen pen mark")


TWO_ICON = "M19 23C19 15.5 24.5 11 32 11C39.8 11 45 15.8 45 22.6C45 28.6 41.4 32 35.4 36.6L20 49H46"


def two(c, ground=True) -> str:
    parts = []
    if ground:
        parts.append(f'<rect width="64" height="64" rx="14" fill="{c["fill"]}"/>')
    parts.append(f'<path d="{TWO_ICON}" fill="none" stroke="{c["line"]}" stroke-width="12" stroke-linecap="round" stroke-linejoin="round"/>')
    parts.append(f'<path d="{TWO_ICON}" fill="none" stroke="{c["accent"]}" stroke-width="5.5" stroke-linecap="round" stroke-linejoin="round"/>')
    return svg(64, 64, "\n".join(parts), "Punch2Pen 2")


def favicon(c) -> str:
    # 16-32px: the outline would muddy, so a solid green 2 on graphite.
    return svg(
        32,
        32,
        f'<rect width="32" height="32" rx="7" fill="{c["fill"]}"/>\n'
        f'<path d="M9.6 11.6C9.6 8 12.3 5.8 16 5.8S22.4 8 22.4 11.3C22.4 14.2 20.6 15.9 17.7 18.2L10 24.6H22.8" '
        f'fill="none" stroke="{c["accent"]}" stroke-width="3.6" stroke-linecap="round" stroke-linejoin="round"/>',
        "Punch2Pen",
    )


def bolt(c) -> str:
    return svg(
        110,
        280,
        f'<path d="{bolt_path(BOLT_GLYPH, dx=-68)}" fill="{c["accent"]}" stroke="{c["line"]}" stroke-width="6" stroke-linejoin="round"/>',
        "Punch2Pen bolt",
    )


# --- Wordmark: Archivo 75/900 outlined -------------------------------------

ARCHIVO_CSS = "https://fonts.googleapis.com/css2?family=Archivo:wdth,wght@62..125,100..900&display=swap"
UA = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120 Safari/537.36"


def archivo_latin() -> bytes:
    css = subprocess.run(["curl", "-sS", "-A", UA, ARCHIVO_CSS], check=True, capture_output=True).stdout.decode()
    # last @font-face block is the basic Latin subset
    url = css.split("/* latin */")[-1].split("url(")[1].split(")")[0]
    return subprocess.run(["curl", "-sS", url], check=True, capture_output=True).stdout


def wordmark_paths(text="PUNCH2PEN", wdth=75, wght=900, cap=100):
    from fontTools.pens.svgPathPen import SVGPathPen
    from fontTools.pens.transformPen import TransformPen
    from fontTools.ttLib import TTFont
    from fontTools.varLib.instancer import instantiateVariableFont

    font = TTFont(io.BytesIO(archivo_latin()))
    font = instantiateVariableFont(font, {"wdth": wdth, "wght": wght})
    cmap = font.getBestCmap()
    glyphs = font.getGlyphSet()
    cap_height = font["OS/2"].sCapHeight
    scale = cap / cap_height
    tracking = 0.02 * font["head"].unitsPerEm
    x = 0.0
    out = []
    for ch in text:
        name = cmap[ord(ch)]
        pen = SVGPathPen(glyphs)
        glyphs[name].draw(TransformPen(pen, (scale, 0, 0, -scale, x * scale, cap)))
        out.append((ch, pen.getCommands()))
        x += glyphs[name].width + tracking
    width = (x - tracking) * scale
    return out, width, cap


def wordmark(c, paths, width, cap) -> str:
    pad = 4
    body = []
    for i, (ch, d) in enumerate(paths):
        color = c["accent"] if ch == "2" else c["letter"]
        body.append(f'<path d="{d}" fill="{color}"/>')
    return svg(f"{width + pad * 2:.1f}", cap + pad * 2, f'<g transform="translate({pad} {pad})">' + "\n".join(body) + "</g>", "Punch2Pen")


def main():
    out = HERE
    written = []
    for ground, c in GROUNDS.items():
        for name, fn in [("fist", fist), ("pen", pen), ("two", two), ("bolt", bolt)]:
            p = out / f"{name}-{ground}.svg"
            p.write_text(fn(c))
            written.append(p.name)
    (out / "favicon.svg").write_text(favicon(GROUNDS["booth"]))
    written.append("favicon.svg")
    try:
        paths, width, cap = wordmark_paths()
        for ground, c in GROUNDS.items():
            p = out / f"wordmark-{ground}.svg"
            p.write_text(wordmark(c, paths, width, cap))
            written.append(p.name)
    except ModuleNotFoundError:
        print("fonttools not installed: skipped wordmark (pip install fonttools brotli)")
    print("wrote", ", ".join(written))


if __name__ == "__main__":
    main()
