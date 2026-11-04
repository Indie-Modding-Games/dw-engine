#include <dwe/game/weather/look.hpp>

#include <algorithm>
#include <cmath>

namespace dwe::game::weather {

namespace {

// How strong light rain and snow are next to heavy.
constexpr float Light = 0.45F;

float toward(float from, float to, float step) {
  return from < to ? std::min(from + step, to) : std::max(from - step, to);
}

} // namespace

float WeatherLook::intensity() const {
  switch (kind) {
  case WeatherKind::None:
    return 0.0F;
  case WeatherKind::Clear:
    return sunshine;
  case WeatherKind::Cloudy:
    return cloudCover;
  case WeatherKind::LightRain:
  case WeatherKind::HeavyRain:
    return rain;
  case WeatherKind::LightSnow:
  case WeatherKind::HeavySnow:
    return snow;
  }
  return 0.0F;
}

bool WeatherLook::clear() const {
  return cloudCover <= 0.0F && rain <= 0.0F && snow <= 0.0F && sunshine <= 0.0F;
}

WeatherLook lookOf(WeatherKind kind) {
  switch (kind) {
  case WeatherKind::None:
    return {};
  case WeatherKind::Clear:
    return {.kind = kind, .sunshine = 1.0F};
  case WeatherKind::Cloudy:
    return {.kind = kind, .cloudCover = 0.8F};
  case WeatherKind::LightRain:
    return {.kind = kind, .cloudCover = 0.75F, .rain = Light};
  case WeatherKind::HeavyRain:
    return {.kind = kind, .cloudCover = 1.0F, .rain = 1.0F};
  case WeatherKind::LightSnow:
    return {.kind = kind, .cloudCover = 0.6F, .snow = Light};
  case WeatherKind::HeavySnow:
    return {.kind = kind, .cloudCover = 0.9F, .snow = 1.0F};
  }
  return {};
}

void WeatherBlend::advance(WeatherKind target, double seconds) {
  const WeatherLook to = lookOf(target);
  if (fadeSeconds_ <= 0.0F) {
    shown_ = to;
    return;
  }
  const auto step = static_cast<float>(std::max(seconds, 0.0) / fadeSeconds_);
  shown_.kind = target;
  shown_.cloudCover = toward(shown_.cloudCover, to.cloudCover, step);
  shown_.rain = toward(shown_.rain, to.rain, step);
  shown_.snow = toward(shown_.snow, to.snow, step);
  shown_.sunshine = toward(shown_.sunshine, to.sunshine, step);
}

bool SeasonLook::neutral() const {
  return grade == SeasonGrade{.foliageHue = grade.foliageHue} && petals <= 0.0F && leaves <= 0.0F;
}

SeasonLook seasonLookOf(const ClimateRules& rules, const ClimateArea& area, Season season) {
  const SeasonRules& of = rules.season(season);
  const float strength = std::clamp(area.seasonStrength, 0.0F, 1.0F);
  return {.season = season,
          .grade = seasonalGrade(rules, area, season),
          .petals = of.petals * strength,
          .leaves = of.leaves * strength};
}

void SeasonBlend::advance(const SeasonLook& target, double seconds) {
  if (fadeSeconds_ <= 0.0F) {
    shown_ = target;
    return;
  }
  const auto step = static_cast<float>(std::max(seconds, 0.0) / fadeSeconds_);
  // The foliage's hue only matters while it turns: from neutral it starts at the target's hue, and
  // between two seasons it moves with the turn (a hue is up to 360 degrees away, the rest 1 or 2);
  // toward neutral it stays while the turn fades out.
  if (shown_.grade.foliage <= 0.0F) {
    shown_.grade.foliageHue = target.grade.foliageHue;
  }
  const float hue = target.grade.foliage > 0.0F ? target.grade.foliageHue : shown_.grade.foliageHue;
  shown_.season = target.season;
  shown_.grade.foliageHue = toward(shown_.grade.foliageHue, hue, step * 360.0F);
  shown_.grade.foliage = toward(shown_.grade.foliage, target.grade.foliage, step);
  shown_.grade.saturation = toward(shown_.grade.saturation, target.grade.saturation, step);
  for (std::size_t i = 0; i < shown_.grade.gain.size(); ++i) {
    shown_.grade.gain.at(i) = toward(shown_.grade.gain.at(i), target.grade.gain.at(i), step);
  }
  shown_.grade.lift = toward(shown_.grade.lift, target.grade.lift, step);
  shown_.petals = toward(shown_.petals, target.petals, step);
  shown_.leaves = toward(shown_.leaves, target.leaves, step);
}

} // namespace dwe::game::weather
