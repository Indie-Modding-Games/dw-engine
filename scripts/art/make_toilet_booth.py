#!/usr/bin/env python3
"""Builds the extra toilets' booth (content/packs/game/props/toilet_booth.glb): our own low-poly model of an outdoor
wooden booth with an open front, a seat inside and a roof sloping to the back, textured by a small painted atlas. It is
in world space and units (100 = one collision cell, +Y up) and stands on 200 x 200 around its origin, its open front
to +Z, as field/extra_toilets.yaml places it. Standard library only; the noise is seeded, so the output is the same on
every run.

Usage: scripts/art/make_toilet_booth.py [output file]
"""

import json
import random
import struct
import sys
import zlib
from pathlib import Path

ATLAS = 128
# The atlas's four 64 x 64 corners: walls, roof, trim and posts, seat and floor.
WALL, ROOF, TRIM, STONE = (0, 0), (64, 0), (0, 64), (64, 64)


def png(size, pixels):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    rows = b"".join(b"\x00" + bytes(c for x in range(size) for c in pixels[y][x]) for y in range(size))
    header = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b"")


def atlas():
    rng = random.Random(1108)
    pixels = [[(0, 0, 0, 255)] * ATLAS for _ in range(ATLAS)]

    def paint(corner, colour_at):
        ox, oy = corner
        for y in range(64):
            for x in range(64):
                r, g, b = colour_at(x, y)
                grain = rng.randint(-10, 10)
                pixels[oy + y][ox + x] = (*(max(0, min(255, c + grain)) for c in (r, g, b)), 255)

    # Weathered vertical planks, a dark seam every 16 pixels and a knot here and there.
    tones = [rng.randint(-14, 14) for _ in range(4)]

    def wall(x, y):
        if x % 16 in (0, 15):
            return 58, 38, 24
        t = tones[x // 16] + (6 if (y * 7 + x * 3) % 23 < 2 else 0)
        return 132 + t, 92 + t, 58 + t

    # Grey-brown roof boards across, with moss creeping in.
    def roof(x, y):
        if y % 12 in (0, 11):
            return 52, 46, 40
        moss = (x * 13 + y * 29) % 41 < 4
        return (86, 104, 52) if moss else (112, 100, 86)

    # Reddish painted trim and posts.
    # The lowest 16 rows are the plaque over the door: a toilet seen from the side in dark blue on cream,
    # no words.
    glyph = [
        "..........",
        "###.......",
        "###.......",
        "###.......",
        "###.......",
        "##########",
        ".########.",
        "..######..",
        "...####...",
        "...####...",
        "..######..",
    ]

    def trim(x, y):
        if y >= 48:
            gx, gy = x - 27, y - 50
            if 0 <= gy < len(glyph) and 0 <= gx < len(glyph[gy]) and glyph[gy][gx] == "#":
                return 40, 52, 96
            edge = x in (0, 63) or y in (48, 63)
            return (96, 60, 40) if edge else (226, 214, 180)
        return (118, 52, 40) if y % 32 > 2 else (78, 34, 26)

    # A pale seat and a stone floor.
    def stone(x, y):
        return (190, 190, 182) if y < 32 else (128, 122, 110)

    paint(WALL, wall)
    paint(ROOF, roof)
    paint(TRIM, trim)
    paint(STONE, stone)
    return png(ATLAS, pixels)


def region(corner, rows=(0, 64)):
    """UVs of an atlas corner, or of a band of its rows."""
    u0, u1 = corner[0] / ATLAS, (corner[0] + 64) / ATLAS
    v0, v1 = (corner[1] + rows[0]) / ATLAS, (corner[1] + rows[1]) / ATLAS
    return u0, v0, u1, v1


class Mesh:
    def __init__(self):
        self.positions, self.normals, self.uvs, self.indices = [], [], [], []

    def quad(self, corners, uv):
        """A face from four corners, counter-clockwise seen from outside, with its flat normal."""
        a, b, c, d = corners
        e1 = [b[i] - a[i] for i in range(3)]
        e2 = [d[i] - a[i] for i in range(3)]
        n = [e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]]
        length = sum(c * c for c in n) ** 0.5 or 1.0
        n = [c / length for c in n]
        u0, v0, u1, v1 = uv
        base = len(self.positions)
        for corner, tex in zip(corners, [(u0, v1), (u1, v1), (u1, v0), (u0, v0)]):
            self.positions.append(corner)
            self.normals.append(n)
            self.uvs.append(tex)
        self.indices += [base, base + 1, base + 2, base, base + 2, base + 3]

    def block(self, low, high, uv, top_uv=None, front_rise=0.0):
        """A box from its lowest to its highest corner; front_rise lifts its top's +Z edge (a sloping roof)."""
        x0, y0, z0 = low
        x1, y1, z1 = high
        yb, yf = y1, y1 + front_rise
        lb, lf = y0, y0 + front_rise
        top = top_uv or uv
        self.quad([(x0, lf, z1), (x1, lf, z1), (x1, yf, z1), (x0, yf, z1)], uv)  # front (+Z)
        self.quad([(x1, lb, z0), (x0, lb, z0), (x0, yb, z0), (x1, yb, z0)], uv)  # back (-Z)
        self.quad([(x0, lb, z0), (x0, lf, z1), (x0, yf, z1), (x0, yb, z0)], uv)  # left (-X)
        self.quad([(x1, lf, z1), (x1, lb, z0), (x1, yb, z0), (x1, yf, z1)], uv)  # right (+X)
        self.quad([(x0, yf, z1), (x1, yf, z1), (x1, yb, z0), (x0, yb, z0)], top)  # top
        self.quad([(x0, lb, z0), (x1, lb, z0), (x1, lf, z1), (x0, lf, z1)], uv)  # bottom


