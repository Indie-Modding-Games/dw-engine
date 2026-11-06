#pragma once

#include <dwe/assets/map_document.hpp>
#include <dwe/core/math.hpp>

#include <optional>

namespace dwe::render {

// The screen the original frames a map for, in pixels (main.c: GsInitGraph(320, 240)): a viewport
// with this aspect ratio shows exactly what it showed.
inline constexpr glm::vec2 OriginalScreenSize{320.0F, 240.0F};

// The original's near clip, in world units (main.c: view_init sets GsSetNearClip(100)).
inline constexpr float MapNearClip = 100.0F;

// The far end of the original's ordering table: 4096 entries of 16 units of view depth each (see
// mapSpriteDepth).
inline constexpr float MapFarClip = 65536.0F;

// Units of view depth per entry of the original's ordering table: models are sorted by their
// average screen Z >> 4.
inline constexpr float OrderingTableStep = 16.0F;

// The fixed camera of a field map, in world space (docs/architecture/world-space.md).
struct MapCameraDesc {
  glm::vec3 eye{0.0F};
  glm::vec3 target{0.0F, 0.0F, -1.0F};
  // The original's projection distance H: background pixels per unit of x / z in view space.
  float focalLength = 1.0F;
  // The size of the whole prerendered background, in pixels.
  glm::vec2 backgroundSize = OriginalScreenSize;
};

// A rectangle in pixels, from its top-left corner.
struct PixelRect {
  glm::vec2 origin{0.0F};
  glm::vec2 size{0.0F};
};

// A point projected by a map camera: where it falls on the background and how far it is from the
// camera.
struct BackgroundPoint {
  glm::vec2 pixel{0.0F};
  float viewDepth = 0.0F;
};

// The camera of a field map for a viewport of any size (ADR 0020). The view is fixed; the
// projection is an off-axis perspective whose optical centre is the centre of the background, so a
// world point lands on the same background pixel at any viewport size. The viewport shows a window
// of the background that follows a focus point and stops at the background's edges. Window
// coordinates are pixels from the viewport's top-left corner, y down.
class MapCamera {
public:
  explicit MapCamera(const MapCameraDesc& desc);
  // A camera for a viewport already known: a new map keeps the window's size, which the renderer
  // only learns again when it next prepares the map (not while the scenery is hidden).
  MapCamera(const MapCameraDesc& desc, glm::vec2 viewport);

  // Sets the viewport size in pixels and keeps the current focus. Sizes below one pixel count as
  // one.
  void setViewport(glm::vec2 size);

  // Centres the visible window on a world point, clamped to the background like the original's
  // cameraX/cameraY.
  void follow(glm::vec3 worldFocus);

  // Centres the visible window on a background pixel, clamped the same way.
  void followBackgroundPixel(glm::vec2 pixel);

  // Moves everything drawn by `offset` background pixels after the clamp, as a change of the
  // original's drawing offset does (map.c: tickMeramonShake moves DRAWING_OFFSET_X); zero puts it
  // back. It takes effect at the next follow.
  void setShake(glm::vec2 offset) { shake_ = offset; }

  [[nodiscard]] const MapCameraDesc& desc() const { return desc_; }
  [[nodiscard]] glm::vec2 viewportSize() const { return viewport_; }

  // Viewport pixels per background pixel: min(width / 320, height / 240).
  [[nodiscard]] float scale() const { return scale_; }

  // How many background pixels the viewport shows, at least 320 x 240.
  [[nodiscard]] glm::vec2 visibleSize() const { return viewport_ / scale_; }

  // The background pixel at the viewport's top-left corner. Negative or past the edge when the
  // background is smaller than the visible window on that side (it is then centred, with bars).
  [[nodiscard]] glm::vec2 scroll() const { return scroll_; }

  // The part of the viewport the background covers, in window pixels: the rest are bars. Clip the
  // scene to it.
  [[nodiscard]] PixelRect backgroundRect() const;

  // World to view (right-handed, looking down -Z).
  [[nodiscard]] const glm::mat4& view() const { return view_; }

  // View to Vulkan clip space for the current viewport and scroll, depth 0 at MapNearClip and 1 at
  // MapFarClip.
  [[nodiscard]] const glm::mat4& projection() const { return projection_; }

  [[nodiscard]] glm::mat4 viewProjection() const { return projection_ * view_; }

  // Distance from the camera along its axis (the original's SZ) of a world point.
  [[nodiscard]] float viewDepth(glm::vec3 world) const;

  // The background pixel a world point projects to, as the original computes it; nothing for points
  // behind the camera.
  [[nodiscard]] std::optional<BackgroundPoint> projectToBackground(glm::vec3 world) const;

  // Background pixels to window pixels and back, with the current scroll.
  [[nodiscard]] glm::vec2 backgroundToWindow(glm::vec2 pixel) const;
  [[nodiscard]] glm::vec2 windowToBackground(glm::vec2 window) const;

  // Where a world point appears in the viewport, in window pixels; nothing for points behind the
  // camera.
  [[nodiscard]] std::optional<glm::vec2> worldToWindow(glm::vec3 world) const;

  // The world point under a window pixel on the horizontal plane y = groundHeight (click-to-move);
  // nothing when that ray does not meet the plane in front of the camera.
  [[nodiscard]] std::optional<glm::vec3> pickGround(glm::vec2 window,
                                                    float groundHeight = 0.0F) const;

  // The depth buffer value of a point at this view depth, matching projection().
  [[nodiscard]] static float depthBufferValue(float viewDepth);

  // A background pixel at a view depth, in Vulkan clip space: how screen-aligned quads (the
  // background and the depth layers) are placed so they line up with the 3D scene.
  [[nodiscard]] glm::vec4 backgroundToClip(glm::vec2 pixel, float viewDepth) const;

private:
  void updateProjection();

  MapCameraDesc desc_;
  glm::mat4 view_{1.0F};
  glm::mat4 inverseView_{1.0F};
  glm::mat4 projection_{1.0F};
  glm::vec2 viewport_ = OriginalScreenSize;
  float scale_ = 1.0F;
  glm::vec2 focus_{0.0F};
  glm::vec2 scroll_{0.0F};
  glm::vec2 shake_{0.0F};
};

// The raw camera of a map document as the disc stores it: the original's axes, and viewpoint and
// reference before the original doubles them.
struct OriginalMapCamera {
  glm::ivec3 viewpoint{0};
  glm::ivec3 reference{0};
  int projection = 1;
  glm::ivec2 tiles{0}; // background size in tiles of TileSize pixels
};

// The size of a background tile, in pixels.
inline constexpr int TileSize = 128;

// Converts a map's raw camera to world space: map.c: loadMapSetup doubles the viewpoint and the
// reference (<< 1) and passes the projection distance as it is.
MapCameraDesc mapCameraDesc(const OriginalMapCamera& camera);

// The camera of a map document.
MapCameraDesc mapCameraDesc(const assets::MapDocument& map);

// The view depth of the plane a depth-layer sprite occludes from, given its ordering-table entry.
constexpr float mapSpriteDepth(int orderValue) {
  return static_cast<float>(orderValue) * OrderingTableStep;
}

} // namespace dwe::render
