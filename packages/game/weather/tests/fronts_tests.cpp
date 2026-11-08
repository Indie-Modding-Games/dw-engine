#include <dwe/game/weather/climate.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

namespace {

using dwe::game::weather::ClimateArea;
using dwe::game::weather::ClimateRules;
using dwe::game::weather::Front;
using dwe::game::weather::WeatherKind;

ClimateRules packRules() {
  std::ifstream file{std::filesystem::path{DWE_GAME_PACK} / "field" / "weather.yaml"};
  std::stringstream text;
  text << file.rdbuf();
  auto rules = dwe::game::weather::parseClimateRules(text.str());
  REQUIRE(rules.has_value());
  return *rules;
}

ClimateRules rulesOf(const std::string& yaml) {
  auto rules = dwe::game::weather::parseClimateRules(yaml);
  REQUIRE(rules.has_value());
  return *rules;
}

// Area a only ever rains hard and only its heavy rain travels; b can have it (so the only front
// over b is a's), c cannot.
const std::string Chain = "period_hours: 3\nkeep_chance: 0.5\nfade_seconds: 8\n"
                          "fronts: { chance: 1, delay_hours: 2, reach: 2, kinds: [heavy_rain] }\n"
                          "areas:\n"
                          "  - id: a\n    maps: [a*]\n    weights: { heavy_rain: 1 }\n"
                          "    neighbours: [b]\n"
                          "  - id: b\n    maps: [b*]\n    weights: { clear: 1, heavy_rain: 1 }\n"
                          "  - id: c\n    maps: [c*]\n    weights: { clear: 1 }\n"
                          "    neighbours: [b]\n";

// The front over b at an hour: only a's heavy rain can be one.
std::optional<Front> frontOnB(const ClimateRules& rules, std::int64_t hour) {
  return dwe::game::weather::frontAt(rules, *rules.areaNamed("b"), 9, hour);
}

} // namespace

TEST_CASE("the neighbours go both ways and join every area of the pack") {
  const ClimateRules rules = packRules();
  REQUIRE(rules.hasFronts());
  CHECK(rules.fronts.reach == 2);
  CHECK(rules.fronts.delayHours == 2);
  const ClimateArea& city = *rules.areaNamed("file_city");
  for (const ClimateArea& area : rules.areas) {
    CHECK(dwe::game::weather::areaSteps(rules, city, area).has_value());
    for (const std::string& id : area.neighbours) {
      const ClimateArea* neighbour = rules.areaNamed(id);
      REQUIRE(neighbour != nullptr);
      CHECK(std::ranges::find(neighbour->neighbours, area.id) != neighbour->neighbours.end());
    }
  }
  const ClimateArea& forest = *rules.areaNamed("native_forest");
  CHECK(dwe::game::weather::areaSteps(rules, forest, forest) == 0);
  CHECK(dwe::game::weather::areaSteps(rules, forest, *rules.areaNamed("tropical_jungle")) == 1);
  CHECK(dwe::game::weather::areaSteps(rules, forest, *rules.areaNamed("great_canyon")) == 2);
}

TEST_CASE("a new weather spreads to the next area after the delay and stays a period") {
  const ClimateRules rules = rulesOf(Chain);
  const ClimateArea& a = *rules.areaNamed("a");
  int spread = 0;
  int kept = 0;
  for (std::int64_t period = 1; period < 200; ++period) {
    const std::int64_t arrives = (period * 3) + 2;
    const std::optional<Front> front = frontOnB(rules, arrives);
    if (!front) {
      // a kept its weather that period: nothing new left it.
      ++kept;
      continue;
    }
    ++spread;
    CHECK(front->kind == WeatherKind::HeavyRain);
    CHECK(front->from == "a");
    CHECK(front->steps == 1);
    CHECK(front->arrived == arrives);
    CHECK(dwe::game::weather::weatherAt(rules, a, 9, period) == WeatherKind::HeavyRain);
    // It stays the rest of a period and is the weather shown.
    CHECK(frontOnB(rules, arrives + 2)->arrived == arrives);
    CHECK(dwe::game::weather::weatherAtHour(rules, *rules.areaNamed("b"), 9, arrives + 1) ==
          WeatherKind::HeavyRain);
    // Not before it arrives: an hour earlier only the last period's front (if any) is over b.
    const std::optional<Front> before = frontOnB(rules, arrives - 1);
    CHECK((!before || before->arrived == arrives - 3));
  }
  CHECK(spread > 50);
  CHECK(kept > 50);
}

