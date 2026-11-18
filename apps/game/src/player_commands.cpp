#include "player_commands.hpp"

namespace dwe::app {

void LocalController::neutral() {
  PlayerCommands commands;
  commands.sequence = nextSequence_++;
  pending_ = commands;
}

void LocalController::sample(const input::InputState& input, const WalkOrder& walk,
                             input::Vector2 directionalInput, bool clickWalksAway) {
  PlayerCommands commands;
  commands.sequence = nextSequence_++;
  commands.field.stick = walk.stick;
  commands.field.run = !input.isDown("walk");
  commands.field.moveTarget = walk.moveTarget;
  commands.field.cancelTarget = input.wasPressed("cancel");
  commands.field.confirm = clickWalksAway ? input.wasPressedByKeyOrPad("confirm")
                                          : input.wasPressed("confirm");
  commands.fishing = {.confirmDown = input.isDown("confirm"),
                      .cancelPressed = commands.field.cancelTarget,
                      .cameraDown = input.isDown("alt"),
                      .left = directionalInput.x < 0.0F,
                      .right = directionalInput.x > 0.0F,
                      .up = directionalInput.y < 0.0F,
                      .down = directionalInput.y > 0.0F};
  commands.story = {.confirmPressed = input.wasPressed("confirm"),
                     .confirmPressedByKeyOrPad = input.wasPressedByKeyOrPad("confirm"),
                     .confirmDown = input.isDown("confirm"),
                     .cancelPressed = input.wasPressed("cancel"),
                     .menuPressed = input.wasPressed("menu"),
                     .menuDown = input.isDown("menu"),
                     .upPressed = input.wasPressed("move_up"),
                     .downPressed = input.wasPressed("move_down")};
  pending_ = commands;
}

PlayerCommands LocalController::next(std::uint64_t /*tick*/) {
  if (pending_) {
    return std::exchange(pending_, std::nullopt).value();
  }
  PlayerCommands commands;
  commands.sequence = nextSequence_++;
  return commands;
}

PlayerCommands ScriptedController::next(std::uint64_t /*tick*/) {
  if (cursor_ >= commands_.size()) {
    PlayerCommands neutral;
    neutral.sequence = nextSequence_++;
    return neutral;
  }
  PlayerCommands commands = commands_[cursor_++];
  commands.sequence = nextSequence_++;
  return commands;
}

} // namespace dwe::app
