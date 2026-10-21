#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/evolution_table.hpp>
#include <dwe/assets/move_table.hpp>
#include <dwe/assets/raise_table.hpp>
#include <dwe/core/error.hpp>
#include <dwe/game/evolution/inputs.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace dwe::assets {
class VirtualFileSystem;
} // namespace dwe::assets

namespace dwe::ruleset {
class Ruleset;
} // namespace dwe::ruleset

namespace dwe::game::evolution {

// The table roles of the ruleset manifest that evolution reads (`tables:`).
inline constexpr std::string_view DigimonTableRole = "digimon";
inline constexpr std::string_view RaiseTableRole = "raise";
inline constexpr std::string_view MoveTableRole = "moves";
inline constexpr std::string_view EvolutionTableRole = "evolution";

// "No requirement" in the requirement table.
inline constexpr std::int32_t NoRequirement = -1;
// In a rookie target's requirement, this stat value marks a favoured stat.
inline constexpr std::int32_t FavouredStat = 1;
// The bits of a requirement's flags.
inline constexpr std::int32_t BattlesAreMaximumFlag = 0x01;
inline constexpr std::int32_t CareIsMaximumFlag = 0x10;

// What evolution needs of a Digimon type.
struct SpeciesData {
    std::string id;
    Level level = Level::None;
    // Its first element type, which picks the move it learns on evolving; nothing for none.
    std::optional<std::int32_t> firstSpecialty;
    // The move of each of its 16 slots, as an index of the move table, or nothing for an empty slot.
    std::array<std::optional<std::int32_t>, std::tuple_size_v<decltype(assets::DigimonRow::moves)>> moves{};
    // RAISE_DATA's default weight; 0 when the raise table has no entry.
    std::int32_t defaultWeight = 0;
};

// What a Digimon needs to evolve into a target (EvoRequirements), with species resolved.
struct Requirement {
    // The form it must come from for the bonus point, or nothing.
    std::optional<Species> from;
    // Minimum stats (HP and MP divided), or, for a rookie target, FavouredStat on its favoured stats; NoRequirement
    // for none.
    BaseStats minimum;
    std::int32_t careMistakes = NoRequirement;
    std::int32_t weight = NoRequirement;
    std::int32_t discipline = NoRequirement;
    std::int32_t happiness = NoRequirement;
    std::int32_t battles = NoRequirement;
    std::int32_t learnedMoves = NoRequirement;
    bool careIsMaximum = false;
    bool battlesAreMaximum = false;
};

// The reference stats of a target (EvoStatsGains) and the form that is really loaded.
struct StatReference {
    BaseStats stats{};
    Species loads = 0;
};

// The base pack's tables, resolved for the evolution rules: species, requirements, paths, reference stats, move
// powers and evolution items, all by the indices the original uses.
class EvolutionTables {
public:
    // Resolves the tables: every id they refer to must be in its table. References the extractor wrote as raw numbers
    // outside the Digimon table (a path's -2) are left out.
    static core::Result<EvolutionTables> build(const assets::DigimonTable& digimon, const assets::RaiseTable& raise,
                                               const assets::MoveTable& moves, const assets::EvolutionTable& evolution);

    // Reads the four tables a ruleset names (digimon, raise, moves, evolution) and resolves them.
    static core::Result<EvolutionTables> load(const assets::VirtualFileSystem& vfs, const ruleset::Ruleset& ruleset);

    // The species with this index, or nullptr.
    [[nodiscard]] const SpeciesData* species(Species species) const;

    // The level of a species; Level::None for an unknown one.
    [[nodiscard]] Level levelOf(Species species) const;

    // The index of the species with this id.
    [[nodiscard]] std::optional<Species> find(std::string_view id) const;

    // The id of a species, or an empty text for an unknown one.
    [[nodiscard]] std::string_view idOf(Species species) const;

    // Every species index, ascending.
    [[nodiscard]] std::vector<Species> allSpecies() const;

    // The requirement to evolve into a target, or nullptr when the table has no row for it.
    [[nodiscard]] const Requirement* requirement(Species target) const;

    // The reference stats of a target, or nullptr when the table has no row for it.
    [[nodiscard]] const StatReference* statReference(Species target) const;

    // The targets of a species' evolution chart, in the table's order, without the empty ones.
    [[nodiscard]] std::span<const Species> pathTargets(Species from) const;

    // The power of a move; 0 for an unknown one.
    [[nodiscard]] std::int32_t movePower(std::int32_t move) const;

    // The form an item of the evolution item table leads to, or nothing.
    [[nodiscard]] std::optional<Species> itemTarget(std::string_view item) const;

private:
    std::map<Species, SpeciesData> species_;
    std::map<std::string, Species, std::less<>> speciesById_;
    std::map<Species, Requirement> requirements_;
    std::map<Species, StatReference> statReferences_;
    std::map<Species, std::vector<Species>> paths_;
    std::map<std::int32_t, std::int32_t> movePowers_;
    std::map<std::string, Species, std::less<>> itemTargets_;
};

} // namespace dwe::game::evolution
