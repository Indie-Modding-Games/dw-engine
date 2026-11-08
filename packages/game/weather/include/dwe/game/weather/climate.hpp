#pragma once

#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The field's weather (an engine improvement: the original has no weather). Which weathers each
// area can have comes from the game pack (field/weather.yaml); what the weather is at a moment is a
// pure function of the playthrough's weather seed, the area and the game time, so it never rolls
// the game's random numbers, is the same on every map of an area and comes back the same after a
// load. Nothing here changes a rule: stats, encounters, timers and collisions never read it.
namespace dwe::game::weather {

enum class WeatherKind : std::uint8_t {
  // No weather: interiors, caves, buildings, maps no area lists, or the option turned off.
  None,
  // Clear sky: sunny by day.
  Clear,
  Cloudy,
  LightRain,
  HeavyRain,
  LightSnow,
  HeavySnow,
  // A haze that thickens toward the far (upper) part of the map and dulls its colours.
  Fog,
  // Heavy rain driven by the wind, with lightning and thunder.
  Storm,
  // Leaves or dust blowing sideways (ClimateArea::wind).
  Wind,
};

inline constexpr std::size_t WeatherKindCount = 10;

// The kinds an area can roll (every kind but None), in the order of WeatherWeights.
inline constexpr std::array RolledKinds{
    WeatherKind::Clear,     WeatherKind::Cloudy,    WeatherKind::LightRain,
    WeatherKind::HeavyRain, WeatherKind::LightSnow, WeatherKind::HeavySnow,
    WeatherKind::Fog,       WeatherKind::Storm,     WeatherKind::Wind};

// The kind's name in the pack and on the command line (none, clear, cloudy, light_rain,
// heavy_rain, light_snow, heavy_snow, fog, storm, wind).
[[nodiscard]] std::string_view weatherKindName(WeatherKind kind);
// A kind by name; `rain` and `snow` are the light ones, and dashes count as underscores.
[[nodiscard]] std::optional<WeatherKind> weatherKindNamed(std::string_view name);

// How often an area rolls each kind: a weight per kind of RolledKinds, 0 for never.
using WeatherWeights = std::array<std::uint32_t, RolledKinds.size()>;

// A factor per kind of RolledKinds that multiplies an area's weights (1 leaves a kind as it is).
using WeatherOdds = std::array<float, RolledKinds.size()>;
inline constexpr WeatherOdds EvenOdds = [] {
  WeatherOdds odds{};
  odds.fill(1.0F);
  return odds;
}();

// The parts of the day the weather's odds follow: dawn, day, dusk and night. The clock's time of
// day has no dawn, so each part starts at an hour of its own (ClimateRules::dayParts).
enum class DayPart : std::uint8_t { Dawn, Day, Dusk, Night };

inline constexpr std::size_t DayPartCount = 4;

// The part's name in the pack (dawn, day, dusk, night).
[[nodiscard]] std::string_view dayPartName(DayPart part);
[[nodiscard]] std::optional<DayPart> dayPartNamed(std::string_view name);

// A part of the day: the hour it starts at and its odds, stacked on the season's.
struct DayPartRules {
  // The hour of the day, 0 to 23, it starts at; it lasts until the next part starts.
  std::int32_t from = 0;
  WeatherOdds odds = EvenOdds;
};

// The seasons (an engine improvement on top of the weather: the original has none). They follow the
// game calendar in turns of ClimateRules::seasonDays days from year 0, day 0, spring first.
enum class Season : std::uint8_t { Spring, Summer, Autumn, Winter };

inline constexpr std::size_t SeasonCount = 4;

// The season's name in the pack and on the command line (spring, summer, autumn, winter).
[[nodiscard]] std::string_view seasonName(Season season);
// A season by name; `fall` is autumn.
[[nodiscard]] std::optional<Season> seasonNamed(std::string_view name);

// How a season grades the scenery (the map's background and its depth layers, never the
// characters): greens turned toward another hue, then the saturation, then a gain per channel and
// a lift. The neutral grade leaves every colour as it is.
struct SeasonGrade {
  // Hue in degrees the greens of the foliage turn toward, and how far, 0 (not at all) to 1.
  float foliageHue = 0.0F;
  float foliage = 0.0F;
  // 1 as it is, 0 grey, above 1 more vivid.
  float saturation = 1.0F;
  // Multiplies red, green and blue.
  std::array<float, 3> gain{1.0F, 1.0F, 1.0F};
  // Added to every channel: a paler picture.
  float lift = 0.0F;

