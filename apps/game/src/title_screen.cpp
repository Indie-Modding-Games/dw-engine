#include "title_screen.hpp"

#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/map_table.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/game/save/settings.hpp>
#include <dwe/ruleset/ruleset.hpp>
#include <dwe/screens/name_keys.hpp>
#include <dwe/session/slot_names.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <variant>

#include "battle_trigger.hpp"
#include "common.hpp"
#include "field_scene.hpp"
#include "messages.hpp"
#include "session_start.hpp"

namespace dwe::app {
namespace {

// The names of the saves' species and maps in the packs' tables; a table the packs lack leaves
// its IDs unnamed.
SaveNames namesOf(const assets::VirtualFileSystem& files, const Options& options) {
  SaveNames names;
  if (auto rules = ruleset::loadRuleset(files, options.ruleset)) {
    if (auto table = speciesTable(files, *rules)) {
      auto species = std::make_shared<const assets::DigimonTable>(std::move(*table));
      names.species = [species](std::string_view id) {
        return screens::NameKeys{species.get(), nullptr, nullptr}.species(id);
      };
    }
  }
  if (auto table = assets::loadMapTable(files)) {
    auto maps = std::make_shared<const assets::MapTable>(std::move(*table));
    names.map = [maps](std::string_view id) { return mapNameKeyOf(*maps, id); };
  }
  return names;
}

// The slots in the saves folder, with the packs' save settings; nothing without a folder.
std::optional<game::save::SaveSlots> slotsIn(const assets::VirtualFileSystem& files,
                                             game::save::SaveStorage* storage) {
  if (storage == nullptr) {
    return std::nullopt;
  }
  const auto settings = game::save::loadSaveSettings(files);
  if (!settings) {
    core::logWarning(messages::CannotReadSaveSettings, settings.error().message);
    return std::nullopt;
  }
  game::save::SaveSlots slots{*storage, *settings};
  slots.setSummaryReader(session::summaryFromSections);
  return slots;
}

} // namespace

TitleScreen::TitleScreen(const assets::VirtualFileSystem& files, const Options& options,
                         bool keepsSaves, ui::Ui& ui)
    : ui_(&ui), storage_(saveStorageFor(options, keepsSaves)),
      slots_(slotsIn(files, storage_.get())),
      flow_(slots_ ? &*slots_ : nullptr, namesOf(files, options)) {}

void TitleScreen::show() {
  listShown_ = false;
  screens::publish(*ui_, flow_.titleSettings(), "save.revision");
  ui_->show(MainMenuDocument, true);
}

bool TitleScreen::handle(const ui::UiCommand& command) {
  const std::string& name = command.name;
  if (name == "play") {
    if (flow_.keepsSaves()) {
      showList(SlotListMode::NewGame);
    } else {
      start_ = PlayStart{};
    }
  } else if (name == "continue") {
    showList(SlotListMode::Continue);
  } else if (name == "slots_back") {
    ui_->hide(SaveSlotsDocument);
    show();
  } else if (name == "choose_slot") {
    const double* entry = std::get_if<double>(&command.value);
    if (entry != nullptr && *entry >= 0.0) {
      if (const auto dialog = flow_.choose(static_cast<std::size_t>(*entry))) {
        showDialog(*dialog);
      }
    }
  } else if (name == "play_without_saving") {
    showDialog(flow_.chooseWithoutSaving());
  } else if (name == "dialog_answer") {
    const bool* yes = std::get_if<bool>(&command.value);
    ui_->hide(DialogDocument);
    start_ = flow_.answer(yes != nullptr && *yes);
    if (!start_) {
      back();
    }
  } else {
    return false;
  }
  core::logInfo(messages::TitleCommand, name);
  return true;
}

std::optional<PlayStart> TitleScreen::takeStart() {
  return std::exchange(start_, std::nullopt);
}

void TitleScreen::startFailed(const PlayStart& start, bool saveFailed) {
  showDialog(SaveFlow::startFailed(start, saveFailed));
}

void TitleScreen::showList(SlotListMode mode) {
  ui_->hide(MainMenuDocument);
  listShown_ = true;
  screens::publish(*ui_, flow_.open(mode), "save.slots.revision");
  ui_->show(SaveSlotsDocument, true);
}

void TitleScreen::showDialog(const SaveDialog& dialog) {
  ui_->hide(MainMenuDocument);
  ui_->hide(SaveSlotsDocument);
  screens::publish(*ui_, dialogSettings(dialog), "dialog.revision");
  ui_->show(DialogDocument, true);
}

void TitleScreen::back() {
  if (listShown_) {
    showList(flow_.mode());
  } else {
    show();
  }
}

} // namespace dwe::app
