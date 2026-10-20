#include <dwe/render/map_scene.hpp>

#include <utility>

namespace dwe::render {

bool timeOfDayChangesAt(std::int32_t hour, const TimeOfDaySchedule& schedule) {
    return hour == schedule.dayStart || hour == schedule.duskStart || hour == schedule.nightStart;
}

glm::vec3 timeOfDayLightScale(TimeOfDay time, bool fixedTimeOfDay, const TimeOfDaySchedule& schedule) {
    if (fixedTimeOfDay || time == TimeOfDay::Day) {
        return glm::vec3{1.0F};
    }
    return time == TimeOfDay::Dusk ? schedule.duskLight : schedule.nightLight;
}

TimeOfDay timeOfDayAt(std::int32_t hour, const TimeOfDaySchedule& schedule) {
    if (hour >= schedule.duskStart && hour < schedule.nightStart) {
        return TimeOfDay::Dusk;
    }
    if (hour < schedule.dayStart || hour >= schedule.nightStart) {
        return TimeOfDay::Night;
    }
    return TimeOfDay::Day;
}

std::size_t MapSceneData::backgroundVariant(TimeOfDay time, bool fixedTimeOfDay) const {
    const auto variant = static_cast<std::size_t>(time);
    // map.c: updateTimeOfDay shows the first palette on maps with the fixed-time flag (0x40 of their MAP_ENTRIES
    // flags).
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