TEST_CASE("a front reaches farther areas later, only those that can have it, up to its reach") {
  // c has no heavy rain: no front ever comes over it.
  const ClimateRules closed = rulesOf(Chain);
  for (std::int64_t hour = 0; hour < 300; ++hour) {
    CHECK_FALSE(dwe::game::weather::frontAt(closed, *closed.areaNamed("c"), 9, hour).has_value());
  }
  // With heavy rain in c, a's gets there two areas and four hours after its period starts.
  std::string open = Chain;
  const std::string closedWeights = "weights: { clear: 1 }\n    neighbours: [b]";
  open.replace(open.find(closedWeights), closedWeights.size(),
               "weights: { light_rain: 1, heavy_rain: 1 }\n    neighbours: [b]");
  const ClimateRules rules = rulesOf(open);
  const ClimateArea& c = *rules.areaNamed("c");
  int fromA = 0;
  for (std::int64_t hour = 0; hour < 600; ++hour) {
    const std::optional<Front> front = dwe::game::weather::frontAt(rules, c, 9, hour);
    if (front && front->from == "a") {
      ++fromA;
      CHECK(front->steps == 2);
      CHECK((front->arrived - 4) % 3 == 0);
      CHECK(front->arrived <= hour);
      CHECK(hour < front->arrived + 3);
    }
  }
  CHECK(fromA > 20);
  // With a reach of one area, a's fronts stop at b.
  std::string near = open;
  near.replace(near.find("reach: 2"), std::string{"reach: 2"}.size(), "reach: 1");
  const ClimateRules nearRules = rulesOf(near);
  for (std::int64_t hour = 0; hour < 600; ++hour) {
    const auto front = dwe::game::weather::frontAt(nearRules, *nearRules.areaNamed("c"), 9, hour);
    CHECK((!front || front->from != "a"));
  }
}

TEST_CASE("fronts are a pure function of seed, area and hour") {
  const ClimateRules rules = packRules();
  int fronts = 0;
  int differentSeeds = 0;
  for (const char* map : {"mayo01", "trop01", "gcan01"}) {
    const ClimateArea& area = *rules.areaOf(map, false);
    for (std::int64_t hour = 0; hour < 400; ++hour) {
      const WeatherKind kind = dwe::game::weather::weatherAtHour(rules, area, 77, hour, true);
      // Asked again, after other areas, or after a jump in time (a load), the same answer.
      static_cast<void>(dwe::game::weather::weatherAtHour(rules, rules.areas.front(), 77, 9000));
      CHECK(kind == dwe::game::weather::weatherAtHour(rules, area, 77, hour, true));
      fronts += dwe::game::weather::frontAt(rules, area, 77, hour, true).has_value() ? 1 : 0;
      differentSeeds +=
          kind != dwe::game::weather::weatherAtHour(rules, area, 78, hour, true) ? 1 : 0;
    }
  }
  CHECK(fronts > 50);
  CHECK(differentSeeds > 100);
  // No fronts: the weather of the hour is the area's own of its period.
  ClimateRules still = rules;
  still.fronts.chance = 0.0F;
  const ClimateArea& forest = *still.areaOf("mayo01", false);
  for (std::int64_t hour = 0; hour < 200; ++hour) {
    CHECK(
        dwe::game::weather::weatherAtHour(still, forest, 5, hour) ==
        dwe::game::weather::weatherAt(still, forest, 5, dwe::game::weather::periodOf(still, hour)));
  }
}

TEST_CASE("no front brings snow to the areas that never snow") {
  const ClimateRules rules = packRules();
  for (const char* map : {"trop01", "gcan01", "gias01"}) {
    const ClimateArea& area = *rules.areaOf(map, false);
    for (std::int64_t hour = 0; hour < 24 * 48; ++hour) {
      const WeatherKind kind = dwe::game::weather::weatherAtHour(rules, area, 3, hour, true);
      CHECK(kind != WeatherKind::LightSnow);
      CHECK(kind != WeatherKind::HeavySnow);
    }
  }
}

TEST_CASE("bad fronts and neighbours are refused") {
  using dwe::game::weather::parseClimateRules;
  const std::string head = "period_hours: 3\nkeep_chance: 0.5\nfade_seconds: 8\n";
  const std::string areas = "areas:\n  - id: a\n    maps: [a*]\n    weights: { clear: 1 }\n"
                            "  - id: b\n    maps: [b*]\n    weights: { clear: 1 }\n";
  CHECK(parseClimateRules(head + "fronts: { chance: 0.5, delay_hours: 2, reach: 1 }\n" + areas +
                          "    neighbours: [a]\n")
            .has_value());
  CHECK_FALSE(parseClimateRules(head + areas + "    neighbours: [z]\n").has_value());
  CHECK_FALSE(parseClimateRules(head + areas + "    neighbours: [b]\n").has_value());
  CHECK_FALSE(parseClimateRules(head + "fronts: { chance: 2, delay_hours: 2, reach: 1 }\n" + areas)
                  .has_value());
  CHECK_FALSE(
      parseClimateRules(head + "fronts: { chance: 0.5, delay_hours: 0, reach: 1 }\n" + areas)
          .has_value());
  CHECK_FALSE(
      parseClimateRules(head + "fronts: { chance: 0.5, delay_hours: 2, reach: 0 }\n" + areas)
          .has_value());
  CHECK_FALSE(
      parseClimateRules(
          head + "fronts: { chance: 0.5, delay_hours: 2, reach: 1, kinds: [hail] }\n" + areas)
          .has_value());
  CHECK_FALSE(parseClimateRules(head + "fronts: { chance: 0.5, reach: 1 }\n" + areas).has_value());
}
