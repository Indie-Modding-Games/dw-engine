#pragma once

#include <dwe/game/battle/folder.hpp>
#include <dwe/game/battle/state.hpp>

#include <memory>

namespace dwe::game::battle {

struct BattleContext;

// The rules that differ between the battle systems (docs/architecture/folder-battle.md): who
// decides what each fighter does on a frame, and how an attack that reaches a fighter resolves.
// Everything else of the frame (walking, animations, effects, statuses, the finisher, items) is
// shared.
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
  // The decisions of the frame, before the fighters act.
  virtual void tickDecisions(BattleContext& context) = 0;
  // An attack reached its defender (BTL_tickAttackHits).
  virtual void handleAttack(BattleContext& context, const AttackInFlight& attack) = 0;
  // After the fighters acted and the damage drained.
  virtual void endFrame(BattleContext& context) = 0;
};

std::unique_ptr<BattleSystem> makeClassicBattle();
std::unique_ptr<BattleSystem> makeFolderBattle();

} // namespace dwe::game::battle
