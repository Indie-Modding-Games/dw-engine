#include <dwe/game/inventory/use.hpp>

#include <algorithm>

namespace dwe::game::inventory {

namespace {

constexpr std::int32_t OneItem = 1;

bool usableIn(const assets::ItemEntry& item, UseContext context, const UseRules& rules) {
  const std::vector<std::int32_t>& colors =
      context == UseContext::Field ? rules.fieldColors : rules.battleColors;
  return std::ranges::contains(colors, item.itemColor);
}

} // namespace

core::Result<UseResult> offerItem(const Inventory& inventory, assets::ItemHandle item,
                                  UseContext context, const assets::ItemTable& items,
                                  const ItemEffects& effects, ItemEffectTarget& target) {
  const assets::ItemEntry* entry = &items.at(item);
  const auto result = [&](UseOutcome outcome) {
    return UseResult{.outcome = outcome, .keyItem = isKeyItem(*entry, inventory.rules().use)};
  };
  if (inventory.countOf(item) == 0) {
    return result(UseOutcome::NotInBag);
  }
  if (!usableIn(*entry, context, inventory.rules().use)) {
    return result(UseOutcome::NotUsableHere);
  }
  auto effect = effects.resolve(entry->id);
  if (!effect) {
    return std::unexpected{effect.error()};
  }
  if (!effect->doesSomething() && !inventory.rules().quirks.uselessItemsConsumed) {
    return result(UseOutcome::Useless);
  }
  if (context == UseContext::Field && !target.accepts(*effect, context)) {
    return result(UseOutcome::Refused);
  }
  return result(UseOutcome::Accepted);
}

core::Result<UseResult> giveItem(Inventory& inventory, assets::ItemHandle item, UseContext context,
                                 const assets::ItemTable& items, const ItemEffects& effects,
                                 ItemEffectTarget& target) {
  const assets::ItemEntry* entry = &items.at(item);
  const UseResult used{.outcome = UseOutcome::Used,
                       .keyItem = isKeyItem(*entry, inventory.rules().use)};
  if (inventory.countOf(item) == 0) {
    return UseResult{.outcome = UseOutcome::NotInBag, .keyItem = used.keyItem};
  }
  auto effect = effects.resolve(entry->id);
  if (!effect) {
    return std::unexpected{effect.error()};
  }
  inventory.remove(item, OneItem);
  target.apply(*effect, context);
  return used;
}

core::Result<UseResult> useItem(Inventory& inventory, assets::ItemHandle item, UseContext context,
                                const assets::ItemTable& items, const ItemEffects& effects,
                                ItemEffectTarget& target) {
  auto offered = offerItem(inventory, item, context, items, effects, target);
  if (!offered || offered->outcome != UseOutcome::Accepted) {
    return offered;
  }
  return giveItem(inventory, item, context, items, effects, target);
}

} // namespace dwe::game::inventory
