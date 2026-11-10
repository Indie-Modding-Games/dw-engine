#pragma once

#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/table_composition.hpp>
#include <dwe/assets/table_reference.hpp>
#include <dwe/core/error.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace dwe::assets {

class VirtualFileSystem;

// The evolution tables, `data/evolution` (content/schemas/evolution.schema.json): the executable's
// EVOLUTION_REQS, EVOLUTION_STATS_GAINS and EVOLUTION_PATHS (evl.h), and, when the extractor writes
// it, EVOLUTION_ITEM_TARGET. Numbers are kept as the extractor wrote them; -1 is "no requirement".

inline constexpr std::size_t EvolutionPathSources = 5;
inline constexpr std::size_t EvolutionPathTargets = 6;

// What a Digimon needs to evolve into the target (EvoRequirements), indexed by the target's type.
struct EvolutionRequirementEntry {
  TableReference target;
  // The place of the entry the row belongs to in the original (TableComposition: unnumbered); none
  // for a pack's new row.
  std::optional<std::int32_t> index;
  // The form it must come from for the bonus point, or nothing.
  TableReference digimon;
  std::int32_t hp = 0;
  std::int32_t mp = 0;
  std::int32_t offense = 0;
  std::int32_t defense = 0;
  std::int32_t speed = 0;
  std::int32_t brain = 0;
  std::int32_t care = 0;
  std::int32_t weight = 0;
  std::int32_t discipline = 0;
  std::int32_t happiness = 0;
  std::int32_t battles = 0;
  std::int32_t techs = 0;
  // 0x01: battles is a maximum; 0x10: care mistakes is a maximum.
  std::int32_t flags = 0;
};

// The reference stats of the target (EvoStatsGains) and the form that is really loaded, indexed by
// the target's type.
struct EvolutionStatsGainEntry {
  TableReference target;
  // The place of the entry the row belongs to in the original (TableComposition: unnumbered); none
  // for a pack's new row.
  std::optional<std::int32_t> index;
  std::int32_t hp = 0;
  std::int32_t mp = 0;
  std::int32_t offense = 0;
  std::int32_t defense = 0;
  std::int32_t speed = 0;
  std::int32_t brains = 0;
  TableReference targetDigimon;
};

// The evolution chart of a Digimon (EvolutionPath): entry i belongs to type i + 1.
struct EvolutionPathEntry {
  TableReference digimon;
  // The place of the entry the row belongs to in the original (TableComposition: unnumbered); none
  // for a pack's new row.
  std::optional<std::int32_t> index;
  // The disc's rows have EvolutionPathSources and EvolutionPathTargets slots; a ruleset's patches
  // may add more (ruleset/species_extra.hpp).
  std::vector<TableReference> from = std::vector<TableReference>(EvolutionPathSources);
  std::vector<TableReference> to = std::vector<TableReference>(EvolutionPathTargets);
};

// The form an evolution item leads to, written by hand: optional, it overrides the item effects'
// evolves_to (EVOLUTION_ITEM_TARGET), which the extractor writes.
struct EvolutionItemTargetEntry {
  TableReference item;
  // The place of the entry the row belongs to in the original (TableComposition: unnumbered); none
  // for a pack's new row.
  std::optional<std::int32_t> index;
  TableReference target;
};

struct EvolutionTable {
  std::vector<EvolutionRequirementEntry> requirements;
  std::vector<EvolutionStatsGainEntry> statsGains;
  std::vector<EvolutionPathEntry> paths;
  std::vector<EvolutionItemTargetEntry> itemTargets;
};

// How the packs' files of the table compose: requirements and stats gains by their target Digimon,
// paths by their Digimon and item targets by their item (table_composition.hpp).
inline constexpr std::array<EntrySection, 4> EvolutionEntrySections{{
    {.section = "requirements", .entryKey = "target", .numbered = false, .ownsIds = false},
    {.section = "stats_gains", .entryKey = "target", .numbered = false, .ownsIds = false},
    {.section = "paths", .entryKey = "digimon", .numbered = false, .ownsIds = false},
    {.section = "item_targets", .entryKey = "item", .numbered = false, .ownsIds = false},
}};
inline constexpr TableComposition EvolutionComposition{.sections = EvolutionEntrySections};

// Parses the text of `data/evolution`. Unknown and missing keys are errors; `item_targets` may be
// left out.
core::Result<EvolutionTable> parseEvolutionTable(std::string_view yaml);

// Reads and parses an evolution table of the packs, `data/evolution` or the one a ruleset names.
core::Result<EvolutionTable> loadEvolutionTable(const VirtualFileSystem& vfs, const AssetId& id);

} // namespace dwe::assets
