#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/ui/bitmap_font.hpp>

#include <RmlUi/Core/StringUtilities.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

#include "messages.hpp"

namespace dwe::ui {
namespace {

constexpr std::size_t RgbaBytes = 4;
constexpr std::size_t AlphaByte = 3;
constexpr std::byte Opaque{0xFF};

// The one character a meta string holds, or nothing.
std::optional<char32_t> singleCharacter(std::string_view text) {
  if (text.empty()) {
    return std::nullopt;
  }
  const char* begin = text.data();
  const char* end = begin + text.size();
  const Rml::Character character = Rml::StringUtilities::ToCharacter(begin, end);
  if (character == Rml::Character::Null ||
      Rml::StringUtilities::BytesUTF8(character) != text.size()) {
    return std::nullopt;
  }
  return static_cast<char32_t>(character);
}

bool isInk(const assets::Image& atlas, std::size_t x, std::size_t y) {
  return atlas.pixels[(((y * atlas.width) + x) * RgbaBytes) + AlphaByte] != std::byte{0};
}

// Adds the characters a glyph answers to: its character and its ASCII equivalent, each when
// present.
void addCharacters(assets::YamlReader& reader, const core::YamlNode& entry,
                   const BitmapGlyph& glyph, std::map<char32_t, BitmapGlyph>& glyphs) {
  for (std::string_view key : {"character", "ascii"}) {
    const core::YamlNode* node = entry.find(key);
    if (node == nullptr || assets::YamlReader::isNull(*node)) {
      continue;
    }
    const auto character = singleCharacter(reader.text(*node));
    if (!character) {
      reader.fail(*node, messages::NotOneCharacter);
      continue;
    }
    glyphs.try_emplace(*character, glyph);
  }
}

} // namespace

core::Result<BitmapFont> BitmapFont::parse(std::string_view meta, assets::Image atlas,
                                           int baseline) {
  auto tree = core::parseYaml(meta);
  if (!tree) {
    return std::unexpected{tree.error()};
  }
  assets::YamlReader reader;
  BitmapFont font;
  font.cellWidth_ = reader.integer<int>(reader.field(*tree, "cell_width"));
  font.cellHeight_ = reader.integer<int>(reader.field(*tree, "cell_height"));
  font.baseline_ = baseline;
  if (reader.ok() && (font.cellWidth_ < 1 || font.cellHeight_ < 1)) {
    reader.fail(*tree, messages::CellSizeTooSmall);
  }
  if (atlas.pixels.size() != std::size_t{atlas.width} * atlas.height * RgbaBytes) {
    reader.fail(*tree, messages::AtlasPixelsMismatch);
  }
  if (reader.ok() && (baseline < 1 || baseline > font.cellHeight_)) {
    reader.fail(*tree, messages::BaselineOutsideCell);
  }
  for (const core::YamlNode& entry : reader.sequence(reader.field(*tree, "glyphs"), "glyphs")) {
    const BitmapGlyph glyph{.x = reader.integer<int>(reader.field(entry, "x")),
                            .y = reader.integer<int>(reader.field(entry, "y")),
                            .advance = reader.integer<int>(reader.field(entry, "width"))};
    const bool inside = glyph.x >= 0 && glyph.y >= 0 && glyph.advance >= 0 &&
                        std::cmp_less_equal(glyph.x + font.cellWidth_, atlas.width) &&
                        std::cmp_less_equal(glyph.y + font.cellHeight_, atlas.height);
    if (reader.ok() && !inside) {
      reader.fail(entry, messages::GlyphOutsideAtlas);
    }
    addCharacters(reader, entry, glyph, font.glyphs_);
  }
  font.atlas_ = std::move(atlas);
  return reader.finish(std::move(font), messages::BitmapFontContext);
}

const BitmapGlyph* BitmapFont::glyph(char32_t character) const {
  auto found = glyphs_.find(character);
  return found == glyphs_.end() ? nullptr : &found->second;
}

std::vector<TextRun> BitmapFont::splitRuns(std::string_view text) const {
  std::vector<TextRun> runs;
  std::size_t runStart = 0;
  bool runBitmap = false;
  for (Rml::StringIteratorU8 it{text.data(), text.data(), text.data() + text.size()}; it; ++it) {
    const auto offset = static_cast<std::size_t>(it.offset());
    const bool bitmap = glyph(static_cast<char32_t>(*it)) != nullptr;
    if (offset == 0) {
      runBitmap = bitmap;
    } else if (bitmap != runBitmap) {
      runs.push_back({.text = text.substr(runStart, offset - runStart), .bitmap = runBitmap});
      runStart = offset;
      runBitmap = bitmap;
    }
  }
  if (runStart < text.size()) {
    runs.push_back({.text = text.substr(runStart), .bitmap = runBitmap});
  }
  return runs;
}

int BitmapFont::inkTop(const BitmapGlyph& glyph) const {
  for (int row = 0; row < cellHeight_; ++row) {
    for (int column = 0; column < cellWidth_; ++column) {
      if (isInk(atlas_, static_cast<std::size_t>(glyph.x) + static_cast<std::size_t>(column),
                static_cast<std::size_t>(glyph.y) + static_cast<std::size_t>(row))) {
        return row;
      }
    }
  }
  return cellHeight_;
}

assets::Image BitmapFont::inkAtlas(int scale) const {
  const auto factor = static_cast<std::size_t>(std::max(scale, 1));
  assets::Image ink{.width = static_cast<std::uint32_t>(atlas_.width * factor),
                    .height = static_cast<std::uint32_t>(atlas_.height * factor),
                    .pixels = {}};
  ink.pixels.resize(std::size_t{ink.width} * ink.height * RgbaBytes);
  for (std::size_t y = 0; y < ink.height; ++y) {
    for (std::size_t x = 0; x < ink.width; ++x) {
      if (isInk(atlas_, x / factor, y / factor)) {
        const std::size_t at = ((y * ink.width) + x) * RgbaBytes;
        std::fill_n(ink.pixels.begin() + static_cast<std::ptrdiff_t>(at), RgbaBytes, Opaque);
      }
    }
  }
  return ink;
}

} // namespace dwe::ui
