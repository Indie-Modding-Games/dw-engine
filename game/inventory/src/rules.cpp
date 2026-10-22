#include <dwe/assets/content_id.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/inventory/rules.hpp>
#include <dwe/game/inventory/stacking.hpp>

#include <algorithm>
#include <array>
#include <span>
#include <utility>

#include "messages.hpp"

namespace dwe::game::inventory {

namespace {

using assets::YamlReader;
using core::YamlNode;

constexpr std::int32_t FullPercent = 100;

constexpr std::string_view NewGameItemsKey = "new_game_items";
constexpr std::string_view StartingItemKey = "item";
constexpr std::string_view StartingCountKey = "count";

struct HandlerName {
  std::string_view name;
  assets::ItemHandler handler;
};

// The handler names of data/item_effects (content/schemas/item_effects.schema.json).
constexpr std::array<HandlerName, 10> HandlerNames{{
    {.name = "none", .handler = assets::ItemHandler::None},
    {.name = "hp_healing", .handler = assets::ItemHandler::HpHealing},
    {.name = "mp_healing", .handler = assets::ItemHandler::MpHealing},
    {.name = "double_floppy", .handler = assets::ItemHandler::DoubleFloppy},
    {.name = "status", .handler = assets::ItemHandler::Status},
    {.name = "revive", .handler = assets::ItemHandler::Revive},
    {.name = "battle_disk", .handler = assets::ItemHandler::BattleDisk},
    {.name = "chip", .handler = assets::ItemHandler::Chip},
    {.name = "food", .handler = assets::ItemHandler::Food},
    {.name = "evolution", .handler = assets::ItemHandler::Evolution},
}};

std::int32_t integer(YamlReader& reader, const YamlNode& node, std::string_view key) {
  return reader.integer<std::int32_t>(reader.field(node, key));
}

std::int32_t positive(YamlReader& reader, const YamlNode& node, std::string_view key) {
  const std::int32_t value = integer(reader, node, key);
  if (reader.ok() && value <= 0) {
    reader.fail(reader.field(node, key), messages::NotPositive);
  }
  return value;
}

std::int32_t nonNegative(YamlReader& reader, const YamlNode& node, std::string_view key) {
  const std::int32_t value = integer(reader, node, key);
  if (reader.ok() && value < 0) {
    reader.fail(reader.field(node, key), messages::Negative);
  }
  return value;
}

std::int32_t percent(YamlReader& reader, const YamlNode& node, std::string_view key) {
  const std::int32_t value = integer(reader, node, key);
  if (reader.ok() && (value < 0 || value > FullPercent)) {
    reader.fail(reader.field(node, key), messages::PercentRange);
  }
  return value;
}

// An item's id in data/items, which the session checks against the table (checkItemIds).
std::string itemId(YamlReader& reader, const YamlNode& node, std::string_view key) {
  const YamlNode& field = reader.field(node, key);
  std::string id = reader.text(field);
  if (reader.ok()) {
    if (auto parsed = assets::ContentId::parse(id); !parsed) {
      reader.fail(field, parsed.error().message);
    }
  }
  return id;
}

std::vector<std::string> itemIds(YamlReader& reader, const YamlNode& node, std::string_view key) {
  std::vector<std::string> ids;
  for (const YamlNode& item : reader.sequence(reader.field(node, key), key)) {
    std::string id = reader.text(item);
    if (reader.ok()) {
      if (auto parsed = assets::ContentId::parse(id); !parsed) {
        reader.fail(item, parsed.error().message);
      }
    }
    ids.push_back(std::move(id));
  }
  return ids;
}

std::vector<std::int32_t> integers(YamlReader& reader, const YamlNode& node, std::string_view key) {
  std::vector<std::int32_t> values;
  for (const YamlNode& item : reader.sequence(reader.field(node, key), key)) {
    values.push_back(reader.integer<std::int32_t>(item));
  }
  return values;
}

std::optional<std::int32_t> optionalInteger(YamlReader& reader, const YamlNode& node,
                                            std::string_view key) {
  const YamlNode* value = node.find(key);
  if (value == nullptr) {
    return std::nullopt;
  }
  return reader.integer<std::int32_t>(*value);
}

std::int32_t integerOr(YamlReader& reader, const YamlNode& node, std::string_view key,
                       std::int32_t fallback) {
  return optionalInteger(reader, node, key).value_or(fallback);
}

bool booleanOr(YamlReader& reader, const YamlNode& node, std::string_view key, bool fallback) {
  const YamlNode* value = node.find(key);
  return value == nullptr ? fallback : reader.boolean(*value);
}

std::vector<StartingItem> readStartingItems(YamlReader& reader, const YamlNode& node,
                                            std::int32_t maxStack) {
  std::vector<StartingItem> items;
  for (const YamlNode& entry :
       reader.sequence(reader.field(node, NewGameItemsKey), NewGameItemsKey)) {
    if (!reader.expectKeys(entry, messages::StartingItemText,
                           {StartingItemKey, StartingCountKey})) {
      return items;
    }
    StartingItem item{.item = itemId(reader, entry, StartingItemKey),
                      .count = integer(reader, entry, StartingCountKey)};
    if (reader.ok() && (item.count < 1 || item.count > maxStack)) {
      reader.fail(reader.field(entry, StartingCountKey),
                  messages::formatMessage(messages::StartingCountRange, maxStack));
    }
    items.push_back(std::move(item));
  }
  return items;
}

BagRules readBag(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, "bag",
                         {"max_slots", "slot_counts", "new_game_slots", "new_game_money",
                          NewGameItemsKey, "max_stack", "stacking"})) {
    return {};
  }
  BagRules bag{.maxSlots = positive(reader, node, "max_slots"),
               .slotCounts = integers(reader, node, "slot_counts"),
               .newGameSlots = positive(reader, node, "new_game_slots"),
               .newGameMoney = nonNegative(reader, node, "new_game_money"),
               .newGameItems = {},
               .maxStack = positive(reader, node, "max_stack")};
  bag.newGameItems = readStartingItems(reader, node, bag.maxStack);
  const bool valid = !bag.slotCounts.empty() && bag.slotCounts.front() > 0 &&
                     std::ranges::adjacent_find(bag.slotCounts, std::ranges::greater_equal{}) ==
                         bag.slotCounts.end() &&
                     bag.slotCounts.back() == bag.maxSlots;
  if (reader.ok() && !valid) {
    reader.fail(reader.field(node, "slot_counts"), messages::SlotCountsOrder);
  }
  if (reader.ok() && !std::ranges::contains(bag.slotCounts, bag.newGameSlots)) {
    reader.fail(reader.field(node, "new_game_slots"),
                messages::formatMessage(messages::NotASlotCount, bag.newGameSlots));
  }
  return bag;
}

