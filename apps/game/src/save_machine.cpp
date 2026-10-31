#include "save_machine.hpp"

#include <dwe/core/log.hpp>
#include <dwe/screens/name_keys.hpp>
#include <dwe/screens/screen_settings.hpp>
#include <dwe/session/game_clear.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <variant>

namespace dwe::app {

namespace {

constexpr std::string_view ChooseCommand = "save_machine_choose";
constexpr std::string_view CancelCommand = "save_machine_cancel";
constexpr std::string_view Revision = "save_machine.revision";
constexpr std::string_view SlotList = "save_machine.slot";
// The original's frames, 20 a second (3 vblanks each).
constexpr double FrameSeconds = 1.0 / 20.0;

std::string text(std::string_view value) {
  return std::string{value};
}

double number(std::int32_t value) {
  return static_cast<double>(value);
}

} // namespace

SaveMachine::SaveMachine(const Desc& desc)
    : desc_(desc),
      flow_(GameSaves(
                desc.session->slots(),
                [session = desc.session](game::save::SlotId slot) { return session->save(slot); }),
            *desc.registry,
            [desc] { return session::registeredPartnerOf(*desc.session, *desc.battle); }),
      document_(*desc.ui, SaveMachineDocument) {}

SaveMachineFacts SaveMachine::facts() const {
  const session::GameSession& session = *desc_.session;
  return {.hasSlot = session.homeSlot().has_value() && session.slots() != nullptr,
          .belowRookie = session.partner().species().level < game::partner::Level::Rookie,
          .playthrough = {.home = session.homeSlot(), .started = session.playthroughStarted()}};
}

void SaveMachine::open() {
  if (active()) {
    return;
  }
  // openSaveMachine: MAIN_STATE 1, a story game again.
  desc_.session->resumeStory();
  flow_.open(facts());
  frameSeconds_ = 0.0;
  closed_ = false;
  follow();
}

std::optional<std::string> SaveMachine::clearedMap() const {
  if (desc_.city == nullptr || desc_.maps == nullptr) {
    return std::nullopt;
  }
  const std::optional<std::int32_t> screen = desc_.city->clearedMap();
  const assets::MapEntry* entry = screen ? desc_.maps->findByOriginalIndex(*screen) : nullptr;
  if (entry == nullptr) {
    core::logWarning("[port:save] gameClearSave (main_menu.c:5324): no map {} to continue on",
                     screen.value_or(-1));
    return std::nullopt;
  }
  return entry->id;
}

void SaveMachine::openClearSave() {
  if (active()) {
    return;
  }
  static const std::optional<session::EndingGift> NoGift;
  const std::optional<session::EndingGift>& gift =
      desc_.scenes != nullptr ? desc_.scenes->rules().ending : NoGift;
  if (auto cleared =
          session::clearGame(*desc_.session, *desc_.registry, *desc_.battle, gift, clearedMap());
      !cleared) {
    core::logWarning("[port:save] awardMachinedramonData (main_menu.c:5299): {}",
                     cleared.error().message);
  }
  flow_.openSaveQuestion(facts());
  frameSeconds_ = 0.0;
  // Without a slot nothing opens and the script goes on (scriptTickMainMenu sees CURRENT_MENU -1).
  closed_ = !active();
  follow();
}

bool SaveMachine::handle(const ui::UiCommand& command) {
  if (!active() || (command.name != ChooseCommand && command.name != CancelCommand)) {
    return false;
  }
  if (command.name == CancelCommand) {
    flow_.cancel();
  } else if (const double* row = std::get_if<double>(&command.value);
             row != nullptr && *row >= 0.0) {
    flow_.choose(static_cast<std::size_t>(*row));
  }
  follow();
  return true;
}

void SaveMachine::tick(double seconds) {
  if (!active()) {
    return;
  }
  frameSeconds_ += seconds;
  while (frameSeconds_ >= FrameSeconds && active()) {
    frameSeconds_ -= FrameSeconds;
    flow_.tick();
  }
  if (!active()) {
    follow();
  }
}

bool SaveMachine::takeClosed() {
  return std::exchange(closed_, false);
}

void SaveMachine::follow() {
  if (const auto result = flow_.takeResult()) {
    desc_.ui->setSetting("save.last_result", std::string{saveResultName(*result)});
  }
  if (!active()) {
    if (document_.shown()) {
      document_.hide();
      closed_ = true;
    }
    return;
  }
  publish();
  document_.show(true);
}

// main_menu.c: drawMainMenuStrings for the machine's views (14 to 19): the partner's record, the
// registry's rows, the slot chosen and the save's dialog.
void SaveMachine::publish() {
  const session::GameSession& session = *desc_.session;
  const auto* catalog = desc_.battle != nullptr ? &desc_.battle->catalog() : nullptr;
  const screens::NameKeys names{desc_.species, nullptr, catalog};
  screens::ScreenSettings settings{
      {.name = "save_machine.view", .value = text(saveMachineViewName(flow_.view()))},
      {.name = "save_machine.chosen_slot", .value = static_cast<double>(flow_.slot())},
      {.name = "save_machine.slots",
       .value = static_cast<double>(session::BattleRegistry::MachineSlots)},
      {.name = "save_machine.name", .value = session.names().partner}};
  if (auto partner = session::registeredPartnerOf(session, *desc_.battle); partner) {
    settings.push_back({.name = "save_machine.species", .value = names.species(partner->species)});
    settings.push_back({.name = "save_machine.hp", .value = number(partner->hp)});
    settings.push_back({.name = "save_machine.mp", .value = number(partner->mp)});
    settings.push_back({.name = "save_machine.offense", .value = number(partner->offense)});
    settings.push_back({.name = "save_machine.defense", .value = number(partner->defense)});
    for (std::size_t place = 0; place < partner->techniques.size(); ++place) {
      const auto& move = partner->techniques.at(place);
      settings.push_back({.name = screens::listEntryName("save_machine.technique", place, "name"),
                          .value = move ? names.move(*move) : std::string{}});
    }
  } else {
    core::logWarning("[port:save] drawMainMenuStrings (main_menu.c:3065): {}",
                     partner.error().message);
  }
  for (std::size_t slot = 0; slot < session::BattleRegistry::MachineSlots; ++slot) {
    const auto& registered = desc_.registry->at(slot);
    settings.push_back(
        {.name = screens::listEntryName(SlotList, slot, "taken"), .value = registered.has_value()});
    settings.push_back({.name = screens::listEntryName(SlotList, slot, "name"),
                        .value = registered ? registered->name : std::string{}});
    settings.push_back({.name = screens::listEntryName(SlotList, slot, "species"),
                        .value = registered ? names.species(registered->species) : std::string{}});
  }
  const auto& dialog = flow_.dialog();
  settings.push_back({.name = "save_machine.dialog_text", .value = dialog ? dialog->text : ""});
  settings.push_back({.name = "save_machine.dialog_file", .value = dialog ? dialog->file : ""});
  settings.push_back({.name = "save_machine.dialog_question", .value = dialog && dialog->question});
  settings.push_back(
      {.name = "save_machine.dialog_start_on_yes", .value = dialog && dialog->startOnYes});
  screens::publish(*desc_.ui, settings, Revision);
}

} // namespace dwe::app
