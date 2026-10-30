#pragma once

#include <dwe/core/error.hpp>
#include <dwe/session/battle_registry.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <utility>

#include "game_saves.hpp"

namespace dwe::app {

// The boxes of the save machine, as main_menu.c: tickMainMenu's menus 0x3C to 0x46 (the memory
// card's checks, 0x3F to 0x43, are the PC's save to the playthrough's slot).
enum class SaveMachineView : std::uint8_t {
  Closed,
  Register,        // 0x3C: the partner's record, «Do you want to register?»
  Slots,           // 0x3D: «Select registration slot», ten rows of the registry
  ConfirmRegister, // 0x3E: «Do you want to register <name>», the slot's note
  NoSlot,          // 0x44: a playthrough with no slot (no memory card) cannot register
  InTraining,      // 0x46: a partner below Rookie cannot register
  SaveQuestion,    // 0x45: «Do you want to save?»
  SaveDialog,      // 0x42 and the save's notices: a question or a notice of GameSaves
};

[[nodiscard]] std::string_view saveMachineViewName(SaveMachineView view);

// What the machine needs to know when it opens (main_menu.c: openSaveMachine).
struct SaveMachineFacts {
  // The playthrough saves to a slot (MEMORY_CARD_ID and MEMORY_CARD_SLOT set).
  bool hasSlot = false;
  // The partner's level is below Rookie (its species' level below 3).
  bool belowRookie = false;
  Playthrough playthrough;
};

// The save machine's menus (main_menu.c: openSaveMachine, tickMainMenu 0x3C to 0x46,
// registerBattleData), without UI: which box is open, what each answer does, and the registry and
// save they write. The answers are rows from 0 (yes is 0 and no is 1); cancel is the cancel button.
class SaveMachineFlow {
public:
  // The partner as it is registered now, read when the player registers it.
  using PartnerRecord = std::function<core::Result<session::RegisteredPartner>()>;

  // The no-slot notice closes by itself after this many of the original's frames (main_menu.c:
  // tickMainMenu 0x44, MAIN_MENU_TICKS >= 0x79), one a tick.
  static constexpr std::int32_t NoSlotFrames = 0x79;

  // `registry` must outlive the flow.
  SaveMachineFlow(GameSaves saves, session::BattleRegistry& registry, PartnerRecord partner);

  [[nodiscard]] SaveMachineView view() const { return view_; }
  [[nodiscard]] bool active() const { return view_ != SaveMachineView::Closed; }
  // The registration slot chosen last (from 0), where the slot list's cursor starts.
  [[nodiscard]] std::size_t slot() const { return slot_; }
  // The save's dialog on screen in the SaveDialog view.
  [[nodiscard]] const std::optional<SaveDialog>& dialog() const { return dialog_; }

  void open(const SaveMachineFacts& facts);
  // The row confirmed in the open box.
  void choose(std::size_t row);
  void cancel();
  // One of the original's frames (20 a second).
  void tick();
  // How the save ended once the machine wrote, once.
  [[nodiscard]] std::optional<SaveResult> takeResult() { return std::exchange(result_, {}); }

private:
  void registerAndSave();
  void save();
  void follow(const SaveStep& step);
  void close();

  GameSaves saves_;
  session::BattleRegistry* registry_;
  PartnerRecord partner_;
  Playthrough playthrough_;
  SaveMachineView view_ = SaveMachineView::Closed;
  std::size_t slot_ = 0;
  std::int32_t frames_ = 0;
  std::optional<SaveDialog> dialog_;
  std::optional<SaveResult> result_;
  // The record the registration replaced, put back when the save after it does not happen.
  std::optional<std::optional<session::RegisteredPartner>> replaced_;
};

} // namespace dwe::app