std::vector<std::array<std::int32_t, SortCategoryCount>> readSort(YamlReader& reader,
                                                                  const YamlNode& node) {
  std::vector<std::array<std::int32_t, SortCategoryCount>> orders;
  if (!reader.expectKeys(node, "sort", {"orders"})) {
    return orders;
  }
  for (const YamlNode& order : reader.sequence(reader.field(node, "orders"), "orders")) {
    std::array<std::int32_t, SortCategoryCount> categories{};
    const std::span<const YamlNode> values =
        reader.sequence(order, "a sort order", SortCategoryCount);
    for (std::size_t i = 0; i < values.size(); ++i) {
      categories.at(i) = reader.integer<std::int32_t>(values[i]);
    }
    std::array<std::int32_t, SortCategoryCount> sorted = categories;
    std::ranges::sort(sorted);
    for (std::size_t i = 0; reader.ok() && i < sorted.size(); ++i) {
      if (std::cmp_not_equal(sorted.at(i), i)) {
        reader.fail(order, messages::SortOrderNotPermutation);
      }
    }
    orders.push_back(categories);
  }
  return orders;
}

UseRules readUse(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(
          node, "use",
          {"field_colors", "battle_colors", "discard_droppable", "key_item_droppable"})) {
    return {};
  }
  return {.fieldColors = integers(reader, node, "field_colors"),
          .battleColors = integers(reader, node, "battle_colors"),
          .discardDroppable = integer(reader, node, "discard_droppable"),
          .keyItemDroppable = integer(reader, node, "key_item_droppable")};
}

