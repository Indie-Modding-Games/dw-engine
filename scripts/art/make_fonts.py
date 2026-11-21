#!/usr/bin/env python3
# /// script
# dependencies = ["fonttools"]
# ///
"""Makes the UI's fonts for the languages beyond Latin (docs/architecture/ui.md#fonts) and checks that every text of the
packs has its glyphs.

- ui/fonts/pixel_body.ttf and pixel_small.ttf get Latin Extended-A, Greek, Cyrillic and the general punctuation of the
  public-domain X11 misc-fixed fonts they come from (9x15 bold and 7x13 bold), drawn as the existing glyphs are: one
  square of 64 units per pixel.
- ui/fonts/cjk/noto_sans_cjk_{sc,jp,kr}.otf: Noto Sans CJK (OFL), cut down to each language's common characters (GB 2312
  for Simplified Chinese, JIS X 0208 for Japanese, KS X 1001's Hangul and symbols for Korean) plus every character
  the packs' texts of that language use and the languages' own names. The smooth skins' fallback.
- ui/fonts/cjk/pixel_cjk_{sc,jp,kr}.otf: Fusion Pixel 12px proportional (OFL), cut down the same way. The pixel
  families' and the original look's fallback.

Only GSUB/GPOS and hinting are dropped: RmlUi draws through FreeType without shaping. A text that later uses a
character outside these sets is found by `check`, and running the script again adds it.

Usage:
  uv run scripts/art/make_fonts.py make [--fusion <folder of the Fusion Pixel 12px proportional .otf files>]
      [--noto-cjk /usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc] [--misc /usr/share/fonts/X11/misc]
  uv run scripts/art/make_fonts.py check
"""

import argparse
import gzip
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FONTS = ROOT / "content/packs/game/ui/fonts"
LOCALIZATION = ROOT / "content/packs/game/localization"
PACKS = ROOT / "content/packs"

# The pixel families: their file, the misc-fixed font they were converted from, and its ascent in pixels.
PIXEL_FONTS = {"pixel_body.ttf": "9x15B.pcf.gz", "pixel_small.ttf": "7x13B.pcf.gz"}
UNITS_PER_PIXEL = 64
# What the pixel families gain from misc-fixed: Latin Extended-A and the Romanian commas, Greek, Cyrillic (with the
# Ukrainian ghe), the general punctuation, the euro and the numero sign.
PIXEL_RANGES = [
    (0x0100, 0x017F),
    (0x0218, 0x021B),
    (0x0370, 0x03FF),
    (0x0400, 0x045F),
    (0x0490, 0x0491),
    (0x2010, 0x203A),
    (0x20AC, 0x20AC),
    (0x2116, 0x2116),
]

# Glyphs drawn again over misc-fixed's, where its bold cell blurs one letter into another: the lowercase em (м) of
# 9x15B and 7x13B fills a row between its stems and reads as en (н). Each is (advance, left, ascent, rows) in pixels,
# as read_pcf gives them.
PIXEL_REDRAWN = {
    "pixel_body.ttf": {
        0x043C: (
            9,
            0,
            7,
            [
                [0, 1, 1, 0, 0, 0, 1, 1, 0],
                [0, 1, 1, 1, 0, 1, 1, 1, 0],
                [0, 1, 1, 0, 1, 0, 1, 1, 0],
                [0, 1, 1, 0, 0, 0, 1, 1, 0],
                [0, 1, 1, 0, 0, 0, 1, 1, 0],
                [0, 1, 1, 0, 0, 0, 1, 1, 0],
                [0, 1, 1, 0, 0, 0, 1, 1, 0],
            ],
        ),
    },
    "pixel_small.ttf": {
        0x043C: (
            7,
            0,
            6,
            [
                [1, 1, 0, 0, 0, 1, 1],
                [1, 1, 1, 0, 1, 1, 1],
                [1, 1, 0, 1, 0, 1, 1],
                [1, 1, 0, 0, 0, 1, 1],
                [1, 1, 0, 0, 0, 1, 1],
                [1, 1, 0, 0, 0, 1, 1],
            ],
        ),
    },
}

