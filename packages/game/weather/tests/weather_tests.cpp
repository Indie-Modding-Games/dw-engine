#include <dwe/game/weather/climate.hpp>
#include <dwe/game/weather/look.hpp>

#include <doctest/doctest.h>

#include <algorithm>
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

namespace {

using dwe::game::weather::Season;

// The kinds an area rolls with seasons over the periods of one season (a period is 3 game hours, so
// a season of 3 days is 24 periods), over many years.
std::map<WeatherKind, int> rolledIn(const ClimateRules& rules, const ClimateArea& area,
                                    Season season, int years) {
  std::map<WeatherKind, int> counts;
  const int perSeason = rules.seasonDays * 24 / rules.periodHours;
  for (int turn = static_cast<int>(season); turn < years * 4; turn += 4) {
    for (int period = turn * perSeason; period < (turn + 1) * perSeason; ++period) {
      ++counts[dwe::game::weather::weatherAt(rules, area, 11, period, true)];
    }
  }
  return counts;
}

int snowIn(const std::map<WeatherKind, int>& counts) {
  const auto count = [&](WeatherKind kind) { return counts.contains(kind) ? counts.at(kind) : 0; };
  return count(WeatherKind::LightSnow) + count(WeatherKind::HeavySnow);
}

int rainIn(const std::map<WeatherKind, int>& counts) {
  const auto count = [&](WeatherKind kind) { return counts.contains(kind) ? counts.at(kind) : 0; };
  return count(WeatherKind::LightRain) + count(WeatherKind::HeavyRain);
}

} // namespace

TEST_CASE("the seasons follow the game days, spring first, a few days each") {
  using dwe::game::weather::seasonOfDay;
  using dwe::game::weather::seasonOfHour;
  const ClimateRules rules = packRules();
  REQUIRE(rules.hasSeasons());
  // A new partner lives about 15 days: it sees all four seasons.
  CHECK(rules.seasonDays == 3);
  CHECK(seasonOfDay(rules, 0) == Season::Spring);
  CHECK(seasonOfDay(rules, 2) == Season::Spring);
  CHECK(seasonOfDay(rules, 3) == Season::Summer);
  CHECK(seasonOfDay(rules, 6) == Season::Autumn);
  CHECK(seasonOfDay(rules, 11) == Season::Winter);
  CHECK(seasonOfDay(rules, 12) == Season::Spring);
  CHECK(seasonOfDay(rules, -1) == Season::Winter);
  CHECK(seasonOfHour(rules, 71) == Season::Spring);
  CHECK(seasonOfHour(rules, 72) == Season::Summer);
  // Across a game year (30 days) the turns go on.
  CHECK(seasonOfDay(rules, 30) == Season::Autumn);
  ClimateRules none = rules;
  none.seasonDays = 0;
  CHECK(seasonOfDay(none, 40) == Season::Spring);
  CHECK(dwe::game::weather::seasonName(Season::Autumn) == "autumn");
  CHECK(dwe::game::weather::seasonNamed("winter") == Season::Winter);
  CHECK(dwe::game::weather::seasonNamed("fall") == Season::Autumn);
  CHECK_FALSE(dwe::game::weather::seasonNamed("monsoon").has_value());
}

TEST_CASE("the seasons move the odds: snow in winter, sun in summer, rain in spring and autumn") {
  const ClimateRules rules = packRules();
  const ClimateArea& forest = *rules.areaOf("mayo01", false);
  const auto spring = rolledIn(rules, forest, Season::Spring, 40);
  const auto summer = rolledIn(rules, forest, Season::Summer, 40);
  const auto autumn = rolledIn(rules, forest, Season::Autumn, 40);
  const auto winter = rolledIn(rules, forest, Season::Winter, 40);
  // The forest only snows in winter (and a snow rolled late in winter may last into spring's first
  // hours), and then more than it rains.
  CHECK(snowIn(winter) > rainIn(winter));
  CHECK(snowIn(summer) == 0);
  CHECK(snowIn(autumn) == 0);
  CHECK(snowIn(spring) < snowIn(winter) / 10);
  // Summer is the sunniest; spring and autumn rain more than summer.
  CHECK(summer.at(WeatherKind::Clear) > spring.at(WeatherKind::Clear));
  CHECK(summer.at(WeatherKind::Clear) > autumn.at(WeatherKind::Clear));
  CHECK(rainIn(spring) > rainIn(summer) * 3 / 2);
  CHECK(rainIn(autumn) > rainIn(summer) * 3 / 2);
  // The weights themselves: winter turns most of the rain into snow.
  const auto weights = dwe::game::weather::seasonalWeights(rules, forest, Season::Winter);
  CHECK(weights.at(4) > weights.at(2));
  CHECK(dwe::game::weather::seasonalWeights(rules, forest, Season::Summer).at(4) == 0U);
}

