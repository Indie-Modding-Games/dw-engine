#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/evolution/inputs.hpp>
#include <dwe/game/evolution/result.hpp>

#include <cstdint>
#include <set>

namespace dwe::game::evolution {

class EvolutionTables;

// The version of the save section of the evolution state, for the save system's migrations: toYaml writes and
// fromYaml reads this one.
inline constexpr std::int32_t EvolutionStateVersion = 1;

// What the evolution rules remember beyond the partner's own state. The partner owns the evolution counters and the
// virus backup.
struct EvolutionState {
    // The forms raised at least once (setDigimonRaised), which the scoring prefers to leave for new ones.
    std::set<Species> raised;
    // Whether an evolution item was used in this session (HAS_USED_EVOITEM). Like the original, it is not saved: it
    // only goes back to false when the game starts.
    bool evolutionItemUsed = false;

    friend bool operator==(const EvolutionState&, const EvolutionState&) = default;
};

// Records an evolution the session applied: the new form is raised and the item flag follows the result.
void recordEvolution(EvolutionState& state, const EvolutionResult& result);

// The saved form of the state, the raised forms by table id, in the section version EvolutionStateVersion: for the
// session to register in the save.
core::Result<core::YamlNode> toYaml(const EvolutionState& state, const EvolutionTables& tables);

// Reads a saved state; an unknown form or a version this build does not read is an error.
core::Result<EvolutionState> fromYaml(const core::YamlNode& node, const EvolutionTables& tables);

} // namespace dwe::game::evolution
