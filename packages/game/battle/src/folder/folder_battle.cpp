#include <dwe/game/battle/folder/formulas.hpp>

#include <memory>

#include "core/battle_system.hpp"
#include "folder/defense/defense.hpp"
#include "folder/fighters.hpp"
#include "folder/folder.hpp"
#include "folder/orders/orders.hpp"
#include "folder/stances/stances.hpp"
#include "folder/strip.hpp"
#include "mechanics/decisions/decisions.hpp"

namespace dwe::game::battle {

namespace {

// The combat design's battle (docs/architecture/folder-battle.md): the trainer's orders drive the
// commanded fighters, the enemies decide as in the original, and the defense bar takes the hits.
class FolderBattle final : public BattleSystem {
public:
  [[nodiscard]] BattleSystemId id() const override { return BattleSystemId::Folder; }

  void start(BattleContext& context) override {
    BattleState& state = *context.state;
    state.folder.emplace();
    state.folder->fighters.resize(state.fighters.size());
    for (FighterId id = 0; id < state.fighters.size(); ++id) {
      Fighter& fighter = state.fighters[id];
      fighter.moveScalePercent = folder::movementPercent(folderRules(context), fighter.stats.speed);
      if (state.commanded(id)) {
        fighter.planned = true;
        fighter.moveRange = -1;
        setTarget(context, id, nearestFoe(context, id));
        CommandState& commands = state.commandsOf(id);
        commands.menu = folderMenu(state, id);
        commands.current = stanceCommand(state.folder->fighters[id].stance);
      }
    }
  }

  void tickDecisions(BattleContext& context) override {
    BattleState& state = *context.state;
    for (FighterId id = 0; id < state.fighters.size(); ++id) {
      if (state.commanded(id) && !state.fighters[id].gone()) {
        tickFolderCommanded(context, id);
      }
    }
    tickEnemyDecisions(context);
  }

  void handleAttack(BattleContext& context, const AttackInFlight& attack) override {
    handleFolderAttack(context, attack);
  }

  core::Result<void> command(BattleContext& context, Command command, FighterId side) override {
    return folderCommand(context, command, side);
  }

  void endFrame(BattleContext& context) override {
    for (FighterId id = 0; id < context.state->fighters.size(); ++id) {
      tickDefense(context, id);
    }
  }
};

} // namespace

std::unique_ptr<BattleSystem> makeFolderBattle() {
  return std::make_unique<FolderBattle>();
}

} // namespace dwe::game::battle