TEST_CASE("the areas with a climate of their own keep it in every season") {
  const ClimateRules rules = packRules();
  for (const Season season : {Season::Spring, Season::Summer, Season::Autumn, Season::Winter}) {
    // Freezeland snows in summer too, and never rains.
    const auto frozen = rolledIn(rules, *rules.areaOf("frzl01", false), season, 30);
    CHECK(rainIn(frozen) == 0);
    CHECK(snowIn(frozen) * 2 > frozen.at(WeatherKind::Clear));
    // The jungle and the canyon never snow, the canyon stays mostly clear.
    CHECK(snowIn(rolledIn(rules, *rules.areaOf("trop01", false), season, 30)) == 0);
    const auto canyon = rolledIn(rules, *rules.areaOf("gcan01", false), season, 30);
    CHECK(snowIn(canyon) == 0);
    CHECK(canyon.at(WeatherKind::Clear) * 2 > rainIn(canyon) + canyon.at(WeatherKind::Cloudy));
  }
  // The season only nudges them: their grade is nearer neutral than the forest's.
  const auto forest =
      dwe::game::weather::seasonalGrade(rules, *rules.areaOf("mayo01", false), Season::Winter);
  const auto frozen =
      dwe::game::weather::seasonalGrade(rules, *rules.areaOf("frzl01", false), Season::Winter);
  CHECK(forest.saturation < frozen.saturation);
  CHECK(frozen.saturation < 1.0F);
}

TEST_CASE("with the seasons off the weather is exactly the one without seasons") {
  const ClimateRules rules = packRules();
  ClimateRules without = rules;
  without.seasonDays = 0;
  for (const char* map : {"mayo01", "frzl01", "trop01", "twna01"}) {
    const ClimateArea& area = *rules.areaOf(map, false);
    int differ = 0;
    for (int period = -50; period < 2000; ++period) {
      const WeatherKind off = weatherAt(rules, area, 5, period);
      CHECK(off == weatherAt(rules, area, 5, period, false));
      CHECK(off == weatherAt(without, area, 5, period, true));
      differ += off != weatherAt(rules, area, 5, period, true) ? 1 : 0;
    }
    // On, they do change it.
    CHECK(differ > 0);
  }
}

TEST_CASE("a season's look fades in and out, neutral with nothing to show") {
  using dwe::game::weather::SeasonBlend;
  using dwe::game::weather::SeasonLook;
  const ClimateRules rules = packRules();
  const ClimateArea& forest = *rules.areaOf("mayo01", false);
  const SeasonLook autumn = dwe::game::weather::seasonLookOf(rules, forest, Season::Autumn);
  CHECK(autumn.season == Season::Autumn);
  CHECK(autumn.leaves == doctest::Approx(1.0F));
  CHECK(autumn.petals == doctest::Approx(0.0F));
  CHECK(autumn.grade.foliage > 0.0F);
  CHECK(dwe::game::weather::seasonLookOf(rules, forest, Season::Spring).petals > 0.0F);
  SeasonBlend blend{8.0F};
  CHECK(blend.look().neutral());
  blend.advance(autumn, 4.0);
  CHECK(blend.look().leaves == doctest::Approx(0.5F));
  CHECK(blend.look().grade.foliageHue == doctest::Approx(autumn.grade.foliageHue));
  // Every part moves at most 1 / fadeSeconds a second.
  CHECK(blend.look().grade.foliage == doctest::Approx(std::min(autumn.grade.foliage, 0.5F)));
  blend.advance(autumn, 4.0);
  CHECK(blend.look() == autumn);
  // Off (or indoors): back to neutral, the hue staying while the turn fades out.
  blend.advance(SeasonLook{}, 4.0);
  CHECK(blend.look().grade.foliageHue == doctest::Approx(autumn.grade.foliageHue));
  blend.advance(SeasonLook{}, 4.0);
  CHECK(blend.look().neutral());
  blend.snap(autumn);
  blend.snap(SeasonLook{});
  CHECK(blend.look().neutral());
}

TEST_CASE("bad seasons are refused") {
  using dwe::game::weather::parseClimateRules;
  const std::string head = "period_hours: 3\nkeep_chance: 0.5\nfade_seconds: 8\nareas:\n"
                           "  - id: a\n    maps: [x*]\n    weights: { clear: 1, light_rain: 1 }\n"
                           "    season_strength: 0.5\n    snow_from_rain: false\n";
  const std::string four = "  spring: {}\n  summer: {}\n  autumn: {}\n  winter: {}\n";
  auto good = parseClimateRules(head + "seasons:\n  days: 2\n" + four);
  REQUIRE(good.has_value());
  CHECK(good->seasonDays == 2);
  CHECK(good->areas.at(0).seasonStrength == doctest::Approx(0.5F));
  CHECK_FALSE(good->areas.at(0).snowFromRain);
  CHECK_FALSE(parseClimateRules(head + "seasons:\n  days: 0\n" + four).has_value());
  CHECK_FALSE(parseClimateRules(head + "seasons:\n  days: 3\n  spring: {}\n").has_value());
  CHECK_FALSE(
      parseClimateRules(head + "seasons:\n  days: 3\n" + four + "  monsoon: {}\n").has_value());
  CHECK_FALSE(parseClimateRules(head + "seasons:\n  days: 3\n  spring: { odds: { hail: 2 } }\n"
                                       "  summer: {}\n  autumn: {}\n  winter: {}\n")
                  .has_value());
  CHECK_FALSE(parseClimateRules(head +
                                "seasons:\n  days: 3\n  spring: { grade: { gain: [1, 1] } }\n"
                                "  summer: {}\n  autumn: {}\n  winter: {}\n")
                  .has_value());
  CHECK_FALSE(parseClimateRules(head + "seasons:\n  days: 3\n  spring: { leaves: 2 }\n"
                                       "  summer: {}\n  autumn: {}\n  winter: {}\n")
                  .has_value());
}
