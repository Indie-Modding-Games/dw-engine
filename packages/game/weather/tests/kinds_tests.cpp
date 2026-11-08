#include <dwe/game/weather/climate.hpp>
#include <dwe/game/weather/look.hpp>

#include <doctest/doctest.h>

#include <algorithm>
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
using dwe::game::weather::lookOf;
using dwe::game::weather::WeatherKind;
using dwe::game::weather::WindDebris;

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

// The kinds an area shows hour by hour, fronts and seasons included.
std::map<WeatherKind, int> shown(const ClimateRules& rules, const ClimateArea& area, int hours) {
  std::map<WeatherKind, int> counts;
  for (std::int64_t hour = 0; hour < hours; ++hour) {
    ++counts[dwe::game::weather::weatherAtHour(rules, area, 21, hour, true)];
  }
  return counts;
}

} // namespace

TEST_CASE("fog, storm and wind have names for the pack and the command line") {
  using dwe::game::weather::weatherKindName;
  using dwe::game::weather::weatherKindNamed;
  CHECK(weatherKindNamed("fog") == WeatherKind::Fog);
  CHECK(weatherKindNamed("storm") == WeatherKind::Storm);
  CHECK(weatherKindNamed("thunderstorm") == WeatherKind::Storm);
  CHECK(weatherKindNamed("wind") == WeatherKind::Wind);
  CHECK(weatherKindName(WeatherKind::Fog) == "fog");
  CHECK(weatherKindName(WeatherKind::Storm) == "storm");
  CHECK(weatherKindName(WeatherKind::Wind) == "wind");
  // Every rolled kind reads back from its own name.
  for (const WeatherKind kind : dwe::game::weather::RolledKinds) {
    CHECK(weatherKindNamed(weatherKindName(kind)) == kind);
  }
}

TEST_CASE("fog, storm and wind look their part and fade like the others") {
  const auto fog = lookOf(WeatherKind::Fog);
  CHECK(fog.fog == doctest::Approx(1.0F));
  CHECK(fog.rain == 0.0F);
  CHECK(fog.intensity() == doctest::Approx(1.0F));
  const auto storm = lookOf(WeatherKind::Storm);
  CHECK(storm.rain == doctest::Approx(1.0F));
  CHECK(storm.storm == doctest::Approx(1.0F));
  CHECK(storm.wind > 0.0F);
  CHECK(storm.cloudCover == doctest::Approx(1.0F));
  const auto wind = lookOf(WeatherKind::Wind);
  CHECK(wind.wind == doctest::Approx(1.0F));
  CHECK(wind.rain == 0.0F);
  CHECK(wind.snow == 0.0F);
  CHECK_FALSE(wind.clear());
  dwe::game::weather::WeatherBlend blend{8.0F};
  blend.snap(WeatherKind::Fog);
  blend.advance(WeatherKind::Storm, 4.0);
  CHECK(blend.look().fog == doctest::Approx(0.5F));
  CHECK(blend.look().storm == doctest::Approx(0.5F));
  blend.advance(WeatherKind::Storm, 4.0);
  CHECK(blend.settled());
  CHECK(blend.look().fog == 0.0F);
}

