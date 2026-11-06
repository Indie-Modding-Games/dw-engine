#pragma once

#include <dwe/assets/indexed_image.hpp>
#include <dwe/assets/map_images.hpp>
#include <dwe/core/math.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace dwe::render {

// Which depth-layer sprites are cut-outs of the background where they are drawn, for HD scenery
// (map.slang, SamplingCutout; docs/architecture/remaster-pipeline.md, «Alta definición en el
// motor»). Most depth-layer sprites are pieces of the background copied out so that they can be
// drawn in front of the characters: with HD scenery such a sprite takes its colours from the
// smoothed background under it, so it matches the background pixel for pixel, and only says which
// pixels it draws. A sprite is a cut-out where it follows the background's palettes (clut -1) and
// every texel it draws in some palette variant holds the background's index under it.
class MapCutouts {
public:
  MapCutouts() = default;
  // Keeps what the check needs from `images`: the background's indices, those of the sprite
  // textures that follow the background's palettes and which indices any variant draws.
  explicit MapCutouts(const assets::MapImages& images);

  // Whether sprite `sprite` (an index of the sheet's placements) drawn with its top-left corner on
  // background pixel `position` is a cut-out. A position between pixels never is. Remembered per
  // sprite and position.
  [[nodiscard]] bool isCutout(std::size_t sprite, glm::vec2 position);

private:
  [[nodiscard]] bool check(std::size_t sprite, glm::ivec2 position) const;

  assets::IndexedImage background_;
  // Per sheet texture: its indices when it follows the background's palettes.
  std::vector<std::optional<assets::IndexedImage>> textures_;
  std::vector<assets::MapSpriteSheet::Placement> placements_;
  // Whether some palette variant draws the index (its colour is not 0x0000).
  std::array<bool, 256> drawn_{};
  std::unordered_map<std::uint64_t, bool> known_;
};

} // namespace dwe::render
