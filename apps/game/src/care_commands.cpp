#include "care_commands.hpp"

#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>

#include <charconv>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "messages.hpp"

namespace dwe::app {

namespace {

constexpr char ArgumentSeparator = ' ';

} // namespace

void CareCommands::bind(script::ScriptVm& scripts) {
  const auto queue = [this](Kind kind) {
    return [this, kind](std::string_view argument) {
      queued_.push_back({.kind = kind, .argument = std::string{argument}});
    };
  };
  scripts.bindCommand(ToiletCommand, queue(Kind::Toilet));
  scripts.bindCommand(SleepCommand, queue(Kind::Sleep));
  scripts.bindCommand(TrainCommand, queue(Kind::Train));
  scripts.bindCommand(ScoldDebugCommand, queue(Kind::Scold));
  scripts.bindCommand(ClockSpeedCommand, queue(Kind::ClockSpeed));
}

bool CareCommands::apply(session::GameSession& session) {
  const std::vector<Queued> commands = std::exchange(queued_, {});
  for (const Queued& command : commands) {
    if (!session.playing()) {
      core::logWarning("{}", core::formatMessage(messages::CareCommandFailed, messages::NoGame));
      continue;
    }
    if (auto done = run(session, command); !done) {
      core::logWarning("{}",
                       core::formatMessage(messages::CareCommandFailed, done.error().message));
    }
  }
  return !commands.empty();
}

core::Result<void> CareCommands::run(session::GameSession& session, const Queued& command) {
  switch (command.kind) {
  case Kind::Toilet:
    session.useToilet();
    core::logInfo("{}", messages::CareToilet);
    return {};
  case Kind::Sleep:
    if (!session.partner().canSleep()) {
      return core::fail(std::string{messages::CannotSleep});
    }
    if (auto slept = session.sleep({.hasPillow = false, .area = session.areaResponse()}); !slept) {
      return slept;
    }
    core::logInfo("{}", messages::CareSlept);
    return {};
  case Kind::Train:
    return train(session, command.argument);
  case Kind::Scold:
    session.scold();
    core::logInfo("{}", messages::CareScolded);
    return {};
  case Kind::ClockSpeed:
    return setClockSpeed(session, command.argument);
  }
  return {};
}

core::Result<void> CareCommands::train(session::GameSession& session, std::string_view argument) {
  const std::size_t separator = argument.find(ArgumentSeparator);
  if (separator == std::string_view::npos) {
    return core::fail(core::formatMessage(messages::BadTrainArgument, argument));
  }
  const std::string place{argument.substr(0, separator)};
  const std::string course{argument.substr(separator + 1)};
  auto trained = session.train({.place = place, .course = course});
  if (!trained) {
    return std::unexpected{trained.error()};
  }
  core::logInfo("{}", core::formatMessage(messages::CareTrained, place, course));
  return {};
}

core::Result<void> CareCommands::setClockSpeed(session::GameSession& session,
                                               std::string_view argument) {
  double speed = 0.0;
  const auto [rest, error] =
      std::from_chars(argument.data(), argument.data() + argument.size(), speed);
  if (error != std::errc{} || rest != argument.data() + argument.size() || speed < 0.0) {
    return core::fail(core::formatMessage(messages::BadClockSpeed, argument));
  }
  session.clock().setSpeed(speed);
  core::logInfo("{}", core::formatMessage(messages::ClockSpeedSet, speed));
  return {};
}

} // namespace dwe::app
