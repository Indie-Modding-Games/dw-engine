#include <dwe/render/map_camera.hpp>

#include <algorithm>
#include <cmath>

namespace dwe::render {

namespace {

constexpr glm::vec3 WorldUp{0.0F, 1.0F, 0.0F};

// GsSetRefView2 has no roll: the camera's "down" is the original's +Y. A camera looking straight up or down has no
// such direction; the original's maps never do, and then -Z is used so the matrix stays valid.
glm::mat4 lookAtWithoutRoll(glm::vec3 eye, glm::vec3 target) {
    const glm::vec3 forward = target - eye;
    const float horizontal = std::hypot(forward.x, forward.z);
    const glm::vec3 up = horizontal > 1e-4F * glm::length(forward) ? WorldUp : glm::vec3{0.0F, 0.0F, -1.0F};
    return glm::lookAt(eye, target, up);
}

// The top-left background pixel of a window of `visible` pixels centred on `focus` along one axis: clamped to the
// background like the original's cameraX (map.c: updateDrawingOffsets and cameraIsAtEdge keep it in [0, width - 320],
// so the player leaves the centre only near the edges), or centred when the background is the smaller one. The corner
// is snapped to whole viewport pixels so the background does not shimmer while it scrolls.
float scrollAxis(float focus, float visible, float background, float scale) {
    if (background < visible) {
        return (background - visible) * 0.5F;
    }
    const float snapped = std::round((focus - (visible * 0.5F)) * scale) / scale;
    return std::clamp(snapped, 0.0F, background - visible);
}

} // namespace

MapCamera::MapCamera(const MapCameraDesc& desc)
    : desc_(desc), view_(lookAtWithoutRoll(desc.eye, desc.target)), inverseView_(glm::inverse(view_)),
      focus_(desc.backgroundSize * 0.5F) { // map.c: setupMap starts centred: cameraX = width / 2 - 160
    setViewport(OriginalScreenSize);
}

void MapCamera::setViewport(glm::vec2 size) {
    viewport_ = glm::max(size, glm::vec2{1.0F});
    scale_ = std::min(viewport_.x / OriginalScreenSize.x, viewport_.y / OriginalScreenSize.y);
    followBackgroundPixel(focus_);
}

// map.c: tickCameraFollowPlayer moves the camera by the player's whole projected movement every frame, with no easing
// or dead zone. It only does so while the d-pad moves the player, which here is the same as always, and while the
// tamer's state is 0: during a map change's walk-out the camera stays put, which the caller does by keeping the
// focus (tamer.c: checkMapInteraction calls unsetCameraFollowPlayer).
void MapCamera::follow(glm::vec3 worldFocus) {
    if (const auto point = projectToBackground(worldFocus)) {
        followBackgroundPixel(point->pixel);
    }
}

void MapCamera::followBackgroundPixel(glm::vec2 pixel) {
    focus_ = pixel;
    const glm::vec2 visible = visibleSize();
    scroll_ = {
        scrollAxis(pixel.x, visible.x, desc_.backgroundSize.x, scale_),
        scrollAxis(pixel.y, visible.y, desc_.backgroundSize.y, scale_),
    };
    updateProjection();
}

PixelRect MapCamera::backgroundRect() const {
    const glm::vec2 topLeft = glm::max(backgroundToWindow({0.0F, 0.0F}), glm::vec2{0.0F});
    const glm::vec2 bottomRight = glm::min(backgroundToWindow(desc_.backgroundSize), viewport_);
    return {.origin = topLeft, .size = glm::max(bottomRight - topLeft, glm::vec2{0.0F})};
}

void MapCamera::updateProjection() {
    // Background pixel x = H * x / d + centre.x, with d = -z the view depth; the visible window maps to NDC -1..1.
    // Vulkan's NDC y points down like the background's rows, and the original's view Y is the negated view y.
    const glm::vec2 visible = visibleSize();
    const glm::vec2 centre = desc_.backgroundSize * 0.5F;
    const float focal = desc_.focalLength;
    projection_ = glm::mat4{0.0F};
    projection_[0][0] = 2.0F * focal / visible.x;
    projection_[2][0] = 1.0F - (2.0F * (centre.x - scroll_.x) / visible.x);
    projection_[1][1] = -2.0F * focal / visible.y;
    projection_[2][1] = 1.0F - (2.0F * (centre.y - scroll_.y) / visible.y);
    projection_[2][2] = -MapFarClip / (MapFarClip - MapNearClip);
    projection_[3][2] = -MapFarClip * MapNearClip / (MapFarClip - MapNearClip);
    projection_[2][3] = -1.0F;
}

float MapCamera::viewDepth(glm::vec3 world) const {
    return -(view_ * glm::vec4{world, 1.0F}).z;
}

std::optional<BackgroundPoint> MapCamera::projectToBackground(glm::vec3 world) const {
    const glm::vec4 view = view_ * glm::vec4{world, 1.0F};
    const float depth = -view.z;
    if (depth <= 0.0F) {
        return std::nullopt;
    }
    const glm::vec2 originalView{view.x, -view.y};
    return BackgroundPoint{
        .pixel = (desc_.focalLength * originalView / depth) + (desc_.backgroundSize * 0.5F),
        .viewDepth = depth,
    };
}

glm::vec2 MapCamera::backgroundToWindow(glm::vec2 pixel) const {
    return (pixel - scroll_) * scale_;
}

glm::vec2 MapCamera::windowToBackground(glm::vec2 window) const {
    return (window / scale_) + scroll_;
}

std::optional<glm::vec2> MapCamera::worldToWindow(glm::vec3 world) const {
    const auto point = projectToBackground(world);
    if (!point) {
        return std::nullopt;
    }
    return backgroundToWindow(point->pixel);
}

std::optional<glm::vec3> MapCamera::pickGround(glm::vec2 window, float groundHeight) const {
    const glm::vec2 offset = (windowToBackground(window) - (desc_.backgroundSize * 0.5F)) / desc_.focalLength;
    const glm::vec3 viewDirection{offset.x, -offset.y, -1.0F};
    const glm::vec3 direction = glm::mat3{inverseView_} * viewDirection;
    if (std::abs(direction.y) < 1e-6F) {
        return std::nullopt;
    }
    const float distance = (groundHeight - desc_.eye.y) / direction.y;
    if (distance <= 0.0F) {
        return std::nullopt;
    }
    return desc_.eye + (direction * distance);
}

float MapCamera::depthBufferValue(float viewDepth) {
    return MapFarClip * (viewDepth - MapNearClip) / (viewDepth * (MapFarClip - MapNearClip));
}

glm::vec4 MapCamera::backgroundToClip(glm::vec2 pixel, float viewDepth) const {
    const glm::vec2 ndc = ((pixel - scroll_) / visibleSize() * 2.0F) - 1.0F;
    return {ndc, depthBufferValue(viewDepth), 1.0F};
}

MapCameraDesc mapCameraDesc(const OriginalMapCamera& camera) {
    // map.c: loadMapSetup applies the viewpoint and the reference shifted left by one.
    const auto doubled = [](glm::ivec3 value) { return core::fromOriginalAxes(glm::vec3{value * 2}); };
    return {
        .eye = doubled(camera.viewpoint),
        .target = doubled(camera.reference),
        .focalLength = static_cast<float>(camera.projection),
        .backgroundSize = glm::vec2{camera.tiles * TileSize},
    };
}

MapCameraDesc mapCameraDesc(const assets::MapDocument& map) {
    return mapCameraDesc(OriginalMapCamera{
        .viewpoint = map.camera.viewpoint,
        .reference = map.camera.reference,
        .projection = map.camera.projection,
        .tiles = {map.tiles.width, map.tiles.height},
    });
}

} // namespace dwe::render
