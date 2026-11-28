#include <dwe/game/battle/messages.hpp>

#include <string>

#include "classic/classic.hpp"

namespace dwe::game::battle {

core::Result<void> classicCommand(BattleContext& context, Command command, FighterId side) {
  CommandState& commands = context.state->commandsOf(side);
  switch (command) {
  case Command::Finisher: {
    const Fighter& partner = context.fighter(side);
    if (!partner.hasFinisher() || partner.finisherProgress < partner.finisherGoal) {
      return core::fail(std::string{messages::FinisherNotReady});
    }
    commands.buffered = command;
    commands.current = command;
    context.emit({.kind = EventKind::CommandGiven,
                  .fighter = side,
                  .other = std::nullopt,
                  .move = std::nullopt,
                  .amount = 0,
                  .status = StatusEffect::None,
                  .command = command});
    return {};
  }
  case Command::Change:
    commands.changeTarget = true;
    break;
  default:
    break;
  }
  commands.buffered = command;
  // vs_combat.c: VS___tickVSInput works the delay out and then sets it to 0.
  commands.delay = context.state->versus() ? 0
                                           : context.strategies->obedience->commandDelay(
                                                 context.view(side), context.disciplineOf(side));
  return {};
}

} // namespace dwe::game::battle
