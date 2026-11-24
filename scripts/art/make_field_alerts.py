#!/usr/bin/env python3
"""Draws the pixel skin's field alert icons (content/packs/game/ui/skins/pixel/assets/field_alerts.png): the need meters'
meal, toilet and tiredness drops and the danger alert's small devil, our own pixel art in the skin's palette. Each art
pixel is a 4 x 4 block, so the sheet stays sharp when the UI scales it; skin.yaml cuts it at 0.75 dp per pixel (3 dp
per art pixel). Standard library only.

Usage: scripts/art/make_field_alerts.py [output file]
"""

import struct
import sys
import zlib
from pathlib import Path

SCALE = 4
CELL = 16

PALETTE = {
    ".": None,
    "K": (5, 12, 26),
    "C": (242, 240, 225),
    "c": (196, 188, 160),
    "M": (238, 140, 96),
    "m": (204, 86, 58),
    "d": (136, 50, 34),
    "L": (196, 140, 80),
    "b": (150, 96, 46),
    "B": (96, 60, 28),
    "w": (255, 255, 255),
    "l": (182, 228, 247),
    "u": (124, 182, 226),
    "U": (63, 120, 190),
    "H": (196, 160, 255),
    "p": (150, 88, 226),
    "P": (96, 44, 168),
    "y": (239, 198, 60),
    "r": (226, 72, 61),
}

MEAL = [
    "......KKKK..",
    ".....KMMMMK.",
    "....KMmmmmMK",
    "...KMmmmmmmK",
    "...KmmmmmmmK",
    "...KmmmmmmdK",
    "...KmmmmmddK",
    "...KKmmmddK.",
    "..KCcKKKKK..",
    ".KCcK.......",
    "KCCCcK......",
    "KCKKCK......",
    ".K..K.......",
]

TOILET = [
    ".....KK.....",
    "....KLK.....",
    "....KbLK....",
    "...KKKKKK...",
    "..KLbbbbbK..",
    "..KbbbbbBK..",
    ".KKKKKKKKKK.",
    "KLbbbbbbbbBK",
    "KbbbbbbbbbBK",
    "KbbbbbbbbBBK",
    ".KBBBBBBBBK.",
    "..KKKKKKKK..",
]

DROPS = [
    "...K........",
    "..KlK.......",
    "..KlK.......",
    ".KlwuK..K...",
    ".KluuK.KlK..",
    "KluuuUKKlK..",
    "KuuuuUKlwuK.",
    "KuuuUUKluuK.",
    ".KUUUKKuuuUK",
    "..KKK.KuuuUK",
    "......KuuUUK",
    ".......KUUK.",
    "........KK..",
]

DEVIL = [
    ".K..........K.",
    "KHK........KHK",
    "KHHK......KHHK",
    ".KHHKKKKKKHHK.",
    ".KppppppppppK.",
    "KpKKppppppKKpK",
    "KppKKppppKKppK",
    "KpKyyKppKyyKpK",
    "KpKyKKppKKyKpK",
    "KppppppppppppK",
    "KpKwKKKKKKwKpK",
    "KppKrrrrrrKppK",
    ".KPPKKKKKKPPK.",
    "..KPPPPPPPPK..",
    "...KKKKKKKK...",
]

ICONS = [MEAL, TOILET, DROPS, DEVIL]


def png(width, height, rows):
    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + bytes(row) for row in rows)
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[2] / (
        "content/packs/game/ui/skins/pixel/assets/field_alerts.png")
    side = CELL * SCALE
    width, height = side * len(ICONS), side
    pixels = [[0] * (width * 4) for _ in range(height)]
    for slot, art in enumerate(ICONS):
        top = (CELL - len(art)) // 2
        left = (CELL - len(art[0])) // 2
        for y, line in enumerate(art):
            for x, key in enumerate(line):
                colour = PALETTE[key]
                if colour is None:
                    continue
                for dy in range(SCALE):
                    for dx in range(SCALE):
                        px = (slot * side) + (left + x) * SCALE + dx
                        row = pixels[(top + y) * SCALE + dy]
                        row[px * 4:px * 4 + 4] = [*colour, 255]
    out.write_bytes(png(width, height, pixels))


if __name__ == "__main__":
    main()
