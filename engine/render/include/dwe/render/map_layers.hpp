#pragma once

#include <dwe/assets/map_document.hpp>
#include <dwe/assets/map_images.hpp>
#include <dwe/core/math.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace dwe::render {

class MapCamera;

// A depth-layer sprite to draw this frame: a screen-aligned rectangle of the background at a fixed
// view depth.
struct DepthLayerQuad {
  std::size_t instance = 0;
  std::size_t sprite = 0;
  // Top-left corner and size, in background pixels.
  glm::vec2 position{0.0F};
  glm::vec2 size{0.0F};
  // The plane it occludes from: everything at this view depth or farther is behind it.
  float viewDepth = 0.0F;
  assets::SpriteBlend blend = assets::SpriteBlend::Opaque;
};

// The animated sprite instances of a map with their place in the original's ordering table
// (overworld.c: calcMapObjectOrder, renderMapOverlays).
class MapDepthLayers {
public:
  // The original's field runs at 20 frames per second (main.c: gameLoop waits VSync(3)); sprite
  // animations count those frames.
  static constexpr double FramesPerSecond = 20.0;

  MapDepthLayers() = default;

  // Computes every instance's ordering-table entry with the map's camera, and the order they are
  // drawn in.
  MapDepthLayers(const assets::MapObjects& objects, const MapCamera& camera);

  // Advances the animations by real time.
  void update(double seconds);

  // Advances the animations by one original frame.
  void tick();

  // The instances to draw now, from far to near; in one ordering-table entry the instance added
  // last goes first, as the original's AddPrim prepends. Hidden, blank and screen-space (snow)
  // instances are left out. The order is sorted once, so this only refreshes a buffer the layers
  // keep: the reference is valid until the next call.
  [[nodiscard]] const std::vector<DepthLayerQuad>& quads();

  // An instance's ordering-table entry (assets::MapOrder): Nearest-Farthest, BehindEverything or
  // ScreenSpace.
  [[nodiscard]] std::int32_t orderValue(std::size_t instance) const;

  [[nodiscard]] std::size_t instanceCount() const { return instances_.size(); }

  // Hides or shows instances, as scripts and the day/night objects do
  // (MapObjectInstance::HiddenFlag).
  void setHidden(std::size_t instance, bool hidden);

  // An instance's place in background pixels, nothing past the last; and moving it, as scripts do
  // (overworld.c: tickMoveObjectTo).
  [[nodiscard]] std::optional<glm::vec2> position(std::size_t instance) const;
  void setPosition(std::size_t instance, glm::vec2 position);

private:
  struct Instance {
    std::vector<assets::MapSpriteFrame> frames;
    glm::vec2 position{0.0F};
    std::int32_t orderValue = 0;
    bool hidden = false;
    bool screenSpace = false;
    std::size_t frame = 0;
    std::int32_t timer = 0;
  };

  std::vector<assets::MapSprite> sprites_;
  std::vector<Instance> instances_;
  // Instance indices from far to near.
  std::vector<std::size_t> drawOrder_;
  std::vector<DepthLayerQuad> quads_;
  double pendingSeconds_ = 0.0;
};

// The ordering-table entry of a sprite's depth point seen by a map camera
// (assets::mapSpriteOrderValue).
std::int32_t mapSpriteOrderValue(const assets::MapSprite& sprite, const MapCamera& camera);

} // namespace dwe::render
