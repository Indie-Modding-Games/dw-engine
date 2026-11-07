#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/game/battle/battle.hpp>
#include <dwe/game/tournament/champion_show.hpp>
#include <dwe/game/tournament/match_show.hpp>
#include <dwe/ruleset/random.hpp>
#include <dwe/session/battle_adapter.hpp>
#include <dwe/session/story_scenes.hpp>
#include <dwe/session/tournament_adapter.hpp>

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "battle_screen.hpp"
#include "field_scene.hpp"
#include "tournament_bracket_screen.hpp"
#include "tournament_match_screen.hpp"

namespace dwe::app {

// The story's tournament (call_routine 6 -> tournament.c: scriptStartTournament): it starts the
// cup on the session's tournament adapter, plays its matches one after another on the battle
// screen, gives each outcome back and, once the partner lost or won the third, tells the story,
// which reads the wins (pstat 255). The matches are fought in the arena (std_setup.c:
// STD_initializeTournament): its model and ground instead of the map, the map's characters hidden,
// and the orbit camera across the fighters (std_main.c: STD_setCameraOrbit). Before the first match
// the bracket's intro plays, before each match the board (STD_addBracket(1)) and after the final
// the champion's climb (STD_addBracket(2)); the partner is not on the field meanwhile. Each match
// opens with the intro (std_main.c: STD_runIntro: each fighter runs in under a chasing camera
// with its stats), is fought under the cup's camera (the orbit, the banner's pull-out and the
// corner view), shows TIME UP and the damage box when the time runs out
// (STD_tickBattleResultScreen) and ends with the won, lost or drawn match's letters under the
// circling camera (std_hud.c: STD_addWinScene, STD_addLoseScene; STD_startCameraIntro). After the
// champion's climb the partner stands on the podium for the champion scene
// (game::tournament::ChampionShow).
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
  // What the match shows over the arena, the partner as a fighter (its stats for the intro) and
  // the species' sizes and names; without them the match starts at once.
  void attachMatch(TournamentMatchScreen* screen, const session::BattleAdapter* battle,
                   const assets::DigimonTable* species) {
    matchScreen_ = screen;
    battleAdapter_ = battle;
    species_ = species;
  }
  // The confirm button was pressed (it cuts the intro short and the time-up notice).
  void pressConfirm() { confirm_ = true; }
  // Whether the intro or the champion scene plays: the field waits.
  [[nodiscard]] bool introPlaying() const { return intro_.has_value() || champion_.has_value(); }
  // How black the champion scene's fades make the screen, from 0 to 1.
  [[nodiscard]] float fadeLevel() const {
    return champion_ ? static_cast<float>(champion_->blackout()) : 0.0F;
  }
  // Once per engine tick: the match's show runs at the original's 20 frames a second.
  void tick(double seconds, BattleScreen& battle, FieldScene& scene);
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
  // The arena's stage seen from this camera, with the letters' model when they show.
  void showArena(FieldScene& scene, const game::tournament::ShowView& view);
  // STD_runIntro before the match: the rival on its start, the intro's fighters.
  bool startIntro(session::BattleRequest request, FieldScene& scene);
  // STD_runMatch after the intro: the battle with both fighters where the intro left them.
  void startMatch(session::StoryScenes& scenes, BattleScreen& battle, FieldScene& scene);
  void frame(BattleScreen& battle, FieldScene& scene, bool confirm);
  // The arena's song of the intro's turn (0 the partner, 1 the rival).
  void playIntroSong(std::int32_t turn);
  void watchFight(const game::battle::Battle& fight);
  // STD_setupMoveExecution's camera: tells the match camera of the moves that started.
  void noteMoveStarts(const game::battle::Battle& fight);
  void startLetters(game::tournament::MatchEnding ending);
  void showLetters(FieldScene& scene, const game::tournament::ShowView& view);
  // STD_loadChampionModels, STD_initializeChampionScene once the champion's climb is over.
  bool startChampion();
  void championFrame(FieldScene& scene);
  void clearShow(FieldScene& scene);
  [[nodiscard]] std::string speciesName(const std::string& id) const;

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
  // The champion's climb showed; the champion scene follows it.
  bool championNext_ = false;
  // The arena the cup is fought in, and where the player and the partner stood on the map.
  std::optional<session::TournamentAdapter::ArenaStage> arena_;
  game::field::CharacterState playerBefore_;
  game::field::CharacterState partnerBefore_;

  // The match's show (game::tournament::match_show.hpp).
  TournamentMatchScreen* matchScreen_ = nullptr;
  const session::BattleAdapter* battleAdapter_ = nullptr;
  const assets::DigimonTable* species_ = nullptr;
  // The cup's own rolls for its cameras and letters (rand in the original).
  ruleset::Random random_{0x3039};
  double frameSeconds_ = 0.0;
  bool confirm_ = false;
  std::optional<session::BattleRequest> request_;
  std::optional<game::tournament::MatchIntro> intro_;
  std::optional<game::tournament::MatchCamera> camera_;
  // STD_CAMERA_STATE 10 between the matches: the arena turns behind the bracket.
  game::tournament::ArenaTurnCamera turnCamera_;
  // The frame each fighter's attack was on when last seen: a lower one, or a new attack, is a move
  // that just started (STD_setupMoveExecution).
  std::array<std::optional<std::int32_t>, 2> attackFrames_{};
  std::optional<game::tournament::EndCamera> endCamera_;
  std::optional<game::tournament::TimeUpNotice> timeUp_;
  std::optional<game::tournament::MatchLetters> letters_;
  bool timeUpDone_ = false;
  // The fight as last seen (std_main.c: STD_DAMAGE, the fighters' places and sizes).
  struct Seen {
    game::tournament::ArenaPoint partner;
    game::tournament::ArenaPoint rival;
    game::tournament::ShowBody partnerBody;
    game::tournament::ShowBody rivalBody;
    std::int32_t partnerLost = 0;
    std::int32_t rivalLost = 0;
    std::int32_t seconds = -1;
    bool partnerDown = false;
    bool rivalDown = false;
    // Where each stands, its yaw and the range of the move it performs, for the technique camera.
    std::array<game::tournament::FightFigure, 2> figures{};
  };
  std::optional<Seen> seen_;
  std::string partnerName_;
  std::string rivalName_;
  std::optional<game::tournament::ShowView> view_;
  std::string lettersModel_;
  // The champion scene, the partner's figure on the podium and the podium's model.
  std::optional<game::tournament::ChampionShow> champion_;
  FighterFigure championFigure_;
  std::string podiumModel_;
};

} // namespace dwe::app