ShopRules readShop(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(
          node, "shop",
          {"stock_trigger_base", "sell_divisor", "baby_discount_percent", "max_amount"})) {
    return {};
  }
  return {.stockTriggerBase = nonNegative(reader, node, "stock_trigger_base"),
          .sellDivisor = positive(reader, node, "sell_divisor"),
          .babyDiscountPercent = percent(reader, node, "baby_discount_percent"),
          .maxAmount = positive(reader, node, "max_amount")};
}

std::vector<assets::ItemHandler> readHandlers(YamlReader& reader, const YamlNode& node,
                                              std::string_view key) {
  std::vector<assets::ItemHandler> handlers;
  for (const YamlNode& item : reader.sequence(reader.field(node, key), key)) {
    const std::string name = reader.text(item);
    const auto found = std::ranges::find(HandlerNames, name, &HandlerName::name);
    if (found == HandlerNames.end()) {
      if (reader.ok()) {
        reader.fail(item, messages::formatMessage(messages::UnknownHandler, name));
      }
      continue;
    }
    handlers.push_back(found->handler);
  }
  return handlers;
}

std::vector<StatusItemRule> readStatus(YamlReader& reader, const YamlNode& node) {
  std::vector<StatusItemRule> rules;
  for (const YamlNode& item : reader.sequence(node, "status")) {
    if (!reader.expectKeys(
            item, "a status item", {"item"},
            {"battle_status_cure", "hp", "mp", "protects", "injury_cure", "sickness_cure"})) {
      return rules;
    }
    rules.push_back({.item = itemId(reader, item, "item"),
                     .battleStatusCure = optionalInteger(reader, item, "battle_status_cure"),
                     .hp = integerOr(reader, item, "hp", 0),
                     .mp = integerOr(reader, item, "mp", 0),
                     .protects = booleanOr(reader, item, "protects", false),
                     .injuryCure = integerOr(reader, item, "injury_cure", 0),
                     .sicknessCure = integerOr(reader, item, "sickness_cure", 0)});
  }
  return rules;
}

std::vector<ReviveItemRule> readRevive(YamlReader& reader, const YamlNode& node) {
  std::vector<ReviveItemRule> rules;
  for (const YamlNode& item : reader.sequence(node, "revive")) {
    if (!reader.expectKeys(item, "a revive item", {"item", "hp_percent"}, {"battle_status_cure"})) {
      return rules;
    }
    rules.push_back({.item = itemId(reader, item, "item"),
                     .hpPercent = percent(reader, item, "hp_percent"),
                     .battleStatusCure = optionalInteger(reader, item, "battle_status_cure")});
  }
  return rules;
}

void readDisks(YamlReader& reader, const YamlNode& node, EffectRules& effects) {
  if (!reader.expectKeys(node, "battle_disk", {"limit_percent", "limit_cap", "disks"})) {
    return;
  }
  effects.diskLimitPercent = positive(reader, node, "limit_percent");
  effects.diskLimitCap = positive(reader, node, "limit_cap");
  for (const YamlNode& item : reader.sequence(reader.field(node, "disks"), "disks")) {
    if (!reader.expectKeys(item, "a disk", {"item", "offense", "defense", "speed"}, {"omni"})) {
      return;
    }
    effects.disks.push_back({.item = itemId(reader, item, "item"),
                             .offense = nonNegative(reader, item, "offense"),
                             .defense = nonNegative(reader, item, "defense"),
                             .speed = nonNegative(reader, item, "speed"),
                             .omni = booleanOr(reader, item, "omni", false)});
  }
}

void readChip(YamlReader& reader, const YamlNode& node, EffectRules& effects) {
  if (!reader.expectKeys(node, "chip", {"script", "toilet", "tamer_level"})) {
    return;
  }
  for (const YamlNode& item : reader.sequence(reader.field(node, "script"), "script")) {
    if (reader.expectKeys(item, "a script chip", {"item", "section"})) {
      effects.scriptChips.push_back(
          {.item = itemId(reader, item, "item"), .section = nonNegative(reader, item, "section")});
    }
  }
  effects.toiletChips = itemIds(reader, node, "toilet");
  const YamlNode& tamer = reader.field(node, "tamer_level");
  if (reader.expectKeys(tamer, "tamer_level", {"items", "chance_percent", "amount"})) {
    effects.tamerLevel = {.items = itemIds(reader, tamer, "items"),
                          .chancePercent = percent(reader, tamer, "chance_percent"),
                          .amount = integer(reader, tamer, "amount")};
  }
}

