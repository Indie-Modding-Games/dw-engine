#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/game/story/story_ports.hpp>
#include <dwe/game/tournament/tournament.hpp>
#include <dwe/input/input_state.hpp>
#include <dwe/screens/screen_settings.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/story_adapter.hpp>
#include <dwe/session/story_scenes.hpp>
#include <dwe/session/tournament_adapter.hpp>
#include <dwe/ui/ui.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "story_text_rows.hpp"

namespace dwe::app {

class FieldScene;

inline constexpr std::string_view TournamentScheduleDocument = "tournament_schedule";

struct TournamentScheduleScreenDesc {
  // They must outlive the screen; without a tournament adapter that has registration rules the
  // story's routine 18 closes at once.
  session::TournamentAdapter* tournament = nullptr;
  session::GameSession* session = nullptr;
  session::StoryAdapter* story = nullptr;
  const assets::DigimonTable* species = nullptr;
  ui::Ui* ui = nullptr;
};

// The registration at the arena's counter (call_routine 18 -> dget/dget.c: initTournamentSchedule,
// tickTournamentSchedule, initTournamentInfo), on the tournament_schedule document: the five days'
// schedule with the cups the partner may enter, a cup's text with the pause key (the original's
// Start), and once a cup is picked its text, the receptionist's question and the player's choice
// to enter. The field and the game time wait meanwhile.
class TournamentScheduleScreen {
public:
  explicit TournamentScheduleScreen(TournamentScheduleScreenDesc desc) : desc_(desc) {}

  [[nodiscard]] bool active() const { return phase_ != Phase::Closed; }
  // Takes the story's TournamentSchedule screen; false for any other screen.
  bool take(const session::StoryScreenRequest& request, const FieldScene& scene);
  // Takes the document's commands while it is open; false for any other command.
  bool handle(const ui::UiCommand& command);
  // Once per engine tick: the pause key (P) shows the cup under the cursor.
  void press(const input::InputState& input);
  // Once per frame: publishes the screen, and tells the story once it closed.
  void update(session::StoryScenes& scenes);

private:
  enum class Phase : std::uint8_t { Closed, Schedule, Peek, Question, Choice };

  void act(std::string_view action);
  void actInSchedule(std::string_view action);
  void pick();
  void answer(std::size_t option);
  void close();
  void sound(std::int32_t sound) const;
  [[nodiscard]] const std::optional<game::tournament::ScheduleCell>& cell() const;
  [[nodiscard]] std::string partnerName() const;
  [[nodiscard]] std::vector<TextRow> talkRows() const;
  [[nodiscard]] std::vector<TextRow> infoRows() const;
  [[nodiscard]] screens::ScreenSettings settings() const;

  TournamentScheduleScreenDesc desc_;
  Phase phase_ = Phase::Closed;
  game::tournament::Schedule schedule_;
  std::size_t column_ = 0;
  std::size_t row_ = 0;
  // The cup picked and whether the partner is too strong for it (tournamentCheckFair).
  std::int32_t cup_ = 0;
  bool overlevel_ = false;
  // The choice's cursor: 0 enter, 1 don't.
  std::size_t choice_ = 0;
  // The receptionist (variable 254's NPC), by species, for the name over its lines.
  std::string owner_;
  bool waiting_ = false;
};

} // namespace dwe::app
