// The field's weather (game::weather, an engine improvement: the original has no weather). It only
// draws: no rule of the field, the partner or the battles reads it.
#include <dwe/core/log.hpp>
#include <dwe/game/weather/climate.hpp>

#include "field_scene.hpp"

namespace dwe::app {

namespace {

float daylightOf(render::TimeOfDay time) {
  switch (time) {
  case render::TimeOfDay::Day:
    return 1.0F;
  case render::TimeOfDay::Dusk:
    return 0.5F;
  case render::TimeOfDay::Night:
    return 0.0F;
  }
  return 1.0F;
}

} // namespace

void FieldScene::setWeatherSeed(std::uint64_t seed) {
  weatherSeed_ = seed;
  weatherBlend_.snap(weatherTarget());
}

void FieldScene::setWeatherEnabled(bool enabled) {
  weatherEnabled_ = enabled;
  weatherBlend_.snap(weatherTarget());
}

void FieldScene::forceWeather(std::optional<game::weather::WeatherKind> kind) {
  forcedWeather_ = kind;
  weatherBlend_.snap(weatherTarget());
}

game::weather::WeatherKind FieldScene::weatherTarget() const {
  if (!weatherEnabled_ || weatherArea_ == nullptr || !climate_) {
    return game::weather::WeatherKind::None;
  }
  if (forcedWeather_) {
    return *forcedWeather_;
  }
  const std::int64_t framesPerHour =
      clock_ != nullptr
          ? static_cast<std::int64_t>(clock_->rules().framesPerMinute) * ruleset::MinutesPerHour
          : 0;
  const std::int64_t hour = framesPerHour > 0 ? clock_->elapsedFrames() / framesPerHour : 0;
  return game::weather::weatherAt(*climate_, *weatherArea_, weatherSeed_,
                                  game::weather::periodOf(*climate_, hour));
}

void FieldScene::enterWeatherArea(std::string_view mapId, bool fixedTimeOfDay) {
  const game::weather::ClimateArea* area =
      climate_ ? climate_->areaOf(mapId, fixedTimeOfDay) : nullptr;
  const bool sameArea = area != nullptr && weatherArea_ != nullptr && area->id == weatherArea_->id;
  weatherArea_ = area;
  if (!sameArea) {
    weatherBlend_.snap(weatherTarget());
  }
  core::logDebug("field: map {} weather area {}: {}", mapId, area != nullptr ? area->id : "none",
                 game::weather::weatherKindName(weatherTarget()));
}

void FieldScene::updateWeather(double frameSeconds, bool paused) {
  weatherBlend_.advance(weatherTarget(), frameSeconds);
  if (!paused) {
    weatherSeconds_ += frameSeconds;
  }
  const bool drawn = mapRenderer_->hasMap() && !mapLayerHidden_ && !stage_.sceneryHidden &&
                     !stage_.camera && !weatherBlend_.look().clear();
  if (!drawn) {
    weatherDrawer_->clear();
    return;
  }
  const render::MapCamera& camera = mapRenderer_->camera();
  const render::PixelRect inside = camera.backgroundRect();
  weatherDrawer_->set(weatherQuads(weatherBlend_.look(), {.areaOrigin = inside.origin,
                                                          .areaSize = inside.size,
                                                          .scale = camera.scale(),
                                                          .scroll = camera.scroll(),
                                                          .seconds = weatherSeconds_,
                                                          .daylight = daylightOf(time_)}),
                      inside.origin, inside.size);
}

} // namespace dwe::app
