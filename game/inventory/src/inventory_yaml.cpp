#include <dwe/assets/yaml_reader.hpp>
#include <dwe/game/inventory/inventory_yaml.hpp>

#include <string>
#include <utility>

#include "messages.hpp"

namespace dwe::game::inventory {

namespace {

using assets::YamlReader;
using core::YamlNode;

constexpr std::string_view SlotCountKey = "slot_count";
constexpr std::string_view MoneyKey = "money";
constexpr std::string_view SlotsKey = "slots";
constexpr std::string_view ItemKey = "item";
constexpr std::string_view CountKey = "count";
constexpr std::string_view NullText = "null";

YamlNode number(std::int32_t value) {
    return YamlNode::scalar(std::to_string(value));
}

std::optional<Stack> readSlot(YamlReader& reader, const YamlNode& node) {
    if (YamlReader::isNull(node)) {
        return std::nullopt;
    }
    if (!reader.expectKeys(node, "a slot", {ItemKey, CountKey})) {
        return std::nullopt;
    }
    return Stack{.item = reader.integer<std::int32_t>(reader.field(node, ItemKey)),
                 .count = reader.integer<std::int32_t>(reader.field(node, CountKey))};
}

} // namespace

YamlNode toYaml(const Inventory& inventory) {
    const InventoryState& state = inventory.state();
    YamlNode slots = YamlNode::sequence();
    for (const std::optional<Stack>& slot : state.slots) {
        if (!slot) {
            slots.append(YamlNode::scalar(std::string{NullText}));
            continue;
        }
        YamlNode stack = YamlNode::mapping();
        stack.set(std::string{ItemKey}, number(slot->item));
        stack.set(std::string{CountKey}, number(slot->count));
        slots.append(std::move(stack));
    }
    YamlNode root = YamlNode::mapping();
    root.set(std::string{SlotCountKey}, number(state.slotCount));
    root.set(std::string{MoneyKey}, number(state.money));
    root.set(std::string{SlotsKey}, std::move(slots));
    return root;
}

core::Result<void> fromYaml(Inventory& inventory, const YamlNode& node) {
    YamlReader reader;
    if (!reader.expectKeys(node, "the inventory", {SlotCountKey, MoneyKey, SlotsKey})) {
        return std::unexpected{reader.error(messages::SaveContext)};
    }
    InventoryState state{.slots = {},
                         .slotCount = reader.integer<std::int32_t>(reader.field(node, SlotCountKey)),
                         .money = reader.integer<std::int32_t>(reader.field(node, MoneyKey))};
    for (const YamlNode& slot : reader.sequence(reader.field(node, SlotsKey), SlotsKey)) {
        state.slots.push_back(readSlot(reader, slot));
    }
    auto read = reader.finish(std::move(state), messages::SaveContext);
    if (!read) {
        return std::unexpected{read.error()};
    }
    return inventory.restore(std::move(*read));
}

} // namespace dwe::game::inventory
