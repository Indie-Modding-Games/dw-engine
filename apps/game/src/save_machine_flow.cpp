#include "save_machine_flow.hpp"

#include <dwe/core/log.hpp>

#include <string>
#include <utility>

namespace dwe::app {

namespace {

constexpr std::size_t Yes = 0;

} // namespace

std::string_view saveMachineViewName(SaveMachineView view) {
  switch (view) {
  case SaveMachineView::Closed:
    return "closed";
  case SaveMachineView::Register:
    return "register";
  case SaveMachineView::Slots:
    return "slots";
  case SaveMachineView::ConfirmRegister:
    return "confirm_register";
  case SaveMachineView::NoSlot:
    return "no_slot";
  case SaveMachineView::InTraining:
    return "in_training";
  case SaveMachineView::SaveQuestion:
    return "save_question";
  case SaveMachineView::SaveDialog:
    return "save_dialog";
  }
  return "closed";
}

SaveMachineFlow::SaveMachineFlow(GameSaves saves, session::BattleRegistry& registry,
                                 PartnerRecord partner)
    : saves_(std::move(saves)), registry_(&registry), partner_(std::move(partner)) {}

void SaveMachineFlow::open(const SaveMachineFacts& facts) {
  if (active()) {
    return;
  }
  // main_menu.c: openSaveMachine: the registration (0x3C), or its notice below Rookie (0x46); with
  // no card or slot, the notice of 0x44 whatever the partner.
  playthrough_ = facts.playthrough;
  slot_ = 0;
  frames_ = 0;
  dialog_.reset();
  replaced_.reset();
  view_ = !facts.hasSlot      ? SaveMachineView::NoSlot
          : facts.belowRookie ? SaveMachineView::InTraining
                              : SaveMachineView::Register;
  core::logDebug("[port:save] openSaveMachine (main_menu.c:5273): {}", saveMachineViewName(view_));
}

void SaveMachineFlow::choose(std::size_t row) {
  switch (view_) {
  case SaveMachineView::Register:
    // 0x3C: yes lists the slots, no goes on to the save's question.
    view_ = row == Yes ? SaveMachineView::Slots : SaveMachineView::SaveQuestion;
    break;
  case SaveMachineView::Slots:
    // 0x3D: the slot under the cursor (BATTLE_REGISTRATION_SLOT).
    if (row < session::BattleRegistry::MachineSlots) {
      slot_ = row;
      view_ = SaveMachineView::ConfirmRegister;
    }
    break;
  case SaveMachineView::ConfirmRegister:
    // 0x3E: yes registers and saves (0x3F on), no goes back to the list.
    if (row == Yes) {
      registerAndSave();
    } else {
      view_ = SaveMachineView::Slots;
    }
    break;
  case SaveMachineView::SaveQuestion:
    // 0x45: yes saves, no closes the machine.
    if (row == Yes) {
      save();
    } else {
      close();
    }
    break;
  case SaveMachineView::NoSlot:
    // 0x44: any button closes the notice.
    close();
    break;
  case SaveMachineView::InTraining:
    // 0x46: any button goes on to the save's question.
    view_ = SaveMachineView::SaveQuestion;
    break;
  case SaveMachineView::SaveDialog:
    dialog_.reset();
    follow(saves_.answer(row == Yes));
    break;
  case SaveMachineView::Closed:
    break;
  }
}

void SaveMachineFlow::cancel() {
  switch (view_) {
  case SaveMachineView::Register:
    view_ = SaveMachineView::SaveQuestion;
    break;
  case SaveMachineView::Slots:
  case SaveMachineView::ConfirmRegister:
    view_ = SaveMachineView::Register;
    break;
  case SaveMachineView::NoSlot:
  case SaveMachineView::InTraining:
  case SaveMachineView::SaveDialog:
    choose(1);
    break;
  case SaveMachineView::SaveQuestion:
    // 0x45 takes no cancel: the player answers yes or no.
  case SaveMachineView::Closed:
    break;
  }
}

void SaveMachineFlow::tick() {
  if (view_ == SaveMachineView::NoSlot && ++frames_ >= NoSlotFrames) {
    close();
  }
}

void SaveMachineFlow::registerAndSave() {
  auto partner = partner_();
  if (!partner) {
    core::logWarning("[port:save] registerBattleData (main_menu.c:5230): {}",
                     partner.error().message);
    save();
    return;
  }
  std::optional<session::RegisteredPartner> before = registry_->at(slot_);
  if (auto registered = registry_->registerAt(slot_, std::move(*partner)); !registered) {
    core::logWarning("{}", registered.error().message);
    save();
    return;
  }
  replaced_ = std::move(before);
  save();
}

void SaveMachineFlow::save() {
  follow(saves_.save(playthrough_));
}

void SaveMachineFlow::follow(const SaveStep& step) {
  core::logDebug("[port:save] tickMainMenu (main_menu.c:4259): machine save: result {}, dialog {}",
                 step.result ? saveResultName(*step.result) : "pending",
                 step.dialog ? step.dialog->text : std::string{"none"});
  if (step.result) {
    result_ = step.result;
    // The registration lives in the save (SAVE_FILE.saves[0]): one that was not written is undone.
    if (*step.result != SaveResult::Saved && replaced_) {
      if (auto restored = std::move(*replaced_)) {
        static_cast<void>(registry_->registerAt(slot_, std::move(*restored)));
      } else {
        registry_->clear(slot_);
      }
    }
    replaced_.reset();
  }
  if (step.dialog) {
    dialog_ = step.dialog;
    view_ = SaveMachineView::SaveDialog;
  } else {
    close();
  }
}

void SaveMachineFlow::close() {
  core::logDebug("[port:save] tickSaveMachine (main_menu.c:5288): save machine closed");
  view_ = SaveMachineView::Closed;
  dialog_.reset();
}

} // namespace dwe::app
