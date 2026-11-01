#pragma once

#include <dwe/screens/screen_settings.hpp>
#include <dwe/ui/ui.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dwe::app {

inline constexpr std::string_view TournamentMatchDocument = "tournament_match";

// What a cup match shows over the arena, on the tournament_match document: the intro's name and
// stats (std_main.c: STD_renderIntroName, STD_renderIntroStats), TIME UP and the damage box once
// the time ran out (STD_tickBattleResultScreen, STD_renderTimeoutWindow) and the lost match's
// wipe (std_hud.c: STD_renderLoseScene). StoryTournament works out each frame
// (game::tournament::MatchIntro, TimeUpNotice, MatchLetters); this only draws it.
struct TournamentMatchView {
  struct Intro {
    // The name as far as it came in, and its row's y (the original's pixels from the centre).
    std::string name;
    std::int32_t nameY = 0;
    // HP, MP, offense, defense, speed and brains as they count up; nothing before the box opens.
    std::optional<std::array<std::int32_t, 6>> stats;
    bool counted = false;
  };
  struct TimeUp {
    bool box = false;
    std::string partner;
    std::string rival;
    // STD_DAMAGE[1] (the HP the rival lost) and STD_DAMAGE[0] (the partner's).
    std::int32_t dealt = 0;
    std::int32_t taken = 0;
  };
  std::optional<Intro> intro;
  std::optional<TimeUp> timeUp;
  std::vector<std::int32_t> wipe;
};

class TournamentMatchScreen {
public:
  explicit TournamentMatchScreen(ui::Ui* ui) : ui_(ui) {}

  void show(const TournamentMatchView& view);
  void hide();
  [[nodiscard]] bool shown() const { return shown_; }

private:
  [[nodiscard]] screens::ScreenSettings settings(const TournamentMatchView& view) const;

  ui::Ui* ui_ = nullptr;
  bool shown_ = false;
  std::optional<screens::ScreenSettings> last_;
};

} // namespace dwe::app
