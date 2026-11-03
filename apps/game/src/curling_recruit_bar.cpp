#include "curling_recruit_bar.hpp"

#include <algorithm>
#include <cstddef>
#include <ranges>

namespace dwe::app {

namespace {

namespace curling = game::curling;

// A stone thrown off or never placed (Stone::state -101): it cannot score again.
bool stillInGame(const curling::Stone& stone) {
  return stone.state >= -100;
}

std::int32_t throwsLeft(const curling::Team& team) {
  return std::max(static_cast<std::int32_t>(curling::StonesPerTeam) - team.thrown, 0);
}

} // namespace

CurlingRecruitView curlingRecruitOf(const curling::Match* match, const curling::CurlingRules& rules,
                                    bool enabled) {
  // Only the first opponent joins the city when beaten (script 206 section 0x0a, trigger 257).
  if (!enabled || match == nullptr || match->finished() ||
      match->opponent() != curling::Opponent::Penguinmon) {
    return {};
  }
  const auto& teams = match->teams();
  const curling::Team& player = teams.at(curling::PlayerTeam);
  const curling::Team& opponent = teams.at(curling::OpponentTeam);
  // KAR_classifyStoneRings recounts the rings after every throw: at best each of the player's
  // stones still in the game rests on the ring worth the most.
  std::int32_t best = 0;
  for (const curling::Ring& ring : rules.rings) {
    best = std::max(best, ring.points);
  }
  const auto inGame = std::ranges::count_if(player.stones, stillInGame);
  return {.shown = true,
          .player = player.score,
          .opponent = opponent.score,
          .playerThrows = throwsLeft(player),
          .opponentThrows = throwsLeft(opponent),
          .perfectReachable = player.score >= rules.perfectScore ||
                              static_cast<std::int32_t>(inGame) * best >= rules.perfectScore};
}

screens::ScreenSettings settingsOf(const CurlingRecruitView& view) {
  return {
      {.name = "curling_recruit.shown", .value = view.shown},
      {.name = "curling_recruit.player", .value = static_cast<double>(view.player)},
      {.name = "curling_recruit.opponent", .value = static_cast<double>(view.opponent)},
      {.name = "curling_recruit.player_throws", .value = static_cast<double>(view.playerThrows)},
      {.name = "curling_recruit.opponent_throws",
       .value = static_cast<double>(view.opponentThrows)},
      {.name = "curling_recruit.perfect", .value = view.perfectReachable},
  };
}

} // namespace dwe::app
