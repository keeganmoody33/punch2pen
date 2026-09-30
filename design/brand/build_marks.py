#!/usr/bin/env python3
"""Builds the Punch2Pen marks as SVG.

    pip install fonttools brotli   # only needed for the wordmark
    python3 design/brand/trace_logo.py   # only when the source logo changes
    python3 design/brand/build_marks.py

Marks:
  fist      the founder's 2PEN logo (source/logo-legal-pad.webp), vector-traced
            by trace_logo.py: black keyline, white fist and bolt, red P E N and badge
  pen       retractable pen whose clip is a red "2" (the green-marker sketch, recolored)
  two       the red 2 badge from the logo: app icon, favicon, plugin corner
  bolt      the lightning bolt glyph, white with a black keyline like the logo
  wordmark  PUNCH2PEN in Archivo 75/900, outlined to paths, the 2 set as the badge

Each mark is written for up to three grounds:
  -booth   graphite ground on the site: red badge, white type
  -pad     legal-yellow ground (lyric sheet, print): red badge, pen-black type
  -plugin  graphite ground inside the plugin: the badge goes one-color (white
           disc, 2 cut through), because a red dot in a DAW means record-armed
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


FIST = json.loads((HERE / "fist-paths.json").read_text())
BADGE = FIST["badge"]
KEYLINE = "#0B0B0C"  # the logo's black: a touch off pure black, like the print
WHITE = "#FFFFFF"

GROUNDS = {
    "booth": {
        "line": tok("color.ink.base"),
        "fill": tok("color.graphite.850"),
        "accent": tok("color.red.600"),
        "letter": tok("color.ink.base"),
        "badge": tok("color.red.600"),
    },
    "pad": {
        "line": tok("color.pad.ink"),
        "fill": WHITE,
        "accent": tok("color.red.600"),
        "letter": tok("color.pad.ink"),
        "badge": tok("color.red.600"),
        "tile": tok("color.pad.paper"),
    },
    "plugin": {
        "line": tok("color.ink.base"),
        "fill": tok("color.graphite.850"),
        "accent": tok("color.ink.base"),
        "letter": tok("color.ink.base"),
        "badge": None,  # one-color: the 2 is cut through the disc
    },
}

# --- Geometry --------------------------------------------------------------

# Bolt glyph: the classic single-jag proportions.
BOLT_GLYPH = [(120, 8), (166, 8), (137, 100), (171, 100), (80, 272), (106, 146), (76, 146)]


def bolt_path(points, dx=0.0, dy=0.0, scale=1.0) -> str:
    pts = [((x * scale) + dx, (y * scale) + dy) for x, y in points]
    return "M" + " L".join(f"{x:.1f} {y:.1f}" for x, y in pts) + " Z"


def svg(view_w, view_h, body, title) -> str:
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{view_w if str(view_w).count(" ") else f"0 0 {view_w}"} {view_h}" '
        f'role="img" aria-label="{title}">\n<title>{title}</title>\n{body}\n</svg>\n'
    )


def fist(c) -> str:
    L = FIST["layers"]
    w, h = FIST["viewBox"][2:]
    keyline = KEYLINE if c is not GROUNDS["pad"] else c["line"]
    body = (
        f'<path d="{L["outline"]}" fill="{keyline}" fill-rule="evenodd"/>\n'
        f'<path d="{L["white"]}" fill="{WHITE}" fill-rule="evenodd"/>\n'
        f'<path d="{L["red"]}" fill="{tok("color.red.600")}" fill-rule="evenodd"/>'
    )
    return svg(w, h, body, "Punch2Pen fist")


def badge_group(c, x, y, size) -> str:
    """The 2 badge at (x, y), size across. Red disc with a white 2, or one-color."""
    s = size / BADGE["viewBox"][2]
    t = f'transform="translate({x:.2f} {y:.2f}) scale({s:.4f})"'
    if c["badge"] is None:
        return f'<path {t} d="{BADGE["disc"]}{BADGE["two"]}" fill="{c["accent"]}" fill-rule="evenodd"/>'
    return f'<g {t}><path d="{BADGE["disc"]}" fill="{c["badge"]}"/><path d="{BADGE["two"]}" fill="{WHITE}"/></g>'


def path_bounds(d: str):
    import re
    nums = [float(n) for n in re.findall(r"-?\d+(?:\.\d+)?", d)]
    xs, ys = nums[0::2], nums[1::2]
    return min(xs), min(ys), max(xs), max(ys)


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


def two(c, ground=True) -> str:
    parts = []
    if ground:
        parts.append(f'<rect width="64" height="64" rx="14" fill="{c.get("tile", c["fill"])}"/>')
    parts.append(badge_group(c, 7, 7, 50) if ground else badge_group(c, 0, 0, 64))
    return svg(64, 64, "\n".join(parts), "Punch2Pen 2")


def two_glyph(c) -> str:
    """The 2 alone, tight to its bounds: an alpha shape for logo shaders."""
    x0, y0, x1, y1 = path_bounds(BADGE["two"])
    pad = 4
    return svg(
        f"{x0 - pad:.1f} {y0 - pad:.1f} {x1 - x0 + pad * 2:.1f}",
        f"{y1 - y0 + pad * 2:.1f}",
        f'<path d="{BADGE["two"]}" fill="{c["letter"]}"/>',
        "Punch2Pen 2",
    )


def favicon(c) -> str:
    # 16-32px: the badge alone, edge to edge, reads as a red dot with a 2 in it.
    return svg(32, 32, badge_group(c, 0, 0, 32), "Punch2Pen")


def bolt(c) -> str:
    keyline = KEYLINE if c is not GROUNDS["pad"] else c["line"]
    return svg(
        110,
        280,
        f'<path d="{bolt_path(BOLT_GLYPH, dx=-68)}" fill="{WHITE}" stroke="{keyline}" stroke-width="9" stroke-linejoin="round"/>',
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
    """Glyph outlines at cap height `cap`, each drawn at x = 0, with its advance."""
    from fontTools.pens.svgPathPen import SVGPathPen
    from fontTools.pens.transformPen import TransformPen
    from fontTools.ttLib import TTFont
    from fontTools.varLib.instancer import instantiateVariableFont

    font = TTFont(io.BytesIO(archivo_latin()))
    font = instantiateVariableFont(font, {"wdth": wdth, "wght": wght})
    cmap = font.getBestCmap()
    glyphs = font.getGlyphSet()
    scale = cap / font["OS/2"].sCapHeight
    tracking = 0.02 * font["head"].unitsPerEm * scale
    out = []
    for ch in text:
        name = cmap[ord(ch)]
        pen = SVGPathPen(glyphs)
        glyphs[name].draw(TransformPen(pen, (scale, 0, 0, -scale, 0, cap)))
        out.append((ch, pen.getCommands(), glyphs[name].width * scale))
    return out, tracking, cap


def wordmark(c, glyphs, tracking, cap) -> str:
    """PUNCH2PEN with the 2 set as the badge, a touch taller than the caps."""
    size = cap * 1.14
    gap = tracking * 2.5
    body, x = [], 0.0
    for i, (ch, d, adv) in enumerate(glyphs):
        if ch == "2":
            x += gap - tracking
            body.append(badge_group(c, x, (cap - size) / 2, size))
            x += size + gap
        else:
            body.append(f'<path transform="translate({x:.2f} 0)" d="{d}" fill="{c["letter"]}"/>')
            x += adv + tracking
    width = x - tracking
    top = (cap - size) / 2 - 2
    height = size + 4
    return svg(f"-2 {top:.2f} {width + 4:.2f}", f"{height:.2f}", "\n".join(body), "Punch2Pen")


# --- Explorations: marks drawn from how the plugin works --------------------
# Proposals on the marks board, not adopted. Written to brand/explore/.


def punch_range(c) -> str:
    """[2]: the badge between a DAW's punch-in and punch-out brackets. Also reads
    as lyric-sheet notation: [Verse 2], a bracketed adlib."""
    line = c["line"]
    st = f'fill="none" stroke="{line}" stroke-width="7" stroke-linecap="round" stroke-linejoin="round"'
    body = [
        f'<path d="M50 14H30V106H50" {st}/>',
        f'<path d="M150 14H170V106H150" {st}/>',
        badge_group(c, 64, 24, 72),
    ]
    return svg(200, 120, "\n".join(body), "Punch range")


# Waveform overview: quiet before the punch, the bolt as the transient, then decay.
TRANSIENT_BARS = [8, 12, 7, 14, 10, 6, 12, 9, None, 44, 38, 32, 27, 22, 18, 15, 12, 10, 8, 6]


def transient(c) -> str:
    """The bolt is the transient: the punch landing on the waveform."""
    line = c["line"]
    keyline = KEYLINE if c is not GROUNDS["pad"] else c["line"]
    body, x, mid = [], 14.0, 70.0
    for h in TRANSIENT_BARS:
        if h is None:
            # bolt glyph (170 wide units, 264 tall) scaled into the gap
            scale = 136 / 264
            bx = x - 76 * scale + 2
            body.append(
                f'<path d="{bolt_path(BOLT_GLYPH, dx=bx - 0, dy=mid - 140 * scale, scale=scale)}" fill="{WHITE}" '
                f'stroke="{keyline}" stroke-width="5" stroke-linejoin="round"/>'
            )
            x += 96 * scale + 14
            continue
        body.append(f'<path d="M{x:.1f} {mid - h:.1f}V{mid + h:.1f}" stroke="{line}" stroke-width="5" stroke-linecap="round"/>')
        x += 11
    return svg(f"{x + 4:.0f}", 140, "\n".join(body), "Transient")


def seek_word(c, glyphs_by_char, cap) -> str:
    """Click a word, move the playhead: the word under a highlighter, a playhead
    through its first syllable, the timecode on the flag."""
    s = 64 / cap
    word, x, parts = "PEN", 36.0, []
    letters = []
    for ch in word:
        d, adv = glyphs_by_char[ch]
        letters.append(f'<path transform="translate({x:.2f} 40) scale({s:.4f})" d="{d}" fill="{tok("color.pad.ink")}"/>')
        x += adv * s + 4
    right = x
    # a real highlighter: solid legal yellow with pen-black type, on either ground
    hl = tok("color.pad.paper") if c is not GROUNDS["pad"] else "#F2E27A"
    parts.append(f'<path d="M26 {40 + 6}L{right + 8:.1f} {40 + 2}L{right + 10:.1f} {40 + 70}L24 {40 + 72}Z" fill="{hl}"/>')
    parts += letters
    # playhead: line, flag, timecode
    ph = 30
    parts.append(f'<path d="M{ph} 22V122" stroke="{c["line"]}" stroke-width="3"/>')
    parts.append(f'<path d="M{ph - 8} 6H{ph + 62}V22H{ph + 8}L{ph} 30L{ph - 8} 22Z" fill="{c["line"]}"/>')
    tc, tx = "0:42.1", ph - 1.0
    ts = 11 / cap
    for ch in tc:
        d, adv = glyphs_by_char[ch]
        parts.append(f'<path transform="translate({tx:.2f} 9.5) scale({ts:.4f})" d="{d}" fill="{c.get("tile", c["fill"]) if c is not GROUNDS["pad"] else WHITE}"/>')
        tx += adv * ts + 0.8
    return svg(f"{right + 24:.0f}", 130, "\n".join(parts), "Seek word")


def main():
    out = HERE
    written = []

    def write(name, text):
        (out / name).write_text(text)
        written.append(name)

    for ground in ("booth", "pad"):
        c = GROUNDS[ground]
        for name, fn in [("fist", fist), ("pen", pen), ("bolt", bolt)]:
            write(f"{name}-{ground}.svg", fn(c))
        # The 2 alone: a clean alpha shape for logo shaders (gem smoke).
        write(f"two-glyph-{ground}.svg", two_glyph(c))
    for ground, c in GROUNDS.items():
        write(f"two-{ground}.svg", two(c))
    write("favicon.svg", favicon(GROUNDS["booth"]))
    try:
        glyphs, tracking, cap = wordmark_paths()
        for ground, c in GROUNDS.items():
            write(f"wordmark-{ground}.svg", wordmark(c, glyphs, tracking, cap))
        extra, _, _ = wordmark_paths(text="0:42.1PEN")
        by_char = {ch: (d, adv) for ch, d, adv in extra}
    except ModuleNotFoundError:
        by_char = None
        print("fonttools not installed: skipped wordmark (pip install fonttools brotli)")
    (out / "explore").mkdir(exist_ok=True)
    for ground in ("booth", "pad"):
        c = GROUNDS[ground]
        write(f"explore/punch-range-{ground}.svg", punch_range(c))
        write(f"explore/transient-{ground}.svg", transient(c))
        if by_char:
            write(f"explore/seek-word-{ground}.svg", seek_word(c, by_char, cap))
    print("wrote", ", ".join(written))


if __name__ == "__main__":
    main()
