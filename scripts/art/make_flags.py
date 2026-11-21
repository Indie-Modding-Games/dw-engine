#!/usr/bin/env python3
"""Draws the language box's flags (content/packs/game/ui/flags/<language>.png): plain geometric flags, our own art,
3:2 and 144 x 96 pixels, which the original look shows at 72 x 48 dp. Each pixel averages 4 x 4 samples, so the edges
stay clean at any scale. Standard library only.

Usage: scripts/art/make_flags.py [output folder]
"""

import math
import struct
import sys
import zlib
from pathlib import Path

WIDTH, HEIGHT = 144, 96
SAMPLES = 4


def union_flag(x, y):
    """The United Kingdom's flag, simplified: the red diagonals run down the middle of the white ones."""
    w, h = 3.0, 2.0
    blue, white, red = (1, 33, 105), (255, 255, 255), (200, 16, 46)
    cx, cy = x - w / 2, y - h / 2
    if abs(cx) < h / 10 or abs(cy) < h / 10:
        return red
    if abs(cx) < h / 6 or abs(cy) < h / 6:
        return white
    length = math.hypot(w, h)
    to_diagonal = min(abs(h * x - w * y), abs(h * x + w * y - w * h)) / length
    if to_diagonal < h / 30:
        return red
    if to_diagonal < h / 10:
        return white
    return blue


def spain_flag(_x, y):
    """Spain's flag without its coat of arms: red, yellow twice as tall, red."""
    red, yellow = (170, 21, 27), (241, 191, 0)
    return yellow if 0.5 <= y < 1.5 else red


def stripes(colors, vertical=False):
    """A flag of equal stripes, top to bottom or (vertical) hoist to fly."""

    def flag(x, y):
        at = x / 3.0 if vertical else y / 2.0
        return colors[min(len(colors) - 1, int(at * len(colors)))]

    return flag


def france_flag(x, y):
    """France's flag: blue, white and red, upright."""
    return stripes([(0, 38, 84), (255, 255, 255), (206, 17, 38)], vertical=True)(x, y)


def germany_flag(x, y):
    """Germany's flag: black, red and gold."""
    return stripes([(0, 0, 0), (221, 0, 0), (255, 206, 0)])(x, y)


def russia_flag(x, y):
    """Russia's flag: white, blue and red."""
    return stripes([(255, 255, 255), (0, 57, 166), (213, 43, 30)])(x, y)


def japan_flag(x, y):
    """Japan's flag: a red disc three fifths of the height across, in the middle of white."""
    return (188, 0, 45) if math.hypot(x - 1.5, y - 1.0) < 0.6 else (255, 255, 255)


def in_star(x, y, cx, cy, radius, angle):
    """Whether (x, y) is inside a five-pointed star centred at (cx, cy), one point towards `angle`."""
    inner = radius * math.sin(math.pi / 10) / math.sin(7 * math.pi / 10)
    points = []
    for i in range(10):
        r = radius if i % 2 == 0 else inner
        a = angle + i * math.pi / 5
        points.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    inside = False
    for i, (x1, y1) in enumerate(points):
        x2, y2 = points[i - 1]
        if (y1 > y) != (y2 > y) and x < x1 + (y - y1) * (x2 - x1) / (y2 - y1):
            inside = not inside
    return inside


def china_flag(x, y):
    """China's flag: red with a large yellow star and four small ones turned towards it, on the
    flag's 30 x 20 grid."""
    red, yellow = (238, 28, 37), (255, 255, 0)
    unit = 0.1
    big = (5 * unit, 5 * unit)
    if in_star(x, y, big[0], big[1], 3 * unit, -math.pi / 2):
        return yellow
    for gx, gy in ((10, 2), (12, 4), (12, 7), (10, 9)):
        cx, cy = gx * unit, gy * unit
        towards = math.atan2(big[1] - cy, big[0] - cx)
        if in_star(x, y, cx, cy, unit, towards):
            return yellow
    return red


