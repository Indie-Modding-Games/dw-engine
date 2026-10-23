#pragma once

#include <dwe/session/game_session.hpp>
#include <dwe/ui/ui.hpp>

#include <cstdint>
#include <string_view>

#include "game_saves.hpp"

namespace dwe::app {

// save_machine(""): opens the save machine as script routine 48 does, until the story scripts call
// it.
inline constexpr std::string_view SaveMachineCommand = "save_machine";

// The save machine (docs/architecture/guardar-y-cargar.md, 3.7), as main_menu.c: openSaveMachine
// and tickMainMenu's save menus: «Do you want to save?», then the save to the playthrough's slot.
// The script that opened it waits until it closes. Registering the partner for versus battles comes
// with the versus mode (phase 5).
class SaveMachine {
public:
  // The session and the UI must outlive this object.
  SaveMachine(session::GameSession& session, ui::Ui& ui);

  // Whether the machine's dialogs are on screen: the field and the game time wait meanwhile.
  [[nodiscard]] bool active() const { return state_ != State::Closed; }

  void open();

  // Takes the answers to the machine's dialogs; false for any other command.
  bool handle(const ui::UiCommand& command);

private:
  // NoSlot: the notice of a playthrough with no slot. Asking: «Do you want to save?». Saving: a
  // dialog of the save itself.
  enum class State : std::uint8_t { Closed, NoSlot, Asking, Saving };

  void show(const SaveDialog& dialog, State state);
  void follow(const SaveStep& step);
  void close();

  session::GameSession* session_;
  ui::Ui* ui_;
  GameSaves saves_;
  State state_ = State::Closed;
};

} // namespace dwe::app
