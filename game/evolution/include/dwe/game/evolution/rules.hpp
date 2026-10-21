#pragma once

#include <dwe/core/error.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::game::evolution {

// The ruleset section of evolution: rules/<id>/evolution.yaml (content/schemas/ruleset_evolution.schema.json).
inline constexpr std::string_view EvolutionSection = "evolution";

// The keys of the strategies the section names.
inline constexpr std::string_view SelectionKey = "selection";
inline constexpr std::string_view ScoringKey = "scoring";

// The quirks of the section (docs/reference/evolution.md, "Rarezas"); true reproduces the original.
inline constexpr std::string_view EvolutionItemSticks = "evolution_item_sticks";
inline constexpr std::string_view SelectionAverageCarriesOver = "selection_average_carries_over";
inline constexpr std::string_view UnlistedBabyRestartsTimer = "unlisted_baby_restarts_timer";
inline constexpr std::string_view RookieStatTiePicksLast = "rookie_stat_tie_picks_last";
inline constexpr std::string_view TimerSpecialOverridesNormal = "timer_special_overrides_normal";

// The rules below name forms and items by their table ids; Evolution::create checks that each one exists.

// From how many hours of the counter each level checks its evolution.
struct CheckHours {
    std::int32_t baby = 0;
    std::int32_t inTraining = 0;
    std::int32_t rookie = 0;
    std::int32_t champion = 0;
};

// How a candidate is scored.
struct RequirementRules {
    std::int32_t passPoints = 0;
    std::int32_t weightMargin = 0;
    std::int32_t hpMpDivisor = 1;
    // Only the learned moves with an index below this count (evolution.c: getNumMasteredMoves).
    std::int32_t masteredMovesBelow = 0;
};

// Where a rookie with no candidate goes.
struct FallbackRules {
    std::int32_t rookieHours = 0;
    std::string rookieTarget;
};

// What a special rule compares.
enum class SpecialField : std::uint8_t {
    Level,
    EvolutionTimer,
    Discipline,
    Happiness,
    Tiredness,
    Battles,
    Defense,
    Map,
    DisciplineBefore,
    HappinessBefore,
};

enum class Comparison : std::uint8_t {
    Equals,
    AtLeast,
};

struct SpecialRequirement {
    SpecialField field = SpecialField::Level;
    Comparison comparison = Comparison::Equals;
    std::int32_t value = 0;
};

// A special evolution: when everything it asks holds, the partner turns into the target.
struct SpecialRule {
    std::string target;
    // The check's shared roll must be below this.
    std::optional<std::int32_t> rollBelow;
    // The partner must be one of these forms; empty for any.
    std::vector<std::string> from;
    // Only right after a scold.
    bool scolded = false;
    std::vector<SpecialRequirement> requirements;
};

// The partner of this level with the counter at exactly these hours also runs the praise and scold rules.
struct TimerSpecialRule {
    std::int32_t level = 0;
    std::int32_t hours = 0;
};

struct SpecialRules {
    std::int32_t rollLimit = 1;
    std::vector<SpecialRule> wakeUp;
    std::vector<SpecialRule> praiseScold;
    TimerSpecialRule praiseScoldOnTimer;
};

struct ItemRules {
    // Item id -> form, whatever the level.
    std::map<std::string, std::string, std::less<>> fixed;
    // An item of the item table works when its form is this many levels above the partner.
    std::int32_t levelStep = 0;
};

struct StatGainRules {
    std::int32_t gainDivisor = 1;
    std::int32_t scaleDivisor = 1;
    std::vector<std::string> scaledTargets;
    std::int32_t scaledBelowLevel = 0;
    std::int32_t ultimateLifetimeHours = 0;
    std::int32_t championTamerLevelChance = 0;
    std::int32_t ultimateTamerLevelChance = 0;
};

struct MoveRules {
    // The first move of each specialty's block, by specialty.
    std::vector<std::int32_t> specialtyBlocks;
    std::int32_t blockSize = 0;
    std::int32_t basicSlotBelowLevel = 0;
    // The fourth equipped slot is the last move of the list in [finisherFirst, finisherEnd).
    std::int32_t finisherFirst = 0;
    std::int32_t finisherEnd = 0;
};

struct RebirthRules {
    std::vector<std::string> babies;
};

// The evolution section, as read. The strategy names are checked against their registries.
struct EvolutionRules {
    std::string selection;
    std::string scoring;
    CheckHours checkHours;
    // Baby id -> in-training id.
    std::map<std::string, std::string, std::less<>> babyTargets;
    RequirementRules requirements;
    FallbackRules fallback;
    SpecialRules specials;
    ItemRules items;
    StatGainRules statGains;
    MoveRules moves;
    RebirthRules rebirth;
    ruleset::Quirks quirks;
};

// Reads the evolution section; errors name the file, line and column.
core::Result<EvolutionRules> readEvolutionRules(const ruleset::Section& section);

// The check of the evolution section for RulesetStore: it reads the section and checks the strategy names.
core::Result<void> checkEvolutionSection(const ruleset::Section& section);

// How many of the learned moves (their indices in the move table) the requirements count.
[[nodiscard]] std::int32_t countMasteredMoves(const RequirementRules& rules, std::span<const std::int32_t> learned);

} // namespace dwe::game::evolution