def booth():
    mesh = Mesh()
    wall, roof = region(WALL), region(ROOF)
    trim, plaque = region(TRIM, (0, 48)), region(TRIM, (48, 64))
    seat, floor = region(STONE, (0, 32)), region(STONE, (32, 64))
    mesh.block((-96, 0, -96), (96, 8, 96), floor)
    # Four posts, the front ones taller under the sloping roof.
    for x in (-100, 86):
        mesh.block((x, 0, 82), (x + 14, 290, 96), trim)
        mesh.block((x, 0, -100), (x + 14, 256, -86), trim)
    # The back and the two sides; the front stays open.
    mesh.block((-86, 8, -96), (86, 256, -88), wall)
    for x in (-96, 88):
        mesh.block((x, 8, -86), (x + 8, 256, 82), wall)
    # A beam over the door and the roof, overhanging and sloping down to the back.
    mesh.block((-100, 256, 82), (100, 290, 96), trim)
    mesh.block((-118, 252, -122), (118, 264, 124), trim, top_uv=roof, front_rise=44)
    # The plaque hangs at the roof's front edge, where the camera sees it from above.
    mesh.block((-46, 270, 124), (46, 316, 128), plaque)
    # The seat in the middle, where the open front shows it, its tank against the back wall.
    mesh.block((-34, 8, -88), (34, 120, -62), seat)
    mesh.block((-30, 8, -62), (30, 56, 10), seat)
    mesh.block((-22, 56, -54), (22, 58, 0), floor)
    return mesh


def glb(mesh, texture):
    blobs, views, accessors = [], [], []

    def add(data, target=None):
        offset = sum(len(b) for b in blobs)
        pad = (-offset) % 4
        if pad:
            blobs.append(b"\x00" * pad)
            offset += pad
        blobs.append(data)
        view = {"buffer": 0, "byteOffset": offset, "byteLength": len(data)}
        if target:
            view["target"] = target
        views.append(view)
        return len(views) - 1

    def accessor(values, fmt, component, kind, extra=None):
        flat = [c for v in values for c in (v if isinstance(v, (list, tuple)) else [v])]
        view = add(struct.pack("<%d%s" % (len(flat), fmt), *flat), 34963 if kind == "SCALAR" else 34962)
        entry = {"bufferView": view, "componentType": component, "count": len(values), "type": kind}
        entry.update(extra or {})
        accessors.append(entry)
        return len(accessors) - 1

    lows = [min(p[i] for p in mesh.positions) for i in range(3)]
    highs = [max(p[i] for p in mesh.positions) for i in range(3)]
    position = accessor(mesh.positions, "f", 5126, "VEC3", {"min": lows, "max": highs})
    normal = accessor(mesh.normals, "f", 5126, "VEC3")
    uv = accessor(mesh.uvs, "f", 5126, "VEC2")
    index = accessor(mesh.indices, "H", 5123, "SCALAR")
    image = add(texture)
    document = {
        "asset": {"version": "2.0", "generator": "scripts/art/make_toilet_booth.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "toilet_booth", "mesh": 0}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": position, "NORMAL": normal, "TEXCOORD_0": uv},
                                    "indices": index, "material": 0}]}],
        "materials": [{"name": "booth", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0},
                                                                 "metallicFactor": 0.0}}],
        "textures": [{"source": 0, "sampler": 0}],
        "samplers": [{"magFilter": 9728, "minFilter": 9728, "wrapS": 33071, "wrapT": 33071}],
        "images": [{"bufferView": image, "mimeType": "image/png"}],
        "bufferViews": views,
        "accessors": accessors,
    }
    binary = b"".join(blobs)
    binary += b"\x00" * ((-len(binary)) % 4)
    document["buffers"] = [{"byteLength": len(binary)}]
    text = json.dumps(document, separators=(",", ":")).encode()
    text += b" " * ((-len(text)) % 4)
    chunks = struct.pack("<II", len(text), 0x4E4F534A) + text + struct.pack("<II", len(binary), 0x004E4942) + binary
    return struct.pack("<III", 0x46546C67, 2, 12 + len(chunks)) + chunks


def main():
    root = Path(__file__).resolve().parents[2]
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "content/packs/game/props/toilet_booth.glb"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(glb(booth(), atlas()))
    print(out)


if __name__ == "__main__":
    main()
