#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/evolution/messages.hpp>
#include <dwe/game/evolution/rules.hpp>
#include <dwe/game/evolution/strategies.hpp>

#include <algorithm>
#include <array>
#include <utility>

namespace dwe::game::evolution {

namespace {

using assets::YamlReader;
using core::YamlNode;

constexpr std::string_view CheckHoursKey = "check_hours";
constexpr std::string_view BabyTargetsKey = "baby_targets";
constexpr std::string_view RequirementsKey = "requirements";
constexpr std::string_view FallbackKey = "fallback";
constexpr std::string_view SpecialsKey = "specials";
constexpr std::string_view VirusKey = "virus";
constexpr std::string_view ItemsKey = "items";
constexpr std::string_view StatGainsKey = "stat_gains";
constexpr std::string_view MovesKey = "moves";
constexpr std::string_view RebirthKey = "rebirth";

constexpr std::string_view BabyKey = "baby";
constexpr std::string_view InTrainingKey = "in_training";
constexpr std::string_view RookieKey = "rookie";
constexpr std::string_view ChampionKey = "champion";

constexpr std::string_view PassPointsKey = "pass_points";
constexpr std::string_view WeightMarginKey = "weight_margin";
constexpr std::string_view HpMpDivisorKey = "hp_mp_divisor";
constexpr std::string_view MasteredMovesBelowKey = "mastered_moves_below";

constexpr std::string_view RookieHoursKey = "rookie_hours";
constexpr std::string_view RookieTargetKey = "rookie_target";

constexpr std::string_view RollLimitKey = "roll_limit";
constexpr std::string_view WakeUpKey = "wake_up";
constexpr std::string_view PraiseScoldKey = "praise_scold";
constexpr std::string_view PraiseScoldOnTimerKey = "praise_scold_on_timer";
constexpr std::string_view LevelKey = "level";
constexpr std::string_view HoursKey = "hours";
constexpr std::string_view TargetKey = "target";
constexpr std::string_view RollBelowKey = "roll_below";
constexpr std::string_view FromKey = "from";
constexpr std::string_view ScoldedKey = "scolded";
constexpr std::string_view RequireKey = "require";
constexpr std::string_view EqualsKey = "equals";
constexpr std::string_view AtLeastKey = "at_least";

constexpr std::string_view BarLimitKey = "bar_limit";
constexpr std::string_view FixedKey = "fixed";
constexpr std::string_view LevelStepKey = "level_step";

constexpr std::string_view GainDivisorKey = "gain_divisor";
constexpr std::string_view ScaleDivisorKey = "scale_divisor";
constexpr std::string_view ScaledTargetsKey = "scaled_targets";
constexpr std::string_view ScaledBelowLevelKey = "scaled_below_level";
constexpr std::string_view UltimateLifetimeHoursKey = "ultimate_lifetime_hours";
constexpr std::string_view ChampionTamerLevelChanceKey = "champion_tamer_level_chance";
constexpr std::string_view UltimateTamerLevelChanceKey = "ultimate_tamer_level_chance";

constexpr std::string_view SpecialtyBlocksKey = "specialty_blocks";
constexpr std::string_view BlockSizeKey = "block_size";
constexpr std::string_view BasicSlotBelowLevelKey = "basic_slot_below_level";
constexpr std::string_view FinisherFirstKey = "finisher_first";
constexpr std::string_view FinisherEndKey = "finisher_end";

constexpr std::string_view BabiesKey = "babies";

struct SpecialFieldName {
    std::string_view key;
    SpecialField field;
};

constexpr std::array<SpecialFieldName, 10> SpecialFieldNames{{
    {.key = "level", .field = SpecialField::Level},
    {.key = "evolution_timer", .field = SpecialField::EvolutionTimer},
    {.key = "discipline", .field = SpecialField::Discipline},
    {.key = "happiness", .field = SpecialField::Happiness},
    {.key = "tiredness", .field = SpecialField::Tiredness},
    {.key = "battles", .field = SpecialField::Battles},
    {.key = "defense", .field = SpecialField::Defense},
    {.key = "map", .field = SpecialField::Map},
    {.key = "discipline_before", .field = SpecialField::DisciplineBefore},
    {.key = "happiness_before", .field = SpecialField::HappinessBefore},
}};

std::int32_t readNumber(YamlReader& reader, const YamlNode& node, std::string_view key) {
    return reader.integer<std::int32_t>(reader.field(node, key));
}

// A divisor or a limit: 0 would divide by zero or never roll.
std::int32_t readPositive(YamlReader& reader, const YamlNode& node, std::string_view key) {
    const YamlNode& value = reader.field(node, key);
    const auto number = reader.integer<std::int32_t>(value);
    if (reader.ok() && number < 1) {
        reader.fail(value, messages::formatMessage(messages::NotPositive, key));
    }
    return number;
}

std::vector<std::string> readIds(YamlReader& reader, const YamlNode& node, std::string_view what) {
    std::vector<std::string> ids;
    for (const YamlNode& item : reader.sequence(node, what)) {
        ids.push_back(reader.text(item));
    }
    return ids;
}

std::map<std::string, std::string, std::less<>> readIdMap(YamlReader& reader, const YamlNode& node,
                                                          std::string_view what) {
    std::map<std::string, std::string, std::less<>> ids;
    if (!node.isMapping()) {
        reader.fail(node, messages::formatMessage(messages::NotMapping, what));
        return ids;
    }
    for (const core::YamlEntry& entry : node.entries()) {
        ids.insert_or_assign(entry.key, reader.text(entry.value));
    }
    return ids;
}

CheckHours readCheckHours(YamlReader& reader, const YamlNode& node) {
    if (!reader.expectKeys(node, messages::CheckHoursText, {BabyKey, InTrainingKey, RookieKey, ChampionKey})) {
        return {};
    }
    return {.baby = readNumber(reader, node, BabyKey),
            .inTraining = readNumber(reader, node, InTrainingKey),
            .rookie = readNumber(reader, node, RookieKey),
            .champion = readNumber(reader, node, ChampionKey)};
}

RequirementRules readRequirementRules(YamlReader& reader, const YamlNode& node) {
    if (!reader.expectKeys(node, messages::RequirementsText,
                           {PassPointsKey, WeightMarginKey, HpMpDivisorKey, MasteredMovesBelowKey})) {
        return {};
    }
    return {.passPoints = readNumber(reader, node, PassPointsKey),
            .weightMargin = readNumber(reader, node, WeightMarginKey),
            .hpMpDivisor = readPositive(reader, node, HpMpDivisorKey),
            .masteredMovesBelow = readPositive(reader, node, MasteredMovesBelowKey)};
}

FallbackRules readFallback(YamlReader& reader, const YamlNode& node) {
    if (!reader.expectKeys(node, messages::FallbackText, {RookieHoursKey, RookieTargetKey})) {
        return {};
    }
    return {.rookieHours = readNumber(reader, node, RookieHoursKey),
            .rookieTarget = reader.text(reader.field(node, RookieTargetKey))};
}

SpecialRequirement readSpecialRequirement(YamlReader& reader, const core::YamlEntry& entry) {
    SpecialRequirement requirement;
    const auto name = std::ranges::find(SpecialFieldNames, entry.key, &SpecialFieldName::key);
    if (name == SpecialFieldNames.end()) {
        reader.fail(entry.value, messages::formatMessage(messages::UnknownSpecialField, entry.key));
        return requirement;
    }
    requirement.field = name->field;
    if (!reader.expectKeys(entry.value, messages::ComparisonText, {}, {EqualsKey, AtLeastKey})) {
        return requirement;
    }
    const YamlNode* equals = entry.value.find(EqualsKey);
    const YamlNode* atLeast = entry.value.find(AtLeastKey);
    if ((equals == nullptr) == (atLeast == nullptr)) {
        reader.fail(entry.value, messages::OneComparison);
        return requirement;
    }
    requirement.comparison = equals != nullptr ? Comparison::Equals : Comparison::AtLeast;
    requirement.value = reader.integer<std::int32_t>(equals != nullptr ? *equals : *atLeast);
    return requirement;
}

SpecialRule readSpecialRule(YamlReader& reader, const YamlNode& node) {
    if (!reader.expectKeys(node, messages::SpecialText, {TargetKey}, {RollBelowKey, FromKey, ScoldedKey, RequireKey})) {
        return {};
    }
    SpecialRule rule{.target = reader.text(reader.field(node, TargetKey)),
                     .rollBelow = std::nullopt,
                     .from = {},
                     .scolded = false,
                     .requirements = {}};
    if (const YamlNode* roll = node.find(RollBelowKey); roll != nullptr) {
        rule.rollBelow = reader.integer<std::int32_t>(*roll);
    }
    if (const YamlNode* from = node.find(FromKey); from != nullptr) {
        rule.from = readIds(reader, *from, FromKey);
    }
    if (const YamlNode* scolded = node.find(ScoldedKey); scolded != nullptr) {
        rule.scolded = reader.boolean(*scolded);
    }
    if (const YamlNode* require = node.find(RequireKey); require != nullptr) {
        if (!require->isMapping()) {
            reader.fail(*require, messages::formatMessage(messages::NotMapping, messages::RequireText));
            return rule;
        }
        for (const core::YamlEntry& entry : require->entries()) {
            rule.requirements.push_back(readSpecialRequirement(reader, entry));
        }
    }
    return rule;
}

std::vector<SpecialRule> readSpecialList(YamlReader& reader, const YamlNode& node, std::string_view what) {
    std::vector<SpecialRule> rules;
    for (const YamlNode& item : reader.sequence(node, what)) {
        rules.push_back(readSpecialRule(reader, item));
    }
    return rules;
}

SpecialRules readSpecials(YamlReader& reader, const YamlNode& node) {
    if (!reader.expectKeys(node, messages::SpecialsText,
                           {RollLimitKey, WakeUpKey, PraiseScoldKey, PraiseScoldOnTimerKey})) {
        return {};
    }
    SpecialRules specials{.rollLimit = readPositive(reader, node, RollLimitKey),
                          .wakeUp = readSpecialList(reader, reader.field(node, WakeUpKey), WakeUpKey),
                          .praiseScold = readSpecialList(reader, reader.field(node, PraiseScoldKey), PraiseScoldKey),
                          .praiseScoldOnTimer = {}};
    const YamlNode& timer = reader.field(node, PraiseScoldOnTimerKey);
    if (reader.expectKeys(timer, messages::TimerSpecialText, {LevelKey, HoursKey})) {
        specials.praiseScoldOnTimer = {.level = readNumber(reader, timer, LevelKey),
                                       .hours = readNumber(reader, timer, HoursKey)};
    }
    return specials;
}

VirusRules readVirus(YamlReader& reader, const YamlNode& node) {
    if (!reader.expectKeys(node, messages::VirusText, {BarLimitKey, TargetKey})) {
        return {};
    }
    return {.barLimit = readNumber(reader, node, BarLimitKey), .target = reader.text(reader.field(node, TargetKey))};
}

ItemRules readItems(YamlReader& reader, const YamlNode& node) {
    if (!reader.expectKeys(node, messages::ItemsText, {FixedKey, LevelStepKey})) {
        return {};
    }
    return {.fixed = readIdMap(reader, reader.field(node, FixedKey), FixedKey),
            .levelStep = readNumber(reader, node, LevelStepKey)};
}

StatGainRules readStatGains(YamlReader& reader, const YamlNode& node) {
    if (!reader.expectKeys(node, messages::StatGainsText,
                           {GainDivisorKey, ScaleDivisorKey, ScaledTargetsKey, ScaledBelowLevelKey,
                            UltimateLifetimeHoursKey, ChampionTamerLevelChanceKey, UltimateTamerLevelChanceKey})) {
        return {};
    }
    return {.gainDivisor = readPositive(reader, node, GainDivisorKey),
            .scaleDivisor = readPositive(reader, node, ScaleDivisorKey),
            .scaledTargets = readIds(reader, reader.field(node, ScaledTargetsKey), ScaledTargetsKey),
            .scaledBelowLevel = readNumber(reader, node, ScaledBelowLevelKey),
            .ultimateLifetimeHours = readNumber(reader, node, UltimateLifetimeHoursKey),
            .championTamerLevelChance = readNumber(reader, node, ChampionTamerLevelChanceKey),
            .ultimateTamerLevelChance = readNumber(reader, node, UltimateTamerLevelChanceKey)};
}

MoveRules readMoves(YamlReader& reader, const YamlNode& node) {
    if (!reader.expectKeys(
            node, messages::MovesText,
            {SpecialtyBlocksKey, BlockSizeKey, BasicSlotBelowLevelKey, FinisherFirstKey, FinisherEndKey})) {
        return {};
    }
    MoveRules moves{.specialtyBlocks = {},
                    .blockSize = readPositive(reader, node, BlockSizeKey),
                    .basicSlotBelowLevel = readNumber(reader, node, BasicSlotBelowLevelKey),
                    .finisherFirst = readNumber(reader, node, FinisherFirstKey),
                    .finisherEnd = readNumber(reader, node, FinisherEndKey)};
    for (const YamlNode& item : reader.sequence(reader.field(node, SpecialtyBlocksKey), SpecialtyBlocksKey)) {
        moves.specialtyBlocks.push_back(reader.integer<std::int32_t>(item));
    }
    return moves;
}

RebirthRules readRebirth(YamlReader& reader, const YamlNode& node) {
    if (!reader.expectKeys(node, messages::RebirthText, {BabiesKey})) {
        return {};
    }
    return {.babies = readIds(reader, reader.field(node, BabiesKey), BabiesKey)};
}

} // namespace

core::Result<EvolutionRules> readEvolutionRules(const ruleset::Section& section) {
    YamlReader reader;
    const YamlNode& root = section.node;
    if (!reader.expectKeys(root, messages::SectionText,
                           {SelectionKey, ScoringKey, CheckHoursKey, BabyTargetsKey, RequirementsKey, FallbackKey,
                            SpecialsKey, VirusKey, ItemsKey, StatGainsKey, MovesKey, RebirthKey, ruleset::QuirksKey})) {
        return std::unexpected{reader.error(section.file)};
    }
    EvolutionRules rules{
        .selection = reader.text(reader.field(root, SelectionKey)),
        .scoring = reader.text(reader.field(root, ScoringKey)),
        .checkHours = readCheckHours(reader, reader.field(root, CheckHoursKey)),
        .babyTargets = readIdMap(reader, reader.field(root, BabyTargetsKey), BabyTargetsKey),
        .requirements = readRequirementRules(reader, reader.field(root, RequirementsKey)),
        .fallback = readFallback(reader, reader.field(root, FallbackKey)),
        .specials = readSpecials(reader, reader.field(root, SpecialsKey)),
        .virus = readVirus(reader, reader.field(root, VirusKey)),
        .items = readItems(reader, reader.field(root, ItemsKey)),
        .statGains = readStatGains(reader, reader.field(root, StatGainsKey)),
        .moves = readMoves(reader, reader.field(root, MovesKey)),
        .rebirth = readRebirth(reader, reader.field(root, RebirthKey)),
        .quirks = ruleset::readQuirks(reader, reader.field(root, ruleset::QuirksKey),
                                      {EvolutionItemSticks, SelectionAverageCarriesOver, UnlistedBabyRestartsTimer,
                                       RookieStatTiePicksLast, TimerSpecialOverridesNormal})};
    return reader.finish(std::move(rules), section.file);
}

core::Result<void> checkEvolutionSection(const ruleset::Section& section) {
    if (auto rules = readEvolutionRules(section); !rules) {
        return std::unexpected{rules.error()};
    }
    if (auto selection = selectionStrategies().check(section, SelectionKey); !selection) {
        return selection;
    }
    return scoringStrategies().check(section, ScoringKey);
}

std::int32_t countMasteredMoves(const RequirementRules& rules, std::span<const std::int32_t> learned) {
    // evolution.c: getNumMasteredMoves counts the bits of the two 32-bit words of learnedMoves.
    return static_cast<std::int32_t>(std::ranges::count_if(
        learned, [&](std::int32_t index) { return index >= 0 && index < rules.masteredMovesBelow; }));
}

} // namespace dwe::game::evolution
