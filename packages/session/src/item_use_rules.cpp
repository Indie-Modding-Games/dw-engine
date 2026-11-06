#include <dwe/assets/content_id.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/session/item_use_rules.hpp>
#include <dwe/session/messages.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <utility>

namespace dwe::session {

namespace {

using assets::YamlReader;
using core::YamlNode;
using game::partner::Stat;
using messages::formatMessage;

constexpr std::string_view RefusalKey = "refusal";
constexpr std::string_view MealsKey = "meals";
constexpr std::string_view TrainingBoostBitsKey = "training_boost_bits";
constexpr std::string_view ShopDiscountLevelsKey = "shop_discount_levels";
constexpr std::string_view NeverTakenKey = "never_taken";
constexpr std::string_view DisciplineCheckedBelowKey = "discipline_checked_below";
constexpr std::string_view DisciplineCheckedIdsKey = "discipline_checked_ids";
constexpr std::string_view FixedLevelEvolutionsKey = "fixed_level_evolutions";
constexpr std::string_view RefusedLevelDifferencesKey = "refused_level_differences";
constexpr std::string_view ItemKey = "item";
constexpr std::string_view LevelKey = "level";

// The keys of the stats, in the order of game::partner::AllStats.
constexpr std::array<std::string_view, game::partner::StatCount> StatKeys{
    "hp", "mp", "offense", "defense", "speed", "brain"};

std::int32_t nonNegative(YamlReader& reader, const YamlNode& node) {
  const auto value = reader.integer<std::int32_t>(node);
  if (reader.ok() && value < 0) {
    reader.fail(node, formatMessage(messages::NegativeValue, reader.text(node)));
  }
  return value;
}

assets::EntryClass readClass(YamlReader& reader, const YamlNode& parent, std::string_view key) {
  assets::EntryClass items;
  assets::readEntryList(reader, reader.field(parent, key), key, items);
  return items;
}

std::string readItemId(YamlReader& reader, const YamlNode& node) {
  std::string id = reader.text(node);
  if (reader.ok()) {
    if (auto parsed = assets::ContentId::parse(id); !parsed) {
      reader.fail(node, parsed.error().message);
    }
  }
  return id;
}

std::vector<FixedLevelEvolution> readFixedLevels(YamlReader& reader, const YamlNode& parent) {
  std::vector<FixedLevelEvolution> items;
  for (const YamlNode& entry :
       reader.sequence(reader.field(parent, FixedLevelEvolutionsKey), FixedLevelEvolutionsKey)) {
    if (!reader.expectKeys(entry, messages::FixedLevelText, {ItemKey, LevelKey})) {
      return items;
    }
    items.push_back({.item = readItemId(reader, reader.field(entry, ItemKey)),
                     .level = nonNegative(reader, reader.field(entry, LevelKey))});
  }
  return items;
}

ItemRefusalRules readRefusal(YamlReader& reader, const YamlNode& node) {
  ItemRefusalRules rules;
  if (!reader.expectKeys(node, RefusalKey,
                         {NeverTakenKey, DisciplineCheckedBelowKey, FixedLevelEvolutionsKey,
                          RefusedLevelDifferencesKey},
                         {DisciplineCheckedIdsKey})) {
    return rules;
  }
  rules.neverTaken = readClass(reader, node, NeverTakenKey);
  rules.disciplineChecked.below =
      nonNegative(reader, reader.field(node, DisciplineCheckedBelowKey));
  if (const YamlNode* ids = node.find(DisciplineCheckedIdsKey)) {
    assets::readEntryIds(reader, *ids, DisciplineCheckedIdsKey, rules.disciplineChecked);
  }
  rules.fixedLevelEvolutions = readFixedLevels(reader, node);
  for (const YamlNode& difference : reader.sequence(reader.field(node, RefusedLevelDifferencesKey),
                                                    RefusedLevelDifferencesKey)) {
    rules.refusedLevelDifferences.push_back(reader.integer<std::int32_t>(difference));
  }
  return rules;
}

game::partner::StatBlock readBits(YamlReader& reader, const YamlNode& node) {
  game::partner::StatBlock bits;
  if (!reader.expectKeys(
          node, TrainingBoostBitsKey,
          {StatKeys[0], StatKeys[1], StatKeys[2], StatKeys[3], StatKeys[4], StatKeys[5]})) {
    return bits;
  }
  for (std::size_t index = 0; index < StatKeys.size(); ++index) {
    bits[game::partner::AllStats[index]] = nonNegative(reader, reader.field(node, StatKeys[index]));
  }
  return bits;
}

} // namespace

core::Result<void> checkItemUseItems(const ItemUseRules& rules, const assets::ItemTable& items) {
  const std::array<std::pair<std::string_view, const assets::EntryClass*>, 3> classes{{
      {messages::NeverTakenRule, &rules.refusal.neverTaken},
      {messages::DisciplineCheckedRule, &rules.refusal.disciplineChecked},
      {messages::MealsRule, &rules.meals},
  }};
  for (const auto& [rule, entries] : classes) {
    if (auto checked = assets::checkEntryClass(*entries, items, rule, messages::ItemText);
        !checked) {
      return checked;
    }
  }
  for (const FixedLevelEvolution& fixed : rules.refusal.fixedLevelEvolutions) {
    if (items.find(fixed.item) == nullptr) {
      return std::unexpected{
          assets::entryClassIdError(messages::FixedLevelRule, fixed.item, messages::ItemText)};
    }
  }
  return {};
}

core::Result<ItemUseRules> readItemUseRules(const ruleset::Section& section) {
  YamlReader reader;
  const YamlNode& root = section.node;
  if (!reader.expectKeys(root, messages::ItemUseSectionText,
                         {RefusalKey, MealsKey, ShopDiscountLevelsKey, TrainingBoostBitsKey})) {
    return std::unexpected{reader.error(section.file)};
  }
  ItemUseRules rules{.refusal = readRefusal(reader, reader.field(root, RefusalKey)),
                     .meals = readClass(reader, root, MealsKey),
                     .shopDiscountLevels = {},
                     .trainingBoostBits =
                         readBits(reader, reader.field(root, TrainingBoostBitsKey))};
  for (const YamlNode& level :
       reader.sequence(reader.field(root, ShopDiscountLevelsKey), ShopDiscountLevelsKey)) {
    rules.shopDiscountLevels.push_back(nonNegative(reader, level));
  }
  return reader.finish(std::move(rules), section.file);
}

core::Result<void> checkItemUseSection(const ruleset::Section& section) {
  if (auto read = readItemUseRules(section); !read) {
    return std::unexpected{read.error()};
  }
  return {};
}

} // namespace dwe::session
