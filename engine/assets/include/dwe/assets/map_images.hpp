#pragma once

#include <dwe/assets/image.hpp>
#include <dwe/assets/indexed_image.hpp>
#include <dwe/assets/map_document.hpp>
#include <dwe/core/error.hpp>
#include <dwe/core/math.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The images of a field map as the extraction leaves them (docs/formats/mapas.md): the background
// as palette indices with one palette per time of day, and the texture pages of its sprites with
// the CLUT rows that come with them.
//
// The extracted files still refer to each other the way the original's map files do: a sprite names
// its page by a texture X and its palette by a VRAM CLUT row, and its depth point maps to an
// ordering-table entry with clamps and a sentinel. The constants below are those properties of the
// extracted data. This loader resolves them, so the renderer only draws textured quads with an
// order value. Resolving them once in the extraction tool, as an additive output, is the planned
// follow-up (ADR 0020).
namespace dwe::assets {

class VirtualFileSystem;

// The side of a background tile, in pixels (MapTiles counts them).
inline constexpr std::int32_t MapTileSize = 128;

// The side of a texture page, in texels, for 4 and 8 bits. A map's texture entries are uploaded
// where their metadata says, whatever their size, and read back as pages of this size.
inline constexpr std::int32_t MapPageTexels = 256;

// Where the original puts a map's texture pages and palettes in VRAM, as the extracted metadata and
// sprites name them.
struct MapVramLayout {
  // A sprite's texX / MapPageTexels is a page counted from this VRAM X (overworld.c:
  // buildMapOverlayPrim).
  static constexpr std::int32_t FirstPageX = 384;
  // The width in VRAM units of an 8-bit page and of a 4-bit page.
  static constexpr std::int32_t EightBitPageWidth = 128;
  static constexpr std::int32_t FourBitPageWidth = 64;
  // The row of the palette of the time of day, which sprites with clut -1 read (map.c: setupMap
  // copies it there).
  static constexpr std::int32_t TimeOfDayRow = 480;
  // The row of background palette 0; palette i is in row FirstBackgroundPaletteRow + i (map.c:
  // setupMap).
  static constexpr std::int32_t FirstBackgroundPaletteRow = 481;
  // clut 0-15 is a row of the first 4-bit page's CLUT; clut 16 or more is the row clut +
  // ClutRowBase.
  static constexpr std::int32_t FourBitCluts = 16;
  static constexpr std::int32_t ClutRowBase = 468;
};

// One texture entry or page of a map: its indices and where its metadata says the original puts
// it (overworld.c: loadMapImage1, loadMapImage2).
struct MapTexturePage {
  std::int32_t bitsPerPixel = 8;
  // The left edge in VRAM, in 16-bit units (image_rect.x of an entry's metadata).
  std::int32_t vramX = 0;
  // The top row in VRAM (image_rect.y); 0 for a page.
  std::int32_t vramY = 0;
  // The VRAM row of its CLUT (clut_rect.y), the first row for several.
  std::int32_t clutRow = 0;
  IndexedImage image;
  // Its CLUT rows, from the 15-bit colours of its metadata.
  std::vector<std::vector<PaletteColor>> cluts;
};

// Reads a texture page from its indexed image and the text of its `<nnn>_meta` document.
core::Result<MapTexturePage> parseMapTexturePage(std::int32_t bitsPerPixel, IndexedImage image,
                                                 std::string_view metaYaml);

// The alpha of a colour whose STP bit is set: drawn opaque by an opaque sprite and mixed by a
// semi-transparent one, as the original does per texel. Other colours have alpha 255, and 0x0000
// alpha 0.
inline constexpr std::uint8_t SemiTransparentAlpha = 128;

// A colour of the original's 15-bit format as RGBA8: each channel's bits are repeated (v << 3 | v
// >> 2), 0x0000, the only colour never drawn, is transparent, and a set STP bit gives
// SemiTransparentAlpha (docs/formats/tim.md).
PaletteColor colorFromOriginal(std::uint16_t value);

// The colours of a palette that came through a PNG, which keeps no STP bits, as colorFromOriginal
// would give them where that is still known: an opaque black can only have been 0x8000, whose STP
// bit is set, so it gets SemiTransparentAlpha. The other colours keep alpha 255 (their STP bit is
// lost; docs/formats/mapas.md).
std::vector<PaletteColor> paletteFromPng(std::vector<PaletteColor> palette);

// How a sprite mixes with what is behind it: the four semi-transparency modes, or opaque
// (transparency 4).
enum class SpriteBlend : std::uint8_t {
  Average,    // 0: B / 2 + F / 2
  Add,        // 1: B + F
  Subtract,   // 2: B - F
  AddQuarter, // 3: B + F / 4
  Opaque,     // 4
};

SpriteBlend spriteBlend(std::int32_t transparency);

// The order values of depth-layer sprites: the original's ordering-table entries, each
// MapOrderingTableStep units of view depth deep (overworld.c: calcMapObjectOrder,
// renderMapOverlays).
inline constexpr float MapOrderingTableStep = 16.0F;
struct MapOrder {
  // The nearest entry a sprite's depth point gets.
  static constexpr std::int32_t Nearest = 0x23;
  // Depths of Limit entries or more get Farthest; entries just under Limit are kept as they are.
  static constexpr std::int32_t Limit = 0x1000;
  static constexpr std::int32_t Farthest = 0xFF6;
  // A sprite whose depth point has y == BehindEverythingY goes just in front of the background.
  static constexpr std::int32_t BehindEverythingY = 10000;
  static constexpr std::int32_t BehindEverything = 0xFFE;
  // Instances with MapObjectInstance::ScreenSpaceFlag (snow), drawn over the scene.
  static constexpr std::int32_t ScreenSpace = 0x1E;
};

// A sprite's order value given the view depth of its depth point: depth / MapOrderingTableStep, at
// least Nearest, Farthest from Limit on, or BehindEverything for the sentinel.
std::int32_t mapSpriteOrderValue(const MapSprite& sprite, float viewDepth);

// The images of a map's sprites, resolved once for every time of day, and where each sprite is in
// them.
struct MapSpriteSheet {
  struct Texture {
    // Sprites with clut -1 follow the background's palette of the time of day: their texture keeps
    // the indices (indices, without a palette) to be drawn with the background's palettes. The
    // others have a fixed palette and are RGBA (colors).
    bool timeOfDayPalette = false;
    IndexedImage indices;
    Image colors;

