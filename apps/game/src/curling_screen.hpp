#pragma once

#include <dwe/game/curling/curling.hpp>
#include <dwe/game/story/story_ports.hpp>
#include <dwe/session/curling_adapter.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/ui/ui.hpp>

#include <string_view>

namespace dwe::input {
class InputState;
} // namespace dwe::input

namespace dwe::app {

class FieldScene;

inline constexpr std::string_view CurlingHudDocument = "curling_hud";

// The stones game the story starts (call_partner_routine 1: kar.c, the KAR overlay): the match runs
// instead of the field at the original's 20 frames a second, the camera goes where the match sends
// it, and the scores, the prompt and the power gauge show on a document. The stones are drawn as
// markers until their models are; the throwers stand where they are.
class CurlingScreen {
public:
  // The adapter, the session, the UI and the sound (nullptr: silent) must outlive this object.
  CurlingScreen(session::CurlingAdapter& curling, session::GameSession& session, ui::Ui& ui,
                game::story::SoundPort* sound);

  // KAR_start: a match from the start. False when there is none to play (no rules), logged.
  bool start(FieldScene& scene);
  [[nodiscard]] bool active() const { return running_; }
  // Once per engine tick: the arrows and the confirm button, kept until a match frame reads them.
  void press(const input::InputState& input);
  // One logic tick.
  void tick(FieldScene& scene, double seconds);
  // Whether a match ended since the last call (its result is written for the story).
  bool takeOver();

private:
  void frame(FieldScene& scene);
  void follow(FieldScene& scene, const game::curling::CurlingEvent& event);
  void showMarkers(FieldScene& scene) const;
  void showHud();
  void end(FieldScene& scene);

  session::CurlingAdapter* curling_;
  session::GameSession* session_;
  ui::Ui* ui_;
  game::story::SoundPort* sound_;
  bool running_ = false;
  bool over_ = false;
  double frameSeconds_ = 0.0;
  // What the buttons did since the last match frame.
  game::curling::CurlingButtons buttons_;
};

} // namespace dwe::app
