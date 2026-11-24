#include "folder/strip.hpp"

#include <dwe/game/battle/messages.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "folder/fighters.hpp"
#include "folder/folder.hpp"
#include "mechanics/decisions/decisions.hpp"

namespace dwe::game::battle {

std::vector<Command> folderMenu(const BattleState& state, FighterId id) {
  const Fighter& fighter = state.fighters.at(id);
  std::vector<Command> menu{Command::Finisher};
  if (!state.scripted) {
    menu.push_back(Command::Flee);
  }
  for (const Command command :
       {Command::Change, Command::Defend, Command::Distance, Command::Moderate, Command::Attack}) {
    menu.push_back(command);
  }
  for (const Command technique : {Command::Technique3, Command::Technique2, Command::Technique1}) {
    const auto slot =
        static_cast<std::size_t>(technique) - static_cast<std::size_t>(Command::Technique1);
    if (fighter.moves.at(slot)) {
      menu.push_back(technique);
    }
  }
  menu.push_back(Command::YourCall);
  return menu;
}

core::Result<void> folderCommand(BattleContext& context, Command command, FighterId side) {
  switch (command) {
  case Command::Technique1:
  case Command::Technique2:
  case Command::Technique3:
    return folderOrder(
        context, static_cast<std::size_t>(command) - static_cast<std::size_t>(Command::Technique1),
        aimOf(context, side), side);
  case Command::Finisher:
    return folderOrder(context, FinisherSlot, aimOf(context, side), side);
  case Command::Attack:
    folderSetStance(context, Stance::Aggressive, side);
    return {};
  case Command::Moderate:
    folderSetStance(context, Stance::Hold, side);
    return {};
  case Command::Distance:
    folderSetStance(context, Stance::Distance, side);
    return {};
  case Command::Defend:
    folderSetStance(context, Stance::Guard, side);
    return {};
  case Command::YourCall:
    folderCancelOrders(context, side);
    return {};
  case Command::Change: {
    const std::optional<FighterId> current = context.fighter(side).target;
    std::optional<FighterId> next;
    for (FighterId step = 1; step <= context.state->fighters.size(); ++step) {
      const FighterId other = (current.value_or(0) + step) % context.state->fighters.size();
      if (validFoe(context, side, other)) {
        next = other;
        break;
      }
    }
    if (next) {
      setTarget(context, side, next);
    }
    return {};
  }
  default:
    return core::fail(std::string{messages::NotInFolder});
  }
}

} // namespace dwe::game::battle