void readFood(YamlReader& reader, const YamlNode& node, EffectRules& effects) {
  if (!reader.expectKeys(
          node, "food", {"favourite_energy_percent", "favourite_happiness", "poop_level_change"})) {
    return;
  }
  effects.favouriteEnergyPercent = nonNegative(reader, node, "favourite_energy_percent");
  effects.favouriteHappiness = integer(reader, node, "favourite_happiness");
  effects.poopLevelChange = integer(reader, node, "poop_level_change");
}

void readEvolution(YamlReader& reader, const YamlNode& node, EffectRules& effects) {
  if (!reader.expectKeys(node, "evolution", {"requires_previous_level", "fixed"})) {
    return;
  }
  effects.evolutionRequiresPreviousLevel =
      reader.boolean(reader.field(node, "requires_previous_level"));
  for (const YamlNode& item : reader.sequence(reader.field(node, "fixed"), "fixed")) {
    if (reader.expectKeys(item, "a fixed evolution", {"item", "evolves_to"})) {
      effects.fixedEvolutions.push_back(
          {.item = itemId(reader, item, "item"),
           .evolvesTo = reader.text(reader.field(item, "evolves_to"))});
    }
  }
}

EffectRules readEffects(YamlReader& reader, const YamlNode& node) {
  EffectRules effects;
  if (!reader.expectKeys(node, "effects",
                         {"stat_limit", "hp_mp_limit", "conscious_only", "double_floppy", "status",
                          "injury_roll", "sickness_roll", "revive", "battle_disk", "chip", "food",
                          "evolution"})) {
    return effects;
  }
  effects.statLimit = positive(reader, node, "stat_limit");
  effects.hpMpLimit = positive(reader, node, "hp_mp_limit");
  effects.consciousOnly = readHandlers(reader, node, "conscious_only");
  const YamlNode& floppy = reader.field(node, "double_floppy");
  if (reader.expectKeys(floppy, "double_floppy", {"hp", "mp"})) {
    effects.doubleFloppyHp = nonNegative(reader, floppy, "hp");
    effects.doubleFloppyMp = nonNegative(reader, floppy, "mp");
  }
  effects.status = readStatus(reader, reader.field(node, "status"));
  effects.injuryRoll = positive(reader, node, "injury_roll");
  effects.sicknessRoll = positive(reader, node, "sickness_roll");
  effects.revive = readRevive(reader, reader.field(node, "revive"));
  readDisks(reader, reader.field(node, "battle_disk"), effects);
  readChip(reader, reader.field(node, "chip"), effects);
  readFood(reader, reader.field(node, "food"), effects);
  readEvolution(reader, reader.field(node, "evolution"), effects);
  return effects;
}

InventoryQuirks readInventoryQuirks(YamlReader& reader, const YamlNode& node) {
  const ruleset::Quirks quirks =
      ruleset::readQuirks(reader, node,
                          {StackOverflowIsLost, BabyDiscountLimitedByFullPrice,
                           OmniDiskIgnoresLimit, UselessItemsConsumed});
  return {.stackOverflowIsLost = quirks.on(StackOverflowIsLost),
          .babyDiscountLimitedByFullPrice = quirks.on(BabyDiscountLimitedByFullPrice),
          .omniDiskIgnoresLimit = quirks.on(OmniDiskIgnoresLimit),
          .uselessItemsConsumed = quirks.on(UselessItemsConsumed)};
}

// Each item has at most one rule of its handler.
template <typename Rule>
void checkUniqueItems(YamlReader& reader, const YamlNode& at, const std::vector<Rule>& rules) {
  for (std::size_t i = 0; reader.ok() && i < rules.size(); ++i) {
    for (std::size_t j = 0; j < i; ++j) {
      if (rules[i].item == rules[j].item) {
        reader.fail(at, messages::formatMessage(messages::RuleItemTwice, rules[i].item));
        return;
      }
    }
  }
}

} // namespace

