#pragma once

#include <dwe/game/story/story_ports.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/medals_adapter.hpp>
#include <dwe/ui/ui.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace dwe::app {

inline constexpr std::string_view AwardNoticeDocument = "award_notice";

// The award notice (tamer.c: checkMedalConditions, checkPendingAwards, tamerTickAwardSomething):
// while the player walks freely the medals are checked each frame, and a medal or a tamer level
// change that is due opens the speech box with its words and a sound; the field and the game time
// wait until the player confirms, which clears both notices. The partner falling sick opens the
// same box with its name and "is Sick!" (partner_impl.c: tickSicknessMechanics sets the player's
// state 20 with that line in the text area).
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

  // The partner just fell sick: its notice opens on the next tick() (a medal's or the player
  // level's due with it takes the box, as the original clears the text area for them).
  // `partnerName` is the name shown before the line.
  void noteSickness(std::string partnerName);

  // Takes the notice's answer; false for any other command.
  bool handle(const ui::UiCommand& command);

private:
  void open(bool medal, session::TamerLevelChange change);
  void openSickness();
  void show(std::string_view lines, double color, std::string_view name);

  session::MedalsAdapter* medals_;
  session::GameSession* session_;
  ui::Ui* ui_;
  game::story::SoundPort* sound_;
  bool active_ = false;
  std::optional<std::string> sickPartner_;
};

} // namespace dwe::app
