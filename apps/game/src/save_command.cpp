#include "save_command.hpp"

#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/session/slot_names.hpp>

#include <utility>

#include "messages.hpp"

namespace dwe::app {

core::Result<game::save::SlotId> manualSlotFromName(std::string_view name) {
  const std::optional<game::save::SlotId> slot = session::slotFromName(name);
  if (!slot || slot->kind != game::save::SlotKind::Manual) {
    return core::fail(core::formatMessage(messages::NotAManualSlot, name));
  }
  return *slot;
}

void SaveCommand::bind(script::ScriptVm& scripts) {
  scripts.bindCommand(SaveDebugCommand,
                      [this](std::string_view slot) { pending_ = std::string{slot}; });
}

core::Result<void> SaveCommand::apply(session::GameSession& session) {
  if (!pending_) {
    return {};
  }
  // A dwe safety rule (GameSession::savesWait): the save stays queued until the care scene ends.
  if (session.playing() && session.savesWait()) {
    if (!std::exchange(waitLogged_, true)) {
      core::logDebug("{}", core::formatMessage(messages::SaveWaitsForCare, *pending_));
    }
    return {};
  }
  waitLogged_ = false;
  const std::optional<std::string> pending = std::exchange(pending_, std::nullopt);
  if (auto saved = save(session, *pending); !saved) {
    return core::fail(core::formatMessage(messages::SaveCommandFailed, saved.error().message));
  }
  core::logInfo("{}", core::formatMessage(messages::SavedToSlot, *pending));
  return {};
}

core::Result<void> SaveCommand::save(session::GameSession& session, std::string_view name) {
  if (!session.playing()) {
    return core::fail(std::string{messages::NoGame});
  }
  auto slot = manualSlotFromName(name);
  if (!slot) {
    return std::unexpected{slot.error()};
  }
  return session.save(*slot);
}

} // namespace dwe::app
