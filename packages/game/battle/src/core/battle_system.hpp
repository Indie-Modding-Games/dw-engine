#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/battle/battle_system_id.hpp>
#include <dwe/game/battle/state.hpp>

#include <memory>

namespace dwe::game::battle {

struct BattleContext;

// The rules that differ between the battle systems (docs/architecture/folder-battle.md): what the
// trainer's commands do, who decides what each fighter does on a frame, and how an attack that
// reaches a fighter resolves. Everything else of the frame (walking, animations, effects, statuses,
// the finisher, items) is shared (mechanics/). Each system lives in its own folder: classic/ and
// folder/.
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
  // A command of the strip by the trainer of `side`, once it is in the menu; the flee is the
  // battle's own (Battle::command).
  virtual core::Result<void> command(BattleContext& context, Command command, FighterId side) = 0;
  // The decisions of the frame, before the fighters act.
  virtual void tickDecisions(BattleContext& context) = 0;
  // An attack reached its defender (BTL_tickAttackHits).
  virtual void handleAttack(BattleContext& context, const AttackInFlight& attack) = 0;
  // After the fighters acted and the damage drained.
  virtual void endFrame(BattleContext& context) = 0;
};

// The system a battle plays (battle_systems.cpp).
[[nodiscard]] std::unique_ptr<BattleSystem> makeBattleSystem(BattleSystemId id);

} // namespace dwe::game::battle