# The CJK fonts: the language whose texts they draw first, Noto Sans CJK's face in the collection, the Fusion Pixel
# file, and the legacy encoding whose characters are the common set.
CJK = {
    "sc": {"language": "zh", "noto": "Noto Sans CJK SC", "fusion": "zh_hans", "encoding": "gb2312"},
    "jp": {"language": "ja", "noto": "Noto Sans CJK JP", "fusion": "ja", "encoding": "euc_jp"},
    "kr": {"language": "ko", "noto": "Noto Sans CJK KR", "fusion": "ko", "encoding": "euc_kr"},
}


def is_cjk(code):
    """Whether a character is one the CJK fonts are for: CJK symbols, kana, Hangul, ideographs, full-width forms."""
    return (
        0x1100 <= code <= 0x11FF
        or 0x2E80 <= code <= 0x2FDF
        or 0x3000 <= code <= 0x31FF
        or 0x3200 <= code <= 0x33FF
        or 0x3400 <= code <= 0x4DBF
        or 0x4E00 <= code <= 0x9FFF
        or 0xA960 <= code <= 0xA97F
        or 0xAC00 <= code <= 0xD7FF
        or 0xF900 <= code <= 0xFAFF
        or 0xFE30 <= code <= 0xFE4F
        or 0xFF00 <= code <= 0xFFEF
    )


def encoding_set(encoding):
    """The CJK characters of a two-byte EUC encoding: every lead and trail byte from 0xA1 to 0xFE. For Korean, the
    hanja rows (0xCA to 0xFD) are left out: Korean text does not use them."""
    found = set()
    for lead in range(0xA1, 0xFF):
        if encoding == "euc_kr" and 0xCA <= lead <= 0xFD:
            continue
        for trail in range(0xA1, 0xFF):
            try:
                text = bytes([lead, trail]).decode(encoding)
            except UnicodeDecodeError:
                continue
            found.update(ord(c) for c in text if is_cjk(ord(c)))
    return found


def texts_of(language):
    """The characters of a language's texts: its localization tables and its translation pack (comment lines left
    out, as nothing draws them)."""
    chars = set()
    folders = [LOCALIZATION / language, PACKS / f"{language}-translation"]
    for folder in folders:
        if folder.is_dir():
            for path in folder.rglob("*.yaml"):
                for line in path.read_text(encoding="utf-8").splitlines():
                    if not line.lstrip().startswith("#"):
                        chars.update(ord(c) for c in line)
    return chars


def languages():
    return sorted(p.name for p in LOCALIZATION.iterdir() if (p / "language.yaml").is_file())


def own_names():
    """The characters of every language's name for itself, which every language box shows."""
    chars = set()
    for language in languages():
        chars.update(ord(c) for c in (LOCALIZATION / language / "language.yaml").read_text(encoding="utf-8"))
    return chars


def charset(key):
    spec = CJK[key]
    chars = encoding_set(spec["encoding"])
    chars |= {c for c in texts_of(spec["language"]) | own_names() if is_cjk(c)}
    if key == "kr":
        chars |= set(range(0x3131, 0x318F))  # the compatibility jamo the name entry shows
    if key == "jp":
        chars |= set(range(0x3041, 0x3097)) | set(range(0x30A1, 0x30FF))  # every kana the name entry shows
    return chars


# ---------------------------------------------------------------------------------------------------------------
# misc-fixed (PCF) to pixel outlines