TEST_CASE("the pack's zones: dust in the dry areas, no snow in the tropics, fog in the mist") {
  const ClimateRules rules = packRules();
  CHECK(rules.areaNamed("great_canyon")->wind == WindDebris::Dust);
  CHECK(rules.areaNamed("gear_savanna")->wind == WindDebris::Dust);
  CHECK(rules.areaNamed("native_forest")->wind == WindDebris::Leaves);
  CHECK(rules.areaNamed("tropical_jungle")->wind == WindDebris::Leaves);
  // Over many days, seasons and fronts included.
  const int hours = 24 * 120;
  const auto jungle = shown(rules, *rules.areaNamed("tropical_jungle"), hours);
  CHECK(jungle.count(WeatherKind::LightSnow) == 0);
  CHECK(jungle.count(WeatherKind::HeavySnow) == 0);
  CHECK(jungle.at(WeatherKind::Storm) > 0);
  const auto canyon = shown(rules, *rules.areaNamed("great_canyon"), hours);
  CHECK(canyon.count(WeatherKind::Fog) == 0);
  CHECK(canyon.at(WeatherKind::Wind) > canyon.at(WeatherKind::Storm));
  const auto frozen = shown(rules, *rules.areaNamed("freezeland"), hours);
  CHECK(frozen.count(WeatherKind::Storm) == 0);
  CHECK(frozen.count(WeatherKind::LightRain) == 0);
  const auto mist = shown(rules, *rules.areaNamed("misty_trees"), hours);
  CHECK(mist.at(WeatherKind::Fog) > hours / 20);
  // Every outdoor zone can roll at least one of the new kinds.
  for (const ClimateArea& area : rules.areas) {
    CAPTURE(area.id);
    CHECK(area.weights.at(slotOf(WeatherKind::Fog)) + area.weights.at(slotOf(WeatherKind::Storm)) +
              area.weights.at(slotOf(WeatherKind::Wind)) >
          0U);
  }
}

TEST_CASE("the pack's odds: more fog at dawn, more storms in the afternoon") {
  using dwe::game::weather::oddsWeights;
  const ClimateRules rules = packRules();
  const ClimateArea& swamp = *rules.areaNamed("geko_swamp");
  const auto at = [&](DayPart part, WeatherKind kind) {
    return oddsWeights(rules, swamp, std::nullopt, part).at(slotOf(kind));
  };
  CHECK(at(DayPart::Dawn, WeatherKind::Fog) > 2 * at(DayPart::Day, WeatherKind::Fog));
  CHECK(at(DayPart::Dusk, WeatherKind::Storm) > at(DayPart::Day, WeatherKind::Storm));
  CHECK(at(DayPart::Dusk, WeatherKind::Storm) > 2 * at(DayPart::Dawn, WeatherKind::Storm));
  // Rolled: the changes to fog start at dawn more often than by day, per period of each.
  std::map<DayPart, int> fog;
  std::map<DayPart, int> storm;
  std::map<DayPart, int> periods;
  for (std::int64_t period = 1; period < 20000; ++period) {
    const WeatherKind kind = dwe::game::weather::weatherAt(rules, swamp, 8, period);
    const DayPart part = dwe::game::weather::dayPartOfHour(rules, period * rules.periodHours);
    ++periods[part];
    if (kind != dwe::game::weather::weatherAt(rules, swamp, 8, period - 1)) {
      fog[part] += kind == WeatherKind::Fog ? 1 : 0;
      storm[part] += kind == WeatherKind::Storm ? 1 : 0;
    }
  }
  const auto rate = [&](std::map<DayPart, int>& counts, DayPart part) {
    return static_cast<double>(counts[part]) / static_cast<double>(periods[part]);
  };
  CHECK(rate(fog, DayPart::Dawn) > 2.0 * rate(fog, DayPart::Day));
  CHECK(rate(storm, DayPart::Dusk) > rate(storm, DayPart::Day));
  CHECK(rate(storm, DayPart::Dusk) > 2.0 * rate(storm, DayPart::Dawn));
}

TEST_CASE("an area's wind is leaves or dust") {
  const std::string head =
      "period_hours: 3\nkeep_chance: 0.5\nfade_seconds: 8\nareas:\n"
      "  - id: a\n    maps: [a*]\n    weights: { wind: 1, fog: 1, storm: 1 }\n";
  auto dust = dwe::game::weather::parseClimateRules(head + "    wind: dust\n");
  REQUIRE(dust.has_value());
  CHECK(dust->areas.front().wind == WindDebris::Dust);
  CHECK(dwe::game::weather::parseClimateRules(head + "    wind: leaves\n").has_value());
  CHECK_FALSE(dwe::game::weather::parseClimateRules(head + "    wind: sand\n").has_value());
}
