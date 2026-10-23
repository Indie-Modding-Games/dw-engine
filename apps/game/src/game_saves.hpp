#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/save/slots.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>

#include "save_flow.hpp"

namespace dwe::app {

// How the player's last save in the game ended (save.last_result).
enum class SaveResult : std::uint8_t { Saved, Failed, NotSaved };

[[nodiscard]] std::string_view saveResultName(SaveResult result);

// What a save in the game asks next: a dialog to show, the result once the save ended, or both (a
// notice that it failed). With neither, the player goes back to where the save was asked.
struct SaveStep {
  std::optional<SaveDialog> dialog;
  std::optional<SaveResult> result;

  friend bool operator==(const SaveStep&, const SaveStep&) = default;
};

// The playthrough being played: the manual slot it saves to, and when it started, which tells its
// saves from another playthrough's in the same slot.
struct Playthrough {
  std::optional<game::save::SlotId> home;
  std::chrono::sys_seconds started;
};

// The saves the player asks for in the game (docs/architecture/guardar-y-cargar.md, 3.6 and 4):
// to the playthrough's slot, once the slot is checked to still hold this playthrough, so a save
// never writes over what changed outside the game without asking. It knows no UI.
class GameSaves {
public:
  using Write = std::function<core::Result<void>(game::save::SlotId)>;

  // `slots` is null when the game is not saved, and must outlive this object. `write` saves the
  // playthrough to a slot.
  GameSaves(const game::save::SaveSlots* slots, Write write);

  // Saves to the playthrough's slot, or asks first when the slot is damaged or holds another
  // playthrough. A slot whose file is gone is written again; a newer game's is never written.
  [[nodiscard]] SaveStep save(const Playthrough& playthrough);
  // The answer to the dialog the last step showed.
  [[nodiscard]] SaveStep answer(bool yes);

private:
  [[nodiscard]] SaveStep write(game::save::SlotId slot);

  const game::save::SaveSlots* slots_;
  Write write_;
  // The slot a question asks to write over.
  std::optional<game::save::SlotId> pending_;
};

} // namespace dwe::app
