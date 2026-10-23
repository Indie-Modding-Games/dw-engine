#include "save_machine.hpp"

#include <dwe/core/log.hpp>
#include <dwe/screens/screen_settings.hpp>

#include <string>
#include <variant>

#include "title_screen.hpp"

namespace dwe::app {

SaveMachine::SaveMachine(session::GameSession& session, ui::Ui& ui)
    : session_(&session), ui_(&ui),
      saves_(session.slots(), [&session](game::save::SlotId slot) { return session.save(slot); }) {}

void SaveMachine::open() {
  if (active()) {
    return;
  }
  // main_menu.c: openSaveMachine goes to menu 0x44 (no card) when the game has no card or slot,
  // else to the versus registration (0x3C, or 0x46 below Rookie), which ends in 0x45.
  const bool slot = session_->homeSlot().has_value() && session_->slots() != nullptr;
  core::logDebug("[port:save] openSaveMachine (main_menu.c:5273): home slot {}", slot);
  if (!slot) {
    show({.text = "save.machine_no_slot", .file = {}, .question = false, .startOnYes = false},
         State::NoSlot);
    return;
  }
  show({.text = "save.machine_confirm", .file = {}, .question = true, .startOnYes = false},
       State::Asking);
}

bool SaveMachine::handle(const ui::UiCommand& command) {
  if (command.name != "dialog_answer" || !active()) {
    return false;
  }
  const bool* yes = std::get_if<bool>(&command.value);
  const bool answer = yes != nullptr && *yes;
  ui_->hide(DialogDocument);
  switch (state_) {
  case State::Asking:
    // main_menu.c: tickMainMenu case 0x45: yes writes the card (0x3F on), no closes the menu.
    core::logDebug("[port:save] tickMainMenu (main_menu.c:4428): save? {}", answer);
    if (answer) {
      follow(
          saves_.save({.home = session_->homeSlot(), .started = session_->playthroughStarted()}));
    } else {
      close();
    }
    break;
  case State::Saving:
    follow(saves_.answer(answer));
    break;
  case State::NoSlot:
  case State::Closed:
    // main_menu.c: tickMainMenu case 0x44: any button closes the notice.
    close();
    break;
  }
  return true;
}

void SaveMachine::show(const SaveDialog& dialog, State state) {
  state_ = state;
  screens::publish(*ui_, dialogSettings(dialog), "dialog.revision");
  ui_->show(DialogDocument, true);
}

void SaveMachine::follow(const SaveStep& step) {
  core::logDebug("[port:save] tickMainMenu (main_menu.c): machine save: result {}, dialog {}",
                 step.result ? saveResultName(*step.result) : "pending",
                 step.dialog ? step.dialog->text : std::string{"none"});
  if (step.result) {
    ui_->setSetting("save.last_result", std::string{saveResultName(*step.result)});
  }
  if (step.dialog) {
    show(*step.dialog, State::Saving);
  } else {
    close();
  }
}

void SaveMachine::close() {
  core::logDebug("[port:save] scriptTickMainMenu (main_menu.c:5331): save machine closed");
  state_ = State::Closed;
}

} // namespace dwe::app
