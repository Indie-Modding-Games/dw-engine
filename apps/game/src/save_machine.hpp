#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/session/battle_adapter.hpp>
#include <dwe/session/battle_registry.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/ui/ui.hpp>

#include <string_view>

#include "save_machine_flow.hpp"

namespace dwe::app {

// save_machine(""): opens the save machine as script routine 48 does, from a route or the console.
inline constexpr std::string_view SaveMachineCommand = "save_machine";

inline constexpr std::string_view SaveMachineDocument = "save_machine";

// The save machine of the field (docs/architecture/guardar-y-cargar.md, 3.7), opened by script
// routine 48 (script_interp.c, main_menu.c: openSaveMachine and tickSaveMachine): the partner's
// registration for versus battles and the save, in the boxes of the save_machine document. Its
// menus are SaveMachineFlow's; this object shows them and takes the document's answers. The field
// and the game time wait while it is open.
class SaveMachine {
public:
  struct Desc {
    session::GameSession* session = nullptr;
    ui::Ui* ui = nullptr;
    // The partner's fighter (its stats and moves) and the registry it is registered in.
    session::BattleAdapter* battle = nullptr;
    session::BattleRegistry* registry = nullptr;
    // The names of the species, for the boxes; may be null.
    const assets::DigimonTable* species = nullptr;
  };

  // Everything in `desc` must outlive the machine.
  explicit SaveMachine(const Desc& desc);

  // Whether the machine's boxes are on screen.
  [[nodiscard]] bool active() const { return flow_.active(); }

  void open();

  // The document's answers (save_machine_choose with the row, save_machine_cancel); false for any
  // other command.
  bool handle(const ui::UiCommand& command);
  // Counts the original's frames, 20 a second, for the notice that closes by itself.
  void tick(double seconds);
  // Whether the machine closed since the last call: the script that opened it goes on.
  [[nodiscard]] bool takeClosed();

private:
  void publish();
  void follow();

  Desc desc_;
  SaveMachineFlow flow_;
  double frameSeconds_ = 0.0;
  bool shown_ = false;
  bool closed_ = false;
};

} // namespace dwe::app
