#include <dwe/assets/item_table.hpp>
#include <dwe/core/log.hpp>
#include <dwe/game/inventory/inventory.hpp>
#include <dwe/session/inventory_adapter.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

#include "story_game_context.hpp"
#include "story_game_messages.hpp"

// The story's orders and reads of the bag and the money (script_interp.c: scriptInstruction28to3F,
// scriptInstruction10to27; script_value.c: scriptCompareItem, scriptCompareMoney).
namespace dwe::session {

namespace {

namespace msg = story_game_messages;

using game::inventory::Inventory;
using script::ApiValue;

// remove_item leaves the bag alone for this item number (item.c: removeItem).
constexpr std::int64_t NoItem = 255;

core::Result<Inventory*> bagOf(const StoryGameContext& context) {
  if (context.desc.inventory == nullptr) {
    return core::fail(std::string{msg::NoBag});
  }
  return &context.desc.inventory->inventory();
}

core::Result<assets::ItemHandle> itemOf(const StoryGameContext& context, std::int64_t item) {
  const assets::ItemTable& items = context.desc.inventory->items();
  const assets::ItemEntry* entry = std::in_range<std::int32_t>(item)
                                       ? items.findByOriginalIndex(static_cast<std::int32_t>(item))
                                       : nullptr;
  if (entry == nullptr) {
    return core::fail(msg::formatMessage(msg::NoSuchItem, item));
  }
  return items.handleOf(*entry);
}

std::int32_t amountOf(std::int64_t amount) {
  return static_cast<std::int32_t>(std::clamp<std::int64_t>(
      amount, std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max()));
}

core::Result<ApiValue> giveItem(const StoryGameContext& context, std::int64_t item,
                                std::int64_t amount) {
  auto bag = bagOf(context);
  if (!bag) {
    return std::unexpected{bag.error()};
  }
  const auto handle = itemOf(context, item);
  if (!handle) {
    return std::unexpected{handle.error()};
  }
  const game::inventory::AddResult added = (*bag)->add(*handle, amountOf(amount));
  // script_interp.c:1108: trigger 0 says the bag did not take them.
  core::logDebug("[port:story] give_item (script_interp.c:1106): item {} x{}: added {}, lost {}, "
                 "refused {}; trigger 0 {}",
                 item, amount, added.added, added.lost, added.refused,
                 added.taken() ? "cleared" : "set");
  return done(context.setTrigger(0, !added.taken()));
}

core::Result<ApiValue> removeItem(const StoryGameContext& context, std::int64_t item,
                                  std::int64_t amount) {
  auto bag = bagOf(context);
  if (!bag) {
    return std::unexpected{bag.error()};
  }
  if (item == NoItem) {
    core::logDebug("[port:story] remove_item (script_interp.c:1115): item 255, nothing");
    return ApiValue{};
  }
  const auto handle = itemOf(context, item);
  if (!handle) {
    return std::unexpected{handle.error()};
  }
  core::logDebug("[port:story] remove_item (script_interp.c:1115, item.c:2616): item {} x{} from "
                 "every stack, held {}",
                 item, amount, (*bag)->countOf(*handle));
  (*bag)->remove(*handle, amountOf(amount));
  return ApiValue{};
}

core::Result<ApiValue> setInventorySize(const StoryGameContext& context, std::int64_t size) {
  auto bag = bagOf(context);
  if (!bag) {
    return std::unexpected{bag.error()};
  }
  core::logDebug("[port:story] set_inventory_size (script_interp.c:816, item.c:2388): {} slots",
                 size);
  if (auto set = (*bag)->setSlotCount(amountOf(size)); !set) {
    core::logWarning("{}", msg::formatMessage(msg::SlotCountRefused, size, set.error().message));
  }
  return ApiValue{};
}

core::Result<ApiValue> changeMoney(const StoryGameContext& context, std::int64_t amount,
                                   bool adds) {
  auto bag = bagOf(context);
  if (!bag) {
    return std::unexpected{bag.error()};
  }
  const std::int32_t before = (*bag)->money();
  // script_interp.c:1118-1131: the money stops at its limit and at 0.
  if (adds) {
    (*bag)->addMoney(amountOf(amount));
  } else {
    (*bag)->spendMoney(amountOf(amount));
  }
  core::logDebug("[port:story] {} (script_interp.c:{}): {} -> {}",
                 adds ? "add_money" : "reduce_money", adds ? 1118 : 1125, before, (*bag)->money());
  return ApiValue{};
}

core::Result<ApiValue> itemCount(const StoryGameContext& context, std::int64_t item) {
  auto bag = bagOf(context);
  if (!bag) {
    return std::unexpected{bag.error()};
  }
  const auto handle = itemOf(context, item);
  if (!handle) {
    return std::unexpected{handle.error()};
  }
  // script_value.c: scriptCompareItem reads the first stack (item.c: getItemCount).
  return ApiValue{std::int64_t{(*bag)->countOf(*handle)}};
}

core::Result<ApiValue> money(const StoryGameContext& context) {
  auto bag = bagOf(context);
  if (!bag) {
    return std::unexpected{bag.error()};
  }
  return ApiValue{std::int64_t{(*bag)->money()}};
}

} // namespace

core::Result<void> publishBagOrders(script::ApiRegistry& api, StoryGameContext& context) {
  namespace names = story_game_api;
  const StoryGameContext* game = &context;
  using Operands1 = std::array<std::int64_t, 1>;
  using Operands2 = std::array<std::int64_t, 2>;
  return publishOrders(
      api, {{names::GiveItem, integerOrder<2>([game](const Operands2& operands) {
               return giveItem(*game, operands[0], operands[1]);
             })},
            {names::RemoveItem, integerOrder<2>([game](const Operands2& operands) {
               return removeItem(*game, operands[0], operands[1]);
             })},
            {names::SetInventorySize, integerOrder<1>([game](const Operands1& operands) {
               return setInventorySize(*game, operands[0]);
             })},
            {names::AddMoney, integerOrder<1>([game](const Operands1& operands) {
               return changeMoney(*game, operands[0], true);
             })},
            {names::ReduceMoney, integerOrder<1>([game](const Operands1& operands) {
               return changeMoney(*game, operands[0], false);
             })},
            {names::Items, integerOrder<1>([game](const Operands1& operands) {
               return itemCount(*game, operands[0]);
             })},
            {names::Money, integerOrder<0>([game](const std::array<std::int64_t, 0>& /*none*/) {
               return money(*game);
             })}});
}

} // namespace dwe::session
