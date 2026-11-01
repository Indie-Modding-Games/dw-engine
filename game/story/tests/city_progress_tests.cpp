#include <dwe/game/story/medals.hpp>
#include <dwe/game/story/prosperity.hpp>
#include <dwe/game/story/story_state.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

using dwe::game::story::checkMedalConditions;
using dwe::game::story::CityMapChoice;
using dwe::game::story::CityResident;
using dwe::game::story::cityResidents;
using dwe::game::story::clearedCityMap;
using dwe::game::story::hasMedal;
using dwe::game::story::MedalFacts;
using dwe::game::story::MedalRules;
using dwe::game::story::MedalStats;
using dwe::game::story::ProsperityRules;
using dwe::game::story::recalculateProsperity;
using dwe::game::story::StoryState;

namespace {

// main.c: recalculatePPandArena's numbers.
ProsperityRules prosperityRules() {
  return {.pointsVariable = 1,
          .recruitTrigger = 200,
          .firstSpecies = 3,
          .endSpecies = 59,
          .minLevel = 3,
          .levelOffset = 2,
          .onePointSpecies = {11, 39, 53},
          .cupVariable = 3,
          .noCupFrom = 23,
          .registrationTriggers = {37, 38, 39}};
}

// Species 4 a rookie, 5 a champion, 6 an ultimate, 7 in-training, 11 a champion; the rest unknown.
std::int32_t levelOf(std::int32_t species) {
  switch (species) {
  case 4:
    return 3;
  case 5:
  case 11:
    return 4;
  case 6:
    return 5;
  case 7:
    return 2;
  default:
    return 0;
  }
}

void set(StoryState& state, std::int32_t trigger) {
  REQUIRE(state.setTrigger(trigger, true).has_value());
}

} // namespace

TEST_CASE(
    "the prosperity is rebuilt from the recruit triggers by level, whatever the scripts added") {
  StoryState state;
  REQUIRE(state.setVariable(1, 99).has_value());
  REQUIRE(state.setVariable(3, 5).has_value());
  for (const std::int32_t species : {4, 5, 6, 7, 11}) {
    set(state, 200 + species);
  }
  set(state, 37);
  auto points = recalculateProsperity(state, prosperityRules(), levelOf);
  REQUIRE(points.has_value());
  // 4: 1, 5: 2, 6: 3, 7 (in-training) none, 11: one point whatever its level.
  CHECK(*points == 7);
  CHECK(state.variable(1).value() == 7);
  // A cup is registered (5 < 23): its registration stays.
  CHECK(state.trigger(37).value());
}

TEST_CASE("the prosperity clears a registration without a cup and counts no unrecruited species") {
  StoryState state;
  REQUIRE(state.setVariable(1, 12).has_value());
  REQUIRE(state.setVariable(3, 0xff).has_value());
  set(state, 37);
  set(state, 39);
  auto points = recalculateProsperity(state, prosperityRules(), levelOf);
  REQUIRE(points.has_value());
  CHECK(*points == 0);
  CHECK(state.variable(1).value() == 0);
  CHECK_FALSE(state.trigger(37).value());
  CHECK_FALSE(state.trigger(39).value());
}

TEST_CASE("the city's residents are the species the prosperity counts, with their points") {
  StoryState state;
  set(state, 200 + 5);
  set(state, 200 + 11);
  auto residents = cityResidents(state, prosperityRules(), levelOf);
  REQUIRE(residents.has_value());
  // The rookie 4, the champions 5 and 11 and the ultimate 6; the in-training 7 and the species the
  // table does not have never join.
  REQUIRE(residents->size() == 4);
  const auto at = [&](std::size_t i) { return (*residents)[i]; };
  CHECK(at(0).species == 4);
  CHECK(at(0).points == 1);
  CHECK_FALSE(at(0).recruited);
  CHECK(at(1).species == 5);
  CHECK(at(1).points == 2);
  CHECK(at(1).recruited);
  CHECK(at(2).species == 6);
  CHECK(at(2).level == 5);
  CHECK(at(2).points == 3);
  CHECK(at(3).species == 11);
  CHECK(at(3).points == 1);
  CHECK(at(3).recruited);
  // The recruited ones' points are what the next rebuild writes.
  std::int32_t sum = 0;
  for (const CityResident& resident : *residents) {
    sum += resident.recruited ? resident.points : 0;
  }
  CHECK(recalculateProsperity(state, prosperityRules(), levelOf).value() == sum);
}

