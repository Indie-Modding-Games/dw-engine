# KTX2 textures from `dwx cook`

`dwx cook` converts the PNGs in a package into [KTX 2.0](https://registry.khronos.org/KTX/specs/2.0/ktxspec.v2.html)
textures with the same id (`ui/frame.png` → `ui/frame.ktx2`): **Basis Universal UASTC with
zstd supercompression**, which the engine transcodes on load to **BC7**, or to RGBA8 if the GPU does not sample BC7.
KTX2 is the standard Khronos container for Vulkan and the one that supports Basis Universal. The file is written and
read by libktx (KTX-Software 4.4.2, via vcpkg), which brings the Basis Universal encoder and transcoder. The
decision is in [ADR 0017](../adr/0017-cooking-and-pak-files.md).

## Which PNGs are converted

A PNG is converted unless the game reads it from its PNG or chooses its palette at run time: then an RGBA color
texture would lose the indices or the exact pixels and the engine's reader could no longer use it.

First, by asset type: the IDs in the `assets::PngOnlyImages` list
(`packages/engine/assets/include/dwe/assets/png_only_images.hpp`) are always copied as PNG. The rule is a single
function, `assets::pngReadingOf(id, meta)`, shared by `dwx cook`, `dwx validate` and `dwe-game --check-content`: a
new type is one more entry in the table, and what an image's `_meta` declares can only exempt an image that the table
names (today nothing exempts it), never add one. They are the map backgrounds (`map/<id>/background_*`, with no
sidecar YAML: the time of day chooses their palette and sprites with `clut` −1 use it), the maps' texture pages
(`map/<id>/texture8/NNN`, `texture4/NNN`), also those of a mod's maps (`map/<package>/<name>/...`), and the atlas of
the bitmap map font (`font/<name>`, from which the UI reads the exact alpha of each pixel). `dwx validate` and
`dwe-game --check-content` reject for those IDs any file that is not a PNG their reader can read (indexed, for
the map ones).

Then, by the sidecar YAML (`<id>_meta.yaml`, the one `dwx extract` writes for each TIM). An **indexed** PNG
(color type 3) is copied as is, as a PNG, if its sidecar YAML:

- lists **more than one palette** in `palettes`, that is, the CLUT has several rows for the same pixels: the
  Digimon textures (each model primitive chooses its row), those of the effects and the multi-palette TIMs;
  or
- is a **map TIM with a CLUT** (`source` ends in `.MAP` and has `clut_rect`): each map sprite chooses its row
  (`clut` in `data.yaml`; −1 is row 480, which changes with the time of day), so even if the TIM's CLUT has a single
  row it can be drawn with another.

Everything else is converted: RGBA PNGs and indexed ones with a single palette (cards, most of the loose
textures). This is the integration lead's interpretation and can be reviewed (see the ADR).

## Color or data

The `color_space` key of the sidecar YAML says which: `srgb` (color) or `linear` (depth, masks, normals). Without the
key, or without a sidecar YAML, it is color. Any other value is an error. The extractor does not write the key because
everything it extracts is color. The color space goes in the `transferFunction` of the format descriptor (2 = sRGB, 1
= linear) and the engine uses it to choose the Vulkan format (`BC7_SRGB` or `BC7_UNORM`, `R8G8B8A8_SRGB` or `_UNORM`).

## What it writes

1. It computes the **complete mipmap chain** down to 1×1 (below).
2. It creates a 2D texture `VK_FORMAT_R8G8B8A8_SRGB` (43) or `_UNORM` (37), with no array or cube, with those levels.
3. It encodes it to **UASTC** with `ktxTexture2_CompressBasisEx`: level 2 (`KTX_PACK_UASTC_LEVEL_DEFAULT`), no RDO, a
   single thread per texture (`dwx cook` already cooks several at a time).
4. It supercompresses it with **zstd level 18** (`ktxTexture2_DeflateZstd`).
5. It writes it with two fixed metadata entries: `KTXorientation` = `rd` (rows from top to bottom, like the PNG) and
   `KTXwriter` = `dwx cook / libktx v4.4.2…` (libktx adds its version).

The result, in the container fields:

| Field | Value |
|---|---|
| `vkFormat` | 0 (`VK_FORMAT_UNDEFINED`: Basis Universal data, it must be transcoded) |
| `typeSize` | 1 |
| `pixelWidth`, `pixelHeight` | Sizes of the PNG, not rounded up to multiples of 4 |
| `pixelDepth`, `layerCount`, `faceCount` | 0, 0, 1 |
| `levelCount` | `floor(log2(max(width, height))) + 1` |
| `supercompressionScheme` | 2 (zstd) |
| DFD | Basic block with `colorModel` 166 (UASTC), 4×4 blocks, 16 bytes per block, `transferFunction` 2 or 1 |
| Level index | Per level, the zstd-compressed size and that of the UASTC blocks (16 bytes per 4×4 block) |
| Data | One zstd frame per level, from the smallest to the largest |

An image whose side is not a multiple of 4 is encoded in whole 4×4 blocks; when transcoding, the whole blocks come
out (BC7) or only the image's texels (RGBA8).

## How it is loaded

`assets::decodeKtx2` opens the file with libktx without loading its texels (`KTX_TEXTURE_CREATE_NO_FLAGS`) and reads
the header and the level index (`inspectKtx2`). Before handing it to libktx it checks that the data format descriptor
(DFD), the key/value data (KVD) and the supercompression global data (SGD) that the header declares fit in the
file. With that alone, `checkPlain2dKtx2` checks that it is a single 2D image (no layers or cube faces, depth 1) with
Basis Universal data, from 1 to 16384 texels per side, with a possible number of levels, without supercompression or
with BasisLZ or Zstd, that each level is inside the file and that none takes up on load (`uncompressedByteLength`
with Zstd, `byteLength` without supercompression) more than fits in a level of its size; BasisLZ ones are loaded as
they are, so the file itself bounds them. A texture that promises to inflate to gigabytes is rejected without
reserving anything. Only then does it load and inflate the texels (`ktxTexture2_LoadImageData`) and transcode all the
levels at once:

- to **BC7** (`KTX_TTF_BC7_RGBA`), 16 bytes per 4×4 block, if `rhi::Device::supportsBc7()` says the GPU samples it
  in UNORM and in sRGB (all desktop ones);
- otherwise, to **RGBA8** (`KTX_TTF_RGBA32`), 4 bytes per texel.

It returns the levels from the largest to the smallest and whether the color is sRGB. `rhi::Texture::create` uploads
BC7 only if the device supports it; otherwise it rejects it. The asset viewer of the debug overlay opens `.ktx2`
files this way. UASTC to BC7 is almost lossless (UASTC is designed for that), and to RGBA8 it gives the colors that
UASTC stored.

## How mipmaps are computed

- Each level is half the size of the previous one, rounding down and never less than 1.
- Each texel is the average of the area it covers in the previous level (a box filter), weighted by the part of each
  texel that falls inside. This way an odd side does not lose its last column. The weights are computed with
  integers, so they are exact.
- Color is averaged in **linear light** (the sRGB is decoded, averaged and encoded again) and **weighted by
  alpha**: transparent texels, which in the original's textures are black with alpha 0, do not darken their
  neighbors. Alpha is averaged as is.
- Each level comes from the previous one, in floating point, and is rounded to 8 bits at the end. Level 0 is the PNG
  untouched.

## Determinism

The same image with the same options always gives the same bytes (a test checks it, also with several threads),
within this scope:

- the versions pinned by the vcpkg baseline: libktx 4.4.2 (with its Basis Universal) and zstd 1.5.7;
- the same CPU path: the vcpkg port builds Basis Universal with SSE 4.1 on x86-64 and uses it if the CPU has it; the
  SSE path and the scalar one (or another architecture) do not promise the same blocks;
- the same compiler and the same standard library for the mipmaps: across platforms, `pow` could differ in the
  last bit and change a texel by one unit.

Outside that scope the file may change, but it is still valid and the engine reads it the same. The `dwx cook` cache
stores each output by the SHA-1 of the source and the cooker version, so a cache does not mix outputs from different
cookers.
