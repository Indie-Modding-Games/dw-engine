#include <dwe/game/weather/climate.hpp>
#include <dwe/game/weather/look.hpp>

#include <doctest/doctest.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

namespace {

using dwe::game::weather::ClimateArea;
using dwe::game::weather::ClimateRules;
using dwe::game::weather::WeatherBlend;
using dwe::game::weather::WeatherKind;

// The pack's own field/weather.yaml.
ClimateRules packRules() {
  std::ifstream file{std::filesystem::path{DWE_GAME_PACK} / "field" / "weather.yaml"};
  std::stringstream text;
  text << file.rdbuf();
  auto rules = dwe::game::weather::parseClimateRules(text.str());
  REQUIRE(rules.has_value());
  return *rules;
}

// The kinds an area rolls over many periods, counted.
std::map<WeatherKind, int> rolled(const ClimateRules& rules, const ClimateArea& area,
                                  std::uint64_t seed, int periods) {
  std::map<WeatherKind, int> counts;
  for (int period = 0; period < periods; ++period) {
    ++counts[dwe::game::weather::weatherAt(rules, area, seed, period)];
  }
  return counts;
}

} // namespace

TEST_CASE("the pack's climate puts every outdoor area in a zone and no interior in one") {
  const ClimateRules rules = packRules();
  CHECK(rules.periodHours == 3);
  REQUIRE(rules.areaOf("frzl03", false) != nullptr);
  CHECK(rules.areaOf("frzl03", false)->id == "freezeland");
  CHECK(rules.areaOf("koda02", false)->id == "ancient_glacial");
  CHECK(rules.areaOf("koda05", false)->id == "ancient_regions");
  CHECK(rules.areaOf("mayo06", false)->id == "native_forest");
  CHECK(rules.areaOf("twna01", false)->id == "file_city");
  CHECK(rules.areaOf("stic01", false)->id == "geko_swamp");
  // Interiors, caves, the city's rooms and the tournament's lobby: never.
  CHECK(rules.areaOf("room13", false) == nullptr);
  CHECK(rules.areaOf("tunn09", false) == nullptr);
  CHECK(rules.areaOf("leom01", false) == nullptr);
  CHECK(rules.areaOf("icsa01", false) == nullptr);
  CHECK(rules.areaOf("mgen01", false) == nullptr);
  // A map with a fixed time of day has none, whatever area would list it.
  CHECK(rules.areaOf("frzl17", true) == nullptr);
  CHECK(rules.areaOf("fact06", true) == nullptr);
  // A map no area lists.
  CHECK(rules.areaOf("xyz01", false) == nullptr);
}

TEST_CASE("the frozen areas snow and never rain; the forests rain") {
  const ClimateRules rules = packRules();
  for (const char* map : {"frzl01", "koda01"}) {
    const auto counts = rolled(rules, *rules.areaOf(map, false), 7, 2000);
    CHECK(counts.count(WeatherKind::LightRain) == 0);
    CHECK(counts.count(WeatherKind::HeavyRain) == 0);
    CHECK(counts.count(WeatherKind::LightSnow) > 0);
    CHECK(counts.count(WeatherKind::HeavySnow) > 0);
  }
  for (const char* map : {"mayo01", "trop02", "stic02", "mist03"}) {
    const auto counts = rolled(rules, *rules.areaOf(map, false), 7, 2000);
    CHECK(counts.count(WeatherKind::LightSnow) == 0);
    CHECK(counts.count(WeatherKind::HeavySnow) == 0);
    CHECK(counts.at(WeatherKind::LightRain) > 0);
    CHECK(counts.at(WeatherKind::Cloudy) > 0);
  }
  // The swamp rains more often than the canyon.
  const auto swamp = rolled(rules, *rules.areaOf("stic01", false), 7, 4000);
  const auto canyon = rolled(rules, *rules.areaOf("gcan01", false), 7, 4000);
  CHECK(swamp.at(WeatherKind::LightRain) > 2 * canyon.at(WeatherKind::LightRain));
}

