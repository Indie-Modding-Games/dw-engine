#pragma once

#include <dwe/assets/map_document.hpp>
#include <dwe/assets/map_images.hpp>
#include <dwe/assets/map_patch.hpp>
#include <dwe/core/error.hpp>
#include <dwe/render/map_camera.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace dwe::assets {
class VirtualFileSystem;
}

namespace dwe::render {

// The time of day a map's background shows (map.c: updateTimeOfDay): the index of its palette
// variant.
enum class TimeOfDay : std::uint8_t {
  Day = 0,
  Dusk = 1,
  Night = 2,
};

// When each time of day starts and what it does to a map's lights. The values are game rules: the
// game reads them from its field rules (map.c: updateTimeOfDay).
struct TimeOfDaySchedule {
  std::int32_t dayStart = 0;
  std::int32_t duskStart = 0;
  std::int32_t nightStart = 0;
  glm::vec3 duskLight{1.0F};
  glm::vec3 nightLight{1.0F};
};

// The time of day at an hour of the game's clock, 0-23.
TimeOfDay timeOfDayAt(std::int32_t hour, const TimeOfDaySchedule& schedule);

// Whether the background changes palette at the start of this hour: map.c: renderMap starts the
// fade only at the exact hours a time of day starts, on maps without a fixed time of day.
bool timeOfDayChangesAt(std::int32_t hour, const TimeOfDaySchedule& schedule);

// What the time of day does to a map's three lights (map.c: updateTimeOfDay). Maps with a fixed
// time of day keep them whole.
glm::vec3 timeOfDayLightScale(TimeOfDay time, bool fixedTimeOfDay,
                              const TimeOfDaySchedule& schedule);

// Everything the map renderer needs from a map, read through the VFS: the document, its camera, and
// its images (the background as indices with one palette per variant, and the resolved sprite
// sheet).
struct MapSceneData {
  assets::MapDocument document;
  MapCameraDesc camera;
  assets::MapImages images;

  // The palette variant to show: the time of day's, or the first one for maps with a fixed time of
  // day or without that variant.
  [[nodiscard]] std::size_t backgroundVariant(TimeOfDay time, bool fixedTimeOfDay) const;
};

// Loads a map by id (`map/<id>/data`, its backgrounds and its texture pages:
// assets::loadMapImages).
core::Result<MapSceneData> loadMapSceneData(const assets::VirtualFileSystem& vfs,
                                            std::string_view mapId);
// The same with more patches applied last (assets::loadMapDocument's `extra`).
core::Result<MapSceneData> loadMapSceneData(const assets::VirtualFileSystem& vfs,
                                            std::string_view mapId,
                                            std::span<const assets::MapPatchText> extra);

} // namespace dwe::render