  friend bool operator==(const SeasonGrade&, const SeasonGrade&) = default;
};

// What a season does: its weather odds, its grade and its falling particles.
struct SeasonRules {
  // Multiplies an area's weight of each kind of RolledKinds (1 leaves it as it is).
  WeatherOdds odds = EvenOdds;
  // The part of the rain, 0 to 1, that falls as snow (light rain as light snow, heavy as heavy) in
  // the areas whose rain can turn to snow.
  float rainToSnow = 0.0F;
  SeasonGrade grade;
  // Petals (spring) and leaves (autumn) falling over the map: how many of the most there can be, 0
  // to 1.
  float petals = 0.0F;
  float leaves = 0.0F;
};

// What the wind carries over an area: leaves, or dust in the dry areas.
enum class WindDebris : std::uint8_t { Leaves, Dust };

// A group of maps that share their weather.
struct ClimateArea {
  std::string id;
  // Map ids, or prefixes ending in `*` (`mayo*`).
  std::vector<std::string> maps;
  WeatherWeights weights{};
  // How much the seasons move this area, 0 (not at all) to 1: an area with a climate of its own
  // (snow, desert, jungle) is only nudged by them.
  float seasonStrength = 1.0F;
  // Whether a cold season turns part of its rain into snow.
  bool snowFromRain = true;
  // What its wind blows about.
  WindDebris wind = WindDebris::Leaves;
  // The ids of the areas next to this one (a walk or a ride away), where its fronts spread. Both
  // ways: an area listed by another is its neighbour too.
  std::vector<std::string> neighbours;
};

// Weather fronts: a weather an area rolls anew may spread to the areas around it, one more area
// every delayHours, and stays a period there (a purely visual drift: no rule reads it).
struct FrontRules {
  // The chance, from 0 to 1, that a weather an area rolls anew spreads; 0 for no fronts.
  float chance = 0.0F;
  // Game hours a front takes to reach the next area.
  std::int32_t delayHours = 2;
  // How many areas away a front reaches (1: only the neighbours).
  std::int32_t reach = 1;
  // The kinds that travel; every kind when empty.
  std::vector<WeatherKind> kinds;
};

struct ClimateRules {
  // The weather may change every this many game hours (the first period starts at year 0, day 0,
  // midnight).
  std::int32_t periodHours = 3;
  // The chance, from 0 to 1, that a period keeps the weather of the one before.
  float keepChance = 0.4F;
  // Real seconds a change of weather takes to fade from one look to the other.
  float fadeSeconds = 8.0F;
  // Maps that never have weather, whatever area lists them (ids or `*` prefixes). Maps with a
  // fixed time of day (interiors, caves) never have it either.
  std::vector<std::string> noWeather;
  std::vector<ClimateArea> areas;
  // Days each season lasts; 0 when the pack has no seasons.
  std::int32_t seasonDays = 0;
  // Spring, summer, autumn and winter.
  std::array<SeasonRules, SeasonCount> seasons{};
  FrontRules fronts;
  // Whether the pack gives odds by part of the day, and those of dawn, day, dusk and night.
  bool hasDayParts = false;
  std::array<DayPartRules, DayPartCount> dayParts{};

  [[nodiscard]] bool hasSeasons() const { return seasonDays > 0; }
  [[nodiscard]] const SeasonRules& season(Season which) const {
    return seasons.at(static_cast<std::size_t>(which));
  }

  [[nodiscard]] bool hasFronts() const { return fronts.chance > 0.0F && fronts.reach > 0; }

