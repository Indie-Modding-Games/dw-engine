#include <dwe/game/weather/climate.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>

namespace {

using dwe::game::weather::ClimateArea;
using dwe::game::weather::ClimateRules;
using dwe::game::weather::DayPart;
using dwe::game::weather::Season;
using dwe::game::weather::WeatherKind;

ClimateRules packRules() {
  std::ifstream file{std::filesystem::path{DWE_GAME_PACK} / "field" / "weather.yaml"};
  std::stringstream text;
  text << file.rdbuf();
  auto rules = dwe::game::weather::parseClimateRules(text.str());
  REQUIRE(rules.has_value());
  return *rules;
}

std::size_t slotOf(WeatherKind kind) {
  return static_cast<std::size_t>(std::ranges::find(dwe::game::weather::RolledKinds, kind) -
                                  dwe::game::weather::RolledKinds.begin());
}

} // namespace

TEST_CASE("the part of the day follows the hour of the day") {
  using dwe::game::weather::dayPartOfHour;
  const ClimateRules rules = packRules();
  REQUIRE(rules.hasDayParts);
  CHECK(dayPartOfHour(rules, 4) == DayPart::Night);
  CHECK(dayPartOfHour(rules, 5) == DayPart::Dawn);
  CHECK(dayPartOfHour(rules, 7) == DayPart::Dawn);
  CHECK(dayPartOfHour(rules, 8) == DayPart::Day);
  CHECK(dayPartOfHour(rules, 15) == DayPart::Day);
  CHECK(dayPartOfHour(rules, 16) == DayPart::Dusk);
  CHECK(dayPartOfHour(rules, 20) == DayPart::Night);
  CHECK(dayPartOfHour(rules, 23) == DayPart::Night);
  // Any day, and before year 0.
  CHECK(dayPartOfHour(rules, (24 * 9) + 6) == DayPart::Dawn);
  CHECK(dayPartOfHour(rules, -1) == DayPart::Night);
  CHECK(dayPartOfHour(rules, -24 + 9) == DayPart::Day);
  CHECK(dwe::game::weather::dayPartName(DayPart::Dusk) == "dusk");
  CHECK(dwe::game::weather::dayPartNamed("dawn") == DayPart::Dawn);
  CHECK_FALSE(dwe::game::weather::dayPartNamed("noon").has_value());
  // Without parts every hour is day.
  ClimateRules without = rules;
  without.hasDayParts = false;
  CHECK(dayPartOfHour(without, 2) == DayPart::Day);
}

TEST_CASE("the parts of the day multiply the weights and stack on the season's") {
  using dwe::game::weather::oddsWeights;
  const ClimateRules rules = packRules();
  const ClimateArea& forest = *rules.areaOf("mayo01", false);
  const std::size_t clear = slotOf(WeatherKind::Clear);
  const std::size_t cloudy = slotOf(WeatherKind::Cloudy);
  // Neither: the area's own weights.
  CHECK(oddsWeights(rules, forest, std::nullopt, std::nullopt) == forest.weights);
  // A part alone: the area's weight times its factor, in hundredths.
  const auto dawn = oddsWeights(rules, forest, std::nullopt, DayPart::Dawn);
  const auto& dawnOdds = rules.dayParts.at(static_cast<std::size_t>(DayPart::Dawn)).odds;
  CHECK(dawn.at(cloudy) ==
        static_cast<std::uint32_t>(std::lround(static_cast<float>(forest.weights.at(cloudy)) *
                                               dawnOdds.at(cloudy) * 100.0F)));
  CHECK(dawn.at(cloudy) > forest.weights.at(cloudy) * 100U);
  CHECK(dawn.at(clear) < forest.weights.at(clear) * 100U);
  // With a season, both factors (the forest takes its seasons whole).
  const auto summerDay = oddsWeights(rules, forest, Season::Summer, DayPart::Day);
  const float expected =
      static_cast<float>(forest.weights.at(clear)) * rules.season(Season::Summer).odds.at(clear) *
      rules.dayParts.at(static_cast<std::size_t>(DayPart::Day)).odds.at(clear) * 100.0F;
  CHECK(summerDay.at(clear) == static_cast<std::uint32_t>(std::lround(expected)));
  CHECK(oddsWeights(rules, forest, Season::Summer, std::nullopt) ==
        dwe::game::weather::seasonalWeights(rules, forest, Season::Summer));
}

