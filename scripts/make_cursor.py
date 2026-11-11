#!/usr/bin/env python3
"""Rasterize the yellow mouse-cursor arrow (black border) to an RGBA PNG.

The shape is the arrow the designer gave as an SVG path
(`M12 7 L12 82 L32 62 L47 88 L59 81 L44 56 L72 56 Z` over a 96x96 viewBox): filled yellow
(#f1cb20) with a black border. This tool draws it from code (no SVG runtime in the engine's
image loaders: those read PNG/KTX2 only), supersamples for smooth edges, and writes the PNG
with only the standard library (zlib), so it runs anywhere `scripts/` Python runs.

    python scripts/make_cursor.py [out.png] [--height 32] [--border 3.0]

The tip of the arrow (the point that clicks) is the path's first vertex (12, 7); print its
scaled position as the cursor hotspot so the caller can set `cursor_hotspot`.
"""

from __future__ import annotations

import argparse
import struct
import zlib
from pathlib import Path

# The arrow in its 96x96 viewBox (the designer's SVG path).
VIEWBOX = 96.0
POLY = [(12, 7), (12, 82), (32, 62), (47, 88), (59, 81), (44, 56), (72, 56)]
TIP = (12, 7)  # the hotspot: where a click lands
FILL = (0xF1, 0xCB, 0x20, 255)  # #f1cb20
BORDER = (0x00, 0x00, 0x00, 255)  # black
SS = 4  # supersample factor for antialiased edges


def point_in_poly(x: float, y: float, poly) -> bool:
    inside = False
    n = len(poly)
    j = n - 1
    for i in range(n):
        xi, yi = poly[i]
        xj, yj = poly[j]
        if (yi > y) != (yj > y):
            xin = (xj - xi) * (y - yi) / (yj - yi) + xi
            if x < xin:
                inside = not inside
        j = i
    return inside


def dist_to_seg(px, py, ax, ay, bx, by) -> float:
    dx, dy = bx - ax, by - ay
    l2 = dx * dx + dy * dy
    if l2 == 0:
        return ((px - ax) ** 2 + (py - ay) ** 2) ** 0.5
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / l2))
    cx, cy = ax + t * dx, ay + t * dy
    return ((px - cx) ** 2 + (py - cy) ** 2) ** 0.5


def edge_dist(x, y, poly) -> float:
    n = len(poly)
    return min(
        dist_to_seg(x, y, poly[i][0], poly[i][1], poly[(i + 1) % n][0], poly[(i + 1) % n][1])
        for i in range(n)
    )


def render(height: int, border: float):
    """Return (width, height, bytearray RGBA) and the tip (hotspot) in output pixels.

    Renders the arrow tightly: the source rectangle is the path's bounding box, expanded by half
    the border width, scaled so its vertical span becomes `height` pixels.
    """
    xs = [p[0] for p in POLY]
    ys = [p[1] for p in POLY]
    pad = border / 2.0
    min_x = min(xs) - pad
    min_y = min(ys) - pad
    bbox_w = max(xs) - min_x + pad
    bbox_h = max(ys) - min_y + pad
    scale = height / bbox_h
    width = max(1, round(bbox_w * scale))
    border_vb = border / scale  # border width in viewBox units
    pixels = bytearray(width * height * 4)
    for oy in range(height):
        for ox in range(width):
            cover = 0
            border_count = 0
            for sy in range(SS):
                for sx in range(SS):
                    vx = min_x + (ox * SS + sx + 0.5) / SS / scale
                    vy = min_y + (oy * SS + sy + 0.5) / SS / scale
                    if point_in_poly(vx, vy, POLY):
                        cover += 1
                        if edge_dist(vx, vy, POLY) <= border_vb / 2.0:
                            border_count += 1
            if cover == 0:
                continue
            alpha = round(255 * cover / (SS * SS))
            fill_count = cover - border_count
            r = (BORDER[0] * border_count + FILL[0] * fill_count) // cover
            g = (BORDER[1] * border_count + FILL[1] * fill_count) // cover
            b = (BORDER[2] * border_count + FILL[2] * fill_count) // cover
            idx = (oy * width + ox) * 4
            pixels[idx:idx + 4] = bytes((r, g, b, alpha))
    hot = (round((TIP[0] - min_x) * scale), round((TIP[1] - min_y) * scale))
    return width, height, pixels, hot


def write_png(path: Path, width: int, height: int, rgba) -> None:
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        crc = zlib.crc32(tag + data) & 0xFFFFFFFF
        return c + struct.pack(">I", crc)

    raw = bytearray()
    stride = width * 4
    for y in range(height):
        raw.append(0)  # filter: none
        raw += rgba[y * stride:(y + 1) * stride]
    png = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + chunk(b"IEND", b"")
    )
    path.write_bytes(png)


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("out", nargs="?", default="cursor.png", help="output PNG path")
    p.add_argument("--height", type=int, default=32, help="output height in pixels")
    p.add_argument("--border", type=float, default=3.0, help="black border width in pixels")
    args = p.parse_args(argv)

    out = Path(args.out)
    w, h, pixels, hot = render(args.height, args.border)
    write_png(out, w, h, pixels)
    print(f"wrote {out} ({w}x{h}) — cursor_hotspot: [{hot[0]}, {hot[1]}]")
    # A tiny ASCII preview so the run is verifiable without an image viewer.
    for y in range(0, h, max(1, h // 16)):
        row = ""
        for x in range(0, w, max(1, w // 32)):
            a = pixels[(y * w + x) * 4 + 3]
            row += ("B" if pixels[(y * w + x) * 4 + 1] < 0x80 and a else ("." if a else " "))
        print(row)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
