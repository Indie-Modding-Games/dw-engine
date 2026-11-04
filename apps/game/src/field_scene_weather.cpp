// The field's weather and seasons (game::weather, an engine improvement: the original has neither).
// They only draw: no rule of the field, the partner or the battles reads them.
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

// The season's grade as the map renderer takes it; the lift (a paler picture) is gentler at night,
// so the dark stays dark.
render::MapGrade mapGradeOf(const game::weather::SeasonGrade& grade, float daylight) {
  return {.foliageHue = grade.foliageHue,
          .foliage = grade.foliage,
          .saturation = grade.saturation,
          .gain = {grade.gain[0], grade.gain[1], grade.gain[2]},
          .lift = grade.lift * (0.4F + (0.6F * daylight))};
}

} // namespace

void FieldScene::setSeasonsEnabled(bool enabled) {
  seasonsEnabled_ = enabled;
  weatherBlend_.snap(weatherTarget());
  seasonBlend_.snap(seasonTarget());
}

void FieldScene::forceSeason(std::optional<game::weather::Season> season) {
  forcedSeason_ = season;
  seasonBlend_.snap(seasonTarget());
}

std::optional<game::weather::Season> FieldScene::season() const {
  if (!seasonsEnabled_ || !climate_ || !climate_->hasSeasons()) {
    return std::nullopt;
  }
  if (forcedSeason_) {
    return forcedSeason_;
  }
  return game::weather::seasonOfHour(*climate_, weatherHour());
}

game::weather::SeasonLook FieldScene::seasonTarget() const {
  const std::optional<game::weather::Season> now = season();
  if (!now || weatherArea_ == nullptr) {
    return {};
  }
  return game::weather::seasonLookOf(*climate_, *weatherArea_, *now);
}

std::int64_t FieldScene::weatherHour() const {
  const std::int64_t framesPerHour =
      clock_ != nullptr
          ? static_cast<std::int64_t>(clock_->rules().framesPerMinute) * ruleset::MinutesPerHour
          : 0;
  return framesPerHour > 0 ? clock_->elapsedFrames() / framesPerHour : 0;
}

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
  return game::weather::weatherAt(*climate_, *weatherArea_, weatherSeed_,
                                  game::weather::periodOf(*climate_, weatherHour()),
                                  seasonsEnabled_);
}

void FieldScene::enterWeatherArea(std::string_view mapId, bool fixedTimeOfDay) {
  const game::weather::ClimateArea* area =
      climate_ ? climate_->areaOf(mapId, fixedTimeOfDay) : nullptr;
  const bool sameArea = area != nullptr && weatherArea_ != nullptr && area->id == weatherArea_->id;
  weatherArea_ = area;
  if (!sameArea) {
    weatherBlend_.snap(weatherTarget());
    seasonBlend_.snap(seasonTarget());
  }
  core::logDebug("field: map {} weather area {}: {}", mapId, area != nullptr ? area->id : "none",
                 game::weather::weatherKindName(weatherTarget()));
}

void FieldScene::updateWeather(double frameSeconds, bool paused) {
  weatherBlend_.advance(weatherTarget(), frameSeconds);
  seasonBlend_.advance(seasonTarget(), frameSeconds);
  if (!paused) {
    weatherSeconds_ += frameSeconds;
  }
  const game::weather::SeasonLook& season = seasonBlend_.look();
  mapRenderer_->setGrade(mapGradeOf(season.grade, daylightOf(time_)));
  const bool drawn = mapRenderer_->hasMap() && !mapLayerHidden_ && !stage_.sceneryHidden &&
                     !stage_.camera && (!weatherBlend_.look().clear() || !season.neutral());
  if (!drawn) {
    weatherDrawer_->clear();
    return;
  }
  const render::MapCamera& camera = mapRenderer_->camera();
  const render::PixelRect inside = camera.backgroundRect();
  const WeatherFrame frame{.areaOrigin = inside.origin,
                           .areaSize = inside.size,
                           .scale = camera.scale(),
                           .scroll = camera.scroll(),
                           .seconds = weatherSeconds_,
                           .daylight = daylightOf(time_)};
  std::vector<WeatherQuad> quads = weatherQuads(weatherBlend_.look(), frame);
  std::vector<WeatherQuad> falling = seasonQuads(season, weatherBlend_.look(), frame);
  quads.insert(quads.end(), falling.begin(), falling.end());
  weatherDrawer_->set(std::move(quads), inside.origin, inside.size);
}

} // namespace dwe::app
