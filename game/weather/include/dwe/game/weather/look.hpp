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

} // namespace dwe::game::weather
