#pragma once

#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/game/save/slots.hpp>
#include <dwe/game/save/storage.hpp>
#include <dwe/ui/ui.hpp>

#include <memory>
#include <optional>
#include <string_view>

#include "app_flow.hpp"
#include "new_game_mode.hpp"
#include "options.hpp"
#include "save_flow.hpp"
#include "ui_skin_setting.hpp"

namespace dwe::app {

inline constexpr std::string_view SaveSlotsDocument = "save_slots";
inline constexpr std::string_view DialogDocument = "dialog";
inline constexpr std::string_view NewGameRulesDocument = "new_game_rules";
inline constexpr std::string_view NewGameModeDocument = "new_game_mode";

// The title screen with no game: the main menu, the slot lists and their dialogs, shown and hidden
// by the game as SaveFlow decides (docs/architecture/guardar-y-cargar.md, 3.4 and 3.5), and the
// pause menu's options page over it (title_options.hpp).
class TitleScreen {
public:
  // `keepsSaves` as startGame takes it: without a saves folder a new game starts with no list.
  TitleScreen(const assets::VirtualFileSystem& files, const Options& options, bool keepsSaves,
              ui::Ui& ui);

  // Shows the main menu, with Continue greyed when there is no save.
  void show();
  // Shows the title screen from its start: the original look's "Press Start" screen first, then
  // the main menu (main.c: main, runLandingScreen then runMainMenu); a look without that screen
  // shows the main menu. While the skin is to be asked (setAsksSkin), the skin picker comes first.
  void showBoot();
  // Hides every document of the title screen, for a movie.
  void hide();

  // Handles a command of the title screen's documents; false when it is not one of theirs.
  bool handle(const ui::UiCommand& command);

  // Whether the "Press Start" screen shows and waits for Start (runLandingScreen), as the main
  // menu's script says with title_landing: only there does the boot movie come back on its own.
  [[nodiscard]] bool landing() const { return landing_; }

  // Whether a new game first asks its mode (ui/new_game_mode, only when the packs have more than
  // one) and its rules (ui/new_game_rules): always for the player; a test route only when it says
  // so (new_game_rules), the others start the original's story with the command line's ruleset.
  void setAsksRules(bool asks) { asksRules_ = asks; }

  // Whether the title starts by asking the UI skin (ui/skin_picker, an addition): on the player's
  // first run (asksUiSkin), before the language box; a test route only when it says so
  // (skin_first). Asked once: the confirmed skin is the player's from then on.
  void setAsksSkin(bool asks) { skinPicker_ = SkinPickerStep{asks}; }

  // The game a command chose to start, once.
  [[nodiscard]] std::optional<PlayStart> takeStart();

  // Tells the player a chosen game did not start, or could not be saved to its slot, and goes back
  // to where it was chosen.
  void startFailed(const PlayStart& start, bool saveFailed);

private:
  void showMenu(bool boot);
  // The options page over the title (TitleOptionsCommand).
  void showOptions();
  // fresh: opened from the main menu, so the list starts over at its first page and row instead of
  // the row a question or the rules came back to.
  void showList(SlotListMode mode, bool fresh = false);
  void showDialog(const SaveDialog& dialog);
  // Back from a dialog that started nothing: to the list, or to the main menu without one.
  void back();
  // A chosen game starts, or a new one asks its mode and rules first.
  void startOrAskRules(std::optional<PlayStart> start);
  // The new game waiting goes on in a mode: it asks its rules, or starts.
  void chooseMode(std::string_view mode);
  void showRules();

  ui::Ui* ui_;
  std::unique_ptr<game::save::DiskSaveStorage> storage_;
  std::optional<game::save::SaveSlots> slots_;
  SaveFlow flow_;
  std::optional<PlayStart> start_;
  // The new game waiting for its mode or its rules.
  std::optional<PlayStart> pending_;
  NewGameModes modes_;
  bool asksRules_ = true;
  SkinPickerStep skinPicker_{false};
  // Whether the packs have the ruleset with the Mega step, which the rules offer.
  bool megaOffered_ = false;
  // Whether the packs have the rulesets of every species as a partner, which the rules offer on.
  bool allOffered_ = false;
  bool listShown_ = false;
  bool optionsShown_ = false;
  bool landing_ = false;
};

} // namespace dwe::app
