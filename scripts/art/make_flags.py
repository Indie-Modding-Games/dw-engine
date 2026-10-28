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


FLAGS = {"en": union_flag, "es": spain_flag}


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
