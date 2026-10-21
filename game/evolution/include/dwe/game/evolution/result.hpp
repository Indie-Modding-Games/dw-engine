#pragma once

#include <dwe/game/evolution/inputs.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace dwe::game::evolution {

// Why the partner evolved.
enum class EvolutionReason : std::uint8_t {
    // The periodic check: a fixed baby target, a scored candidate or the rookie fallback.
    Normal,
    // A special rule: on waking up, after a praise or a scold, or at the special counter hour.
    Special,
    // An evolution item.
    Item,
    // The virus bar filled up.
    Virus,
    // An event script: back from the virus form, or a form the script names.
    Script,
    // A new baby after death.
    Rebirth,
};

inline constexpr std::size_t EquippedMoveSlots = 4;

// The four equipped move slots. Each holds a slot of the species' move list (the move of animation 0x2E + slot), or
// nothing for an empty one.
using EquippedMoves = std::array<std::optional<std::int32_t>, EquippedMoveSlots>;

// What an evolution does to the partner: the session applies it to the partner's state and records it with
// recordEvolution. The evolution does not cap the stats: the partner does, and the current HP and MP are capped at the
// new maximums.
struct EvolutionResult {
    // The form that is loaded (EVOLUTION_STATS_GAINS[target].targetDigimon).
    Species target = 0;
    EvolutionReason reason = EvolutionReason::Normal;
    // How much each base stat changes, uncapped (negative when a scaling lowers it): the session adds it through the
    // partner's stat gains, which cap each gain and each stat (docs/reference/evolution.md, "Topes").
    BaseStats statChange{};
    // Leaving the virus form: the base stats rebuilt from the virus backup, unclamped here. The session sets them
    // instead of adding statChange and then clamps each stat to its maximum (care.limits), like EVL_clampBaseStats,
    // without the per-gain cap (design decision of 2026-10-20).
    std::optional<BaseStats> restoredStats = std::nullopt;
    std::int32_t weight = 0;
    std::int32_t careMistakes = 0;
    std::int32_t battles = 0;
    // The evolution counter after evolving: it restarts only when the level changes.
    std::int32_t evolutionTimer = 0;
    // Hours added to the remaining lifetime.
    std::int32_t lifetimeBonusHours = 0;
    // Percent chance that the tamer gains a level.
    std::int32_t tamerLevelChance = 0;
    // The move the new form learns: an index of the move table.
    std::optional<std::int32_t> learnedMove = std::nullopt;
    EquippedMoves equippedMoves{};
    // Whether the virus bar goes back to 0.
    bool restartsVirusBar = false;
    // What to keep when turning into the virus form: the session stores the form and the stats where the partner's
    // virus backup lives, and passes them back in EvolutionInputs::virusBackup.
    std::optional<VirusBackup> virusBackup = std::nullopt;
    // Whether an evolution item has been used in this session after this evolution.
    bool evolutionItemUsed = false;
};

// The result of the periodic check: the evolution counter after it, and the evolution if there is one.
struct NaturalCheck {
    std::int32_t evolutionTimer = 0;
    std::optional<EvolutionResult> evolution = std::nullopt;
};

} // namespace dwe::game::evolution
