#pragma once

#include <dwe/core/error.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace dwe::assets {
struct DigimonRow;
struct MapNpc;
} // namespace dwe::assets

namespace dwe::game::battle {

// The equipped moves of a fighter: three techniques and, in the fourth slot, the finisher.
inline constexpr std::size_t TechniqueSlots = 3;
inline constexpr std::size_t EquippedSlots = 4;
inline constexpr std::size_t FinisherSlot = 3;
inline constexpr std::size_t SpeciesSpecials = 3;
inline constexpr std::size_t SpeciesMoves = 16;

// How the map's NPC records name an equipped move: the first move animation plus the index of the move in the
// species' list, or an empty slot (overworld.c: loadMapDigimon).
inline constexpr std::int32_t FirstMoveAnimation = 0x2E;
inline constexpr std::int32_t EmptyMoveAnimation = 0xFF;

// Digimon types (DIGIMON_DATA type).
enum class DigimonType : std::uint8_t { None = 0, Data = 1, Vaccine = 2, Virus = 3 };

// How a fighter paces itself between attacks (battle_main.c: BTL_applyChargeRequirement).
enum class ChargeMode : std::uint8_t { Aggressive = 0, Normal = 1, Cautious = 2 };

// A point on the battle ground, in world units (the original's X and Z).
struct Point {
    std::int32_t x = 0;
    std::int32_t z = 0;

    friend bool operator==(const Point&, const Point&) = default;
};

struct CombatStats {
    std::int32_t maxHp = 0;
    std::int32_t maxMp = 0;
    std::int32_t offense = 0;
    std::int32_t defense = 0;
    std::int32_t speed = 0;
    std::int32_t brain = 0;

    friend bool operator==(const CombatStats&, const CombatStats&) = default;
};

// Everything the battle needs to know about one fighter before it starts. The session builds the partner's from the
// partner state; enemies come from the Digimon table and the map's NPC records (enemyFromMap).
struct Combatant {
    // The species: its id in the Digimon table.
    std::string species;
    std::int32_t level = 0;
    DigimonType type = DigimonType::None;
    std::array<std::optional<std::int32_t>, SpeciesSpecials> specials{};
    std::int32_t radius = 0;
    CombatStats stats;
    std::int32_t hp = 0;
    std::int32_t mp = 0;
    // Move ids from the moves table; the fourth is the finisher.
    std::array<std::optional<std::string>, EquippedSlots> moves{};
    // How much an enemy likes each slot (NPC movesPriority); unused for the partner.
    std::array<std::int32_t, EquippedSlots> movePriorities{};
    ChargeMode chargeMode = ChargeMode::Normal;
    // An enemy that only ever targets the partner (NPC flag at 0x2A).
    bool focusesPartner = false;
    // What beating it gives.
    std::int32_t bits = 0;
    std::optional<std::string> dropItem;
    std::int32_t dropChance = 0;
    // The species' move list, for learning in battle.
    std::array<std::optional<std::string>, SpeciesMoves> speciesMoves{};
    // Where it stands when the battle starts.
    Point position;
};

// A combatant with the species' fields of a Digimon table entry filled in, and no stats.
Combatant combatantOf(const assets::DigimonRow& species);

// An enemy as the map places it: the species from the Digimon table and the stats, moves, priorities, charge mode
// and bits of its NPC record.
core::Result<Combatant> enemyFromMap(const assets::DigimonRow& species, const assets::MapNpc& npc);

} // namespace dwe::game::battle
