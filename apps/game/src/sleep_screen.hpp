#pragma once

#include <dwe/game/field/rules.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/ui/ui.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

#include "game_saves.hpp"

namespace dwe::app {

inline constexpr std::string_view SleepMenuDocument = "sleep_menu";

// The pause menu's Sleep (docs/architecture/guardar-y-cargar.md, 3.6), as partner.c's
// partnerTickSleep: the screen fades to black, the partner sleeps until morning, the sleep menu
// offers to save to the playthrough's slot, and the screen fades back in; the partner wakes up,
// and may evolve, when the player gets control back. The field and the game time wait meanwhile.
class SleepScreen {
public:
  // The session and the UI must outlive this object; `transition` is the field's.
  SleepScreen(session::GameSession& session, ui::Ui& ui,
              const game::field::TransitionRules& transition);

  // Whether the player waits on a sleep: the field and the game time wait meanwhile.
  [[nodiscard]] bool active() const;

  // How much the sleep's fade takes away from the screen, from 0 (nothing) to 1 (black).
  [[nodiscard]] float fadeLevel() const;

  void tick(double seconds);

  // Takes the pause menu's sleep, and the sleep menu's and its dialog's commands while asleep;
  // false for any other command.
  bool handle(const ui::UiCommand& command);

  // Once per frame: tells the pause menu whether the partner can sleep. `fieldIdle` is whether the
  // pause menu could open over the field (tamer.c: no battle, no fade).
  void update(bool fieldIdle);

private:
  // FadeTail: the player has control back, and the fade in draws its last frames.
  enum class Phase : std::uint8_t { Awake, FadingOut, Menu, FadingIn, FadeTail };

  [[nodiscard]] std::int32_t fadeFrame() const;
  [[nodiscard]] bool canSleep(bool fieldIdle) const;
  void setPhase(Phase phase);
  void fallAsleep();
  void showMenu();
  void follow(const SaveStep& step);
  void startFadeIn();

  session::GameSession* session_;
  ui::Ui* ui_;
  game::field::TransitionRules transition_;
  GameSaves saves_;
  Phase phase_ = Phase::Awake;
  double fadeSeconds_ = 0.0;
  // Whether the session put the partner to bed, so that the fade in wakes it up.
  bool asleep_ = false;
  // Whether the sleep menu's dialog is on screen, so its answer is this screen's.
  bool dialogShown_ = false;
  bool fieldIdle_ = false;
  std::optional<bool> canSleep_;
};

} // namespace dwe::app
