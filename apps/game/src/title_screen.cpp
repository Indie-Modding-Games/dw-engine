#include "title_screen.hpp"

#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/map_table.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/game/save/settings.hpp>
#include <dwe/ruleset/ruleset.hpp>
#include <dwe/ruleset/ruleset_values.hpp>
#include <dwe/screens/name_keys.hpp>
#include <dwe/session/slot_names.hpp>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "battle_trigger.hpp"
#include "common.hpp"
#include "field_scene.hpp"
#include "messages.hpp"
#include "session_start.hpp"
#include "title_options.hpp"

namespace dwe::app {
namespace {

// The game pack's ruleset with the Ultimate to Mega step, which the new game's rules offer when the
// packs have it.
constexpr std::string_view MegaRuleset = "oculto_mega";
// The ruleset of every species as a partner (docs/modulos/game/evoluciones-extra.md), which the new
// game's rules offer, on, when the packs have it.
constexpr std::string_view AllSpeciesRuleset = "todas";

bool hasRuleset(const assets::VirtualFileSystem& files, std::string_view id) {
  const std::vector<std::string> ids = ruleset::listRulesets(files);
  return std::ranges::find(ids, id) != ids.end();
}

// The names of the saves' species and maps in the packs' tables; a table the packs lack leaves its
// IDs unnamed.
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
      flow_(slots_ ? &*slots_ : nullptr, namesOf(files, options)),
      modes_(game::modes::ModeCatalog::load(files)), megaOffered_(hasRuleset(files, MegaRuleset)),
      allOffered_(hasRuleset(files, AllSpeciesRuleset)) {}

void TitleScreen::show() {
  showMenu(false);
}

void TitleScreen::showBoot() {
  showMenu(true);
}

void TitleScreen::showMenu(bool boot) {
  listShown_ = false;
  landing_ = false;
  screens::ScreenSettings settings = flow_.titleSettings();
  // Read by the main menu as it shows (title.boot): whether it starts at the "Press Start" screen.
  settings.push_back({.name = "title.boot", .value = boot});
  screens::publish(*ui_, settings, "save.revision");
  ui_->show(MainMenuDocument, true);
}

void TitleScreen::hide() {
  landing_ = false;
  if (optionsShown_) {
    optionsShown_ = false;
    ui_->hide(MenuDocument);
  }
  ui_->hide(MainMenuDocument);
  ui_->hide(SaveSlotsDocument);
  ui_->hide(DialogDocument);
  ui_->hide(NewGameRulesDocument);
  ui_->hide(NewGameModeDocument);
}

bool TitleScreen::handle(const ui::UiCommand& command) {
  const std::string& name = command.name;
  if (name == "title_landing") {
    const bool* landing = std::get_if<bool>(&command.value);
    landing_ = landing != nullptr && *landing;
    // Sent each time the screen is entered or left: not a choice of the player's to log.
    return true;
  }
  if (name == TitleOptionsCommand) {
    showOptions();
  } else if (name == TitleOptionsBackCommand) {
    // Back from the options to the main menu, on its Options entry.
    optionsShown_ = false;
    ui_->hide(MenuDocument);
    ui_->hide(MainMenuDocument);
    show();
  } else if (name == "play") {
    if (flow_.keepsSaves()) {
      showList(SlotListMode::NewGame);
    } else {
      startOrAskRules(PlayStart{});
    }
  } else if (name == "continue") {
    showList(SlotListMode::Continue);
  } else if (name == "erase") {
    showList(SlotListMode::Erase);
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
    if (flow_.erasing()) {
      if (const auto notice = flow_.answerErase(yes != nullptr && *yes)) {
        showDialog(*notice);
      } else {
        back();
      }
    } else {
      auto start = flow_.answer(yes != nullptr && *yes);
      if (start) {
        // The list stayed behind the question; the game starts without it.
        ui_->hide(SaveSlotsDocument);
        startOrAskRules(std::move(start));
      } else {
        back();
      }
    }
  } else if (name == "new_game_rules") {
    const std::string* ruleset = std::get_if<std::string>(&command.value);
    ui_->hide(NewGameRulesDocument);
    if (pending_ && ruleset != nullptr) {
      pending_->rulesetId = *ruleset;
      start_ = std::exchange(pending_, std::nullopt);
    }
  } else if (name == "new_game_rules_back") {
    ui_->hide(NewGameRulesDocument);
    pending_.reset();
    back();
  } else if (name == "new_game_mode") {
    const std::string* mode = std::get_if<std::string>(&command.value);
    if (pending_ && mode != nullptr) {
      ui_->hide(NewGameModeDocument);
      chooseMode(*mode);
    }
  } else if (name == "new_game_mode_back") {
    ui_->hide(NewGameModeDocument);
    pending_.reset();
    back();
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

void TitleScreen::showOptions() {
  // The pause menu's options page over the title (docs/modulos/apps/game.md, «Opciones en
  // partida»): the main menu stays behind it, its own box hidden, so the title's picture shows;
  // the page reads both settings as it shows.
  optionsShown_ = true;
  ui_->setSetting(MenuAtTitleSetting, true);
  ui_->setSetting(MenuStartPageSetting, std::string{"options"});
  ui_->show(MenuDocument, true);
  ui_->setSetting(MenuStartPageSetting, std::string{});
  ui_->setSetting(MenuAtTitleSetting, false);
}

void TitleScreen::showList(SlotListMode mode) {
  ui_->hide(MainMenuDocument);
  listShown_ = true;
  screens::publish(*ui_, flow_.open(mode), "save.slots.revision");
  ui_->show(SaveSlotsDocument, true);
}

void TitleScreen::showDialog(const SaveDialog& dialog) {
  ui_->hide(MainMenuDocument);
  // A question about a slot shows over the list it came from, as the original's boxes over its
  // menus.
  if (!listShown_) {
    ui_->hide(SaveSlotsDocument);
  }
  screens::publish(*ui_, dialogSettings(dialog, true), "dialog.revision");
  ui_->show(DialogDocument, true);
}

void TitleScreen::startOrAskRules(std::optional<PlayStart> start) {
  if (!start || start->load || !asksRules_) {
    start_ = std::move(start);
    return;
  }
  pending_ = std::move(start);
  ui_->hide(MainMenuDocument);
  ui_->hide(SaveSlotsDocument);
  if (modes_.asks()) {
    screens::publish(*ui_, modes_.settings(), "new_game_mode.revision");
    ui_->show(NewGameModeDocument, true);
    return;
  }
  if (const game::modes::GameMode* mode = modes_.catalog().defaultMode()) {
    chooseMode(mode->id);
    return;
  }
  showRules();
}

void TitleScreen::chooseMode(std::string_view mode) {
  if (modes_.choose(mode, *pending_) == NewGameModes::Next::Start) {
    start_ = std::exchange(pending_, std::nullopt);
    return;
  }
  showRules();
}

void TitleScreen::showRules() {
  screens::publish(*ui_,
                   {{.name = "new_game_rules.mega", .value = megaOffered_},
                    {.name = "new_game_rules.all", .value = allOffered_}},
                   "new_game_rules.revision");
  ui_->show(NewGameRulesDocument, true);
}

void TitleScreen::back() {
  if (listShown_) {
    showList(flow_.mode());
  } else {
    show();
  }
}

} // namespace dwe::app
