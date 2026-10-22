#pragma once

#include <dwe/game/save/slots.hpp>
#include <dwe/screens/screen_settings.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "app_flow.hpp"

namespace dwe::app {

// Which list the title screen shows: the empty slots for a new game, or the saves to continue.
enum class SlotListMode : std::uint8_t { NewGame, Continue };

// The text keys of what a save's summary names by ID; an empty key when the packs do not know it,
// and the list shows the ID.
struct SaveNames {
  std::function<std::string(std::string_view)> species;
  std::function<std::string(std::string_view)> map;
};

// A message of the title screen: a text key, the save file it is about, and whether it asks yes or
// no.
struct SaveDialog {
  std::string text;
  std::string file;
  bool question = false;

  friend bool operator==(const SaveDialog&, const SaveDialog&) = default;
};

// What the title screen does with the save slots (docs/architecture/guardar-y-cargar.md, 3.4, 3.5
// and 4): the lists, the questions, and the game each answer starts. It knows no UI: the game
// publishes its settings and shows the documents.
class SaveFlow {
public:
  // `slots` is null when the game is not saved, and must outlive the flow.
  SaveFlow(const game::save::SaveSlots* slots, SaveNames names);

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
  // The answer to the dialog shown last: the game to start, or nothing to go back to the list.
  [[nodiscard]] std::optional<PlayStart> answer(bool yes);
  // The notice of a game that did not start, or whose new game could not be saved to its slot
  // (`saveFailed`); the error itself goes to the log.
  [[nodiscard]] static SaveDialog startFailed(const PlayStart& start, bool saveFailed);

private:
  const game::save::SaveSlots* slots_;
  SaveNames names_;
  SlotListMode mode_ = SlotListMode::NewGame;
  std::vector<game::save::SlotInfo> entries_;
  std::optional<PlayStart> pending_;
};

// A dialog as the settings of the dialog document.
[[nodiscard]] screens::ScreenSettings dialogSettings(const SaveDialog& dialog);

} // namespace dwe::app
