#pragma once

#include <dwe/game/save/slots.hpp>
#include <dwe/screens/screen_settings.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "app_flow.hpp"

namespace dwe::app {

// Which list the title screen shows: the empty slots for a new game, the saves to continue, or the
// saves to erase.
enum class SlotListMode : std::uint8_t { NewGame, Continue, Erase };

// The text keys of what a save's summary names by ID; an empty key when the packs do not know it,
// and the list shows the ID.
struct SaveNames {
  std::function<std::string(std::string_view)> species;
  std::function<std::string(std::string_view)> map;
  // How a save's time reads in a question: the player's local time in the game; nothing reads it
  // in UTC ("2026-11-03 19:31").
  std::function<std::string(std::chrono::sys_seconds)> time = {};
};

// A message of the title screen: a text key, the save file it is about, and whether it asks yes or
// no.
struct SaveDialog {
  std::string text;
  std::string file;
  bool question = false;
  // A question starts on yes (the original's "Record new adventure?") instead of no.
  bool startOnYes = false;
  // The save a question offers, for its text's {when} and {place}: when it was written, and where
  // (the map's text key, or its ID when the packs do not name it).
  std::string when = {};
  std::string placeKey = {};
  std::string place = {};

  friend bool operator==(const SaveDialog&, const SaveDialog&) = default;
};

// What the title screen does with the save slots (docs/architecture/guardar-y-cargar.md, 3.4, 3.5
// and 4): the lists, the questions, and the game each answer starts. It knows no UI: the game
// publishes its settings and shows the documents.
class SaveFlow {
public:
  // `slots` is null when the game is not saved, and must outlive the flow.
  SaveFlow(game::save::SaveSlots* slots, SaveNames names);

  // Whether there are slots to list; without them a new game starts with no list.
  [[nodiscard]] bool keepsSaves() const { return slots_ != nullptr; }
  // The main menu's settings: whether there is a slot to continue.
  [[nodiscard]] screens::ScreenSettings titleSettings() const;

  // Reads the slots again and lists those `mode` can choose, as the save_slots document's
  // settings.
  [[nodiscard]] screens::ScreenSettings open(SlotListMode mode);
  [[nodiscard]] SlotListMode mode() const { return mode_; }

  // The listed entry `entry` (from 0) was chosen: the question or the notice to show, nothing when
  // there is no such entry.
  [[nodiscard]] std::optional<SaveDialog> choose(std::size_t entry);
  // A new game with no slot was chosen.
  [[nodiscard]] SaveDialog chooseWithoutSaving();
  // The answer to the dialog shown last: the game to start, or nothing, to show the next question
  // (nextQuestion) or else go back to the list. Continuing a manual save whose own autosave is
  // newer asks first which of the two to load: yes loads the autosave, no (and cancel) the manual
  // save, as the original, which has no autosave.
  [[nodiscard]] std::optional<PlayStart> answer(bool yes);
  // The question an answer that started nothing leads to; nothing to go back to the list.
  [[nodiscard]] const std::optional<SaveDialog>& nextQuestion() const { return next_; }
  // Whether the dialog shown last asks to erase a save; its answer goes to answerErase.
  [[nodiscard]] bool erasing() const { return pendingErase_.has_value(); }
  // The answer to the question to erase a save: erases it on yes. The notice to show when the save
  // could not be erased (the error goes to the log), nothing to go back to the list.
  [[nodiscard]] std::optional<SaveDialog> answerErase(bool yes);
  // The notice of a game that did not start, or whose new game could not be saved to its slot
  // (`saveFailed`); the error itself goes to the log.
  [[nodiscard]] static SaveDialog startFailed(const PlayStart& start, bool saveFailed);

private:
  game::save::SaveSlots* slots_;
  SaveNames names_;
  SlotListMode mode_ = SlotListMode::NewGame;
  std::vector<game::save::SlotInfo> entries_;
  std::optional<PlayStart> pending_;
  std::optional<game::save::SlotId> pendingErase_;
  // The newest autosave of the manual save being continued, when it is newer than that save.
  std::optional<game::save::SlotInfo> newerAutosave_;
  // Whether the question shown last offers newerAutosave_.
  bool offeringAutosave_ = false;
  std::optional<SaveDialog> next_;
};

// A dialog as the settings of the dialog document; `atTitle` when the title screen shows it, over
// its boxes (dialog.at_title).
[[nodiscard]] screens::ScreenSettings dialogSettings(const SaveDialog& dialog,
                                                     bool atTitle = false);

} // namespace dwe::app