TEST_CASE("a finished game continues on the File City screen of how far the city grew") {
  // map.c: getFileCityTopMap as the first choice whose triggers are all set.
  const std::vector<CityMapChoice> choices{{.triggers = {220, 214, 221, 225}, .map = 179},
                                           {.triggers = {220, 214, 221, 246}, .map = 178},
                                           {.triggers = {220, 214, 221}, .map = 177},
                                           {.triggers = {220, 214, 225}, .map = 176},
                                           {.triggers = {220, 214, 246}, .map = 175},
                                           {.triggers = {220, 214}, .map = 174},
                                           {.triggers = {221, 225}, .map = 173},
                                           {.triggers = {221, 246}, .map = 172},
                                           {.triggers = {221}, .map = 171},
                                           {.triggers = {225}, .map = 170},
                                           {.triggers = {246}, .map = 169},
                                           {.triggers = {}, .map = 168}};
  StoryState state;
  CHECK(clearedCityMap(state, choices).value() == 168);
  set(state, 246);
  CHECK(clearedCityMap(state, choices).value() == 169);
  set(state, 220);
  // 220 without 214 is not the large city.
  CHECK(clearedCityMap(state, choices).value() == 169);
  set(state, 214);
  CHECK(clearedCityMap(state, choices).value() == 175);
  set(state, 225);
  set(state, 221);
  CHECK(clearedCityMap(state, choices).value() == 179);
  CHECK_FALSE(clearedCityMap(state, {}).has_value());
}

namespace {

MedalRules medalRules() {
  return {.firstTrigger = 0x16c,
          .medalLimit = 19,
          .allMoves = MedalRules::AllMoves{.medal = 5, .count = 57},
          .maxStats = MedalRules::MaxStats{.medal = 7,
                                           .stats = {.hp = 9999,
                                                     .mp = 9999,
                                                     .offense = 999,
                                                     .defense = 999,
                                                     .speed = 999,
                                                     .brain = 999}},
          .money = MedalRules::Money{.medal = 13, .amount = 999999},
          .year = MedalRules::Year{.medal = 14, .year = 10},
          .allRaised = MedalRules::AllRaised{.medal = 6, .first = 1, .end = 62, .tamerLevels = 1},
          .fish = MedalRules::Fish{.medal = 9, .count = 100},
          .allCards = MedalRules::AllCards{.medal = 12, .count = 66}};
}

MedalFacts noFacts() {
  return {
      .knowsMove = [](std::int32_t move) { return move < 56; },
      .stats = {.hp = 9999, .mp = 9999, .offense = 999, .defense = 999, .speed = 999, .brain = 998},
      .money = 999998,
      .year = 9,
      .raised = [](std::int32_t species) { return species != 61; },
      .fishCaught = 99,
      .cardCopies = [](std::int32_t card) { return card == 65 ? 0 : 2; }};
}

} // namespace

TEST_CASE("no medal is given one short of each condition") {
  StoryState state;
  auto earned = checkMedalConditions(state, medalRules(), noFacts());
  REQUIRE(earned.has_value());
  CHECK(earned->medals.empty());
  CHECK(earned->tamerLevels == 0);
  CHECK(state.triggersSet() == 0);
}

TEST_CASE("each condition met gives its medal once, as trigger 0x16c + medal") {
  StoryState state;
  MedalFacts facts = noFacts();
  facts.knowsMove = [](std::int32_t) { return true; };
  facts.stats.brain = 999;
  facts.money = 999999;
  facts.year = 10;
  facts.raised = [](std::int32_t) { return true; };
  facts.fishCaught = 100;
  facts.cardCopies = [](std::int32_t) { return 1; };
  auto earned = checkMedalConditions(state, medalRules(), facts);
  REQUIRE(earned.has_value());
  // tamer.c: checkMedalConditions' order.
  CHECK(earned->medals == std::vector<std::int32_t>{5, 7, 13, 14, 6, 9, 12});
  CHECK(earned->tamerLevels == 1);
  CHECK(state.trigger(0x16c + 13).value());
  CHECK(hasMedal(state, medalRules(), 6).value());
  auto again = checkMedalConditions(state, medalRules(), facts);
  REQUIRE(again.has_value());
  CHECK(again->medals.empty());
  CHECK(again->tamerLevels == 0);
}

TEST_CASE("the money and the year medals ask for the exact value") {
  StoryState state;
  MedalFacts facts = noFacts();
  facts.money = 1000000;
  facts.year = 11;
  auto earned = checkMedalConditions(state, medalRules(), facts);
  REQUIRE(earned.has_value());
  CHECK(earned->medals.empty());
  // A medal past the limit is never held nor given (script_common.c: unlockMedal).
  CHECK_FALSE(hasMedal(state, medalRules(), 19).value());
}
