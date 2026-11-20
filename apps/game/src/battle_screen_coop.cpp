// The battle screen's part in a co-op battle (docs/architecture/multiplayer.md, §6 decision 3): an
// engine addition, the original has no online play. The battle runs here, on the host; the other
// player's commands, cheers and items arrive from its own game.
#include <dwe/core/log.hpp>
#include <dwe/game/battle/battle.hpp>
#include <dwe/session/battle_adapter.hpp>
#include <dwe/session/inventory_adapter.hpp>

#include <string_view>

#include "battle_screen.hpp"

namespace dwe::app {

game::coop::BattlePhase BattleScreen::coopPhase() const {
  if (intro_) {
    return game::coop::BattlePhase::Intro;
  }
  if (battle_) {
    return game::coop::BattlePhase::Fight;
  }
  if (victoryPoseSeconds_ || matchEndSeconds_) {
    return game::coop::BattlePhase::Victory;
  }
  if (ending_) {
    return game::coop::BattlePhase::End;
  }
  return game::coop::BattlePhase::Closing;
}

bool BattleScreen::commandFromVisitor(game::battle::Command command) {
  if (!battle_ || intro_ || !visiting_) {
    return false;
  }
  if (auto taken = battle_->command(command); !taken) {
    core::logInfo("coop: the visitor's command {} was refused: {}", static_cast<int>(command),
                  taken.error().message);
    return false;
  }
  shout(command);
  publish();
  return true;
}

void BattleScreen::cheer() {
  if (battle_ && !intro_) {
    battle_->pressFinisherCharge();
  }
}

bool BattleScreen::throwLentItem(std::string_view itemId) {
  if (!battle_ || intro_ || desc_.inventory == nullptr) {
    return false;
  }
  const std::optional<assets::ItemHandle> item = desc_.inventory->items().handleOf(itemId);
  if (!item) {
    core::logInfo("coop: the other player's item {} is not in this game's items", itemId);
    return false;
  }
  game::inventory::Inventory& bag = desc_.inventory->inventory();
  if (!bag.add(*item, 1).taken()) {
    core::logInfo("coop: no room to lend the other player's item {}", itemId);
    return false;
  }
  auto thrown = desc_.battle->throwItem(*battle_, *item);
  if (!thrown || thrown->outcome != game::inventory::UseOutcome::Used) {
    bag.remove(*item, 1);
    core::logInfo("coop: the other player's item {} was not thrown{}", itemId,
                  thrown ? std::string{} : ": " + thrown.error().message);
    return false;
  }
  core::logInfo("coop: the other player threw {}", itemId);
  startThrow(*item);
  publish();
  return true;
}

} // namespace dwe::app
