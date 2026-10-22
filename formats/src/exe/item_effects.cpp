#include <dwe/formats/exe/constant_switch.hpp>
#include <dwe/formats/exe/item_effects.hpp>

#include <bytes/byte_cursor.hpp>

#include <array>
#include <utility>

#include "messages.hpp"

namespace dwe::formats::exe {

namespace {

using bytes::ByteCursor;

constexpr std::string_view ItemFunctionsName = "ITEM_FUNCTIONS";
constexpr std::string_view HealingValuesName = "HEALING_VALUES";
constexpr std::string_view EvolutionItemTargetsName = "EVOLUTION_ITEM_TARGET";

constexpr std::size_t PointerSize = 4;
constexpr std::size_t HealingValueSize = 2;
constexpr std::size_t EvolutionTargetSize = 1;

template <typename T>
core::Result<std::vector<T>> readTable(const MemoryImage& image, const TableLocation& table,
                                       std::string_view name, std::size_t stride) {
  if (!table.located()) {
    return core::fail(messages::formatMessage(messages::TableNotLocated, name));
  }
  auto bytes = image.bytesAt(table.address, std::size_t{table.count} * stride);
  if (!bytes) {
    return core::fail(messages::formatMessage(messages::InTable, name, bytes.error().message));
  }
  std::vector<T> values;
  values.reserve(table.count);
  ByteCursor cursor{*bytes};
  for (std::size_t i = 0; i < table.count; ++i) {
    if constexpr (sizeof(T) == PointerSize) {
      values.push_back(static_cast<T>(cursor.u32()));
    } else if constexpr (sizeof(T) == HealingValueSize) {
      values.push_back(static_cast<T>(cursor.u16()));
    } else {
      values.push_back(static_cast<T>(cursor.u8()));
    }
  }
  return values;
}

// The handler a pointer of ITEM_FUNCTIONS names.
core::Result<ItemHandler> handlerAt(std::uint32_t pointer, const ItemHandlerAddresses& handlers,
                                    std::size_t item) {
  const std::array<std::pair<std::uint32_t, ItemHandler>, 9> known{{
      {handlers.hpHealing, ItemHandler::HpHealing},
      {handlers.mpHealing, ItemHandler::MpHealing},
      {handlers.doubleFloppy, ItemHandler::DoubleFloppy},
      {handlers.status, ItemHandler::Status},
      {handlers.revive, ItemHandler::Revive},
      {handlers.battleDisk, ItemHandler::BattleDisk},
      {handlers.chip, ItemHandler::Chip},
      {handlers.food, ItemHandler::Food},
      {handlers.evolution, ItemHandler::Evolution},
  }};
  if (pointer == 0) {
    return ItemHandler::None;
  }
  for (const auto& [address, handler] : known) {
    if (address == pointer) {
      return handler;
    }
  }
  return core::fail(messages::formatMessage(messages::UnknownItemHandler, item, pointer));
}

// An entry of a table indexed from `first`, if the item falls in it.
template <typename T>
std::optional<T> entryFrom(const std::vector<T>& table, std::size_t item, std::size_t first) {
  if (item < first || item - first >= table.size()) {
    return std::nullopt;
  }
  return table[item - first];
}

// The values of the item's case of a constant switch, or why there are none.
void readSwitchValues(const MemoryImage& image, const ConstantSwitch& code, std::size_t item,
                      ItemEffectCode& effect) {
  auto values = evaluateConstantSwitch(image, code, static_cast<std::uint32_t>(item));
  if (values) {
    effect.values = std::move(*values);
  } else {
    effect.skipped = std::move(values.error().message);
  }
}

} // namespace

core::Result<std::vector<ItemEffectCode>> readItemEffects(const MemoryImage& image,
                                                          const ExecutableLayout& layout) {
  auto pointers =
      readTable<std::uint32_t>(image, layout.itemFunctions, ItemFunctionsName, PointerSize);
  if (!pointers) {
    return std::unexpected{pointers.error()};
  }
  auto healing =
      readTable<std::int16_t>(image, layout.healingValues, HealingValuesName, HealingValueSize);
  if (!healing) {
    return std::unexpected{healing.error()};
  }
  auto targets = readTable<std::uint8_t>(image, layout.evolutionItemTargets,
                                         EvolutionItemTargetsName, EvolutionTargetSize);
  if (!targets) {
    return std::unexpected{targets.error()};
  }
  const ItemCodeLayout& code = layout.itemCode;
  std::vector<ItemEffectCode> effects;
  effects.reserve(pointers->size());
  for (std::size_t item = 0; item < pointers->size(); ++item) {
    auto handler = handlerAt((*pointers)[item], code.handlers, item);
    if (!handler) {
      return std::unexpected{handler.error()};
    }
    ItemEffectCode effect;
    effect.handler = *handler;
    switch (effect.handler) {
    case ItemHandler::HpHealing:
      effect.healing = entryFrom(*healing, item, code.firstHpHealingItem);
      break;
    case ItemHandler::MpHealing:
      effect.healing = entryFrom(*healing, item, code.firstMpHealingItem);
      break;
    case ItemHandler::Evolution:
      effect.evolutionTarget = entryFrom(*targets, item, code.firstEvolutionItem);
      break;
    case ItemHandler::Food:
      readSwitchValues(image, code.food, item, effect);
      break;
    case ItemHandler::Chip:
      readSwitchValues(image, code.chips, item, effect);
      break;
    default:
      break;
    }
    effects.push_back(std::move(effect));
  }
  return effects;
}

} // namespace dwe::formats::exe
