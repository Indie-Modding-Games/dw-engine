#pragma once

#include <dwe/game/battle/battle.hpp>
#include <dwe/session/story_scenes.hpp>
#include <dwe/session/tournament_adapter.hpp>

#include "battle_screen.hpp"
#include "field_scene.hpp"

namespace dwe::app {

// The story's tournament (call_routine 6 -> tournament.c: scriptStartTournament): it starts the
// cup on the session's tournament adapter, plays its matches one after another on the battle
// screen, gives each outcome back and, once the partner lost or won the third, tells the story,
// which reads the wins (pstat 255).
class StoryTournament {
public:
  // Both must outlive this object; without them the story's tournaments close at once.
  void attach(session::TournamentAdapter* tournament, session::GameSession* session) {
    tournament_ = tournament;
    session_ = session;
  }
  // Takes the story's Tournament screen; false for any other screen.
  bool take(const session::StoryScreenRequest& request, session::StoryScenes& scenes);
  // Whether a match of the tournament is on the battle screen: its outcome is the tournament's.
  [[nodiscard]] bool matchRunning() const { return matchRunning_; }
  // A match's outcome, once its battle screen closed.
  void matchOver(game::battle::Outcome outcome);
  // Starts the next match or ends the tournament.
  void update(session::StoryScenes& scenes, BattleScreen& battle, const FieldScene& scene);

private:
  void end(session::StoryScenes& scenes);

  session::TournamentAdapter* tournament_ = nullptr;
  session::GameSession* session_ = nullptr;
  // The story waits on the tournament; a match is due; a match plays.
  bool running_ = false;
  bool matchDue_ = false;
  bool matchRunning_ = false;
};

} // namespace dwe::app
