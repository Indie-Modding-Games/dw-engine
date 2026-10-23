#include <dwe/assets/item_table.hpp>
#include <dwe/core/log.hpp>
#include <dwe/game/inventory/inventory.hpp>
#include <dwe/session/inventory_adapter.hpp>

#include <array>

#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

namespace {

constexpr game::story::RoutineNumber RemoveOne = 12;
constexpr game::story::RoutineNumber RemoveAll = 13;

// Takes `count` of the item numbered by the operand from the bag, unless the operand names none
// (script_interp.c:1736 and 1742: removeItem(pstat 254, 1 or 0x63)).
void removeItem(StoryScenesContext& scenes, std::int32_t count) {
  const std::int64_t number = scenes.operand();
  if (number == scenes.rules.noItem) {
    core::logDebug("[port:inventory] call_routine remove: no item named");
    return;
  }
  InventoryAdapter* inventory = scenes.desc.inventory;
  if (inventory == nullptr) {
    core::logWarning("[port:inventory] call_routine remove: no bag");
    return;
  }
  const assets::ItemEntry* item =
      inventory->items().findByOriginalIndex(static_cast<std::int32_t>(number));
  if (item != nullptr) {
    inventory->inventory().remove(inventory->items().handleOf(*item), count);
    core::logDebug("[port:inventory] removeItem (item.c): {} x{}", item->id, count);
    return;
  }
  core::logWarning("[port:inventory] call_routine remove: no item number {}", number);
}

} // namespace

core::Result<void> registerRemoveItemRoutines(StoryScenesContext& scenes) {
  for (const game::story::RoutineNumber number : std::array{RemoveOne, RemoveAll}) {
    auto added = scenes.routines.registerRoutine(
        number,
        [&scenes](game::story::RoutineNumber routine) -> core::Result<game::story::RoutineStart> {
          removeItem(scenes, routine == RemoveOne ? scenes.rules.removeOneCount
                                                  : scenes.rules.removeAllCount);
          return game::story::RoutineGoesOn{};
        });
    if (!added) {
      return added;
    }
  }
  return {};
}

} // namespace dwe::session
