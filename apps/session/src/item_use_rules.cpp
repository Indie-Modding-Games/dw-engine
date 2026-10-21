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
constexpr std::string_view FixedLevelEvolutionsKey = "fixed_level_evolutions";
constexpr std::string_view RefusedLevelDifferencesKey = "refused_level_differences";
constexpr std::string_view FromKey = "from";
constexpr std::string_view ToKey = "to";
constexpr std::string_view ItemKey = "item";
constexpr std::string_view LevelKey = "level";

// The keys of the stats, in the order of game::partner::AllStats.
constexpr std::array<std::string_view, game::partner::StatCount> StatKeys{"hp",      "mp",    "offense",
                                                                          "defense", "speed", "brain"};

std::int32_t nonNegative(YamlReader& reader, const YamlNode& node) {
    const auto value = reader.integer<std::int32_t>(node);
    if (reader.ok() && value < 0) {
        reader.fail(node, formatMessage(messages::NegativeValue, reader.text(node)));
    }
    return value;
}

std::vector<ItemRange> readRanges(YamlReader& reader, const YamlNode& parent, std::string_view key) {
    std::vector<ItemRange> ranges;
    for (const YamlNode& entry : reader.sequence(reader.field(parent, key), key)) {
        if (!reader.expectKeys(entry, messages::ItemRangeText, {FromKey, ToKey})) {
            return ranges;
        }
        const ItemRange range{.from = nonNegative(reader, reader.field(entry, FromKey)),
                              .to = nonNegative(reader, reader.field(entry, ToKey))};
        if (reader.ok() && range.to < range.from) {
            reader.fail(entry, formatMessage(messages::ItemRangeReversed, range.from, range.to));
        }
        ranges.push_back(range);
    }
    return ranges;
}

std::vector<FixedLevelEvolution> readFixedLevels(YamlReader& reader, const YamlNode& parent) {
    std::vector<FixedLevelEvolution> items;
    for (const YamlNode& entry :
         reader.sequence(reader.field(parent, FixedLevelEvolutionsKey), FixedLevelEvolutionsKey)) {
        if (!reader.expectKeys(entry, messages::FixedLevelText, {ItemKey, LevelKey})) {
            return items;
        }
        items.push_back({.item = nonNegative(reader, reader.field(entry, ItemKey)),
                         .level = nonNegative(reader, reader.field(entry, LevelKey))});
    }
    return items;
}

ItemRefusalRules readRefusal(YamlReader& reader, const YamlNode& node) {
    ItemRefusalRules rules;
    if (!reader.expectKeys(
            node, RefusalKey,
            {NeverTakenKey, DisciplineCheckedBelowKey, FixedLevelEvolutionsKey, RefusedLevelDifferencesKey})) {
        return rules;
    }
    rules.neverTaken = readRanges(reader, node, NeverTakenKey);
    rules.disciplineCheckedBelow = nonNegative(reader, reader.field(node, DisciplineCheckedBelowKey));
    rules.fixedLevelEvolutions = readFixedLevels(reader, node);
    for (const YamlNode& difference :
         reader.sequence(reader.field(node, RefusedLevelDifferencesKey), RefusedLevelDifferencesKey)) {
        rules.refusedLevelDifferences.push_back(reader.integer<std::int32_t>(difference));
    }
    return rules;
}

game::partner::StatBlock readBits(YamlReader& reader, const YamlNode& node) {
    game::partner::StatBlock bits;
    if (!reader.expectKeys(node, TrainingBoostBitsKey,
                           {StatKeys[0], StatKeys[1], StatKeys[2], StatKeys[3], StatKeys[4], StatKeys[5]})) {
        return bits;
    }
    for (std::size_t index = 0; index < StatKeys.size(); ++index) {
        bits[game::partner::AllStats[index]] = nonNegative(reader, reader.field(node, StatKeys[index]));
    }
    return bits;
}

} // namespace

bool inRanges(const std::vector<ItemRange>& ranges, std::int32_t item) {
    return std::ranges::any_of(ranges, [item](const ItemRange& range) { return range.contains(item); });
}

core::Result<ItemUseRules> readItemUseRules(const ruleset::Section& section) {
    YamlReader reader;
    const YamlNode& root = section.node;
    if (!reader.expectKeys(root, messages::ItemUseSectionText,
                           {RefusalKey, MealsKey, ShopDiscountLevelsKey, TrainingBoostBitsKey})) {
        return std::unexpected{reader.error(section.file)};
    }
    ItemUseRules rules{.refusal = readRefusal(reader, reader.field(root, RefusalKey)),
                       .meals = readRanges(reader, root, MealsKey),
                       .shopDiscountLevels = {},
                       .trainingBoostBits = readBits(reader, reader.field(root, TrainingBoostBitsKey))};
    for (const YamlNode& level : reader.sequence(reader.field(root, ShopDiscountLevelsKey), ShopDiscountLevelsKey)) {
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
