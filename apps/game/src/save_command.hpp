#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/save/slots.hpp>
#include <dwe/script/script_vm.hpp>
#include <dwe/session/game_session.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace dwe::app {

// The console's and the scripts' save("slot_NN"): saves the game to a manual slot, as a debug aid
// until the game's own save screen exists. It writes only where the session saves: an autotest
// saves nothing unless --saves names a folder (session_start.hpp).
inline constexpr std::string_view SaveDebugCommand = "save";

// The manual slot a save command names (`slot_01`, as --list-saves writes it), or why it names
// none: an old autosave (`autosave_01`) is never written.
core::Result<game::save::SlotId> manualSlotFromName(std::string_view name);

// Queues the save command the console and the scripts send, and runs it between frames, like the
// menus' commands, so a save never sees half a frame.
class SaveCommand {
public:
  // Binds the command to the script VM; it stays valid while this object lives.
  void bind(script::ScriptVm& scripts);

  // Saves to the queued slot, if any, and logs it; nothing to do is a success. The caller decides
  // what a failed save means: the game goes on, an autotest fails. While a care scene holds the
  // field (GameSession::savesWait) the save stays queued and a later call writes it.
  core::Result<void> apply(session::GameSession& session);

  // Whether a save is queued, also while it waits for a care scene.
  [[nodiscard]] bool pending() const { return pending_.has_value(); }

private:
  static core::Result<void> save(session::GameSession& session, std::string_view name);

  std::optional<std::string> pending_;
  bool waitLogged_ = false;
};

} // namespace dwe::app
