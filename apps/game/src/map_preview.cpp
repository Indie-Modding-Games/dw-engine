#include "map_preview.hpp"

#include <dwe/assets/map_table.hpp>
#include <dwe/assets/pak.hpp>
#include <dwe/core/log.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <string_view>
#include <utility>
#include <variant>

namespace dwe::app {

namespace {

// Walking speed of the marker, in world units per second.
constexpr float MarkerSpeed = 400.0F;
// How close to a clicked point the marker stops.
constexpr float ArrivalDistance = 5.0F;
// The collision grid spans -5000..5000 on both ground axes; the marker stays on it.
constexpr float GroundLimit = 5000.0F;
// Frame times logged after a map switch. A frame's time is measured before its ticks run, so frame 0 is from before
// the switch, frame 1 includes the switch (loading and setMap) and frame 2 the texture uploads.
constexpr int FramesLoggedAfterSwitch = 4;

// The actions that fade to day, dusk and night, in render::TimeOfDay order.
constexpr std::array<std::string_view, 3> TimeActions{"debug_time_day", "debug_time_dusk", "debug_time_night"};

glm::vec3 clampToGround(glm::vec3 point) {
    return {std::clamp(point.x, -GroundLimit, GroundLimit), 0.0F, std::clamp(point.z, -GroundLimit, GroundLimit)};
}

} // namespace

core::Result<std::unique_ptr<MapPreview>> MapPreview::create(render::Renderer& renderer, const MapPreviewDesc& desc) {
    if (desc.mapIds.empty()) {
        return core::fail("map preview: no map to show");
    }
    auto content = std::make_unique<Content>();
    auto packs = assets::findPacks(content->disk, desc.content);
    if (!packs) {
        return std::unexpected{packs.error()};
    }
    // A content folder holds the engine's own packs.
    auto opened = assets::openPacks(content->disk, packs->packs, assets::PackOrigin::Engine);
    if (!opened) {
        return std::unexpected{opened.error()};
    }
    content->packs = std::move(*opened);
    auto vfs = assets::VirtualFileSystem::mount(content->packs.locations);
    if (!vfs) {
        return std::unexpected{vfs.error()};
    }
    content->vfs = std::make_unique<assets::VirtualFileSystem>(std::move(*vfs));
    auto map = render::MapRenderer::create(renderer);
    if (!map) {
        return std::unexpected{map.error()};
    }
    std::unique_ptr<MapPreview> preview{new MapPreview{std::move(content), std::move(*map), desc}};
    if (auto shown = preview->show(0, desc.focus); !shown) {
        return std::unexpected{shown.error()};
    }
    return preview;
}

MapPreview::MapPreview(std::unique_ptr<Content> content, std::unique_ptr<render::MapRenderer> map, MapPreviewDesc desc)
    : content_(std::move(content)), map_(std::move(map)), desc_(std::move(desc)) {}

core::Result<void> MapPreview::show(std::size_t mapIndex, std::optional<glm::vec2> focus) {
    const std::string& mapId = desc_.mapIds[mapIndex];
    const auto start = std::chrono::steady_clock::now();
    const assets::VirtualFileSystem& vfs = *content_->vfs;
    auto scene = render::loadMapSceneData(vfs, mapId);
    if (!scene) {
        return std::unexpected{scene.error()};
    }
    bool fixedTime = false;
    if (auto table = assets::loadMapTable(vfs)) {
        if (const auto* entry = table->find(mapId)) {
            fixedTime = entry->fixedTimeOfDay;
        }
    }
    const auto loaded = std::chrono::steady_clock::now();
    if (auto set = map_->setMap(*scene, scene->backgroundVariant(desc_.time, fixedTime)); !set) {
        return std::unexpected{set.error()};
    }
    const auto set = std::chrono::steady_clock::now();
    if (focus) {
        focus_ = {focus->x, 0.0F, focus->y};
    } else if (const assets::MapWarp* entrance = scene->document.warp(0)) {
        focus_ = clampToGround(assets::warpSpawn(*entrance).position);
    } else {
        focus_ = glm::vec3{0.0F};
    }
    target_.reset();
    const assets::MapImages& images = scene->images;
    using Milliseconds = std::chrono::duration<double, std::milli>;
    core::logInfo("map preview: {} ({}x{} background, {} variants, {} sprite textures, {} depth-layer instances), "
                  "focus ({}, {}); loaded in {:.2f} ms, setMap {:.2f} ms",
                  mapId, images.background.width, images.background.height, images.backgroundPalettes.size(),
                  images.sprites.textures.size(), map_->layers().instanceCount(), focus_.x, focus_.z,
                  Milliseconds{loaded - start}.count(), Milliseconds{set - loaded}.count());
    mapIndex_ = mapIndex;
    scene_ = std::move(*scene);
    fixedTime_ = fixedTime;
    return {};
}

void MapPreview::showNextMap() {
    const std::size_t next = (mapIndex_ + 1) % desc_.mapIds.size();
    if (auto shown = show(next, std::nullopt); !shown) {
        core::logError("map preview: {}", shown.error().message);
        return;
    }
    framesToLog_ = FramesLoggedAfterSwitch;
}

void MapPreview::handle(const platform::Event& event) {
    if (const auto* moved = std::get_if<platform::MouseMoved>(&event)) {
        cursor_ = {moved->x, moved->y};
    } else if (const auto* button = std::get_if<platform::MouseButtonChanged>(&event)) {
        if (button->down && button->button == platform::MouseButton::Left && map_->hasMap()) {
            target_ = map_->camera().pickGround(cursor_);
        }
    }
}

void MapPreview::tick(const input::InputState& input, double seconds) {
    for (std::size_t time = 0; time < TimeActions.size(); ++time) {
        if (input.wasPressed(TimeActions[time])) {
            map_->setVariant(scene_.backgroundVariant(static_cast<render::TimeOfDay>(time), fixedTime_));
        }
    }
    if (input.wasPressed("debug_next_map")) {
        showNextMap();
    }
    const auto step = static_cast<float>(seconds) * MarkerSpeed;
    const input::Vector2 stick = input.vector("move_left", "move_right", "move_up", "move_down");
    if (stick.x != 0.0F || stick.y != 0.0F) {
        target_.reset();
        // Up on the stick walks away from the camera, along the ground.
        const render::MapCameraDesc& camera = scene_.camera;
        const glm::vec3 view = camera.target - camera.eye;
        const glm::vec3 forward = glm::normalize(glm::vec3{view.x, 0.0F, view.z});
        const glm::vec3 right = glm::cross(forward, glm::vec3{0.0F, 1.0F, 0.0F});
        focus_ = clampToGround(focus_ + ((right * stick.x) - (forward * stick.y)) * step);
    } else if (target_) {
        const glm::vec3 toTarget = *target_ - focus_;
        const float distance = glm::length(toTarget);
        focus_ = distance <= std::max(step, ArrivalDistance) ? *target_ : focus_ + (toTarget / distance * step);
        focus_ = clampToGround(focus_);
        if (focus_ == *target_) {
            target_.reset();
        }
    }
}

void MapPreview::update(double frameSeconds) {
    if (framesToLog_ > 0) {
        core::logInfo("map preview: frame {} after the switch: {:.2f} ms", FramesLoggedAfterSwitch - framesToLog_,
                      frameSeconds * 1000.0);
        --framesToLog_;
    }
    map_->update(frameSeconds, frameSeconds, focus_);
    const std::array markers{render::MapMarker{.feet = focus_}};
    map_->setMarkers(markers);
}

} // namespace dwe::app