    [[nodiscard]] std::uint32_t width() const {
      return timeOfDayPalette ? indices.width : colors.width;
    }
    [[nodiscard]] std::uint32_t height() const {
      return timeOfDayPalette ? indices.height : colors.height;
    }
  };
  struct Placement {
    // Index into textures, or NoTexture.
    std::size_t texture = 0;
    // Top-left texel and size, in texels of that texture.
    glm::ivec2 texel{0};
    glm::ivec2 size{0};
  };
  static constexpr std::size_t NoTexture = static_cast<std::size_t>(-1);

  std::vector<Texture> textures;
  // One per sprite of the map, in order. Sprites that cannot be resolved (no page at their texture
  // X, or no palette at their CLUT row) get NoTexture.
  std::vector<Placement> placements;
};

// The palette a CLUT reference reads (overworld.c: buildMapOverlayPrim): for clut -1 the
// background's first palette (the day's, which the time of day replaces), for 0-15 a row of the
// first 4-bit page's CLUT, from 16 on the row clut + 468: an 8-bit page's CLUT uploaded there, or
// else a background palette. nullptr when nothing was uploaded there.
const std::vector<PaletteColor>*
mapClutPalette(std::int32_t clut, std::span<const MapTexturePage> pages,
               std::span<const std::vector<PaletteColor>> backgroundPalettes);

// Resolves the page and palette of each sprite (overworld.c: buildMapOverlayPrim).
// `backgroundPalettes` are the background's palettes (day, dusk, night, palette3...). A sprite with
// clut -1 reads the palette of the time of day; one with clut 0-15 a row of the first 4-bit page's
// CLUT; one with clut 16 or more the row clut + 468: an 8-bit page's CLUT uploaded there, or else a
// background palette.
MapSpriteSheet buildMapSpriteSheet(std::span<const MapSprite> sprites,
                                   std::span<const MapTexturePage> pages,
                                   std::span<const std::vector<PaletteColor>> backgroundPalettes);

// The player's own drawing of a map's background (image_replacement.hpp): one RGBA image per
// variant, `scale` times the background's size, drawn instead of its pixels. The background's
// indices and palettes stay the game's, so the sprites, the cut-outs and what is drawn read them as
// before; each pixel's alpha is the class of the background texel under it (colorFromOriginal).
// A variant without a usable image of its own gets the game's, `scale` times larger.
struct MapBackgroundReplacement {
  std::uint32_t scale = 1;
  // One per variant of MapImages::backgroundVariants.
  std::vector<Image> variants;
};

// Everything a map draws, read from its extracted files.
struct MapImages {
  // The background's palette indices, the same for every variant: MapTileSize pixels per tile of
  // the document.
  IndexedImage background;
  std::vector<std::string> backgroundVariants;
  // One per variant, at least one.
  std::vector<std::vector<PaletteColor>> backgroundPalettes;
  // The MapPageTexels square pages that sprites look up (MapVramLayout), read back from VRAM
  // after every entry is uploaded.
  std::vector<MapTexturePage> pages;
  MapSpriteSheet sprites;
  // When a pack of Layer::User has an image of a background variant that fits it.
  std::optional<MapBackgroundReplacement> replacement;
};

// Reads a map's backgrounds and texture pages (document.assets) and resolves its sprites. The
// game's own files are read under any pack of Layer::User; such a pack's background images become
// the replacement, and one that does not fit (not a PNG, another aspect ratio, a factor that is not
// a whole number up to MaxReplacementScale or not that of the other variants) is left out with a
// warning in the log, the game's image drawn instead. Fails when a background is not the size of
// the document's tiles, the variants differ in size or pixels, or a texture entry does not fit in
// the map's part of VRAM (MapVramLayout::FirstPageX to the right edge, rows 0 to MapPageTexels).
core::Result<MapImages> loadMapImages(const VirtualFileSystem& vfs, const MapDocument& document);

} // namespace dwe::assets
