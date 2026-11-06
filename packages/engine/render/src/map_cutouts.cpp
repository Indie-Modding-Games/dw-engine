#include <dwe/render/map_cutouts.hpp>

#include <cmath>
#include <utility>

namespace dwe::render {

MapCutouts::MapCutouts(const assets::MapImages& images)
    : background_{.width = images.background.width,
                  .height = images.background.height,
                  .indices = images.background.indices,
                  .palette = {}},
      placements_(images.sprites.placements) {
  textures_.reserve(images.sprites.textures.size());
  for (const assets::MapSpriteSheet::Texture& texture : images.sprites.textures) {
    textures_.push_back(texture.timeOfDayPalette
                            ? std::optional{assets::IndexedImage{.width = texture.indices.width,
                                                                 .height = texture.indices.height,
                                                                 .indices = texture.indices.indices,
                                                                 .palette = {}}}
                            : std::nullopt);
  }
  for (const std::vector<assets::PaletteColor>& palette : images.backgroundPalettes) {
    for (std::size_t index = 0; index < palette.size() && index < drawn_.size(); ++index) {
      drawn_[index] = drawn_[index] || palette[index].alpha != 0;
    }
  }
}

bool MapCutouts::isCutout(std::size_t sprite, glm::vec2 position) {
  const glm::vec2 whole = glm::round(position);
  if (whole != position || sprite >= placements_.size() || whole.x < 0.0F || whole.y < 0.0F ||
      whole.x >= static_cast<float>(background_.width) ||
      whole.y >= static_cast<float>(background_.height)) {
    return false;
  }
  const glm::ivec2 pixel{whole};
  const std::uint64_t key = (static_cast<std::uint64_t>(sprite) << 32U) |
                            (static_cast<std::uint64_t>(pixel.x) << 16U) |
                            static_cast<std::uint64_t>(pixel.y);
  if (const auto found = known_.find(key); found != known_.end()) {
    return found->second;
  }
  const bool cutout = check(sprite, pixel);
  known_.emplace(key, cutout);
  return cutout;
}

bool MapCutouts::check(std::size_t sprite, glm::ivec2 position) const {
  const assets::MapSpriteSheet::Placement& placement = placements_[sprite];
  if (placement.texture >= textures_.size() || !textures_[placement.texture]) {
    return false;
  }
  const assets::IndexedImage& texture = *textures_[placement.texture];
  const glm::ivec2 end = placement.texel + placement.size;
  if (placement.size.x <= 0 || placement.size.y <= 0 || placement.texel.x < 0 ||
      placement.texel.y < 0 || std::cmp_greater(end.x, texture.width) ||
      std::cmp_greater(end.y, texture.height) ||
      std::cmp_greater(position.x + placement.size.x, background_.width) ||
      std::cmp_greater(position.y + placement.size.y, background_.height)) {
    return false;
  }
  bool drawsAny = false;
  for (int row = 0; row < placement.size.y; ++row) {
    for (int column = 0; column < placement.size.x; ++column) {
      const std::uint8_t index =
          texture.indices[(static_cast<std::size_t>(placement.texel.y + row) * texture.width) +
                          static_cast<std::size_t>(placement.texel.x + column)];
      if (!drawn_[index]) {
        continue;
      }
      drawsAny = true;
      const std::uint8_t under =
          background_.indices[(static_cast<std::size_t>(position.y + row) * background_.width) +
                              static_cast<std::size_t>(position.x + column)];
      if (under != index) {
        return false;
      }
    }
  }
  return drawsAny;
}

} // namespace dwe::render
