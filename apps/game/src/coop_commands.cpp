#include "coop_commands.hpp"

#include <cmath>

namespace dwe::app {

core::Result<game::coop::Commands> toCoopCommand(const PlayerCommands& commands) {
  const glm::vec2 stick = commands.field.stick;
  if (!std::isfinite(stick.x) || !std::isfinite(stick.y) || std::abs(stick.x) > 1.0F ||
      std::abs(stick.y) > 1.0F) {
    return core::fail("cooperative field stick is non-finite or outside [-1, 1]");
  }

  game::coop::FieldCommand field{
      .stickX = static_cast<std::int16_t>(std::lround(stick.x * 32767.0F)),
      .stickY = static_cast<std::int16_t>(std::lround(stick.y * 32767.0F)),
      .run = commands.field.run,
      .cancelTarget = commands.field.cancelTarget,
      .confirm = commands.field.confirm,
      .moveTarget = std::nullopt};
  if (commands.field.moveTarget) {
    const glm::vec3 target = *commands.field.moveTarget;
    if (!std::isfinite(target.x) || !std::isfinite(target.z) ||
        std::abs(target.x) > game::coop::MaxWorldCoordinate ||
        std::abs(target.z) > game::coop::MaxWorldCoordinate) {
      return core::fail("cooperative move target is outside the world-coordinate range");
    }
    field.moveTarget = std::pair{static_cast<std::int32_t>(std::lround(target.x)),
                                 static_cast<std::int32_t>(std::lround(target.z))};
  }
  return game::coop::Commands{.sequence = commands.sequence, .field = field};
}

PlayerCommands fromCoopCommand(const game::coop::Commands& commands) {
  constexpr float StickScale = 1.0F / 32767.0F;
  PlayerCommands result;
  result.sequence = commands.sequence;
  result.field.stick = {static_cast<float>(commands.field.stickX) * StickScale,
                        static_cast<float>(commands.field.stickY) * StickScale};
  result.field.run = commands.field.run;
  result.field.cancelTarget = commands.field.cancelTarget;
  result.field.confirm = commands.field.confirm;
  if (commands.field.moveTarget) {
    result.field.moveTarget = glm::vec3{static_cast<float>(commands.field.moveTarget->first), 0.0F,
                                        static_cast<float>(commands.field.moveTarget->second)};
  }
  return result;
}

PlayerCommands NetworkFieldController::next(std::uint64_t /*tick*/) {
  return fromCoopCommand(game::coop::Commands{.sequence = buffer_.sequence().value_or(0),
                                              .field = buffer_.current(now_)});
}

} // namespace dwe::app
