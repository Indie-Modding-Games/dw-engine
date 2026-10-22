#include <dwe/formats/text/font.hpp>

#include <bytes/byte_cursor.hpp>
#include <yaml/yaml_values.hpp>

#include <algorithm>
#include <charconv>
#include <format>
#include <set>
#include <string>

namespace dwe::formats::text {

namespace {

using bytes::ByteCursor;
using core::YamlNode;
using yaml::null;
using yaml::number;
using yaml::quoted;

constexpr std::size_t GlyphRecordSize = (GlyphRows + 1) * 2;
constexpr std::uint16_t UnusedBits = 0x000F;

constexpr std::array FontLocations = {
    FontLocation{.version = "us",
                 .charToGlyphTable = 0x801307E0,
                 .glyphData = 0x80130880,
                 .clut = 0x80130FE8,
                 .glyphCount = 79},
};

std::string utf8(char32_t character) {
  std::string text;
  appendUtf8(text, character);
  return text;
}

std::optional<std::uint32_t> fieldOf(const YamlNode& node, std::string_view key, int base = 10) {
  const YamlNode* value = node.find(key);
  if (value == nullptr || !value->isScalar()) {
    return std::nullopt;
  }
  const std::string& text = value->text();
  std::uint32_t result = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result, base);
  if (text.empty() || error != std::errc{} || end != text.data() + text.size()) {
    return std::nullopt;
  }
  return result;
}

} // namespace

std::optional<FontLocation> findFontLocation(std::string_view version) {
  const auto found = std::ranges::find(FontLocations, version, &FontLocation::version);
  return found == FontLocations.end() ? std::nullopt : std::optional{*found};
}

core::Result<Font> readFont(std::span<const std::byte> charToGlyphTable,
                            std::span<const std::byte> glyphData, std::span<const std::byte> clut) {
  if (charToGlyphTable.size() % 2 != 0 ||
      glyphData.size() != charToGlyphTable.size() / 2 * GlyphRecordSize) {
    return core::fail(
        std::format("{} bytes of codes and {} bytes of glyphs do not make whole glyphs of {} bytes",
                    charToGlyphTable.size(), glyphData.size(), GlyphRecordSize));
  }
  if (clut.size() != FontClutSize * 2) {
    return core::fail(
        std::format("the colour table has {} bytes, not {}", clut.size(), FontClutSize * 2));
  }
  Font font;
  ByteCursor codes{charToGlyphTable};
  ByteCursor glyphs{glyphData};
  std::set<SjisCode> seen;
  for (std::size_t i = 0; i < charToGlyphTable.size() / 2; ++i) {
    Glyph& glyph = font.glyphs.emplace_back();
    glyph.code = codes.u16();
    if (!seen.insert(glyph.code).second) {
      return core::fail(
          std::format("glyph {}: code {:04X} is already in the table", i, glyph.code));
    }
    std::ranges::generate(glyph.rows, [&] { return glyphs.u16(); });
    glyph.width = glyphs.u16();
    if (std::ranges::any_of(glyph.rows,
                            [](std::uint16_t row) { return (row & UnusedBits) != UnusedBits; })) {
      return core::fail(
          std::format("glyph {}: a row has bits 3-0 cleared, which the font never uses", i));
    }
  }
  ByteCursor colours{clut};
  std::ranges::generate(font.clut, [&] { return colours.u16(); });
  return font;
}

FontAtlas buildFontAtlas(const Font& font, std::uint32_t columns) {
  FontAtlas atlas;
  atlas.columns = std::max<std::uint32_t>(columns, 1);
  const auto cellRows =
      static_cast<std::uint32_t>((font.glyphs.size() + atlas.columns - 1) / atlas.columns);
  atlas.width = atlas.columns * GlyphColumns;
  atlas.height = std::max<std::uint32_t>(cellRows, 1) * GlyphRows;
  atlas.indices.assign(std::size_t{atlas.width} * atlas.height, 0);
  for (std::size_t i = 0; i < font.glyphs.size(); ++i) {
    const std::size_t left = i % atlas.columns * GlyphColumns;
    const std::size_t top = i / atlas.columns * GlyphRows;
    for (std::size_t y = 0; y < GlyphRows; ++y) {
      for (std::size_t x = 0; x < GlyphColumns; ++x) {
        atlas.indices[((top + y) * atlas.width) + left + x] = font.glyphs[i].ink(x, y) ? 1 : 0;
      }
    }
  }
  return atlas;
}

