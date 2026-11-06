#include "minimap_items.hpp"

namespace dwe::app {

std::vector<ui::MinimapMarker>
minimapItems(bool shown, std::span<const std::optional<game::field::GroundItem>> ground,
             const GroundItemDrawn& drawn) {
  std::vector<ui::MinimapMarker> items;
  if (!shown) {
    return items;
  }
  for (const std::optional<game::field::GroundItem>& item : ground) {
    if (item && drawn(item->item)) {
      items.push_back(ui::MinimapMarker{
          .x = item->position.x, .z = item->position.z, .facingX = 0.0F, .facingZ = -1.0F});
    }
  }
  return items;
}

} // namespace dwe::app
