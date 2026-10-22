#include <dwe/assets/image_palettes.hpp>
#include <dwe/assets/map_images.hpp>
#include <dwe/core/yaml.hpp>

#include <algorithm>
#include <charconv>
#include <string>
#include <utility>

#include "messages.hpp"

namespace dwe::assets {
namespace {

constexpr std::string_view PalettesKey = "palettes";
constexpr int Hexadecimal = 16;
constexpr std::uint8_t Opaque = 255;

} // namespace

core::Result<std::vector<std::uint16_t>> parseOriginalPalette(std::string_view colors) {
  std::vector<std::uint16_t> palette;
  std::size_t start = 0;
  while (start < colors.size()) {
    const std::size_t end = std::min(colors.find(' ', start), colors.size());
    std::uint16_t value = 0;
    const auto [last, error] =
        std::from_chars(colors.data() + start, colors.data() + end, value, Hexadecimal);
    if (error != std::errc{} || last != colors.data() + end) {
      return core::fail(std::string{messages::PaletteColorNotHex});
    }
    palette.push_back(value);
    start = end + 1;
  }
  return palette;
}

core::Result<std::vector<std::vector<std::uint16_t>>>
parseImagePalettes(std::string_view metaYaml) {
  auto root = core::parseYaml(metaYaml);
  if (!root) {
    return std::unexpected{root.error()};
  }
  const core::YamlNode* palettes = root->find(PalettesKey);
  if (palettes == nullptr || !palettes->isSequence()) {
    return core::fail(std::string{messages::NoImagePalettes});
  }
  std::vector<std::vector<std::uint16_t>> parsed;
  for (const core::YamlNode& palette : palettes->items()) {
    if (!palette.isScalar()) {
      return core::fail(std::string{messages::PaletteNotText});
    }
    auto colors = parseOriginalPalette(palette.text());
    if (!colors) {
      return std::unexpected{colors.error()};
    }
    parsed.push_back(std::move(*colors));
  }
  return parsed;
}

PaletteColor imageColorFromOriginal(std::uint16_t value) {
  PaletteColor color = colorFromOriginal(value);
  if (value != 0) {
    color.alpha = Opaque;
  }
  return color;
}

} // namespace dwe::assets
