#pragma once

#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/game/save/slots.hpp>
#include <dwe/game/save/storage.hpp>
#include <dwe/ui/ui.hpp>

#include <memory>
#include <optional>
#include <string_view>

#include "app_flow.hpp"
#include "options.hpp"
#include "save_flow.hpp"

namespace dwe::app {

inline constexpr std::string_view SaveSlotsDocument = "save_slots";
inline constexpr std::string_view DialogDocument = "dialog";

// The title screen with no game: the main menu, the slot lists and their dialogs, shown and hidden
// by the game as SaveFlow decides (docs/architecture/guardar-y-cargar.md, 3.4 and 3.5).
class TitleScreen {
public:
  // `keepsSaves` as startGame takes it: without a saves folder a new game starts with no list.
  TitleScreen(const assets::VirtualFileSystem& files, const Options& options, bool keepsSaves,
              ui::Ui& ui);

  // Shows the main menu, with Continue greyed when there is no save.
  void show();
  // Hides every document of the title screen, for a movie.
  void hide();

  // Handles a command of the title screen's documents; false when it is not one of theirs.
  bool handle(const ui::UiCommand& command);

  // The game a command chose to start, once.
  [[nodiscard]] std::optional<PlayStart> takeStart();

  // Tells the player a chosen game did not start, or could not be saved to its slot, and goes back
  // to where it was chosen.
  void startFailed(const PlayStart& start, bool saveFailed);

private:
  void showList(SlotListMode mode);
  void showDialog(const SaveDialog& dialog);
  // Back from a dialog that started nothing: to the list, or to the main menu without one.
  void back();

  ui::Ui* ui_;
  std::unique_ptr<game::save::DiskSaveStorage> storage_;
  std::optional<game::save::SaveSlots> slots_;
  SaveFlow flow_;
  std::optional<PlayStart> start_;
  bool listShown_ = false;
};

} // namespace dwe::app
