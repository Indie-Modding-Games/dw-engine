#pragma once

#include <dwe/game/field/rules.hpp>
#include <dwe/session/field_items_adapter.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/ui/ui.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

#include "field_scene.hpp"
#include "game_saves.hpp"
#include "sleep_steps.hpp"

namespace dwe::app {

inline constexpr std::string_view SleepMenuDocument = "sleep_menu";

// The pause menu's Sleep (docs/architecture/guardar-y-cargar.md, 3.6), as partner.c's
// partnerTickSleep: the partner walks up to the player, the player plays its clip, the partner
// yawns and lies down (SleepSteps), the screen fades to black, the partner sleeps until morning,
// the sleep menu offers to save to the playthrough's slot, and the screen fades back in on the
// partner standing; the partner wakes up, and may evolve, when the player gets control back. The
// game time waits meanwhile, and so does the field while the sleep menu is up; the player has no
// control.
class SleepScreen {
public:
  // The session and the UI must outlive this object; `transition` is the field's.
  SleepScreen(session::GameSession& session, ui::Ui& ui,
              const game::field::TransitionRules& transition);

  // Whether the player waits on a sleep: the field and the game time wait meanwhile.
  [[nodiscard]] bool active() const;

  // Whether the field still moves, without the player: the partner walks up, the two play their
  // clips, and the screen fades out and in (partner.c: partnerTickSleep runs in the field's state).
  [[nodiscard]] bool fieldRuns() const;

  // How much the sleep's fade takes away from the screen, from 0 (nothing) to 1 (black).
  [[nodiscard]] float fadeLevel() const;

  void tick(double seconds);

  // While the field runs for the sleep: ticks it with no input and the game time stopped, plays the
  // player's and the partner's clips, with `arrived`'s sound once the partner is beside the player,
  // and gives the two back to the field with the player's control.
  void tickField(FieldScene& scene, float seconds, const std::optional<session::SoundCue>& arrived);

  // Takes the pause menu's sleep, and the sleep menu's and its dialog's commands while asleep;
  // false for any other command.
  bool handle(const ui::UiCommand& command);

  // Once per frame: tells the pause menu whether the partner can sleep. `fieldIdle` is whether the
  // pause menu could open over the field (tamer.c: no battle, no fade).
  void update(bool fieldIdle);

private:
  // Settling: the player's and the partner's clips before the fade out (SleepSteps). FadeTail: the
  // player has control back, and the fade in draws its last frames.
  enum class Phase : std::uint8_t {
    Awake,
    FadingOut,
    Menu,
    FadingIn,
    FadeTail,
    WalkingUp,
    Settling
  };

  [[nodiscard]] std::int32_t fadeFrame() const;
  [[nodiscard]] bool canSleep(bool fieldIdle) const;
  void setPhase(Phase phase);
  void fallAsleep();
  void showMenu();
  void follow(const SaveStep& step);
  void startFadeIn();
  void apply(FieldScene& scene, const SleepSteps::Order& order);

  session::GameSession* session_;
  ui::Ui* ui_;
  game::field::TransitionRules transition_;
  GameSaves saves_;
  Phase phase_ = Phase::Awake;
  // Whether the next tickField holds the field for the sleep and sends the partner to the player.
  bool holdPending_ = false;
  SleepSteps steps_;
  // What the next tickField does to the field: show the partner standing for the fade in, and give
  // the player and the partner back with control.
  bool wakePending_ = false;
  bool releasePending_ = false;
  double fadeSeconds_ = 0.0;
  // Whether the session put the partner to bed, so that the fade in wakes it up.
  bool asleep_ = false;
  // Whether the sleep menu's dialog is on screen, so its answer is this screen's.
  bool dialogShown_ = false;
  bool fieldIdle_ = false;
  std::optional<bool> canSleep_;
};

} // namespace dwe::app
