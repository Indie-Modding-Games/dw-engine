#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/battle/combatant.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::session {

// The section of the partner's moves at the start and after a rebirth (rules/<id>/partner_moves.yaml).
inline constexpr std::string_view PartnerMovesSection = "partner_moves";

// A position in a species' move list (data/digimon: moves).
using MoveSlot = std::int32_t;

// The partner's moves, as the original keeps them: the learned ones by move id (utils2.c: learnMove, the
// learnedMoves bits) and the equipped ones by their position in the species' move list (stats.base.moves, 0x2E plus
// the position), so after an evolution each position names the new species' move. The fourth is the finisher.
struct PartnerMoves {
    // Sorted, without repeats.
    std::vector<std::string> learned;
    std::array<std::optional<MoveSlot>, game::battle::EquippedSlots> equipped{};

    friend bool operator==(const PartnerMoves&, const PartnerMoves&) = default;
};

// Which positions the partner equips when it starts and when it is reborn.
struct PartnerMoveRules {
    // partner_impl.c: initializePartner: the first technique, which it also learns.
    MoveSlot newGameSlot = 0;
    // evolution.c: reincarnatePartner: the reborn partner's only equipped move; it keeps the learned ones.
    MoveSlot rebornSlot = 0;
};

core::Result<PartnerMoveRules> readPartnerMoveRules(const ruleset::Section& section);
core::Result<void> checkPartnerMovesSection(const ruleset::Section& section);

// The moves' save form: {learned: [ids], equipped: [position or null, x4]}.
core::YamlNode toYaml(const PartnerMoves& moves);
core::Result<PartnerMoves> partnerMovesFromYaml(const core::YamlNode& node);

// Adds a learned move, keeping the list sorted; false if it was known.
bool learn(PartnerMoves& moves, std::string_view move);
[[nodiscard]] bool knows(const PartnerMoves& moves, std::string_view move);

} // namespace dwe::session
