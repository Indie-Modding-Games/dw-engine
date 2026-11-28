#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/battle/battle_system_id.hpp>
#include <dwe/game/battle/state.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace dwe::game::battle {

struct BattleContext;

// What differs between battle systems (docs/architecture/folder-battle.md): commands, decisions and
// how a reaching attack resolves; the rest of the frame is shared (mechanics/). Each lives in
// classic/ or folder/.
class BattleSystem {
public:
  BattleSystem() = default;
  BattleSystem(const BattleSystem&) = delete;
  BattleSystem& operator=(const BattleSystem&) = delete;
  BattleSystem(BattleSystem&&) = delete;
  BattleSystem& operator=(BattleSystem&&) = delete;
  virtual ~BattleSystem() = default;

  [[nodiscard]] virtual BattleSystemId id() const = 0;
  // Once, after the fighters are placed.
  virtual void start(BattleContext& context) = 0;
  // A strip command by the trainer of `side`, once in the menu; flee is the battle's own
  // (Battle::command).
  virtual core::Result<void> command(BattleContext& context, Command command, FighterId side) = 0;
  // The decisions of the frame, before the fighters act.
  virtual void tickDecisions(BattleContext& context) = 0;
  // An attack reached its defender (BTL_tickAttackHits).
  virtual void handleAttack(BattleContext& context, const AttackInFlight& attack) = 0;
  // After the fighters acted and the damage drained.
  virtual void endFrame(BattleContext& context) = 0;
  // The MP a commanded fighter pays for `move` as it starts it: by default the original's, the
  // table's cost with the partner's discount and surcharge (BTL_setupMoveExecution).
  [[nodiscard]] virtual std::int32_t commandedMpCost(const BattleContext& context, FighterId id,
                                                     MoveRef move) const;
  // Whom the attack `id` releases now reaches, given the shared answer: by default that answer.
  [[nodiscard]] virtual std::vector<FighterId> reached(const BattleContext& context, FighterId id,
                                                       MoveRef move,
                                                       std::vector<FighterId> shared) const;
  // Whether the effect of `attacker`'s `move` may touch `other` on its way: by default any foe
  // (BTL_applyRadiusAttackHit).
  [[nodiscard]] virtual bool touches(const BattleContext& context, FighterId attacker,
                                     FighterId other, MoveRef move) const;
};

[[nodiscard]] std::unique_ptr<BattleSystem> makeBattleSystem(BattleSystemId id);

} // namespace dwe::game::battle
