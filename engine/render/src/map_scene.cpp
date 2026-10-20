#include <dwe/render/map_scene.hpp>

#include <utility>

namespace dwe::render {

namespace {

// map.c: updateTimeOfDay.
constexpr std::int32_t DayStart = 6;
constexpr std::int32_t DuskStart = 16;
constexpr std::int32_t NightStart = 20;

} // namespace

TimeOfDay timeOfDayAt(std::int32_t hour) {
    if (hour >= DuskStart && hour < NightStart) {
        return TimeOfDay::Dusk;
    }
    if (hour < DayStart || hour >= NightStart) {
        return TimeOfDay::Night;
    }
    return TimeOfDay::Day;
}

std::size_t MapSceneData::backgroundVariant(TimeOfDay time, bool fixedTimeOfDay) const {
    const auto variant = static_cast<std::size_t>(time);
    return fixedTimeOfDay || variant >= images.backgroundPalettes.size() ? 0 : variant;
}

core::Result<MapSceneData> loadMapSceneData(const assets::VirtualFileSystem& vfs, std::string_view mapId) {
    auto document = assets::loadMapDocument(vfs, mapId);
    if (!document) {
        return std::unexpected{document.error()};
    }
    auto images = assets::loadMapImages(vfs, *document);
    if (!images) {
        return std::unexpected{images.error()};
    }
    MapSceneData scene{.document = std::move(*document), .camera = {}, .images = std::move(*images)};
    scene.camera = mapCameraDesc(scene.document);
    return scene;
}

} // namespace dwe::render
