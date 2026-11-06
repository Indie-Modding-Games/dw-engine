#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/formats/text/charset.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

// The text font of the main executable (font.c): a table of Shift-JIS codes and one 12 x 11 1-bit
// bitmap per code.
namespace dwe::formats::text {

inline constexpr std::size_t GlyphColumns = 12;
inline constexpr std::size_t GlyphRows = 11;
inline constexpr std::size_t FontClutSize = 16;

// Where the font lives in one version's executable (config/<version>/symbols.txt).
struct FontLocation {
  std::string_view version;
  std::uint32_t charToGlyphTable = 0; // CHAR_TO_GLYPH_TABLE: u16 code per glyph
  std::uint32_t glyphData = 0;        // GLYPH_DATA: {u16 rows[11]; u16 width;} per glyph
  std::uint32_t clut = 0;             // FONT_CLUT: 16 colours
  std::uint32_t glyphCount = 0;
};

// Returns where the font is in a version's executable, if the tools know it. Only the US layout
// (24-byte glyphs with a width) is known; the other releases store 22-byte glyphs without one.
std::optional<FontLocation> findFontLocation(std::string_view version);

struct Glyph {
  SjisCode code = 0;
  // One row per line, top first. Bit 15 is the leftmost column and bits 15-4 are columns 0-11; a 0
  // bit is ink.
  std::array<std::uint16_t, GlyphRows> rows{};
  std::uint16_t width = 0; // the advance in pixels (font.c: drawGlyph returns it)

  // Whether column x of row y is ink.
  [[nodiscard]] bool ink(std::size_t x, std::size_t y) const {
    return (rows[y] >> (15 - x) & 1U) == 0;
  }

  bool operator==(const Glyph&) const = default;
};

struct Font {
  std::vector<Glyph> glyphs;
  // The colours text is drawn with (setTextColor picks one); colour 0 is transparent.
  std::array<std::uint16_t, FontClutSize> clut{};

  bool operator==(const Font&) const = default;
};

// The glyph the game draws for a code it does not have: drawGlyph writes the code into the last
// slot before its search, so the search always ends there.
inline std::size_t fallbackGlyph(const Font& font) {
  return font.glyphs.empty() ? 0 : font.glyphs.size() - 1;
}

// Reads the font from its three tables: two bytes per code, 24 bytes per glyph and 32 bytes of
// colours. Fails if the sizes do not match, a code repeats or a glyph's unused bits 3-0 are not all
// set (the atlas could not keep them).
core::Result<Font> readFont(std::span<const std::byte> charToGlyphTable,
                            std::span<const std::byte> glyphData, std::span<const std::byte> clut);

// The font as an atlas: cells of GlyphColumns x GlyphRows in rows of `columns`, glyph i at cell i.
// One colour index per pixel: 1 for ink, 0 for the rest.
struct FontAtlas {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t columns = 0;
  std::vector<std::uint8_t> indices;
};

FontAtlas buildFontAtlas(const Font& font, std::uint32_t columns);

// The YAML that goes with the atlas: cell size, fallback glyph, colours and, per glyph, its code,
// its character, the ASCII character it stands for in script text, its cell and its width.
core::YamlNode fontToYaml(const Font& font, const FontAtlas& atlas);

// Rebuilds the font from its atlas and YAML, the inverse of buildFontAtlas and fontToYaml.
core::Result<Font> fontFromAtlas(const FontAtlas& atlas, const core::YamlNode& yaml);

} // namespace dwe::formats::text
