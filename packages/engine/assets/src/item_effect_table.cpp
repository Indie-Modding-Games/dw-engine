#include <dwe/assets/item_effect_table.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/yaml.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <utility>

#include "document_text.hpp"

namespace dwe::assets {

namespace {

using core::YamlNode;

constexpr std::string_view ItemEffectsId = "data/item_effects";
constexpr std::string_view Context = "item effect table";

struct HandlerName {
  std::string_view name;
  ItemHandler handler;
};

// The names data/item_effects gives the handlers (packages/formats/exe/item_effects.hpp:
// ItemHandlerNames).
constexpr std::array<HandlerName, 10> HandlerNames{{
    {.name = "none", .handler = ItemHandler::None},
    {.name = "hp_healing", .handler = ItemHandler::HpHealing},
    {.name = "mp_healing", .handler = ItemHandler::MpHealing},
    {.name = "double_floppy", .handler = ItemHandler::DoubleFloppy},
    {.name = "status", .handler = ItemHandler::Status},
    {.name = "revive", .handler = ItemHandler::Revive},
    {.name = "battle_disk", .handler = ItemHandler::BattleDisk},
    {.name = "chip", .handler = ItemHandler::Chip},
    {.name = "food", .handler = ItemHandler::Food},
    {.name = "evolution", .handler = ItemHandler::Evolution},
}};

ItemHandler readHandler(YamlReader& reader, const YamlNode& node) {
  const std::string name = reader.text(node);
  const auto found = std::ranges::find(HandlerNames, name, &HandlerName::name);
  if (found == HandlerNames.end()) {
    if (reader.ok()) {
      reader.fail(node, std::format("unknown item handler '{}'", name));
    }
    return ItemHandler::None;
  }
  return found->handler;
}

FoodValues readFood(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, "food values",
                         {"energy", "tiredness_relief", "happiness", "discipline", "weight",
                          "lifetime", "max_hp", "max_mp", "hp", "mp", "offense", "defense", "speed",
                          "brain", "training_boost_flag", "training_boost_value",
                          "training_boost_hours", "sickness_chance"})) {
    return {};
  }
  const auto integer = [&](std::string_view key) {
    return reader.integer<std::int32_t>(reader.field(node, key));
  };
  return {.energy = integer("energy"),
          .tirednessRelief = integer("tiredness_relief"),
          .happiness = integer("happiness"),
          .discipline = integer("discipline"),
          .weight = integer("weight"),
          .lifetime = integer("lifetime"),
          .maxHp = integer("max_hp"),
          .maxMp = integer("max_mp"),
          .hp = integer("hp"),
          .mp = integer("mp"),
          .offense = integer("offense"),
          .defense = integer("defense"),
          .speed = integer("speed"),
          .brain = integer("brain"),
          .trainingBoostFlag = integer("training_boost_flag"),
          .trainingBoostValue = integer("training_boost_value"),
          .trainingBoostHours = integer("training_boost_hours"),
          .sicknessChance = integer("sickness_chance")};
}

ChipValues readChip(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(
          node, "chip values",
          {"max_hp", "max_mp", "offense", "defense", "speed", "brain", "lifetime"})) {
    return {};
  }
  const auto integer = [&](std::string_view key) {
    return reader.integer<std::int32_t>(reader.field(node, key));
  };
  return {.maxHp = integer("max_hp"),
          .maxMp = integer("max_mp"),
          .offense = integer("offense"),
          .defense = integer("defense"),
          .speed = integer("speed"),
          .brain = integer("brain"),
          .lifetime = integer("lifetime")};
}

ItemEffectEntry readEffect(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, "an item effect",
                         {"item", "index", "handler", "healing", "evolves_to", "values"})) {
    return {};
  }
  ItemEffectEntry entry{.item = reader.text(reader.field(node, "item")),
                        .index = reader.integer<std::int32_t>(reader.field(node, "index")),
                        .handler = readHandler(reader, reader.field(node, "handler")),
                        .healing =
                            reader.optionalInteger<std::int32_t>(reader.field(node, "healing")),
                        .evolvesTo = reader.optionalText(reader.field(node, "evolves_to")),
                        .food = std::nullopt,
                        .chip = std::nullopt};
  const YamlNode& values = reader.field(node, "values");
  if (YamlReader::isNull(values)) {
    return entry;
  }
  if (entry.handler == ItemHandler::Food) {
    entry.food = readFood(reader, values);
  } else if (entry.handler == ItemHandler::Chip) {
    entry.chip = readChip(reader, values);
  } else if (reader.ok()) {
    reader.fail(values, "only food and chips have values");
  }
  return entry;
}

} // namespace

const ItemEffectEntry* ItemEffectTable::find(std::string_view item) const {
  const auto found = std::ranges::find(effects, item, &ItemEffectEntry::item);
  return found == effects.end() ? nullptr : &*found;
}

core::Result<ItemEffectTable> parseItemEffectTable(std::string_view yaml) {
  auto tree = core::parseYaml(yaml);
  if (!tree) {
    return std::unexpected{core::Error{std::format("{}: {}", Context, tree.error().message)}};
  }
  YamlReader reader;
  const YamlNode& root = *tree;
  if (!reader.expectKeys(root, "the item effect table", {"item_effects"})) {
    return std::unexpected{reader.error(Context)};
  }
  ItemEffectTable table;
  const std::span<const YamlNode> effects =
      reader.sequence(reader.field(root, "item_effects"), "item_effects");
  for (const YamlNode& effect : effects) {
    table.effects.push_back(readEffect(reader, effect));
  }
  for (std::size_t i = 0; reader.ok() && i < table.effects.size(); ++i) {
    for (std::size_t j = 0; j < i; ++j) {
      if (table.effects[i].index == table.effects[j].index) {
        reader.fail(effects[i], std::format("item {} is listed twice", table.effects[i].index));
        break;
      }
    }
  }
  return reader.finish(std::move(table), Context);
}

core::Result<ItemEffectTable> loadItemEffectTable(const VirtualFileSystem& vfs) {
  auto text = readDocumentText(vfs, ItemEffectsId);
  if (!text) {
    return std::unexpected{text.error()};
  }
  return parseItemEffectTable(*text);
}

} // namespace dwe::assets