def read_pcf(path):
    """The glyphs of a PCF bitmap font: codepoint -> (advance, left, ascent, rows of 0/1)."""
    data = gzip.open(path).read() if str(path).endswith(".gz") else Path(path).read_bytes()
    if data[:4] != b"\x01fcp":
        raise SystemExit(f"{path}: not a PCF font")
    (count,) = struct.unpack("<I", data[4:8])
    tables = {}
    for i in range(count):
        kind, _, _, offset = struct.unpack("<IIII", data[8 + 16 * i : 24 + 16 * i])
        tables[kind] = offset

    def table(kind):
        offset = tables[kind]
        (fmt,) = struct.unpack("<I", data[offset : offset + 4])
        return fmt, (">" if fmt & 4 else "<"), offset + 4

    fmt, end, at = table(1 << 2)
    metrics = []
    if fmt & 0x100:
        (n,) = struct.unpack(end + "H", data[at : at + 2])
        at += 2
        for _ in range(n):
            metrics.append(tuple(b - 0x80 for b in data[at : at + 5]))
            at += 5
    else:
        (n,) = struct.unpack(end + "I", data[at : at + 4])
        at += 4
        for _ in range(n):
            metrics.append(struct.unpack(end + "hhhhhH", data[at : at + 12])[:5])
            at += 12
    fmt, end, at = table(1 << 3)
    (n,) = struct.unpack(end + "I", data[at : at + 4])
    at += 4
    offsets = struct.unpack(end + f"{n}I", data[at : at + 4 * n])
    at += 4 * n
    sizes = struct.unpack(end + "4I", data[at : at + 16])
    bits = data[at + 16 : at + 16 + sizes[fmt & 3]]
    pad = 1 << (fmt & 3)
    msb = bool(fmt & 8)
    _, end, at = table(1 << 5)
    min2, max2, min1, max1, _ = struct.unpack(end + "5H", data[at : at + 10])
    columns = max2 - min2 + 1
    total = columns * (max1 - min1 + 1)
    indices = struct.unpack(end + f"{total}H", data[at + 10 : at + 10 + 2 * total])
    glyphs = {}
    for k, index in enumerate(indices):
        if index == 0xFFFF:
            continue
        code = (min1 + k // columns) * 256 + min2 + k % columns
        left, right, advance, ascent, descent = metrics[index]
        width, height = right - left, ascent + descent
        stride = ((width + pad * 8 - 1) // (pad * 8)) * pad
        rows = []
        for y in range(height):
            row = []
            for x in range(width):
                byte = bits[offsets[index] + y * stride + x // 8]
                row.append((byte >> (7 - x % 8)) & 1 if msb else (byte >> (x % 8)) & 1)
            rows.append(row)
        glyphs[code] = (advance, left, ascent, rows)
    return glyphs


def pixel_glyph(rows, left, ascent):
    """A TrueType glyph of a bitmap: one clockwise rectangle per run of set pixels in a row, as the existing glyphs."""
    from fontTools.pens.ttGlyphPen import TTGlyphPen

    pen = TTGlyphPen(None)
    for y, row in enumerate(rows):
        top = (ascent - y) * UNITS_PER_PIXEL
        bottom = top - UNITS_PER_PIXEL
        x = 0
        while x < len(row):
            if not row[x]:
                x += 1
                continue
            start = x
            while x < len(row) and row[x]:
                x += 1
            x0, x1 = (left + start) * UNITS_PER_PIXEL, (left + x) * UNITS_PER_PIXEL
            pen.moveTo((x0, bottom))
            pen.lineTo((x0, top))
            pen.lineTo((x1, top))
            pen.lineTo((x1, bottom))
            pen.closePath()
    return pen.glyph()


def extend_pixel_font(path, pcf):
    from fontTools.ttLib import TTFont

    font = TTFont(path)
    cmap = font.getBestCmap()
    glyphs = read_pcf(pcf)
    redrawn = PIXEL_REDRAWN.get(Path(path).name, {})
    glyphs.update(redrawn)
    order = list(font.getGlyphOrder())
    added = 0
    for low, high in PIXEL_RANGES:
        for code in range(low, high + 1):
            if (code in cmap and code not in redrawn) or code not in glyphs:
                continue
            advance, left, ascent, rows = glyphs[code]
            name = f"uni{code:04X}"
            glyph = pixel_glyph(rows, left, ascent)
            font["glyf"][name] = glyph
            glyph.recalcBounds(font["glyf"])
            font["hmtx"][name] = (advance * UNITS_PER_PIXEL, glyph.xMin if glyph.numberOfContours else 0)
            if name not in order:
                order.append(name)
            for table in font["cmap"].tables:
                if table.isUnicode():
                    table.cmap[code] = name
            added += 1
    font.setGlyphOrder(order)
    font["glyf"].glyphOrder = order
    font["maxp"].numGlyphs = len(order)
    font.save(path)
    return added


# ---------------------------------------------------------------------------------------------------------------
# CJK subsets


def subset(source, output, chars, font_number=0):
    from fontTools import subset as fs
    from fontTools.ttLib import TTFont

    options = fs.Options()
    options.layout_features = []
    options.hinting = False
    options.desubroutinize = False
    options.name_IDs = [0, 1, 2, 3, 4, 5, 6, 13, 14]
    options.notdef_outline = True
    options.font_number = font_number
    font = fs.load_font(str(source), options)
    subsetter = fs.Subsetter(options)
    subsetter.populate(unicodes=sorted(chars))
    subsetter.subset(font)
    for table in ("GSUB", "GPOS", "GDEF", "BASE", "VORG", "vhea", "vmtx", "DSIG"):
        if table in font:
            del font[table]
    output.parent.mkdir(parents=True, exist_ok=True)
    fs.save_font(font, str(output), options)
    covered = len(TTFont(output).getBestCmap())
    return covered


def noto_number(collection, family):
    from fontTools.ttLib import TTCollection

    for number, font in enumerate(TTCollection(collection).fonts):
        names = {r.toUnicode() for r in font["name"].names if r.nameID == 1}
        if family in names:
            return number
    raise SystemExit(f"{collection} has no {family}")


def make(arguments):
    misc = Path(arguments.misc)
    for name, pcf in PIXEL_FONTS.items():
        added = extend_pixel_font(FONTS / name, misc / pcf)
        print(f"{name}: {added} glyphs added from {pcf}")
    if arguments.fusion is None:
        return
    for key, spec in CJK.items():
        chars = charset(key)
        noto = FONTS / "cjk" / f"noto_sans_cjk_{key}.otf"
        covered = subset(arguments.noto_cjk, noto, chars, noto_number(arguments.noto_cjk, spec["noto"]))
        print(f"{noto.name}: {covered} characters, {noto.stat().st_size // 1024} KiB")
        fusion = Path(arguments.fusion) / f"fusion-pixel-12px-proportional-{spec['fusion']}.otf"
        pixel = FONTS / "cjk" / f"pixel_cjk_{key}.otf"
        covered = subset(fusion, pixel, chars)
        print(f"{pixel.name}: {covered} characters, {pixel.stat().st_size // 1024} KiB")


# ---------------------------------------------------------------------------------------------------------------
# check


def cmap_of(path):
    from fontTools.ttLib import TTFont

    return set(TTFont(path).getBestCmap())


def check(_arguments):
    smooth = cmap_of(FONTS / "noto_sans_regular.ttf")
    pixel = cmap_of(FONTS / "pixel_body.ttf") & cmap_of(FONTS / "pixel_small.ttf")
    for key in CJK:
        smooth |= cmap_of(FONTS / "cjk" / f"noto_sans_cjk_{key}.otf")
        pixel |= cmap_of(FONTS / "cjk" / f"pixel_cjk_{key}.otf")
    # The story's button marks (○ △ □) are drawn as button prompts, not as letters (dwe/ui/prompts.hpp).
    ignored = {0x09, 0x0A, 0x0D, 0x200B, 0xFEFF, 0x25A1, 0x25B3, 0x25CB}
    problems = 0
    for language in languages():
        used = {c for c in texts_of(language) if c >= 0x20 and c not in ignored}
        for label, have in (("smooth", smooth), ("pixel", pixel)):
            missing = sorted(used - have)
            if missing:
                problems += len(missing)
                shown = " ".join(f"U+{c:04X}({chr(c)})" for c in missing[:20])
                print(f"{language}: {len(missing)} characters without a {label} glyph: {shown}")
    # Each language's CJK font must hold every CJK character of that language, so its regional forms draw them.
    for key, spec in CJK.items():
        have = cmap_of(FONTS / "cjk" / f"noto_sans_cjk_{key}.otf") & cmap_of(FONTS / "cjk" / f"pixel_cjk_{key}.otf")
        missing = sorted(c for c in texts_of(spec["language"]) if is_cjk(c) and c not in have)
        if missing:
            problems += len(missing)
            print(f"{spec['language']}: {len(missing)} characters outside its own fonts (run make again)")
    print(f"{problems} problems")
    return 1 if problems else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    making = commands.add_parser("make")
    making.add_argument("--fusion")
    making.add_argument("--noto-cjk", default="/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc")
    making.add_argument("--misc", default="/usr/share/fonts/X11/misc")
    commands.add_parser("check")
    arguments = parser.parse_args()
    if arguments.command == "make":
        make(arguments)
        return 0
    return check(arguments)


if __name__ == "__main__":
    sys.exit(main())
