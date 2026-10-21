#!/usr/bin/env python3
"""Writes the synthetic binary files of tests/mods/example_mod: a background and a one-bone model.

Usage: python3 tests/mods/make_example_mod.py tests/mods/example_mod
Everything is made up here; nothing comes from the game (tests/mods/README.md).
"""
import json
import struct
import sys
import zlib
from pathlib import Path


def png_chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)


def png(width, height, rows, palette=None):
    if palette is None:
        raw = b"".join(b"\x00" + bytes(row) for row in rows)
        header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
        body = png_chunk(b"IHDR", header)
    else:
        raw = b"".join(b"\x00" + bytes(row) for row in rows)
        header = struct.pack(">IIBBBBB", width, height, 8, 3, 0, 0, 0)
        body = png_chunk(b"IHDR", header) + png_chunk(b"PLTE", b"".join(bytes(c) for c in palette))
    body += png_chunk(b"IDAT", zlib.compress(raw, 9)) + png_chunk(b"IEND", b"")
    return b"\x89PNG\r\n\x1a\n" + body


def background(size):
    palette = [(0, 0, 0), (70, 140, 60), (90, 165, 75), (170, 150, 110), (60, 110, 160)]
    rows = []
    for y in range(size):
        row = []
        for x in range(size):
            if abs(y - size // 2) < 12:
                row.append(3)  # a path across the meadow
            elif y < 24:
                row.append(4)  # water at the top
            else:
                row.append(1 if (x // 16 + y // 16) % 2 == 0 else 2)
        rows.append(row)
    return png(size, size, rows, palette)


def model():
    # A tetrahedron on one bone, in the original's units and axes (Y down), as dwx writes Digimon models.
    positions = [(-80, 0, -80), (80, 0, -80), (0, 0, 90), (0, -180, 0)]
    uvs = [(0, 1), (1, 1), (0.5, 1), (0.5, 0)]
    indices = [0, 1, 2, 0, 3, 1, 1, 3, 2, 2, 3, 0]
    texture = png(8, 8, [[c for _ in range(8) for c in (200, 120, 60, 255)] for _ in range(8)])

    blobs = []
    views = []
    accessors = []

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

    def accessor(data, component, kind, count, extra=None):
        entry = {"bufferView": add(data), "componentType": component, "type": kind, "count": count}
        entry.update(extra or {})
        accessors.append(entry)
        return len(accessors) - 1

    flat = [c for p in positions for c in p]
    position = accessor(struct.pack("<12f", *flat), 5126, "VEC3", 4,
                        {"min": [min(p[i] for p in positions) for i in range(3)],
                         "max": [max(p[i] for p in positions) for i in range(3)]})
    uv = accessor(struct.pack("<8f", *[c for t in uvs for c in t]), 5126, "VEC2", 4)
    joints = accessor(bytes([0, 0, 0, 0] * 4), 5121, "VEC4", 4)
    weights = accessor(struct.pack("<16f", *([1.0, 0.0, 0.0, 0.0] * 4)), 5126, "VEC4", 4)
    index = accessor(struct.pack("<12H", *indices), 5123, "SCALAR", 12)
    image_view = add(texture)

    # The animations the field plays for a partner (content/packs/game/models/partner.yaml): 0 to 4, a short bob.
    times = accessor(struct.pack("<2f", 0.0, 0.05), 5126, "SCALAR", 2, {"min": [0.0], "max": [0.05]})
    moves = accessor(struct.pack("<6f", 0, 0, 0, 0, -10, 0), 5126, "VEC3", 2)
    animations = []
    for clip in range(5):
        animations.append({
            "name": f"anim_{clip:02d}",
            "extras": {"id": clip, "frameCount": 2, "ticksPerSecond": 20, "tickCount": 2, "endsInLoop": True,
                       "loops": [{"firstTick": 0, "lastTick": 1, "count": 255}]},
            "samplers": [{"input": times, "output": moves, "interpolation": "LINEAR"}],
            "channels": [{"sampler": 0, "target": {"node": 0, "path": "translation"}}],
        })

    document = {
        "asset": {"version": "2.0", "generator": "dwe tests/mods/example_mod"},
        "scene": 0,
        "scenes": [{"nodes": [0, 1]}],
        "nodes": [{"name": "bone_00"}, {"name": "mesh", "mesh": 0, "skin": 0}],
        "skins": [{"joints": [0]}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": position, "TEXCOORD_0": uv, "JOINTS_0": joints,
                                                   "WEIGHTS_0": weights},
                                    "indices": index, "material": 0}]}],
        "materials": [{"pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}],
        "textures": [{"source": 0}],
        "images": [{"bufferView": image_view, "mimeType": "image/png"}],
        "animations": animations,
        "accessors": accessors,
        "bufferViews": views,
    }
    binary = b"".join(blobs)
    binary += b"\x00" * ((-len(binary)) % 4)
    document["buffers"] = [{"byteLength": len(binary)}]
    text = json.dumps(document, separators=(",", ":")).encode()
    text += b" " * ((-len(text)) % 4)
    chunks = struct.pack("<II", len(text), 0x4E4F534A) + text + struct.pack("<II", len(binary), 0x004E4942) + binary
    return struct.pack("<III", 0x46546C67, 2, 12 + len(chunks)) + chunks


def main(pack):
    pack = Path(pack)
    (pack / "map/example_mod/meadow").mkdir(parents=True, exist_ok=True)
    (pack / "digimon/example_mod/pebblemon").mkdir(parents=True, exist_ok=True)
    (pack / "map/example_mod/meadow/background_day.png").write_bytes(background(256))
    (pack / "digimon/example_mod/pebblemon/model.glb").write_bytes(model())


if __name__ == "__main__":
    main(sys.argv[1])