YamlNode fontToYaml(const Font& font, const FontAtlas& atlas) {
  YamlNode root = YamlNode::mapping();
  root.set("cell_width", number(GlyphColumns));
  root.set("cell_height", number(GlyphRows));
  root.set("columns", number(atlas.columns));
  root.set("fallback", number(fallbackGlyph(font)));
  YamlNode colours = YamlNode::sequence();
  for (const std::uint16_t colour : font.clut) {
    colours.append(quoted(std::format("{:04X}", colour)));
  }
  root.set("colors", std::move(colours));
  YamlNode glyphs = YamlNode::sequence();
  for (std::size_t i = 0; i < font.glyphs.size(); ++i) {
    const Glyph& glyph = font.glyphs[i];
    YamlNode node = YamlNode::mapping();
    node.set("index", number(i));
    node.set("sjis", quoted(std::format("{:04X}", glyph.code)));
    const auto character = sjisToUnicode(glyph.code);
    node.set("character", character ? quoted(utf8(*character)) : null());
    const auto ascii = asciiOf(glyph.code);
    node.set("ascii", ascii ? quoted(std::string(1, *ascii)) : null());
    node.set("x", number(i % atlas.columns * GlyphColumns));
    node.set("y", number(i / atlas.columns * GlyphRows));
    node.set("width", number(glyph.width));
    glyphs.append(std::move(node));
  }
  root.set("glyphs", std::move(glyphs));
  return root;
}

core::Result<Font> fontFromAtlas(const FontAtlas& atlas, const YamlNode& yaml) {
  const YamlNode* colours = yaml.find("colors");
  const YamlNode* glyphs = yaml.find("glyphs");
  if (colours == nullptr || !colours->isSequence() || colours->items().size() != FontClutSize ||
      glyphs == nullptr || !glyphs->isSequence()) {
    return core::fail("the font YAML needs 16 colors and a list of glyphs");
  }
  if (atlas.indices.size() != std::size_t{atlas.width} * atlas.height) {
    return core::fail("the atlas does not have one index per pixel");
  }
  Font font;
  for (std::size_t i = 0; i < FontClutSize; ++i) {
    const YamlNode& colour = colours->items()[i];
    std::uint16_t value = 0;
    const std::string& text = colour.isScalar() ? colour.text() : std::string{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    if (text.empty() || error != std::errc{} || end != text.data() + text.size()) {
      return core::fail(std::format("colors[{}] is not a 16-bit hex value", i));
    }
    font.clut[i] = value;
  }
  for (const YamlNode& node : glyphs->items()) {
    const auto code = fieldOf(node, "sjis", 16);
    const auto x = fieldOf(node, "x");
    const auto y = fieldOf(node, "y");
    const auto width = fieldOf(node, "width");
    if (!code || !x || !y || !width || *code > 0xFFFF || *width > 0xFFFF ||
        *x + GlyphColumns > atlas.width || *y + GlyphRows > atlas.height) {
      return core::fail(std::format("glyph {}: sjis, x, y or width is missing or outside the atlas",
                                    font.glyphs.size()));
    }
    Glyph& glyph = font.glyphs.emplace_back();
    glyph.code = static_cast<SjisCode>(*code);
    glyph.width = static_cast<std::uint16_t>(*width);
    for (std::size_t row = 0; row < GlyphRows; ++row) {
      std::uint16_t bits = UnusedBits;
      for (std::size_t column = 0; column < GlyphColumns; ++column) {
        if (atlas.indices[((*y + row) * atlas.width) + *x + column] == 0) {
          bits = static_cast<std::uint16_t>(bits | 1U << (15 - column));
        }
      }
      glyph.rows[row] = bits;
    }
  }
  return font;
}

} // namespace dwe::formats::text