def brazil_flag(x, y):
    """Brazil's flag, simplified: the yellow rhombus and the blue globe with its white band, without
    the stars and the motto."""
    green, yellow, blue, white = (0, 151, 57), (254, 221, 0), (1, 33, 105), (255, 255, 255)
    cx, cy = 1.5, 1.0
    if math.hypot(x - cx, y - cy) < 0.5:
        band = math.hypot(x - 1.9, y - 2.5)
        return white if 1.47 < band < 1.54 else blue
    if abs(x - cx) / (cx - 0.255) + abs(y - cy) / (cy - 0.243) < 1.0:
        return yellow
    return green


def korea_flag(x, y):
    """South Korea's flag: the red and blue taegeuk, its axis on the flag's diagonal, and the four
    black trigrams at the corners (geon, gam, gon, ri), on the official proportions."""
    white, red, blue, black = (255, 255, 255), (205, 46, 58), (0, 71, 160), (0, 0, 0)
    cx, cy = 1.5, 1.0
    diameter = 1.0
    dx, dy = x - cx, y - cy
    # u along the diagonal from the upper hoist corner to the lower fly corner, v across it.
    length = math.hypot(3.0, 2.0)
    ux, uy = 3.0 / length, 2.0 / length
    u, v = dx * ux + dy * uy, -dx * uy + dy * ux
    if math.hypot(u, v) < diameter / 2:
        if math.hypot(u + diameter / 4, v) < diameter / 4:
            return red
        if math.hypot(u - diameter / 4, v) < diameter / 4:
            return blue
        return red if v < 0 else blue
    # The trigrams, each on a diagonal: bars across it, from the circle's edge a quarter diameter
    # out; a bar is a twelfth of the diameter thick, with a 24th between bars and in a broken bar.
    bar, gap = diameter / 12, diameter / 24
    other = (-ux, uy)  # the other diagonal, from the lower hoist corner to the upper fly one
    for (ax, ay), sign, lines in (
        ((ux, uy), -1, (1, 1, 1)),  # geon, upper hoist
        ((ux, uy), 1, (0, 0, 0)),  # gon, lower fly
        (other, 1, (0, 1, 0)),  # gam, upper fly
        (other, -1, (1, 0, 1)),  # ri, lower hoist
    ):
        along = sign * (dx * ax + dy * ay)
        across = -dx * ay + dy * ax
        start = diameter / 2 + diameter / 4
        if abs(across) >= diameter / 4:
            continue
        for i, solid in enumerate(lines):
            low = start + i * (bar + gap)
            if low <= along < low + bar and (solid or abs(across) >= gap / 2):
                return black
    return white


FLAGS = {
    "en": union_flag,
    "es": spain_flag,
    "pt": brazil_flag,
    "fr": france_flag,
    "de": germany_flag,
    "ru": russia_flag,
    "zh": china_flag,
    "ko": korea_flag,
    "ja": japan_flag,
}


def render(flag):
    rows = []
    for py in range(HEIGHT):
        row = bytearray([0])
        for px in range(WIDTH):
            total = [0, 0, 0]
            for sy in range(SAMPLES):
                for sx in range(SAMPLES):
                    x = (px + (sx + 0.5) / SAMPLES) / WIDTH * 3.0
                    y = (py + (sy + 0.5) / SAMPLES) / HEIGHT * 2.0
                    color = flag(x, y)
                    for channel in range(3):
                        total[channel] += color[channel]
            row += bytes(round(value / SAMPLES**2) for value in total) + b"\xff"
        rows.append(bytes(row))
    return b"".join(rows)


def png(pixels):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    header = struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 6, 0, 0, 0)
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", header)
        + chunk(b"IDAT", zlib.compress(pixels, 9))
        + chunk(b"IEND", b"")
    )


def main():
    folder = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).parents[2] / "content/packs/game/ui/flags"
    folder.mkdir(parents=True, exist_ok=True)
    for language, flag in FLAGS.items():
        (folder / f"{language}.png").write_bytes(png(render(flag)))
        print(folder / f"{language}.png")


if __name__ == "__main__":
    main()
