#pragma once

#include <dwe/game/weather/climate.hpp>

namespace dwe::game::weather {

// What the weather looks like on screen, each part from 0 (none) to 1 (full): the read-only view a
// drawer, the ambience or the lighting pass reads (FieldScene::weather). A change of weather fades
// every part from one look to the other, so a reader never sees a jump.
struct WeatherLook {
  // The weather being faded to (or shown).
  WeatherKind kind = WeatherKind::None;
  // How much of the sky is covered: darker, greyer light and cloud shadows over the ground.
  float cloudCover = 0.0F;
  // How hard it rains and snows.
  float rain = 0.0F;
  float snow = 0.0F;
  // Sunshine: a slightly warmer, brighter light (only a clear sky has it).
  float sunshine = 0.0F;
  // How thick the fog is.
  float fog = 0.0F;
  // How often and how bright the lightning flashes (a storm).
  float storm = 0.0F;
  // How hard the wind blows: leaves or dust sideways, rain and snow slanted.
  float wind = 0.0F;

  // How strong the weather's kind is, 0 to 1 (light rain or snow are about half of heavy).
  [[nodiscard]] float intensity() const;
  // Whether nothing of the weather shows.
  [[nodiscard]] bool clear() const;

  friend bool operator==(const WeatherLook&, const WeatherLook&) = default;
};

// The look a kind settles on.
[[nodiscard]] WeatherLook lookOf(WeatherKind kind);

// The look shown, fading toward the weather's look: every part moves at most 1 / fadeSeconds a
// second, so a whole change takes fadeSeconds.
class WeatherBlend {
public:
  explicit WeatherBlend(float fadeSeconds = 8.0F) : fadeSeconds_(fadeSeconds) {}

  // Fades toward a kind for `seconds`.
  void advance(WeatherKind target, double seconds);
  // Shows a kind at once (a new map of another area, the option turned on or off).
  void snap(WeatherKind target) { shown_ = lookOf(target); }

  [[nodiscard]] const WeatherLook& look() const { return shown_; }
  // Whether the shown look has reached the kind's.
  [[nodiscard]] bool settled() const { return shown_ == lookOf(shown_.kind); }

private:
  float fadeSeconds_;
  WeatherLook shown_{};
};

// What the season looks like on screen: the scenery's grade and the petals or leaves falling, each
// 0 to 1. Neutral (no grade, nothing falling) on maps without weather and with the option off.
struct SeasonLook {
  // The season being faded to (or shown); nothing when neutral.
  std::optional<Season> season;
  SeasonGrade grade;
  float petals = 0.0F;
  float leaves = 0.0F;

  // Whether nothing of the season shows.
  [[nodiscard]] bool neutral() const;

  friend bool operator==(const SeasonLook&, const SeasonLook&) = default;
};

// The look a season settles on in an area (seasonalGrade and the season's particles, both moved
// toward neutral by the area's seasonStrength).
[[nodiscard]] SeasonLook seasonLookOf(const ClimateRules& rules, const ClimateArea& area,
                                      Season season);

// The season shown, fading toward the target look as the weather does: every part moves at most
// 1 / fadeSeconds a second (the hue and the gain as far as their distance needs), so a new season
// at midnight fades in over fadeSeconds.
class SeasonBlend {
public:
  explicit SeasonBlend(float fadeSeconds = 8.0F) : fadeSeconds_(fadeSeconds) {}

  void advance(const SeasonLook& target, double seconds);
  void snap(const SeasonLook& target) { shown_ = target; }

  [[nodiscard]] const SeasonLook& look() const { return shown_; }

private:
  float fadeSeconds_;
  SeasonLook shown_{};
};

} // namespace dwe::game::weather
