#include <dwe/assets/yaml_reader.hpp>
#include <dwe/game/inventory/inventory_yaml.hpp>

#include <optional>
#include <string>
#include <utility>
#include <vector>

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

std::optional<Stack> readSlot(YamlReader& reader, const assets::ItemTable& items,
                              const YamlNode& node, std::vector<DroppedStack>& dropped) {
  if (YamlReader::isNull(node)) {
    return std::nullopt;
  }
  if (!reader.expectKeys(node, messages::SlotText, {ItemKey, CountKey})) {
    return std::nullopt;
  }
  const YamlNode& itemNode = reader.field(node, ItemKey);
  const std::string id = reader.text(itemNode);
  const auto count = reader.integer<std::int32_t>(reader.field(node, CountKey));
  if (!reader.ok()) {
    return std::nullopt;
  }
  const std::optional<assets::ItemHandle> item = items.handleOf(id);
  if (!item) {
    dropped.push_back({.item = id, .count = count});
    return std::nullopt;
  }
  return Stack{.item = *item, .count = count};
}

} // namespace

YamlNode toYaml(const Inventory& inventory, const assets::ItemTable& items) {
  const InventoryState& state = inventory.state();
  YamlNode slots = YamlNode::sequence();
  for (const std::optional<Stack>& slot : state.slots) {
    if (!slot) {
      slots.append(YamlNode::scalar(std::string{NullText}));
      continue;
    }
    YamlNode stack = YamlNode::mapping();
    stack.set(std::string{ItemKey},
              YamlNode::scalar(items.at(slot->item).id, YamlNode::ScalarStyle::Quoted));
    stack.set(std::string{CountKey}, number(slot->count));
    slots.append(std::move(stack));
  }
  YamlNode root = YamlNode::mapping();
  root.set(std::string{SlotCountKey}, number(state.slotCount));
  root.set(std::string{MoneyKey}, number(state.money));
  root.set(std::string{SlotsKey}, std::move(slots));
  return root;
}

core::Result<std::vector<DroppedStack>>
fromYaml(Inventory& inventory, const assets::ItemTable& items, const YamlNode& node) {
  YamlReader reader;
  if (!reader.expectKeys(node, "the inventory", {SlotCountKey, MoneyKey, SlotsKey})) {
    return std::unexpected{reader.error(messages::SaveContext)};
  }
  InventoryState state{.slots = {},
                       .slotCount = reader.integer<std::int32_t>(reader.field(node, SlotCountKey)),
                       .money = reader.integer<std::int32_t>(reader.field(node, MoneyKey))};
  std::vector<DroppedStack> dropped;
  for (const YamlNode& slot : reader.sequence(reader.field(node, SlotsKey), SlotsKey)) {
    state.slots.push_back(readSlot(reader, items, slot, dropped));
  }
  auto read = reader.finish(std::move(state), messages::SaveContext);
  if (!read) {
    return std::unexpected{read.error()};
  }
  if (auto restored = inventory.restore(std::move(*read)); !restored) {
    return std::unexpected{restored.error()};
  }
  return dropped;
}

core::Result<YamlNode> itemNumbersToIds(YamlNode state, const assets::ItemTable& items) {
  const YamlNode* slots = state.isMapping() ? state.find(SlotsKey) : nullptr;
  if (slots == nullptr || !slots->isSequence()) {
    return core::fail(std::string{messages::OldInventoryShape});
  }
  YamlNode migrated = YamlNode::sequence();
  for (const YamlNode& slot : slots->items()) {
    const YamlNode* item = slot.isMapping() ? slot.find(ItemKey) : nullptr;
    if (item == nullptr) {
      migrated.append(slot);
      continue;
    }
    YamlReader reader;
    const auto number = reader.integer<std::int32_t>(*item);
    if (!reader.ok()) {
      return core::fail(messages::formatMessage(messages::OldItemNumberInvalid,
                                                item->isScalar() ? item->text() : ""));
    }
    const assets::ItemEntry* entry = items.findByOriginalIndex(number);
    if (entry == nullptr) {
      migrated.append(slot); // no item of the original: fromYaml drops it
      continue;
    }
    YamlNode stack = slot;
    stack.set(std::string{ItemKey}, YamlNode::scalar(entry->id));
    migrated.append(std::move(stack));
  }
  state.set(std::string{SlotsKey}, std::move(migrated));
  return state;
}

} // namespace dwe::game::inventory
