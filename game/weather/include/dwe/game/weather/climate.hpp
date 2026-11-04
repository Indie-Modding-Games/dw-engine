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

// A group of maps that share their weather.
struct ClimateArea {
  std::string id;
  // Map ids, or prefixes ending in `*` (`mayo*`).
  std::vector<std::string> maps;
  WeatherWeights weights{};
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

// The weather's own random numbers: a hash of the seed, the area and the period (splitmix64), never
// the game's random stream, so the original's roll order stays as it is.
[[nodiscard]] std::uint64_t weatherRoll(std::uint64_t seed, std::string_view area,
                                        std::int64_t period, std::uint64_t salt);

// The weather of an area in a period: each period rolls whether it keeps the last one's weather
// (keepChance) and, when it changes, a kind by the area's weights. A pure function: the same seed,
// area and period always give the same kind. None for an area without weights.
[[nodiscard]] WeatherKind weatherAt(const ClimateRules& rules, const ClimateArea& area,
                                    std::uint64_t seed, std::int64_t period);

// A playthrough's weather seed from its game seed, so a new game started with --seed has the same
// weather every time (a mix of the seed, never a roll of the game's random stream).
[[nodiscard]] std::uint64_t weatherSeedOf(std::uint64_t gameSeed);

} // namespace dwe::game::weather
