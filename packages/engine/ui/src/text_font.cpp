#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/png_only_images.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/ui/text_font.hpp>

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
  reader.expectKeys(*tree, "original_look", {"family", "default", "modern", "original"},
                    {"dialogue_family"});
  look.family = reader.text(reader.field(*tree, "family"));
  if (const core::YamlNode* dialogue = tree->find("dialogue_family"); dialogue != nullptr) {
    look.dialogueFamily = reader.text(*dialogue);
  }
  const core::YamlNode& choice = reader.field(*tree, "default");
  if (const auto font = textFontNamed(reader.text(choice))) {
    look.defaultFont = *font;
  } else {
    reader.fail(choice, messages::UnknownTextFont);
  }
  look.modernFamily = reader.text(reader.field(*tree, "modern"));
  const core::YamlNode& original = reader.field(*tree, "original");
  reader.expectKeys(original, "original", {"atlas", "meta", "baseline", "fallback_size"},
                    {"fallback_family", "fallback_pixels"});
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
  if (const core::YamlNode* family = original.find("fallback_family"); family != nullptr) {
    look.fallbackFamily = reader.text(*family);
  }
  if (const core::YamlNode* pixels = original.find("fallback_pixels"); pixels != nullptr) {
    look.fallbackPixels = reader.integer<int>(*pixels);
    if (reader.ok() && look.fallbackPixels <= 0) {
      reader.fail(*pixels, messages::FallbackPixelsNotPositive);
    }
  }
  return reader.finish(std::move(look), messages::OriginalLookContext);
}

core::Result<std::vector<FallbackChain>> parseFontFallbacks(std::string_view yaml) {
  auto tree = core::parseYaml(yaml);
  if (!tree) {
    return std::unexpected{tree.error()};
  }
  assets::YamlReader reader;
  std::vector<FallbackChain> chains;
  reader.expectKeys(*tree, "fallbacks", {"chains"});
  for (const core::YamlNode& node : reader.sequence(reader.field(*tree, "chains"), "chains")) {
    reader.expectKeys(node, "a chain", {"families", "faces"});
    FallbackChain chain;
    for (const core::YamlNode& family :
         reader.sequence(reader.field(node, "families"), "families")) {
      chain.families.push_back(reader.text(family));
    }
    for (const core::YamlNode& item : reader.sequence(reader.field(node, "faces"), "faces")) {
      reader.expectKeys(item, "a face", {"family"}, {"language", "scale"});
      FallbackFace face;
      face.family = reader.text(reader.field(item, "family"));
      if (const core::YamlNode* language = item.find("language"); language != nullptr) {
        face.language = reader.text(*language);
      }
      if (const core::YamlNode* scale = item.find("scale"); scale != nullptr) {
        face.scale = reader.number(*scale);
        if (reader.ok() && !(face.scale > 0.0F)) {
          reader.fail(*scale, messages::FallbackScaleNotPositive);
        }
      }
      chain.faces.push_back(std::move(face));
    }
    if (reader.ok() && chain.faces.empty()) {
      reader.fail(node, messages::FallbackChainWithoutFaces);
    }
    chains.push_back(std::move(chain));
  }
  return reader.finish(std::move(chains), messages::FontFallbacksContext);
}

std::vector<FallbackFace> fallbackOrder(std::span<const FallbackFace> faces,
                                        std::string_view language) {
  std::vector<FallbackFace> ordered{faces.begin(), faces.end()};
  auto run = ordered.begin();
  while (run != ordered.end()) {
    if (run->language.empty()) {
      ++run;
      continue;
    }
    std::vector<std::string_view> seen;
    const auto end = std::find_if(run, ordered.end(), [&seen](const FallbackFace& face) {
      if (face.language.empty() || std::ranges::contains(seen, face.language)) {
        return true;
      }
      seen.push_back(face.language);
      return false;
    });
    std::stable_partition(
        run, end, [language](const FallbackFace& face) { return face.language == language; });
    run = end;
  }
  return ordered;
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
