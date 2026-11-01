#pragma once

#include <dwe/game/battle/battle.hpp>
#include <dwe/session/story_scenes.hpp>
#include <dwe/session/tournament_adapter.hpp>

#include <optional>
#include <string>

#include "battle_screen.hpp"
#include "field_scene.hpp"
#include "tournament_bracket_screen.hpp"

namespace dwe::app {

// The story's tournament (call_routine 6 -> tournament.c: scriptStartTournament): it starts the
// cup on the session's tournament adapter, plays its matches one after another on the battle
// screen, gives each outcome back and, once the partner lost or won the third, tells the story,
// which reads the wins (pstat 255). The matches are fought in the arena (std_setup.c:
// STD_initializeTournament): its model and ground instead of the map, the map's characters hidden,
// and the orbit camera across the fighters (std_main.c: STD_setCameraOrbit). Before the first match
// the bracket's intro plays, before each match the board (STD_addBracket(1)) and after the final
// the champion's climb (STD_addBracket(2)); the partner is not on the field meanwhile.
class StoryTournament {
public:
  // Both must outlive this object; without them the story's tournaments close at once.
  void attach(session::TournamentAdapter* tournament, session::GameSession* session) {
    tournament_ = tournament;
    session_ = session;
  }
  // The bracket's screen; without it (or without a bracket in the extraction) the matches follow
  // one another at once.
  void attachBracket(TournamentBracketScreen* bracket) { bracket_ = bracket; }
  // Takes the story's Tournament screen; false for any other screen.
  bool take(const session::StoryScreenRequest& request, session::StoryScenes& scenes,
            FieldScene& scene);
  // Whether a match of the tournament is on the battle screen: its outcome is the tournament's.
  [[nodiscard]] bool matchRunning() const { return matchRunning_; }
  // A match's outcome, once its battle screen closed.
  void matchOver(game::battle::Outcome outcome);
  // Starts the next match or ends the tournament.
  void update(session::StoryScenes& scenes, BattleScreen& battle, FieldScene& scene);

private:
  void end(session::StoryScenes& scenes, FieldScene& scene);
  // Enters the arena (STD_initializeTournament) and leaves it for the map as it was.
  void enterArena(FieldScene& scene);
  void leaveArena(FieldScene& scene);
  // The arena's stage with the orbit camera across these fighters (original axes).
  void showArena(FieldScene& scene, game::tournament::ArenaPoint partner,
                 game::tournament::ArenaPoint rival);

  session::TournamentAdapter* tournament_ = nullptr;
  session::GameSession* session_ = nullptr;
  TournamentBracketScreen* bracket_ = nullptr;
  // The story waits on the tournament; a match is due; a match plays.
  bool running_ = false;
  bool matchDue_ = false;
  bool matchRunning_ = false;
  // The board before the due match played already; the champion's climb is due.
  bool boardShown_ = false;
  bool championDue_ = false;
  // The arena the cup is fought in, and where the player and the partner stood on the map.
  std::optional<session::TournamentAdapter::ArenaStage> arena_;
  game::field::CharacterState playerBefore_;
  game::field::CharacterState partnerBefore_;
};

} // namespace dwe::app
