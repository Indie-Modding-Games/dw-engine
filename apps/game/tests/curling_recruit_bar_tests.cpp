#include <dwe/assets/file_system.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/curling/curling.hpp>
#include <dwe/ruleset/random.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

#include "curling_recruit_bar.hpp"

using dwe::app::curlingRecruitOf;
using dwe::app::CurlingRecruitView;
using namespace dwe::game::curling;

namespace {

CurlingRules originalRules() {
  const auto bytes = dwe::assets::DiskFileSystem{}.readFile(std::filesystem::path{DWE_GAME_PACK} /
                                                            "rules" / "original" / "curling.yaml");
  REQUIRE(bytes);
  auto node = dwe::core::parseYaml(
      std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()});
  REQUIRE(node);
  auto rules = parseCurlingRules(
      {.name = "curling", .owner = "test", .file = "curling.yaml", .node = *node});
  REQUIRE_MESSAGE(rules, (rules ? std::string{} : rules.error().message));
  return *rules;
}

constexpr std::int32_t Waiting = -1;
constexpr std::int32_t Out = -101;
constexpr std::int32_t Resting = 1;

// Each team with `thrown` stones on the ice (resting) and the rest in its rack.
std::array<Team, Teams> rink(std::int32_t playerThrown, std::int32_t opponentThrown) {
  std::array<Team, Teams> teams{};
  for (std::size_t team = 0; team < Teams; ++team) {
    const std::int32_t thrown = team == PlayerTeam ? playerThrown : opponentThrown;
    teams.at(team).thrown = thrown;
    for (std::size_t index = 0; index < StonesPerTeam; ++index) {
      teams.at(team).stones.at(index).state = std::cmp_less(index, thrown) ? Resting : Waiting;
      teams.at(team).stones.at(index).weight = 30;
    }
  }
  return teams;
}

} // namespace

TEST_CASE("the stones game's recruit bar shows only during a match, and not with the option off") {
  const CurlingRules rules = originalRules();
  dwe::ruleset::Random random{3};
  CHECK_FALSE(curlingRecruitOf(nullptr, rules, true).shown);
  Match match{rules, Opponent::Penguinmon, random};
  const CurlingRecruitView start = curlingRecruitOf(&match, rules, true);
  CHECK(start.shown);
  CHECK(start.player == 0);
  CHECK(start.opponent == 0);
  CHECK(start.playerThrows == 5);
  CHECK(start.opponentThrows == 5);
  CHECK(start.perfectReachable);
  CHECK(curlingRecruitOf(&match, rules, false) == CurlingRecruitView{});
  // The second opponent does not join the city: no bar in its match.
  const Match second{rules, Opponent::MetalMamemon, random};
  CHECK_FALSE(curlingRecruitOf(&second, rules, true).shown);
  // The match over: the story answers it; the bar is gone.
  match.setScene(rink(5, 5), Phase::Finished, PlayerTeam, 0);
  CHECK_FALSE(curlingRecruitOf(&match, rules, true).shown);
}

TEST_CASE("the stones game's recruit bar follows the score and the throws left") {
  const CurlingRules rules = originalRules();
  dwe::ruleset::Random random{3};
  Match match{rules, Opponent::Penguinmon, random};
  auto teams = rink(2, 1);
  teams.at(PlayerTeam).score = 3;
  teams.at(OpponentTeam).score = 1;
  match.setScene(teams, Phase::Select, OpponentTeam, 1);
  CurlingRecruitView view = curlingRecruitOf(&match, rules, true);
  CHECK(view.player == 3);
  CHECK(view.opponent == 1);
  CHECK(view.playerThrows == 3);
  CHECK(view.opponentThrows == 4);
  // A negative score (the side target's -2) shows as it is.
  teams.at(PlayerTeam).score = -2;
  teams.at(OpponentTeam).score = 4;
  match.setScene(teams, Phase::Select, OpponentTeam, 1);
  view = curlingRecruitOf(&match, rules, true);
  CHECK(view.player == -2);
  CHECK(view.opponent == 4);
}

TEST_CASE("the perfect score shows while every stone of the player could still rest on 2 points") {
  // KAR_finishMatch: a win of perfect_score (10) or more is the perfect score; the best ring is
  // worth 2, so all five of the player's stones must stay in the game.
  const CurlingRules rules = originalRules();
  REQUIRE(rules.perfectScore == 10);
  dwe::ruleset::Random random{3};
  Match match{rules, Opponent::Penguinmon, random};
  auto teams = rink(3, 3);
  match.setScene(teams, Phase::Select, PlayerTeam, 3);
  CHECK(curlingRecruitOf(&match, rules, true).perfectReachable);
  teams.at(PlayerTeam).stones.at(1).state = Out;
  match.setScene(teams, Phase::Select, PlayerTeam, 3);
  CHECK_FALSE(curlingRecruitOf(&match, rules, true).perfectReachable);
  // Out of the game the opponent's stones do not count.
  teams = rink(3, 3);
  teams.at(OpponentTeam).stones.at(0).state = Out;
  match.setScene(teams, Phase::Select, PlayerTeam, 3);
  CHECK(curlingRecruitOf(&match, rules, true).perfectReachable);
}