TEST_CASE(
    "a part of the day without a kind never rolls it; without parts the weather is as before") {
  const std::string head =
      "period_hours: 3\nkeep_chance: 0.3\nfade_seconds: 8\n"
      "areas:\n  - id: a\n    maps: [a*]\n    weights: { clear: 1, heavy_rain: 1 }\n";
  auto rules = dwe::game::weather::parseClimateRules(
      head +
      "day_parts:\n  dawn: { from: 6 }\n  day: { from: 9 }\n"
      "  dusk: { from: 15, odds: { clear: 0 } }\n  night: { from: 21, odds: { heavy_rain: 0 } }\n");
  REQUIRE(rules.has_value());
  auto plain = dwe::game::weather::parseClimateRules(head);
  REQUIRE(plain.has_value());
  const ClimateArea& area = rules->areas.front();
  std::map<DayPart, std::map<WeatherKind, int>> rolled;
  int differ = 0;
  for (std::int64_t period = 0; period < 3000; ++period) {
    // A kind that is rolled anew in its period, and the part that period starts in.
    const WeatherKind kind = dwe::game::weather::weatherAt(*rules, area, 4, period);
    const WeatherKind before = dwe::game::weather::weatherAt(*rules, area, 4, period - 1);
    differ +=
        kind != dwe::game::weather::weatherAt(*plain, plain->areas.front(), 4, period) ? 1 : 0;
    if (kind != before) {
      ++rolled[dwe::game::weather::dayPartOfHour(*rules, period * 3)][kind];
    }
    CHECK(dwe::game::weather::weatherAt(*plain, plain->areas.front(), 4, period) ==
          dwe::game::weather::weatherAt(*plain, plain->areas.front(), 4, period, true));
  }
  // A change at dusk is never to clear, one at night never to heavy rain.
  CHECK(rolled[DayPart::Dusk].count(WeatherKind::Clear) == 0);
  CHECK(rolled[DayPart::Dusk][WeatherKind::HeavyRain] > 50);
  CHECK(rolled[DayPart::Night].count(WeatherKind::HeavyRain) == 0);
  CHECK(rolled[DayPart::Night][WeatherKind::Clear] > 50);
  CHECK(differ > 100);
}

TEST_CASE("bad parts of the day are refused") {
  using dwe::game::weather::parseClimateRules;
  const std::string head = "period_hours: 3\nkeep_chance: 0.5\nfade_seconds: 8\nareas:\n"
                           "  - id: a\n    maps: [a*]\n    weights: { clear: 1 }\n";
  CHECK(parseClimateRules(head + "day_parts:\n  dawn: { from: 5 }\n  day: { from: 8 }\n"
                                 "  dusk: { from: 16 }\n  night: { from: 20 }\n")
            .has_value());
  // Out of order, past the day, a part missing, a bad kind or factor.
  CHECK_FALSE(parseClimateRules(head + "day_parts:\n  dawn: { from: 9 }\n  day: { from: 8 }\n"
                                       "  dusk: { from: 16 }\n  night: { from: 20 }\n")
                  .has_value());
  CHECK_FALSE(parseClimateRules(head + "day_parts:\n  dawn: { from: 5 }\n  day: { from: 8 }\n"
                                       "  dusk: { from: 16 }\n  night: { from: 24 }\n")
                  .has_value());
  CHECK_FALSE(parseClimateRules(head + "day_parts:\n  dawn: { from: 5 }\n  day: { from: 8 }\n"
                                       "  dusk: { from: 16 }\n")
                  .has_value());
  CHECK_FALSE(parseClimateRules(head +
                                "day_parts:\n  dawn: { from: 5, odds: { hail: 2 } }\n"
                                "  day: { from: 8 }\n  dusk: { from: 16 }\n  night: { from: 20 }\n")
                  .has_value());
  CHECK_FALSE(parseClimateRules(head +
                                "day_parts:\n  dawn: { from: 5, odds: { clear: 11 } }\n"
                                "  day: { from: 8 }\n  dusk: { from: 16 }\n  night: { from: 20 }\n")
                  .has_value());
}
