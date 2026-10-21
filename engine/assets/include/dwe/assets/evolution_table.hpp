#pragma once

#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/table_reference.hpp>
#include <dwe/core/error.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace dwe::assets {

class VirtualFileSystem;

// The evolution tables, `data/evolution` (content/schemas/evolution.schema.json): the executable's EVOLUTION_REQS,
// EVOLUTION_STATS_GAINS and EVOLUTION_PATHS (evl.h), and, when the extractor writes it, EVOLUTION_ITEM_TARGET.
// Numbers are kept as the extractor wrote them; -1 is "no requirement".

inline constexpr std::size_t EvolutionPathSources = 5;
inline constexpr std::size_t EvolutionPathTargets = 6;

// What a Digimon needs to evolve into the target (EvoRequirements), indexed by the target's type.
struct EvolutionRequirementEntry {
    TableReference target;
    std::int32_t index = 0;
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

// The reference stats of the target (EvoStatsGains) and the form that is really loaded, indexed by the target's type.
struct EvolutionStatsGainEntry {
    TableReference target;
    std::int32_t index = 0;
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
    std::int32_t index = 0;
    std::array<TableReference, EvolutionPathSources> from{};
    std::array<TableReference, EvolutionPathTargets> to{};
};

// The form an evolution item leads to (EVOLUTION_ITEM_TARGET): optional, the extractor does not write it yet.
struct EvolutionItemTargetEntry {
    TableReference item;
    std::int32_t index = 0;
    TableReference target;
};

struct EvolutionTable {
    std::vector<EvolutionRequirementEntry> requirements;
    std::vector<EvolutionStatsGainEntry> statsGains;
    std::vector<EvolutionPathEntry> paths;
    std::vector<EvolutionItemTargetEntry> itemTargets;
};

// Parses the text of `data/evolution`. Unknown and missing keys are errors; `item_targets` may be left out.
core::Result<EvolutionTable> parseEvolutionTable(std::string_view yaml);

// Reads and parses an evolution table of the packs, `data/evolution` or the one a ruleset names.
core::Result<EvolutionTable> loadEvolutionTable(const VirtualFileSystem& vfs, const AssetId& id);

} // namespace dwe::assets
