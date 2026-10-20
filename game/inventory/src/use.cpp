#include <dwe/game/inventory/use.hpp>

#include <algorithm>

#include "messages.hpp"

namespace dwe::game::inventory {

namespace {

constexpr std::int32_t OneItem = 1;

bool usableIn(const assets::ItemEntry& item, UseContext context, const UseRules& rules) {
    const std::vector<std::int32_t>& colors = context == UseContext::Field ? rules.fieldColors : rules.battleColors;
    return std::ranges::contains(colors, item.itemColor);
}

} // namespace

core::Result<UseResult> useItem(Inventory& inventory, std::int32_t item, UseContext context,
                                const assets::ItemTable& items, const ItemEffects& effects, ItemEffectTarget& target) {
    const assets::ItemEntry* entry = items.findByIndex(item);
    if (entry == nullptr) {
        return core::fail(messages::formatMessage(messages::UnknownItem, item));
    }
    const auto result = [&](UseOutcome outcome) {
        return UseResult{.outcome = outcome, .keyItem = isKeyItem(*entry, inventory.rules().use)};
    };
    if (inventory.countOf(item) == 0) {
        return result(UseOutcome::NotInBag);
    }
    if (!usableIn(*entry, context, inventory.rules().use)) {
        return result(UseOutcome::NotUsableHere);
    }
    auto effect = effects.resolve(item);
    if (!effect) {
        return std::unexpected{effect.error()};
    }
    if (!effect->doesSomething() && !inventory.rules().quirks.uselessItemsConsumed) {
        return result(UseOutcome::Useless);
    }
    if (context == UseContext::Field && !target.accepts(*effect, context)) {
        return result(UseOutcome::Refused);
    }
    inventory.remove(item, OneItem);
    target.apply(*effect, context);
    return result(UseOutcome::Used);
}

} // namespace dwe::game::inventory
