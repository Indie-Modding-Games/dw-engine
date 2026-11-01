#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/game/tournament/bracket_animation.hpp>
#include <dwe/screens/screen_settings.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/tournament_adapter.hpp>
#include <dwe/ui/ui.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace dwe::app {

inline constexpr std::string_view TournamentBracketDocument = "tournament_bracket";

struct TournamentBracketScreenDesc {
  // They must outlive the screen; without a bracket in the tournament adapter the cups are fought
  // without the board.
  session::TournamentAdapter* tournament = nullptr;
  session::GameSession* session = nullptr;
  const assets::DigimonTable* species = nullptr;
  ui::Ui* ui = nullptr;
};

// The cup's bracket on the tournament_bracket document (std_setup.c: STD_tournamentMain;
// std_main.c: STD_addBracketIntro, STD_addBracket, STD_removeBracket): the grade's card over the
// board as the cup opens, then before each of the partner's matches the board plays the other
// matches and the partner's comes up, and after the final the champion climbs to the top. The
// pictures are the player's extraction; the animation is game::tournament::BracketAnimation at the
// original's 20 frames a second. The field and the game time wait while it plays.
class TournamentBracketScreen {
public:
  explicit TournamentBracketScreen(TournamentBracketScreenDesc desc) : desc_(desc) {}

  // The cup started: the intro plays. False (logged) when there is no bracket to show.
  bool open();
  // STD_addBracket(1) before a match, STD_addBracket(2) after the final.
  void show(game::tournament::BracketShow show);
  // The partner's match is over (STD_MATCH_RESULT).
  void matchOver(bool won);
  // The tournament is over: the board goes and the map's music comes back.
  void close();
  [[nodiscard]] bool opened() const { return animation_.has_value(); }
  // The intro, a board or the champion's fade plays: the tournament waits.
  [[nodiscard]] bool playing() const { return phase_ != Phase::Hidden; }
  // Once per engine tick.
  void tick(double seconds);

private:
  enum class Phase : std::uint8_t { Hidden, Intro, Board, Tail };

  void frame();
  void hide();
  void playMusic(std::int32_t track);
  void stopMusic() const;
  void sounds();
  [[nodiscard]] std::string pageSource(std::int32_t page, std::int32_t palette) const;
  [[nodiscard]] std::string speciesName(std::int32_t number) const;
  [[nodiscard]] std::string label() const;
  [[nodiscard]] screens::ScreenSettings settings() const;

  TournamentBracketScreenDesc desc_;
  std::optional<game::tournament::BracketAnimation> animation_;
  std::optional<game::tournament::BracketRules> rules_;
  Phase phase_ = Phase::Hidden;
  game::tournament::BracketShow show_ = game::tournament::BracketShow::Match;
  std::string card_;
  std::array<std::int32_t, game::tournament::Entrants> species_{};
  std::size_t round_ = 0;
  double seconds_ = 0.0;
  std::int32_t tailFrames_ = 0;
  bool musicPlayed_ = false;
};

} // namespace dwe::app
