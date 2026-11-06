#include <dwe/assets/image_replacement.hpp>

#include <string>

namespace dwe::assets {

std::optional<std::uint32_t> replacementScale(std::uint32_t originalWidth,
                                              std::uint32_t originalHeight, std::uint32_t width,
                                              std::uint32_t height) {
  if (originalWidth == 0 || originalHeight == 0 || width % originalWidth != 0) {
    return std::nullopt;
  }
  const std::uint32_t scale = width / originalWidth;
  if (scale < 1 || scale > MaxReplacementScale || std::uint64_t{originalHeight} * scale != height) {
    return std::nullopt;
  }
  return scale;
}

core::Result<AssetId> paletteImageId(const AssetId& image, std::size_t palette) {
  return AssetId::parse(image.str() + std::string{PaletteImageSuffix} + std::to_string(palette));
}

} // namespace dwe::assets
