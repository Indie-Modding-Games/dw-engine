#pragma once

#include <dwe/game/battle/combatant.hpp>
#include <dwe/game/battle/moves.hpp>
#include <dwe/game/battle/rules.hpp>

#include <array>
#include <cstdint>
#include <optional>

// Every formula of the Folder battle in one place (combat-design.md chapter 6, §2.9, §3.8), on the
// ruleset's `battle.folder` numbers. Stats are read out of `stat_scale` (§6.3).
namespace dwe::game::battle::folder {

// §6.4: the walking speed in percent of the base speed (75 to 125).
[[nodiscard]] std::int32_t movementPercent(const FolderRules& rules, std::int32_t vel);
// §6.4: a technique's preparation frames, VEL shortening the base by up to 40 %.
[[nodiscard]] std::int32_t executionFrames(const FolderRules& rules, std::int32_t base,
                                           std::int32_t vel);
// §6.5: an order's processing frames, INT shortening the base by up to 50 %.
[[nodiscard]] std::int32_t processingFrames(const FolderRules& rules, std::int32_t base,
                                            std::int32_t intelligence);

// §3.8: the elemental modifier in percent of the technique's power: +elementPercent for each
// attribute of the defender the move's beats, -elementPercent for each that beats the move's (a
// mutual pair, as Holy and Dark, is an advantage both ways), and STAB when the attacker has the
// move's attribute.
using Specials = std::array<std::optional<std::int32_t>, SpeciesSpecials>;
[[nodiscard]] std::int32_t elementPercent(const FolderRules& rules, std::int32_t moveSpecial,
                                          const Specials& attacker, const Specials& defender);
// §3.8: Vaccine > Virus > Data > Vaccine, +-typePercent; None (Free) is neutral.
[[nodiscard]] std::int32_t typePercent(const FolderRules& rules, DigimonType attacker,
                                       DigimonType defender);

// §6.2: (power + ATK modifier + power x modifiers%) - DEF x (cover ? cover_defense_times : 1); at
// least 1, or 0 for a covered hit.
struct DamageInputs {
  std::int32_t power = 0;
  std::int32_t attack = 0;
  std::int32_t modifierPercent = 0;
  std::int32_t defense = 0;
  bool covered = false;
};
[[nodiscard]] std::int32_t damage(const FolderRules& rules, const DamageInputs& inputs);

// §2.9: the passive cover chance in percent outside Guard.
[[nodiscard]] std::int32_t coverPercent(const FolderRules& rules, std::int32_t defense);
// §2.9: the defense bar's capacity for the current DEF (at least 1).
[[nodiscard]] std::int32_t barCapacity(const FolderRules& rules, std::int32_t defense);
// §2.9: how much VEL and DEF (base values) cut the break and the drain time, in percent.
[[nodiscard]] std::int32_t statCutPercent(const FolderRules& rules, std::int32_t vel,
                                          std::int32_t defense);
[[nodiscard]] std::int32_t breakFrames(const FolderRules& rules, std::int32_t vel,
                                       std::int32_t defense);
// §2.9: what the bar drains each frame, so that a full bar empties in the cut drain time.
[[nodiscard]] std::int32_t barDrain(const FolderRules& rules, std::int32_t capacity,
                                    std::int32_t vel, std::int32_t defense);
// §5: the preparation and recovery of `move` (FolderRules::techniques, then its tier).
[[nodiscard]] TechniqueTiming timingOf(const FolderRules& rules, const BattleMove& move);
// §2.9: a stat during the break.
[[nodiscard]] std::int32_t brokenStat(const FolderRules& rules, std::int32_t stat);

} // namespace dwe::game::battle::folder
