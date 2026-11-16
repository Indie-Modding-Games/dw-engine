#include <dwe/assets/yaml_reader.hpp>
#include <dwe/game/shop_job/messages.hpp>
#include <dwe/game/shop_job/shop_job.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <numeric>
#include <string_view>

namespace dwe::game::shop_job {

namespace {

using assets::YamlReader;
using core::YamlNode;

constexpr std::string_view StoryKey = "story";
constexpr std::string_view ResultVariableKey = "result_variable";
constexpr std::string_view LevelVariableKey = "level_variable";
constexpr std::string_view FaceVariableKey = "face_variable";
constexpr std::string_view SpeciesKey = "species";
constexpr std::string_view ScriptKey = "script";
constexpr std::string_view CountKey = "count";
constexpr std::string_view PatienceKey = "patience";
constexpr std::string_view CharacterKey = "character";
constexpr std::string_view AcceptanceKey = "acceptance";
constexpr std::string_view ItemsKey = "items";
constexpr std::string_view IdKey = "id";
constexpr std::string_view PricesKey = "prices";
constexpr std::string_view MoodsKey = "moods";
constexpr std::string_view NameKey = "name";
constexpr std::string_view WeightKey = "weight";
constexpr std::string_view ShiftKey = "shift";
constexpr std::string_view ReactionsKey = "reactions";
constexpr std::string_view FaceKey = "face";
constexpr std::string_view GapUpToKey = "gap_up_to";
constexpr std::string_view RegularLevelKey = "regular_level";
constexpr std::string_view TalkKey = "talk";
constexpr std::string_view CostKey = "cost";
constexpr std::string_view NeedsKey = "needs";
constexpr std::string_view PerfectScoreKey = "perfect_score";
constexpr std::string_view LeastKey = "least";
constexpr std::string_view MostKey = "most";
constexpr std::string_view TriesKey = "tries";
constexpr std::string_view BonusKey = "bonus";
constexpr std::string_view FromKey = "from";
constexpr std::string_view ItemKey = "item";
constexpr std::int32_t FullPercent = 100;

// The reactions' names in the rules, in the order of Reaction.
constexpr std::array<std::string_view, Reactions> ReactionNames{"delighted", "pleased", "doubtful",
                                                                "annoyed", "furious"};

template <typename T>
T whole(YamlReader& reader, const YamlNode& parent, std::string_view key) {
  return reader.integer<T>(reader.field(parent, key));
}

std::array<std::int32_t, Levels> levels(YamlReader& reader, const YamlNode& parent,
                                        std::string_view key) {
  std::array<std::int32_t, Levels> values{};
  const auto items = reader.sequence(reader.field(parent, key), key, Levels);
  for (std::size_t index = 0; index < items.size() && index < Levels; ++index) {
    values.at(index) = reader.integer<std::int32_t>(items[index]);
  }
  return values;
}

void readSpecies(YamlReader& reader, const YamlNode& root, ShopJobRules& rules) {
  for (const YamlNode& node : reader.sequence(reader.field(root, SpeciesKey), SpeciesKey)) {
    if (!reader.expectKeys(
            node, messages::SpeciesText,
            {ScriptKey, SpeciesKey, CountKey, PatienceKey, CharacterKey, AcceptanceKey})) {
      continue;
    }
    rules.species.push_back({.script = whole<std::int32_t>(reader, node, ScriptKey),
                             .species = whole<std::int32_t>(reader, node, SpeciesKey),
                             .count = whole<std::int32_t>(reader, node, CountKey),
                             .patience = whole<std::int32_t>(reader, node, PatienceKey),
                             .character = reader.text(reader.field(node, CharacterKey)),
                             .acceptance = levels(reader, node, AcceptanceKey)});
  }
}

void readItems(YamlReader& reader, const YamlNode& root, ShopJobRules& rules) {
  for (const YamlNode& node : reader.sequence(reader.field(root, ItemsKey), ItemsKey)) {
    if (!reader.expectKeys(node, messages::ItemText, {IdKey, CountKey, PricesKey})) {
      continue;
    }
    rules.items.push_back({.id = reader.text(reader.field(node, IdKey)),
                           .count = whole<std::int32_t>(reader, node, CountKey),
                           .prices = levels(reader, node, PricesKey)});
  }
}

void readMoods(YamlReader& reader, const YamlNode& root, ShopJobRules& rules) {
  for (const YamlNode& node : reader.sequence(reader.field(root, MoodsKey), MoodsKey)) {
    if (!reader.expectKeys(node, messages::MoodText, {NameKey, WeightKey, ShiftKey})) {
      continue;
    }
    rules.moods.push_back({.name = reader.text(reader.field(node, NameKey)),
                           .weight = whole<std::int32_t>(reader, node, WeightKey),
                           .shift = whole<std::int32_t>(reader, node, ShiftKey)});
  }
}

void readReactions(YamlReader& reader, const YamlNode& root, ShopJobRules& rules) {
  const auto nodes = reader.sequence(reader.field(root, ReactionsKey), ReactionsKey);
  for (std::size_t index = 0; index < nodes.size(); ++index) {
    const YamlNode& node = nodes[index];
    if (!reader.expectKeys(node, messages::ReactionText, {NameKey, FaceKey, PatienceKey},
                           {GapUpToKey})) {
      continue;
    }
    ReactionRules reaction{.reaction = static_cast<Reaction>(std::min(index, Reactions - 1)),
                           .name = reader.text(reader.field(node, NameKey)),
                           .face = whole<std::int32_t>(reader, node, FaceKey),
                           .gapUpTo = reader.optionalInteger<std::int32_t>(node, GapUpToKey)
                                          .value_or(static_cast<std::int32_t>(Levels) + 1),
                           .patienceCost = whole<std::int32_t>(reader, node, PatienceKey)};
    rules.reactions.push_back(std::move(reaction));
  }
}

void readSettings(YamlReader& reader, const YamlNode& root, ShopJobRules& rules) {
  const YamlNode& story = reader.field(root, StoryKey);
  if (reader.expectKeys(story, StoryKey, {ResultVariableKey, LevelVariableKey, FaceVariableKey})) {
    rules.resultVariable = whole<std::int32_t>(reader, story, ResultVariableKey);
    rules.levelVariable = whole<std::int32_t>(reader, story, LevelVariableKey);
    rules.faceVariable = whole<std::int32_t>(reader, story, FaceVariableKey);
  }
  rules.regularLevel = whole<Level>(reader, root, RegularLevelKey);
  const YamlNode& talk = reader.field(root, TalkKey);
  if (reader.expectKeys(talk, messages::TalkText, {ShiftKey, CostKey, NeedsKey})) {
    rules.talkShift = whole<std::int32_t>(reader, talk, ShiftKey);
    rules.talkCost = whole<std::int32_t>(reader, talk, CostKey);
    rules.talkNeeds = whole<std::int32_t>(reader, talk, NeedsKey);
  }
  const YamlNode& perfect = reader.field(root, PerfectScoreKey);
  if (reader.expectKeys(perfect, messages::PerfectScoreText, {LeastKey, MostKey, TriesKey})) {
    rules.perfectLeast = whole<std::int64_t>(reader, perfect, LeastKey);
    rules.perfectMost = whole<std::int64_t>(reader, perfect, MostKey);
    rules.maxLines = whole<std::int32_t>(reader, perfect, TriesKey);
  }
  const YamlNode& bonus = reader.field(root, BonusKey);
  if (reader.expectKeys(bonus, messages::BonusText, {FromKey, ItemKey})) {
    rules.bonusFrom = whole<std::int64_t>(reader, bonus, FromKey);
    rules.bonusItem = reader.text(reader.field(bonus, ItemKey));
  }
}

bool acceptanceValid(const SpeciesRules& species) {
  return std::ranges::is_sorted(species.acceptance) &&
         std::ranges::all_of(species.acceptance, [](std::int32_t percent) {
           return percent >= 0 && percent <= FullPercent;
         });
}

bool reactionsValid(const ShopJobRules& rules) {
  if (rules.reactions.size() != Reactions) {
    return false;
  }
  for (std::size_t index = 0; index < Reactions; ++index) {
    if (rules.reactions[index].name != ReactionNames.at(index) ||
        (index > 0 && rules.reactions[index].gapUpTo <= rules.reactions[index - 1].gapUpTo)) {
      return false;
    }
  }
  return true;
}

core::Result<void> validate(const ShopJobRules& rules, std::string_view file) {
  const auto counted = [](const auto& entries) {
    return std::accumulate(entries.begin(), entries.end(), std::int64_t{0},
                           [](std::int64_t sum, const auto& entry) { return sum + entry.count; });
  };
  if (counted(rules.species) != counted(rules.items)) {
    return core::fail(std::format(messages::BagsDiffer, file));
  }
  if (rules.customers() == 0) {
    return core::fail(std::format(messages::EmptyBag, file));
  }
  if (!std::ranges::all_of(rules.species, acceptanceValid)) {
    return core::fail(std::format(messages::BadAcceptance, file));
  }
  if (!reactionsValid(rules)) {
    return core::fail(std::format(messages::BadReactions, file));
  }
  if (rules.regularLevel < 0 || rules.regularLevel >= NoSale) {
    return core::fail(std::format(messages::BadLevel, file));
  }
  if (rules.moods.empty() ||
      std::ranges::any_of(rules.moods, [](const MoodRules& mood) { return mood.weight <= 0; })) {
    return core::fail(std::format(messages::BadMoods, file));
  }
  if (rules.perfectLeast > rules.perfectMost || rules.maxLines <= 0) {
    return core::fail(std::format(messages::BadWindow, file));
  }
  return {};
}

} // namespace

std::size_t ShopJobRules::customers() const {
  std::size_t total = 0;
  for (const SpeciesRules& entry : species) {
    total += static_cast<std::size_t>(std::max(entry.count, 0));
  }
  return total;
}

std::int64_t ShopJobRules::profit(std::size_t item, Level level) const {
  if (item >= items.size() || level < 0 || level >= NoSale) {
    return 0;
  }
  const auto& prices = items[item].prices;
  return prices.at(static_cast<std::size_t>(level)) - prices.back();
}

const ReactionRules& ShopJobRules::rulesOf(Reaction reaction) const {
  return reactions.at(static_cast<std::size_t>(reaction));
}

core::Result<ShopJobRules> parseShopJobRules(const ruleset::Section& section) {
  YamlReader reader;
  const YamlNode& root = section.node;
  if (!reader.expectKeys(root, messages::SectionText,
                         {StoryKey, SpeciesKey, ItemsKey, MoodsKey, ReactionsKey, RegularLevelKey,
                          TalkKey, PerfectScoreKey, BonusKey})) {
    return std::unexpected{reader.error(section.file)};
  }
  ShopJobRules rules;
  readSpecies(reader, root, rules);
  readItems(reader, root, rules);
  readMoods(reader, root, rules);
  readReactions(reader, root, rules);
  readSettings(reader, root, rules);
  if (!reader.ok()) {
    return std::unexpected{reader.error(section.file)};
  }
  if (auto valid = validate(rules, section.file); !valid) {
    return std::unexpected{valid.error()};
  }
  return rules;
}

} // namespace dwe::game::shop_job