core::Result<void> checkItemIds(const InventoryRules& rules, const assets::ItemTable& items) {
  const EffectRules& effects = rules.effects;
  std::vector<std::pair<std::string_view, std::string_view>> named;
  named.reserve(rules.bag.newGameItems.size() + effects.status.size() + effects.revive.size() +
                effects.disks.size() + effects.scriptChips.size() + effects.toiletChips.size() +
                effects.tamerLevel.items.size() + effects.fixedEvolutions.size());
  for (const StartingItem& item : rules.bag.newGameItems) {
    named.emplace_back("bag.new_game_items", item.item);
  }
  for (const StatusItemRule& rule : effects.status) {
    named.emplace_back("effects.status", rule.item);
  }
  for (const ReviveItemRule& rule : effects.revive) {
    named.emplace_back("effects.revive", rule.item);
  }
  for (const BattleDiskRule& rule : effects.disks) {
    named.emplace_back("effects.battle_disk.disks", rule.item);
  }
  for (const ScriptItemRule& rule : effects.scriptChips) {
    named.emplace_back("effects.chip.script", rule.item);
  }
  for (const std::string& item : effects.toiletChips) {
    named.emplace_back("effects.chip.toilet", item);
  }
  for (const std::string& item : effects.tamerLevel.items) {
    named.emplace_back("effects.chip.tamer_level.items", item);
  }
  for (const FixedEvolutionRule& rule : effects.fixedEvolutions) {
    named.emplace_back("effects.evolution.fixed", rule.item);
  }
  for (const auto& [rule, item] : named) {
    if (items.find(item) == nullptr) {
      return core::fail(messages::formatMessage(messages::RuleItemUnknown, rule, item));
    }
  }
  return {};
}

bool isKeyItem(const assets::ItemEntry& item, const UseRules& rules) {
  return item.droppable == rules.keyItemDroppable;
}

core::Result<InventoryRules> parseInventoryRules(const ruleset::Section& section) {
  YamlReader reader;
  const YamlNode& root = section.node;
  if (!reader.expectKeys(root, "the inventory section",
                         {"bag", "money", "sort", "use", "defeat", "shop", "effects",
                          std::string_view{ruleset::QuirksKey}})) {
    return std::unexpected{reader.error(section.file)};
  }
  InventoryRules rules{.bag = readBag(reader, reader.field(root, "bag")),
                       .maxMoney = 0,
                       .sortOrders = readSort(reader, reader.field(root, "sort")),
                       .use = readUse(reader, reader.field(root, "use")),
                       .defeatLostPercent = 0,
                       .shop = readShop(reader, reader.field(root, "shop")),
                       .effects = readEffects(reader, reader.field(root, "effects")),
                       .quirks =
                           readInventoryQuirks(reader, reader.field(root, ruleset::QuirksKey))};
  const YamlNode& money = reader.field(root, "money");
  if (reader.expectKeys(money, "money", {"max"})) {
    rules.maxMoney = positive(reader, money, "max");
  }
  const YamlNode& defeat = reader.field(root, "defeat");
  if (reader.expectKeys(defeat, "defeat", {"lost_percent"})) {
    rules.defeatLostPercent = percent(reader, defeat, "lost_percent");
  }
  if (reader.ok() && rules.bag.newGameMoney > rules.maxMoney) {
    reader.fail(reader.field(reader.field(root, "bag"), "new_game_money"),
                messages::formatMessage(messages::SaveMoney, rules.maxMoney));
  }
  const YamlNode& effects = reader.field(root, "effects");
  checkUniqueItems(reader, effects, rules.effects.status);
  checkUniqueItems(reader, effects, rules.effects.revive);
  checkUniqueItems(reader, effects, rules.effects.disks);
  checkUniqueItems(reader, effects, rules.effects.fixedEvolutions);
  return reader.finish(std::move(rules), section.file);
}

ruleset::SectionCheck inventorySectionCheck() {
  return [](const ruleset::Section& section) -> core::Result<void> {
    auto rules = parseInventoryRules(section);
    if (!rules) {
      return std::unexpected{rules.error()};
    }
    return stackingRules().check(section, StackingPath);
  };
}

} // namespace dwe::game::inventory
