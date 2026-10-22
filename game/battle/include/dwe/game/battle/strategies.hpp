#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/battle/moves.hpp>
#include <dwe/game/battle/rules.hpp>
#include <dwe/game/battle/state.hpp>
#include <dwe/ruleset/random.hpp>
#include <dwe/ruleset/ruleset.hpp>
#include <dwe/ruleset/strategy.hpp>

#include <cstdint>
#include <memory>
#include <optional>

namespace dwe::game::battle {

// What a strategy may look at, and the dice it rolls with.
struct BattleView {
  const BattleRules* rules = nullptr;
  const MoveCatalog* moves = nullptr;
  const BattleState* state = nullptr;
  ruleset::Random* random = nullptr;
  // The partner is sick (PARTNER_PARA.condition & 0x40) or injured (& 0x20).
  bool partnerSick = false;
  bool partnerInjured = false;
};

// A decision about the next move: a slot to queue, or a wait with no move (cooldown and charge).
struct MoveDecision {
  std::optional<std::size_t> slot;
};

// How a fighter picks a move among its usable ones for the partner's commands.
enum class MoveStyle : std::uint8_t { YourCall, Strongest, Cheapest };

// The partner's own decisions (battle_main.c: BTL_selectPartnerMove, BTL_selectMoveByPower,
// BTL_selectMoveByMpCost, BTL_selectFighterTarget).
class PartnerAi {
public:
  PartnerAi() = default;
  PartnerAi(const PartnerAi&) = delete;
  PartnerAi& operator=(const PartnerAi&) = delete;
  PartnerAi(PartnerAi&&) = delete;
  PartnerAi& operator=(PartnerAi&&) = delete;
  virtual ~PartnerAi() = default;
  // The move to queue in `style`, given which slots are usable.
  virtual MoveDecision chooseMove(const BattleView& view, MoveStyle style,
                                  const std::array<bool, EquippedSlots>& usable) = 0;
  // The target the partner picks now; nullopt keeps none.
  virtual std::optional<FighterId> chooseTarget(const BattleView& view) = 0;
};

// An enemy's decisions (battle_main.c: BTL_selectEnemyMove).
class EnemyAi {
public:
  EnemyAi() = default;
  EnemyAi(const EnemyAi&) = delete;
  EnemyAi& operator=(const EnemyAi&) = delete;
  EnemyAi(EnemyAi&&) = delete;
  EnemyAi& operator=(EnemyAi&&) = delete;
  virtual ~EnemyAi() = default;
  virtual MoveDecision chooseMove(const BattleView& view, FighterId self,
                                  const std::array<bool, EquippedSlots>& usable) = 0;
};

// The chance, 0 to 100, that an attack hits, and whether it breaks the defender's block first (a
// finisher does).
struct HitChance {
  std::int32_t percent = 0;
  bool breaksBlock = false;
};

// The chance to hit and the damage (battle_main.c: BTL_calculateHitChance, BTL_calculateDamage).
class DamageFormula {
public:
  DamageFormula() = default;
  DamageFormula(const DamageFormula&) = delete;
  DamageFormula& operator=(const DamageFormula&) = delete;
  DamageFormula(DamageFormula&&) = delete;
  DamageFormula& operator=(DamageFormula&&) = delete;
  virtual ~DamageFormula() = default;
  virtual HitChance hitChance(const BattleView& view, FighterId attacker, FighterId defender,
                              MoveRef move) = 0;
  // The damage `move` does, rolls included.
  virtual std::int32_t damage(const BattleView& view, FighterId attacker, FighterId defender,
                              MoveRef move) = 0;
};

// Who obeys whom (battle_main.c: BTL_tickPartnerAI, BTL_tickEnemyAI, BTL_retargetAfterHit,
// BTL_selectFighterTarget; battle_tick.c: handleCommands).
class Obedience {
public:
  Obedience() = default;
  Obedience(const Obedience&) = delete;
  Obedience& operator=(const Obedience&) = delete;
  Obedience(Obedience&&) = delete;
  Obedience& operator=(Obedience&&) = delete;
  virtual ~Obedience() = default;
  // Whether `fighter` starts to ignore everything this frame.
  virtual bool disobeys(const BattleView& view, FighterId fighter, std::int32_t discipline) = 0;
  // The frames a command takes to reach the partner.
  virtual std::int32_t commandDelay(const BattleView& view, std::int32_t discipline) = 0;
  // Whether the partner turns to whoever hit it.
  virtual bool partnerTurnsToAttacker(const BattleView& view, std::int32_t discipline) = 0;
  // Whether an enemy turns to whoever hit it rather than the partner.
  virtual bool enemyTurnsToAttacker(const BattleView& view, FighterId enemy) = 0;
};

// The strategies a battle plays with.
struct BattleStrategies {
  std::unique_ptr<PartnerAi> partnerAi;
  std::unique_ptr<EnemyAi> enemyAi;
  std::unique_ptr<DamageFormula> damage;
  std::unique_ptr<Obedience> obedience;
};

// The registries of the four strategies, with `original` registered. A test or a mod adds more.
ruleset::StrategyRegistry<PartnerAi>& partnerAiStrategies();
ruleset::StrategyRegistry<EnemyAi>& enemyAiStrategies();
ruleset::StrategyRegistry<DamageFormula>& damageStrategies();
ruleset::StrategyRegistry<Obedience>& obedienceStrategies();

// The strategy paths of the section.
inline constexpr std::string_view PartnerAiPath = "strategies.partner_ai";
inline constexpr std::string_view EnemyAiPath = "strategies.enemy_ai";
inline constexpr std::string_view DamagePath = "strategies.damage";
inline constexpr std::string_view ObediencePath = "strategies.obedience";

// Checks that every strategy the section names is registered.
core::Result<void> checkStrategyNames(const ruleset::Section& section);

// Builds the strategies the section names.
core::Result<BattleStrategies> createStrategies(const ruleset::Section& section);

// The original strategies, to build a battle without a ruleset section (tests, tools).
BattleStrategies originalStrategies();

} // namespace dwe::game::battle
