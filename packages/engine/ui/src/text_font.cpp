#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/png_only_images.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/ui/text_font.hpp>

#include <cstddef>
#include <string>
#include <utility>

#include "messages.hpp"

namespace dwe::ui {
namespace {

constexpr std::string_view ModernName = "modern";
constexpr std::string_view OriginalName = "original";

// dwx cook keeps the images under font/ as PNG (assets::PngOnlyImages), whose exact pixels
// BitmapFont reads.
bool isFontAtlasId(std::string_view text) {
  const auto id = assets::AssetId::parse(text);
  return id && assets::matchesIdPattern(assets::FontAtlasPattern, *id);
}

} // namespace

std::string_view textFontName(TextFont font) {
  return font == TextFont::Original ? OriginalName : ModernName;
}

std::optional<TextFont> textFontNamed(std::string_view name) {
  if (name == ModernName) {
    return TextFont::Modern;
  }
  if (name == OriginalName) {
    return TextFont::Original;
  }
  return std::nullopt;
}

core::Result<OriginalLook> parseOriginalLook(std::string_view yaml) {
  auto tree = core::parseYaml(yaml);
  if (!tree) {
    return std::unexpected{tree.error()};
  }
  assets::YamlReader reader;
  OriginalLook look;
  reader.expectKeys(*tree, "original_look", {"family", "default", "modern", "original"});
  look.family = reader.text(reader.field(*tree, "family"));
  const core::YamlNode& choice = reader.field(*tree, "default");
  if (const auto font = textFontNamed(reader.text(choice))) {
    look.defaultFont = *font;
  } else {
    reader.fail(choice, messages::UnknownTextFont);
  }
  look.modernFamily = reader.text(reader.field(*tree, "modern"));
  const core::YamlNode& original = reader.field(*tree, "original");
  reader.expectKeys(original, "original", {"atlas", "meta", "baseline", "fallback_size"});
  const core::YamlNode& atlas = reader.field(original, "atlas");
  look.atlas = reader.text(atlas);
  if (reader.ok() && !isFontAtlasId(look.atlas)) {
    reader.fail(atlas, messages::FontAtlasOutsideFonts);
  }
  look.meta = reader.text(reader.field(original, "meta"));
  look.baseline = reader.integer<int>(reader.field(original, "baseline"));
  const core::YamlNode& fallbackSize = reader.field(original, "fallback_size");
  look.fallbackSize = reader.number(fallbackSize);
  if (reader.ok() && !(look.fallbackSize > 0.0F)) {
    reader.fail(fallbackSize, messages::FallbackSizeNotPositive);
  }
  return reader.finish(std::move(look), messages::OriginalLookContext);
}

std::string finishUppercase(std::string_view text) {
  // The UTF-8 lead byte of the Latin-1 letters from U+00C0, and the small ones' second bytes:
  // U+00E0 to U+00FE but the division sign, each 0x20 past its capital.
  constexpr unsigned char LatinLead = 0xC3;
  constexpr unsigned char FirstSmall = 0xA0;
  constexpr unsigned char LastSmall = 0xBE;
  constexpr unsigned char DivisionSign = 0xB7;
  constexpr unsigned char CaseOffset = 0x20;
  bool capitals = false;
  bool accented = false;
  for (std::size_t i = 0; i < text.size(); ++i) {
    const auto byte = static_cast<unsigned char>(text[i]);
    if (byte >= 'a' && byte <= 'z') {
      return std::string{text};
    }
    capitals = capitals || (byte >= 'A' && byte <= 'Z');
    if (byte == LatinLead && i + 1 < text.size()) {
      const auto next = static_cast<unsigned char>(text[i + 1]);
      accented = accented || (next >= FirstSmall && next <= LastSmall && next != DivisionSign);
    }
  }
  std::string result{text};
  if (!capitals || !accented) {
    return result;
  }
  for (std::size_t i = 0; i + 1 < result.size(); ++i) {
    const auto next = static_cast<unsigned char>(result[i + 1]);
    if (static_cast<unsigned char>(result[i]) == LatinLead && next >= FirstSmall &&
        next <= LastSmall && next != DivisionSign) {
      result[i + 1] = static_cast<char>(next - CaseOffset);
      ++i;
    }
  }
  return result;
}

} // namespace dwe::ui
