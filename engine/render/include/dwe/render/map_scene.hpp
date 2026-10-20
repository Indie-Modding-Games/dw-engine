#pragma once

#include <dwe/assets/map_document.hpp>
#include <dwe/assets/map_images.hpp>
#include <dwe/core/error.hpp>
#include <dwe/render/map_camera.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace dwe::assets {
class VirtualFileSystem;
}

namespace dwe::render {

// The time of day a map's background shows (map.c: updateTimeOfDay): the index of its palette variant.
enum class TimeOfDay : std::uint8_t {
    Day = 0,   // 6:00-15:59
    Dusk = 1,  // 16:00-19:59
    Night = 2, // 20:00-5:59
};

// The time of day at an hour of the game's clock, 0-23.
TimeOfDay timeOfDayAt(std::int32_t hour);

// Everything the map renderer needs from a map, read through the VFS: the document, its camera, and its images (the
// background as indices with one palette per variant, and the resolved sprite sheet).
struct MapSceneData {
    assets::MapDocument document;
    MapCameraDesc camera;
    assets::MapImages images;

    // The palette variant to show: the time of day's, or the first one for maps with a fixed time of day or without
    // that variant.
    [[nodiscard]] std::size_t backgroundVariant(TimeOfDay time, bool fixedTimeOfDay) const;
};

// Loads a map by id (`map/<id>/data`, its backgrounds and its texture pages: assets::loadMapImages).
core::Result<MapSceneData> loadMapSceneData(const assets::VirtualFileSystem& vfs, std::string_view mapId);

} // namespace dwe::render
