#pragma once

#include <dwe/assets/map_document.hpp>
#include <dwe/assets/map_table.hpp>
#include <dwe/core/error.hpp>
#include <dwe/core/math.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::assets {
class VirtualFileSystem;
} // namespace dwe::assets

// Extra toilets (an engine improvement, off by default; docs/modulos/apps/game.md, «Baños extra»):
// toilet booths placed on any map from `field/extra_toilets` of the game pack
// (content/schemas/field_extra_toilets.schema.json), so a mod can add its own. One entry gives
// all of it: the booth's model drawn there, its cells solid, the cells in front of it the toilet
// trigger, and the original's toilet routine (tamer.c: checkMapInteraction, partner.c:
// tickPartnerToilet): the partner that needs it runs in front of the booth and walks in.
namespace dwe::game::field {

// The booth's half width and half depth in world units: it stands on 200 x 200, the 2 x 2 cells
// around a cell corner when it faces a quarter turn.
inline constexpr float ExtraToiletHalfSize = assets::CollisionCellSize;
// How far in front of the booth's middle the partner stops before walking in: just outside it.
inline constexpr float ExtraToiletFront = 2.0F * assets::CollisionCellSize;
// How deep the strip of trigger cells in front of the booth is: two cells.
inline constexpr float ExtraToiletStrip = 2.0F * assets::CollisionCellSize;

// An extra toilet of a map.
struct ExtraToilet {
  std::string map;
  // The booth's middle, where the partner goes, on the original's axes (x, z), like the toilet
  // points (TOILET_DATA).
  glm::ivec2 position{0};
  // The original Y rotation (0-4095) the booth's open front faces.
  std::int32_t facing = 0;
  // The booth's model (an asset id of the packs): the entry's own, else the document's.
  std::string model;

  // Where the booth stands in world space, and its yaw: the model's +Z is its front.
  [[nodiscard]] glm::vec3 worldPosition() const;
  [[nodiscard]] float yaw() const;
  // The toilet's two points (MapToilet's index stays 0): ExtraToiletFront in front, then the
  // middle.
  [[nodiscard]] assets::MapToilet points() const;
  // The cells under the booth (their middles inside it), which it makes solid.
  [[nodiscard]] std::vector<assets::CollisionCell> footprint() const;
  // The cells of the strip in front of it, as wide as the booth and ExtraToiletStrip deep, which
  // take the toilet trigger.
  [[nodiscard]] std::vector<assets::CollisionCell> frontCells() const;
};

// Parses the text of `field/extra_toilets`. Unknown and missing keys are errors.
[[nodiscard]] core::Result<std::vector<ExtraToilet>> parseExtraToilets(std::string_view yaml);

// Reads `field/extra_toilets` from the virtual file system.
[[nodiscard]] core::Result<std::vector<ExtraToilet>>
loadExtraToilets(const assets::VirtualFileSystem& vfs);

} // namespace dwe::game::field
