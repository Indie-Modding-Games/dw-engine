#pragma once

#include <dwe/assets/map_document.hpp>
#include <dwe/assets/map_table.hpp>
#include <dwe/core/error.hpp>
#include <dwe/core/math.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::assets {
class VirtualFileSystem;
} // namespace dwe::assets

// Extra toilets (an engine improvement, off by default; docs/modulos/apps/game.md, «Baños extra»):
// more places where the partner can go, listed in `field/extra_toilets` of the game pack
// (content/schemas/field_extra_toilets.schema.json) so a mod can add its own. Each one works like
// the original's toilets (tamer.c: checkMapInteraction, partner.c: tickPartnerToilet): its free
// cells take the toilet trigger, and the partner that needs it runs to the point in front and then
// to the toilet.
namespace dwe::game::field {

// How far in front of an extra toilet the partner stops before walking in, in world units: one
// collision cell.
inline constexpr float ExtraToiletFront = assets::CollisionCellSize;

// An extra toilet of a map.
struct ExtraToilet {
  std::string map;
  // Where the partner goes, on the original's axes (x, z), like the toilet points (TOILET_DATA).
  glm::ivec2 position{0};
  // The original Y rotation (0-4095) the toilet's front faces: the partner comes from that side.
  std::int32_t facing = 0;

  // The toilet's two points (MapToilet's index stays 0): ExtraToiletFront in front, then the
  // position.
  [[nodiscard]] assets::MapToilet points() const;
  // The cells whose trigger it sets: the position's cell and its eight neighbours, so the player
  // finds it by standing next to it.
  [[nodiscard]] std::array<assets::CollisionCell, 9> cells() const;
};

// Parses the text of `field/extra_toilets`. Unknown and missing keys are errors.
[[nodiscard]] core::Result<std::vector<ExtraToilet>> parseExtraToilets(std::string_view yaml);

// Reads `field/extra_toilets` from the virtual file system.
[[nodiscard]] core::Result<std::vector<ExtraToilet>>
loadExtraToilets(const assets::VirtualFileSystem& vfs);

} // namespace dwe::game::field
