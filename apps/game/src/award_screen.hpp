#pragma once

#include <dwe/game/story/story_ports.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/medals_adapter.hpp>
#include <dwe/ui/ui.hpp>

#include <string_view>

namespace dwe::app {

inline constexpr std::string_view AwardNoticeDocument = "award_notice";

// The award notice (tamer.c: checkMedalConditions, checkPendingAwards, tamerTickAwardSomething):
// while the player walks freely the medals are checked each frame, and a medal or a tamer level
// change that is due opens the speech box with its words and a sound; the field and the game time
// wait until the player confirms, which clears both notices.
class AwardScreen {
public:
  // The medals (nullptr: none checked), the session, the UI and the sound (nullptr: silent) must
  // outlive this object.
  AwardScreen(session::MedalsAdapter* medals, session::GameSession& session, ui::Ui& ui,
              game::story::SoundPort* sound);

  // Whether the notice is on screen: the field and the game time wait meanwhile.
  [[nodiscard]] bool active() const { return active_; }

  // One frame of the player walking freely (no script, menu, map change or course): checks the
  // medals and opens the notice when one is due.
  void tick();

  // Takes the notice's answer; false for any other command.
  bool handle(const ui::UiCommand& command);

private:
  void open(bool medal, session::TamerLevelChange change);

  session::MedalsAdapter* medals_;
  session::GameSession* session_;
  ui::Ui* ui_;
  game::story::SoundPort* sound_;
  bool active_ = false;
};

} // namespace dwe::app