  // The area of a map: none for the maps without weather. `fixedTimeOfDay` is the map table's flag.
  [[nodiscard]] const ClimateArea* areaOf(std::string_view mapId, bool fixedTimeOfDay) const;
  // An area by its id; none when no area has it.
  [[nodiscard]] const ClimateArea* areaNamed(std::string_view id) const;
};

// Whether a map id matches a pattern: the same id, or a prefix of it when the pattern ends in `*`.
[[nodiscard]] bool mapMatches(std::string_view pattern, std::string_view mapId);

[[nodiscard]] core::Result<ClimateRules> parseClimateRules(std::string_view yaml);
// field/weather of the mounted packs.
[[nodiscard]] core::Result<ClimateRules> loadClimateRules(const assets::VirtualFileSystem& files);

// The period a game time falls in: the game hour since year 0 (GameClock::elapsedFrames /
// framesPerHour) divided by the rules' period.
[[nodiscard]] std::int64_t periodOf(const ClimateRules& rules, std::int64_t gameHour);

// The season of a game day (days since year 0, day 0: GameClock::elapsedFrames / framesPerDay):
// turns of seasonDays days, spring first. Spring without seasons.
[[nodiscard]] Season seasonOfDay(const ClimateRules& rules, std::int64_t day);
// The season of a game hour since year 0.
[[nodiscard]] Season seasonOfHour(const ClimateRules& rules, std::int64_t gameHour);

// The part of the day of a game hour since year 0: the last part that starts at or before its hour
// of the day (night before dawn). Day without parts in the rules.
[[nodiscard]] DayPart dayPartOfHour(const ClimateRules& rules, std::int64_t gameHour);

// An area's weights with the odds of a season (as seasonalWeights) and of a part of the day
// multiplied in, in hundredths; the area's own weights with neither (or when no kind would be
// left).
[[nodiscard]] WeatherWeights oddsWeights(const ClimateRules& rules, const ClimateArea& area,
                                         std::optional<Season> season, std::optional<DayPart> part);

// An area's weights in a season: each kind times the season's odds (moved toward 1 by the area's
// seasonStrength), and part of the rain as snow where the area lets it. In hundredths, so the
// odds keep two decimals; the area's own weights when no kind would be left.
[[nodiscard]] WeatherWeights seasonalWeights(const ClimateRules& rules, const ClimateArea& area,
                                             Season season);

// A season's grade on an area: the season's, moved toward the neutral grade by the area's
// seasonStrength.
[[nodiscard]] SeasonGrade seasonalGrade(const ClimateRules& rules, const ClimateArea& area,
                                        Season season);

// The weather's own random numbers: a hash of the seed, the area and the period (splitmix64), never
// the game's random stream, so the original's roll order stays as it is.
[[nodiscard]] std::uint64_t weatherRoll(std::uint64_t seed, std::string_view area,
                                        std::int64_t period, std::uint64_t salt);

// The weather of an area in a period: each period rolls whether it keeps the last one's weather
// (keepChance) and, when it changes, a kind by the area's weights times the odds of the part of the
// day the rolling period starts in (when the rules have day parts). A pure function: the same seed,
// area and period always give the same kind. None for an area without weights. With `seasonal`
// (and seasons in the rules) a kind is rolled by the weights of the season of the period that rolls
// it (seasonalWeights); without, by the area's own, exactly as with no seasons at all.
[[nodiscard]] WeatherKind weatherAt(const ClimateRules& rules, const ClimateArea& area,
                                    std::uint64_t seed, std::int64_t period, bool seasonal = false);

// How many areas apart two areas are through their neighbours: 0 for the same area, nothing when
// no chain of neighbours joins them.
[[nodiscard]] std::optional<std::int32_t> areaSteps(const ClimateRules& rules,
                                                    const ClimateArea& from, const ClimateArea& to);

// A front over an area: the weather another area rolled anew, arrived after travelling `steps`
// areas.
struct Front {
  WeatherKind kind = WeatherKind::None;
  // The id of the area it started in.
  std::string from;
  // The game hour it arrived at; it stays periodHours from then.
  std::int64_t arrived = 0;
  std::int32_t steps = 0;
};

// The front over an area at a game hour, if any: an area `steps` away (1 to the fronts' reach)
// rolled a new weather in the period that started steps * delayHours before it arrives (that
// area's own weather, never one that reached it), its spread roll passed, the kind travels and the
// area can have it at that hour (a weight above 0). When several are over it, the latest to arrive,
// then the nearest, then the first area of the rules. A pure function, as weatherAt.
[[nodiscard]] std::optional<Front> frontAt(const ClimateRules& rules, const ClimateArea& area,
                                           std::uint64_t seed, std::int64_t gameHour,
                                           bool seasonal = false);

// The weather shown over an area at a game hour: the front over it if one is, otherwise its own
// weather of the hour's period (weatherAt). The same as weatherAt without fronts.
[[nodiscard]] WeatherKind weatherAtHour(const ClimateRules& rules, const ClimateArea& area,
                                        std::uint64_t seed, std::int64_t gameHour,
                                        bool seasonal = false);

// A playthrough's weather seed from its game seed, so a new game started with --seed has the same
// weather every time (a mix of the seed, never a roll of the game's random stream).
[[nodiscard]] std::uint64_t weatherSeedOf(std::uint64_t gameSeed);

} // namespace dwe::game::weather
