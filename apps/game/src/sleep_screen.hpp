#pragma once

#include <dwe/session/game_session.hpp>
#include <dwe/ui/ui.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

#include "game_saves.hpp"

namespace dwe::app {

inline constexpr std::string_view SleepMenuDocument = "sleep_menu";

// The pause menu's Sleep (docs/architecture/guardar-y-cargar.md, 3.6): the screen fades to black,
// the partner sleeps until morning, the sleep menu offers to save to the playthrough's slot, and
// the screen fades back in. The field and the game time wait from the first fade to the last.
class SleepScreen {
public:
  // The session and the UI must outlive this object.
  SleepScreen(session::GameSession& session, ui::Ui& ui);

  // Whether a sleep runs: the field and the game time wait meanwhile.
  [[nodiscard]] bool active() const { return phase_ != Phase::Awake; }

  // How much the sleep's fade takes away from the screen, from 0 (nothing) to 1 (black).
  [[nodiscard]] float fadeLevel() const;

  // One logic tick of the fades.
  void tick(double seconds);

  // Takes the pause menu's sleep, and the sleep menu's and its dialog's commands while asleep;
  // false for any other command.
  bool handle(const ui::UiCommand& command);

  // Once per frame: tells the pause menu whether the partner can sleep.
  void update();

private:
  enum class Phase : std::uint8_t { Awake, FadingOut, Menu, FadingIn };

  void fallAsleep();
  void showMenu();
  void follow(const SaveStep& step);
  void wakeUp();

  session::GameSession* session_;
  ui::Ui* ui_;
  GameSaves saves_;
  Phase phase_ = Phase::Awake;
  // Seconds into the current fade.
  double fadeSeconds_ = 0.0;
  // Whether the sleep menu's dialog is on screen, so its answer is this screen's.
  bool dialogShown_ = false;
  std::optional<bool> canSleep_;
};

} // namespace dwe::app
