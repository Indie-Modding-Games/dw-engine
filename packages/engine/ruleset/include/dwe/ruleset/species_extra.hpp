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

// The sections: the species patches, the Ultimate to Mega step, which patches the same way, and
// every species as a partner (docs/modulos/game/evoluciones-extra.md), whose patches are fallback
// targets and which adds wake-up specials.
inline constexpr std::string_view SpeciesExtraSection = "species_extra";
inline constexpr std::string_view MegaStepSection = "mega_step";
inline constexpr std::string_view AllSpeciesSection = "all_species";

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
// cup won (its number in data/tournaments; open where no tournaments are run).
struct SpeciesGate {
  std::vector<std::int32_t> triggers = {};
  std::optional<HourRange> hours = {};
  std::optional<std::int32_t> cup = {};

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
  // Its stat reference row (EVOLUTION_STATS_GAINS), loading itself, or the species whose row it
  // copies (stats_gains: {copy: id}).
  std::optional<assets::EvolutionStatsGainEntry> statsGains = {};
  std::optional<std::string> statsGainsCopy = {};
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
  // A fallback target (all_species): the natural check picks it only when none of the original's
  // targets passes.
  bool yields = false;
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

struct SpeciesExtra {
  std::map<std::string, SpeciesPatch, std::less<>> species = {};
  // Whether the section gives cups extra prizes (extra_prizes, which the tournaments read and add
  // to tournament.yaml's, so a ruleset that extends another need not copy its tournament section).
  bool extraPrizes = false;
  std::optional<MegaStep> megaStep = {};
  // The targets of all_species' wake-up specials (game/evolution reads the rules): forms no path
  // may lead to that still get a chart cell.
  std::vector<std::string> specialTargets = {};

  [[nodiscard]] bool empty() const {
    return species.empty() && !extraPrizes && !megaStep && specialTargets.empty();
  }
  // The patch of a species, or nullptr.
  [[nodiscard]] const SpeciesPatch* find(std::string_view id) const;
  // Whether a species is a Mega of the Mega step.
  [[nodiscard]] bool isMega(std::string_view id) const;
};

// Reads the species_extra, mega_step and all_species sections of a ruleset; nothing for a ruleset
// without them. A species species_extra or mega_step patches keeps that patch: its all_species
// entry is skipped. The errors name the file, line and column.
core::Result<SpeciesExtra> readSpeciesExtra(const Ruleset& ruleset);

// The checks of the three sections for RulesetStore (all_species' wake-up rules are
// game/evolution's).
core::Result<void> checkSpeciesExtraSection(const Section& section);

// The patches applied to the disc's tables. An id the table lacks is an error. A path row with no
// free slot left grows: the disc's rows keep their slots and order, new targets go after.
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
