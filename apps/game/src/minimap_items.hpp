#pragma once

#include <dwe/assets/item_table.hpp>
#include <dwe/game/field/field_items.hpp>
#include <dwe/ui/ui.hpp>

#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace dwe::app {

// Whether the minimap marks the items lying on the ground (an engine improvement; the original has
// no minimap, so it changes no rule): a UI setting and the command with the same name that the game
// options' row sends, saved with the user's settings (minimap_items: true, false), on when not
// chosen yet. Off leaves the minimap without them.
inline constexpr std::string_view MinimapItemsSetting = "minimap_items";

// Whether the field draws an item lying on the ground (FieldItemsScreen::show draws the ones with
// an icon of the original's).
using GroundItemDrawn = std::function<bool(assets::ItemHandle)>;

// The ground items the minimap shows: none while `shown` (minimap_items) is off; otherwise every
// item lying on the map's ground that the field draws, where it lies, in the ground's slot order.
// The chests are not among them: they are part of the scenery, as the original's map draws them.
[[nodiscard]] std::vector<ui::MinimapMarker>
minimapItems(bool shown, std::span<const std::optional<game::field::GroundItem>> ground,
             const GroundItemDrawn& drawn);

} // namespace dwe::app
