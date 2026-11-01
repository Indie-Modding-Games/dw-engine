#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/core/error.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/battle/combatant.hpp>
#include <dwe/game/battle/moves.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::session {

// The section of the partner's moves at the start and after a rebirth
// (rules/<id>/partner_moves.yaml).
inline constexpr std::string_view PartnerMovesSection = "partner_moves";

// A position in a species' move list (data/digimon: moves).
using MoveSlot = std::int32_t;

// The partner's moves, as the original keeps them: the learned ones by move id (utils2.c:
// learnMove, the learnedMoves bits) and the equipped ones by their position in the species' move
// list (stats.base.moves, 0x2E plus the position), so after an evolution each position names the
// new species' move. The fourth is the finisher.
struct PartnerMoves {
  // Sorted, without repeats.
  std::vector<std::string> learned;
  std::array<std::optional<MoveSlot>, game::battle::EquippedSlots> equipped{};

  friend bool operator==(const PartnerMoves&, const PartnerMoves&) = default;
};

// Las variantes de animación comparten aprendizaje y selección con su técnica base.
[[nodiscard]] bool knowsTechnique(const PartnerMoves& moves,
                                  const game::battle::MoveCatalog& catalog, std::string_view id);

// Operaciones del editor: alternar una técnica compatible/aprendida, sin tocar el final.
enum class TechniqueToggle { Equipped, Removed, Unavailable, Full };
TechniqueToggle toggleTechnique(PartnerMoves& moves, const assets::DigimonRow& species,
                                const game::battle::MoveCatalog& catalog, MoveSlot slot);
[[nodiscard]] bool canFinishTechniqueEdit(const PartnerMoves& moves,
                                          const assets::DigimonRow& species,
                                          const game::battle::MoveCatalog& catalog);

// Which positions the partner equips when it starts and when it is reborn.
struct PartnerMoveRules {
  // partner_impl.c: initializePartner: the first technique, which it also learns.
  MoveSlot newGameSlot = 0;
  // evolution.c: reincarnatePartner: the reborn partner's only equipped move; it keeps the learned
  // ones.
  MoveSlot rebornSlot = 0;
};

core::Result<PartnerMoveRules> readPartnerMoveRules(const ruleset::Section& section);
core::Result<void> checkPartnerMovesSection(const ruleset::Section& section);

// The moves' save form: {learned: [ids], equipped: [position or null, x4]}.
core::YamlNode toYaml(const PartnerMoves& moves);
core::Result<PartnerMoves> partnerMovesFromYaml(const core::YamlNode& node);

// script_common.c: lostAllLives: the reborn partner may forget the moves numbered below this.
inline constexpr std::int32_t ForgettableMoves = 0x3A;

// lostAllLives: how many of the moves it knows the reborn partner forgets: (10 - trainer level) * 2
// percent of them, rounded half up; none at trainer level 10 or more.
[[nodiscard]] std::int32_t movesToForget(std::int32_t known, std::int32_t trainerLevel);

// utils2.c: unlearnMove: the original numbers whose learned bits forgetting a move clears. The
// mask is 16 bits wide, so the upper sixteen moves of the move's word of 32 always go with it (an
// original bug); 0x30 clears 0x2C's bit and 0x37 and 0x39 only that upper half.
[[nodiscard]] std::vector<std::int32_t> movesClearedBy(std::int32_t number);

// Adds a learned move, keeping the list sorted; false if it was known.
bool learn(PartnerMoves& moves, std::string_view move);
// Removes a learned move; false if it was not known.
bool forget(PartnerMoves& moves, std::string_view move);
[[nodiscard]] bool knows(const PartnerMoves& moves, std::string_view move);

} // namespace dwe::session
