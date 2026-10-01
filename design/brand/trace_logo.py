#!/usr/bin/env python3
"""Traces the founder's logo (source/logo-legal-pad.webp) into vector layers.

    pip install pillow numpy potracer
    python3 design/brand/trace_logo.py

Writes fist-paths.json: one path per color layer, drawn in this order:
  outline  everything that is not the legal pad (the black keyline)
  white    the fist and bolt fill, and the white 2
  red      the letters P E N and the 2 badge (the 2 is a hole in it)
and the badge on its own (disc, and the 2 cut from it) for the icon, favicon
and wordmark. build_marks.py reads that file and colors the layers per ground.
"""
from __future__ import annotations

import json
from collections import deque
from pathlib import Path

import numpy as np
import potrace
from PIL import Image

HERE = Path(__file__).resolve().parent
SOURCE = HERE / "source" / "logo-legal-pad.webp"
OUT = HERE / "fist-paths.json"
WORK = 1000  # trace at this height; plenty for a mark, fast in pure Python


# Every pixel goes to its nearest ink. Pad and rule are the paper behind the logo.
INKS = {
    "black": (12, 12, 12),
    "white": (255, 255, 255),
    "red": (172, 38, 35),
    "pad": (249, 242, 188),
    "rule": (167, 208, 210),
}


def masks(img: Image.Image):
    rgb = np.asarray(img.convert("RGB"), dtype=np.float32)
    names = list(INKS)
    ref = np.array([INKS[n] for n in names], dtype=np.float32)
    dist = ((rgb[:, :, None, :] - ref[None, None, :, :]) ** 2).sum(-1)
    # red is a chroma call, not a distance one: mid-grey edge pixels sit nearer
    # the brick red than either black or white, so gate red on the red channel
    reddish = (rgb[..., 0] - rgb[..., 1:].max(-1)) > 50
    dist[..., names.index("red")] = np.where(reddish, 0, np.inf)
    label = dist.argmin(-1)
    red, white, black = (label == names.index(n) for n in ("red", "white", "black"))
    return red, white, black


def solid(mask: np.ndarray) -> np.ndarray:
    """Fill every hole: whatever the border cannot reach is inside the mark."""
    h, w = mask.shape
    outside = np.zeros_like(mask)
    q = deque()
    for x in range(w):
        for y in (0, h - 1):
            if not mask[y, x] and not outside[y, x]:
                outside[y, x] = True
                q.append((y, x))
    for y in range(h):
        for x in (0, w - 1):
            if not mask[y, x] and not outside[y, x]:
                outside[y, x] = True
                q.append((y, x))
    while q:
        y, x = q.popleft()
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            ny, nx = y + dy, x + dx
            if 0 <= ny < h and 0 <= nx < w and not mask[ny, nx] and not outside[ny, nx]:
                outside[ny, nx] = True
                q.append((ny, nx))
    return ~outside


def grow(mask: np.ndarray, px: int = 1) -> np.ndarray:
    out = mask.copy()
    for _ in range(px):
        m = out.copy()
        m[1:, :] |= out[:-1, :]
        m[:-1, :] |= out[1:, :]
        m[:, 1:] |= out[:, :-1]
        m[:, :-1] |= out[:, 1:]
        out = m
    return out


def components(mask: np.ndarray):
    """4-connected components as (area, (y0, x0, y1, x1), boolean mask)."""
    seen = np.zeros_like(mask)
    h, w = mask.shape
    out = []
    for sy, sx in zip(*np.nonzero(mask)):
        if seen[sy, sx]:
            continue
        pts, q = [], deque([(sy, sx)])
        seen[sy, sx] = True
        while q:
            y, x = q.popleft()
            pts.append((y, x))
            for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                ny, nx = y + dy, x + dx
                if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] and not seen[ny, nx]:
                    seen[ny, nx] = True
                    q.append((ny, nx))
        ys, xs = np.array(pts).T
        comp = np.zeros_like(mask)
        comp[ys, xs] = True
        out.append((len(pts), (ys.min(), xs.min(), ys.max(), xs.max()), comp))
    return out


def badge(red: np.ndarray):
    """The 2 badge is the one red piece that is about as wide as it is tall."""
    def roundness(c):
        _, (y0, x0, y1, x1), _ = c
        return abs(1 - (x1 - x0) / max(y1 - y0, 1))
    area, (y0, x0, y1, x1), comp = min((c for c in components(red) if c[0] > 400), key=roundness)
    disc = solid(comp)
    two = disc & ~comp
    pad = 2
    ox, oy = x0 - pad, y0 - pad
    return {
        "viewBox": [0, 0, int(x1 - x0 + pad * 2 + 1), int(y1 - y0 + pad * 2 + 1)],
        "disc": trace(disc, ox, oy),
        "two": trace(two, ox, oy),
    }


def trace(mask: np.ndarray, ox: float, oy: float) -> str:
    # potracer inks pixels below its black level, so hand it the inverse
    bm = potrace.Bitmap(~mask)
    curves = bm.trace(turdsize=24, turnpolicy=potrace.POTRACE_TURNPOLICY_MINORITY, alphamax=1.0, opticurve=True, opttolerance=0.3)
    parts = []
    f = lambda p: f"{p.x - ox:.1f} {p.y - oy:.1f}"
    for curve in curves:
        parts.append(f"M{f(curve.start_point)}")
        for seg in curve.segments:
            if seg.is_corner:
                parts.append(f"L{f(seg.c)}L{f(seg.end_point)}")
            else:
                parts.append(f"C{f(seg.c1)} {f(seg.c2)} {f(seg.end_point)}")
        parts.append("Z")
    return "".join(parts)


def main():
    img = Image.open(SOURCE)
    scale = WORK / img.height
    img = img.resize((round(img.width * scale), WORK), Image.LANCZOS)
    red, white, black = masks(img)
    # the black keyline closes the logo; anything it encloses is the mark
    silhouette = solid(red | black) & (red | white | black)
    ys, xs = np.nonzero(silhouette)
    pad = 6
    x0, y0 = xs.min() - pad, ys.min() - pad
    x1, y1 = xs.max() + pad, ys.max() + pad
    layers = {
        "outline": trace(silhouette, x0, y0),
        "white": trace(white & silhouette, x0, y0),
        # grow red by a pixel so no dark fringe shows between red and white
        "red": trace(grow(red, 1) & silhouette, x0, y0),
    }
    data = {
        "source": SOURCE.name,
        "viewBox": [0, 0, int(x1 - x0), int(y1 - y0)],
        "fillRule": "evenodd",
        "layers": layers,
        "badge": badge(red),
    }
    OUT.write_text(json.dumps(data, indent=1))
    print(f"wrote {OUT.name}: viewBox {data['viewBox']}, " + ", ".join(f"{k} {len(v)//1000} KB" for k, v in layers.items()) + f", badge {data['badge']['viewBox']}")


if __name__ == "__main__":
    main()
