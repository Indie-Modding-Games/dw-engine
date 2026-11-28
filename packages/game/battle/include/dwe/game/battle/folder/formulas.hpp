#pragma once

#include <dwe/game/battle/combatant.hpp>
#include <dwe/game/battle/moves.hpp>
#include <dwe/game/battle/rules.hpp>

#include <array>
#include <cstdint>
#include <optional>

// Every Folder battle formula (combat-design.md ch. 6, §2.9, §3.8); stats read out of `stat_scale`
// (§6.3).
namespace dwe::game::battle::folder {

// §6.4: the walking speed in percent of the base speed (75 to 125).
[[nodiscard]] std::int32_t movementPercent(const FolderRules& rules, std::int32_t vel);
// §6.4: a technique's preparation frames, VEL shortening the base by up to 40 %.
[[nodiscard]] std::int32_t executionFrames(const FolderRules& rules, std::int32_t base,
                                           std::int32_t vel);
// §6.5: an order's processing frames, INT shortening the base by up to 50 %.
[[nodiscard]] std::int32_t processingFrames(const FolderRules& rules, std::int32_t base,
                                            std::int32_t intelligence);

// §3.8: +elementPercent per defender attribute the move's beats, -elementPercent per one beating it
// (Holy/Dark beat each other), and STAB when the attacker has the move's attribute; percent of
// power.
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
[[nodiscard]] std::int32_t brokenStat(const FolderRules& rules, std::int32_t stat);
// §7.4: the points the cover chance gains when the attack comes from `distance` pixels away.
[[nodiscard]] std::int32_t coverDistanceBonus(const FolderRules& rules, std::int64_t distance);

// §3.2, §3.10, §7.5 and costo-mp.md: a technique as Folder plays it.
struct Technique {
  TechniqueFunctions functions = 0;
  AreaShape area = AreaShape::Single;
  // The area's radius in pixels (none for Single or Global).
  std::int32_t areaPixels = 0;
  // §2.8: whether it also hits the user's own side.
  bool friendlyFire = false;
  ReachClass reach = ReachClass::Short;
  AreaClass areaClass = AreaClass::Single;
  // The trainer's cost, before a sick or injured partner's surcharge; 0 for a finisher.
  std::int32_t mpCost = 0;

  [[nodiscard]] bool serves(TechniqueFunction function) const {
    return (functions & static_cast<TechniqueFunctions>(function)) != 0;
  }
  friend bool operator==(const Technique&, const Technique&) = default;
};

// The move `ref` as Folder plays it: the move table read by the design's rules (an area move is an
// area around its user as wide as its distance, an offensive area hits allies too), then the
// technique's `catalog` entry. A move's cost comes from its table entry, so a species variant
// costs the same; a boosted move keeps the boost's discount.
[[nodiscard]] Technique techniqueOf(const FolderRules& rules, const MoveCatalog& moves,
                                    MoveRef ref);
// costo-mp.md §2: the cost of a technique with these parts.
[[nodiscard]] std::int32_t mpCostOf(const FolderRules& rules, const BattleMove& move,
                                    const Technique& technique, EffectDuration effectDuration);

} // namespace dwe::game::battle::folder
