#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/evolution_table.hpp>
#include <dwe/assets/raise_table.hpp>
#include <dwe/core/error.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::assets {
class VirtualFileSystem;
} // namespace dwe::assets

// Patches by species id over the disc's tables (docs/architecture/rulesets.md, «Parches por
// especie»): `extends` replaces whole sections and tables, and a whole evolution or raise table is
// disc data that cannot be committed, so a ruleset that adds partners (the hidden content) says
// only what it adds: new requirement and stat rows, targets in the free slots of existing paths,
// raise rows copied from another species, moves in empty slots, the gates the original has not,
// and what the screens need (clip stand-ins, field scale, chart cells).
namespace dwe::ruleset {

// The sections: the species patches, and the Ultimate to Mega step, which patches the same way.
inline constexpr std::string_view SpeciesExtraSection = "species_extra";
inline constexpr std::string_view MegaStepSection = "mega_step";

// Hours of the game day, from `from` up to before `to`; it wraps past midnight when to < from.
struct HourRange {
  std::int32_t from = 0;
  std::int32_t to = 0;

  [[nodiscard]] bool holds(std::int32_t hour) const {
    return from <= to ? from <= hour && hour < to : hour >= from || hour < to;
  }
  friend bool operator==(const HourRange&, const HourRange&) = default;
};

// What must hold before a target is even scored: story triggers set, the hour of the day, and a
// cup won. The cup is a hook for the tournaments: until the session records cup wins it is open.
struct SpeciesGate {
  std::vector<std::int32_t> triggers = {};
  std::optional<HourRange> hours = {};
  std::optional<std::string> cup = {};

  friend bool operator==(const SpeciesGate&, const SpeciesGate&) = default;
};

// A raise row made from another species' row, with some values changed.
struct RaisePatch {
  std::string copy = {};
  std::optional<std::string> favoriteFood = {};
  std::optional<std::int32_t> defaultWeight = {};
  std::optional<std::int32_t> energyCap = {};
  std::optional<std::int32_t> energyUsage = {};
  std::optional<std::array<std::int32_t, 8>> hungerTimes = {};
};

// A move's values when the partner of this species uses it (its enemy form keeps the table's).
struct MoveOverride {
  std::optional<std::int32_t> power = {};
  std::optional<std::int32_t> iframes = {};
};

// Where a species sits on the evolution chart (the chart's pixels) and the icon it borrows: the
// cell of another species, drawn with another palette.
struct ChartCell {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::string icon = {};
  std::int32_t palette = 0;
};

// What a ruleset adds or changes for one species. Every field is optional; a field of the Mega
// step replaces the same field of species_extra.
struct SpeciesPatch {
  // Its requirement row (EVOLUTION_REQS), with `target` set to the species.
  std::optional<assets::EvolutionRequirementEntry> requirement = {};
  std::optional<SpeciesGate> gate = {};
  // The forms that evolve into it: it takes the first free `to` slot after their last target, so
  // the original's targets keep their order and win the ties.
  std::optional<std::vector<std::string>> evolvesFrom = {};
  // Its own targets, when it has no path row of its own.
  std::optional<std::vector<std::string>> evolvesTo = {};
  // Its stat reference row (EVOLUTION_STATS_GAINS), loading itself.
  std::optional<assets::EvolutionStatsGainEntry> statsGains = {};
  std::optional<RaisePatch> raise = {};
  // Moves put in the empty slots of its Digimon row, in order.
  std::vector<std::string> moves = {};
  // The type the partner of this species fights with.
  std::optional<std::int32_t> partnerType = {};
  std::map<std::string, MoveOverride, std::less<>> partnerMoves = {};
  // The species whose effect points its moves use (battle.yaml: effects.original_types).
  std::optional<std::string> effectType = {};
  // Original clip id it lacks -> the clip played instead.
  std::map<std::uint32_t, std::uint32_t> clips = {};
  // The size of the partner in the field, 1 for the original's.
  std::optional<float> fieldScale = {};
  std::optional<ChartCell> chart = {};
};

// The Ultimate to Mega step (an option, off in the original): a partner of level 5 that is not a
// Mega checks again from these hours of its counter, and its targets are Megas.
struct MegaStep {
  std::int32_t checkHours = 0;
  std::int32_t lifetimeHours = 0;
  std::int32_t tamerLevelChance = 0;
  // The species that are Megas: they count as level 5 for every other rule.
  std::vector<std::string> mega = {};
};

// A cup's extra prize (a hook for the tournaments: nothing reads it until they land).
struct CupPrize {
  std::string cup = {};
  std::string item = {};
};

struct SpeciesExtra {
  std::map<std::string, SpeciesPatch, std::less<>> species = {};
  std::vector<CupPrize> cupPrizes = {};
  std::optional<MegaStep> megaStep = {};

  [[nodiscard]] bool empty() const { return species.empty() && cupPrizes.empty() && !megaStep; }
  // The patch of a species, or nullptr.
  [[nodiscard]] const SpeciesPatch* find(std::string_view id) const;
  // Whether a species is a Mega of the Mega step.
  [[nodiscard]] bool isMega(std::string_view id) const;
};

// Reads the species_extra and mega_step sections of a ruleset; nothing for a ruleset without them.
// The errors name the file, line and column.
core::Result<SpeciesExtra> readSpeciesExtra(const Ruleset& ruleset);

// The checks of both sections for RulesetStore.
core::Result<void> checkSpeciesExtraSection(const Section& section);

// The patches applied to the disc's tables. An id the table lacks, or a path row with no free
// slot left, is an error.
core::Result<void> applySpeciesExtra(const SpeciesExtra& extra, assets::DigimonTable& table);
core::Result<void> applySpeciesExtra(const SpeciesExtra& extra, assets::RaiseTable& table);
core::Result<void> applySpeciesExtra(const SpeciesExtra& extra, assets::EvolutionTable& table);

// The tables a ruleset names by role (digimon, raise, evolution), with its patches applied.
core::Result<assets::DigimonTable> loadPatchedDigimonTable(const assets::VirtualFileSystem& vfs,
                                                           const Ruleset& ruleset);
core::Result<assets::RaiseTable> loadPatchedRaiseTable(const assets::VirtualFileSystem& vfs,
                                                       const Ruleset& ruleset);
core::Result<assets::EvolutionTable> loadPatchedEvolutionTable(const assets::VirtualFileSystem& vfs,
                                                               const Ruleset& ruleset);

} // namespace dwe::ruleset
