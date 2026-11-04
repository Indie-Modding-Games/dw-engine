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
};

inline constexpr std::size_t WeatherKindCount = 7;

// The kinds an area can roll (every kind but None), in the order of WeatherWeights.
inline constexpr std::array RolledKinds{WeatherKind::Clear,     WeatherKind::Cloudy,
                                        WeatherKind::LightRain, WeatherKind::HeavyRain,
                                        WeatherKind::LightSnow, WeatherKind::HeavySnow};

// The kind's name in the pack and on the command line (none, clear, cloudy, light_rain,
// heavy_rain, light_snow, heavy_snow).
[[nodiscard]] std::string_view weatherKindName(WeatherKind kind);
// A kind by name; `rain` and `snow` are the light ones, and dashes count as underscores.
[[nodiscard]] std::optional<WeatherKind> weatherKindNamed(std::string_view name);

// How often an area rolls each kind: a weight per kind of RolledKinds, 0 for never.
using WeatherWeights = std::array<std::uint32_t, RolledKinds.size()>;

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
  std::array<float, RolledKinds.size()> odds{1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F};
  // The part of the rain, 0 to 1, that falls as snow (light rain as light snow, heavy as heavy) in
  // the areas whose rain can turn to snow.
  float rainToSnow = 0.0F;
  SeasonGrade grade;
  // Petals (spring) and leaves (autumn) falling over the map: how many of the most there can be, 0
  // to 1.
  float petals = 0.0F;
  float leaves = 0.0F;
};

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

  [[nodiscard]] bool hasSeasons() const { return seasonDays > 0; }
  [[nodiscard]] const SeasonRules& season(Season which) const {
    return seasons.at(static_cast<std::size_t>(which));
  }

  // The area of a map: none for the maps without weather. `fixedTimeOfDay` is the map table's flag.
  [[nodiscard]] const ClimateArea* areaOf(std::string_view mapId, bool fixedTimeOfDay) const;
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
// (keepChance) and, when it changes, a kind by the area's weights. A pure function: the same seed,
// area and period always give the same kind. None for an area without weights. With `seasonal`
// (and seasons in the rules) a kind is rolled by the weights of the season of the period that rolls
// it (seasonalWeights); without, by the area's own, exactly as with no seasons at all.
[[nodiscard]] WeatherKind weatherAt(const ClimateRules& rules, const ClimateArea& area,
                                    std::uint64_t seed, std::int64_t period, bool seasonal = false);

// A playthrough's weather seed from its game seed, so a new game started with --seed has the same
// weather every time (a mix of the seed, never a roll of the game's random stream).
[[nodiscard]] std::uint64_t weatherSeedOf(std::uint64_t gameSeed);

} // namespace dwe::game::weather