TEST_CASE("the weather is a pure function of seed, area and period") {
  const ClimateRules rules = packRules();
  const ClimateArea& forest = *rules.areaOf("mayo01", false);
  // Every map of an area shares it: the area, not the map, is rolled.
  CHECK(rules.areaOf("mayo08a", false) == &forest);
  int changes = 0;
  int differentSeeds = 0;
  for (int period = 1; period < 500; ++period) {
    const WeatherKind kind = weatherAt(rules, forest, 42, period);
    CHECK(kind == weatherAt(rules, forest, 42, period));
    changes += kind != weatherAt(rules, forest, 42, period - 1) ? 1 : 0;
    differentSeeds += kind != weatherAt(rules, forest, 43, period) ? 1 : 0;
  }
  // It changes now and then, not every period, and another playthrough has another weather.
  CHECK(changes > 50);
  CHECK(changes < 400);
  CHECK(differentSeeds > 100);
  // Periods follow the game hours.
  CHECK(dwe::game::weather::periodOf(rules, 0) == 0);
  CHECK(dwe::game::weather::periodOf(rules, 2) == 0);
  CHECK(dwe::game::weather::periodOf(rules, 3) == 1);
  CHECK(dwe::game::weather::periodOf(rules, -1) == -1);
  CHECK(dwe::game::weather::weatherSeedOf(1) == dwe::game::weather::weatherSeedOf(1));
  CHECK(dwe::game::weather::weatherSeedOf(1) != dwe::game::weather::weatherSeedOf(2));
}

TEST_CASE("a change of weather fades over the rules' seconds") {
  WeatherBlend blend{8.0F};
  blend.snap(WeatherKind::Clear);
  CHECK(blend.look().sunshine == doctest::Approx(1.0F));
  blend.advance(WeatherKind::HeavyRain, 2.0);
  CHECK(blend.look().kind == WeatherKind::HeavyRain);
  CHECK(blend.look().rain == doctest::Approx(0.25F));
  CHECK(blend.look().sunshine == doctest::Approx(0.75F));
  CHECK_FALSE(blend.settled());
  blend.advance(WeatherKind::HeavyRain, 6.0);
  CHECK(blend.look().rain == doctest::Approx(1.0F));
  CHECK(blend.look().sunshine == doctest::Approx(0.0F));
  CHECK(blend.look().cloudCover == doctest::Approx(1.0F));
  CHECK(blend.settled());
  // Light rain is lighter, and its look fades from heavy in less than the whole fade.
  blend.advance(WeatherKind::LightRain, 8.0);
  CHECK(blend.look().rain < 0.5F);
  CHECK(blend.look().intensity() == doctest::Approx(blend.look().rain));
  blend.snap(WeatherKind::None);
  CHECK(blend.look().clear());
}

TEST_CASE("weather names read from the pack and the command line") {
  using dwe::game::weather::weatherKindNamed;
  CHECK(weatherKindNamed("rain") == WeatherKind::LightRain);
  CHECK(weatherKindNamed("heavy-rain") == WeatherKind::HeavyRain);
  CHECK(weatherKindNamed("snow") == WeatherKind::LightSnow);
  CHECK(weatherKindNamed("sunny") == WeatherKind::Clear);
  CHECK(weatherKindNamed("none") == WeatherKind::None);
  CHECK_FALSE(weatherKindNamed("hail").has_value());
  CHECK(dwe::game::weather::weatherKindName(WeatherKind::HeavySnow) == "heavy_snow");
}

TEST_CASE("bad climate rules are refused") {
  using dwe::game::weather::parseClimateRules;
  const std::string good = "period_hours: 3\nkeep_chance: 0.5\nfade_seconds: 8\nareas:\n"
                           "  - id: a\n    maps: [x*]\n    weights: { clear: 1 }\n";
  CHECK(parseClimateRules(good).has_value());
  CHECK_FALSE(parseClimateRules(good + "  - id: a\n    maps: [y]\n    weights: { clear: 1 }\n")
                  .has_value());
  CHECK_FALSE(parseClimateRules("period_hours: 3\nkeep_chance: 0.5\nfade_seconds: 8\nareas:\n"
                                "  - id: a\n    maps: [x]\n    weights: { hail: 1 }\n")
                  .has_value());
  CHECK_FALSE(parseClimateRules("period_hours: 0\nkeep_chance: 0.5\nfade_seconds: 8\nareas: []\n")
                  .has_value());
  CHECK_FALSE(parseClimateRules("period_hours: 3\nkeep_chance: 1\nfade_seconds: 8\nareas: []\n")
                  .has_value());
  CHECK_FALSE(parseClimateRules("period_hours: 3\nkeep_chance: 0.5\nfade_seconds: 8\nareas:\n"
                                "  - id: a\n    maps: [x]\n    weights: { clear: 0 }\n")
                  .has_value());
}
