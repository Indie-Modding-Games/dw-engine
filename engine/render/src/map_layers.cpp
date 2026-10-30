#include <dwe/render/map_camera.hpp>
#include <dwe/render/map_layers.hpp>

#include <algorithm>
#include <numeric>
#include <utility>

namespace dwe::render {

static_assert(OrderingTableStep == assets::MapOrderingTableStep);

std::int32_t mapSpriteOrderValue(const assets::MapSprite& sprite, const MapCamera& camera) {
  return assets::mapSpriteOrderValue(
      sprite, camera.viewDepth(core::fromOriginalAxes(glm::vec3{sprite.position})));
}

MapDepthLayers::MapDepthLayers(const assets::MapObjects& objects, const MapCamera& camera)
    : sprites_(objects.sprites) {
  instances_.reserve(objects.instances.size());
  for (const assets::MapObjectInstance& source : objects.instances) {
    Instance instance{
        .frames = source.frames,
        .position = {static_cast<float>(source.x), static_cast<float>(source.y)},
        .orderValue = assets::MapOrder::Farthest,
        .hidden = source.flag == assets::MapObjectInstance::HiddenFlag,
        .screenSpace = (source.flag & assets::MapObjectInstance::ScreenSpaceFlag) != 0,
        .frame = 0,
        .timer = 0,
    };
    // The depth comes from the first sprite of the animation that is not blank.
    const auto first =
        std::ranges::find_if(instance.frames, [this](const assets::MapSpriteFrame& frame) {
          return frame.sprite >= 0 && std::cmp_less(frame.sprite, sprites_.size());
        });
    if (first != instance.frames.end()) {
      instance.orderValue =
          mapSpriteOrderValue(sprites_[static_cast<std::size_t>(first->sprite)], camera);
    }
    if (instance.screenSpace) {
      instance.orderValue = assets::MapOrder::ScreenSpace;
    }
    instances_.push_back(std::move(instance));
  }
  // Far to near. In one ordering-table entry the original draws the instance added last first
  // (AddPrim prepends).
  drawOrder_.resize(instances_.size());
  std::ranges::iota(drawOrder_, std::size_t{0});
  std::ranges::sort(drawOrder_, [this](std::size_t lhs, std::size_t rhs) {
    if (instances_[lhs].orderValue != instances_[rhs].orderValue) {
      return instances_[lhs].orderValue > instances_[rhs].orderValue;
    }
    return lhs > rhs;
  });
  quads_.reserve(instances_.size());
}

void MapDepthLayers::update(double seconds) {
  pendingSeconds_ += seconds;
  constexpr double FrameSeconds = 1.0 / FramesPerSecond;
  while (pendingSeconds_ >= FrameSeconds) {
    pendingSeconds_ -= FrameSeconds;
    tick();
  }
}

void MapDepthLayers::tick() {
  for (Instance& instance : instances_) {
    if (instance.frames.empty()) {
      continue;
    }
    // renderMapOverlays counts frames for hidden instances too, and only moves on when the timer
    // reaches the frame's time exactly.
    ++instance.timer;
    if (instance.timer == instance.frames[instance.frame].time) {
      instance.timer = 0;
      instance.frame = (instance.frame + 1) % instance.frames.size();
    }
  }
}

const std::vector<DepthLayerQuad>& MapDepthLayers::quads() {
  quads_.clear();
  for (const std::size_t index : drawOrder_) {
    const Instance& instance = instances_[index];
    if (instance.hidden || instance.screenSpace || instance.frames.empty()) {
      continue;
    }
    const std::int32_t sprite = instance.frames[instance.frame].sprite;
    if (sprite < 0 || std::cmp_greater_equal(sprite, sprites_.size())) {
      continue;
    }
    const assets::MapSprite& definition = sprites_[static_cast<std::size_t>(sprite)];
    quads_.push_back({
        .instance = index,
        .sprite = static_cast<std::size_t>(sprite),
        .position = instance.position + instance.offset,
        .size = {static_cast<float>(definition.width), static_cast<float>(definition.height)},
        .viewDepth = mapSpriteDepth(instance.orderValue),
        .blend = assets::spriteBlend(definition.transparency),
    });
  }
  return quads_;
}

std::int32_t MapDepthLayers::orderValue(std::size_t instance) const {
  return instance < instances_.size() ? instances_[instance].orderValue
                                      : assets::MapOrder::Farthest;
}

void MapDepthLayers::setHidden(std::size_t instance, bool hidden) {
  if (instance < instances_.size()) {
    instances_[instance].hidden = hidden;
  }
}

std::optional<glm::vec2> MapDepthLayers::position(std::size_t instance) const {
  if (instance < instances_.size()) {
    return instances_[instance].position;
  }
  return std::nullopt;
}

std::optional<glm::vec2> MapDepthLayers::spriteSize(std::size_t instance) const {
  if (instance >= instances_.size()) {
    return std::nullopt;
  }
  for (const assets::MapSpriteFrame& frame : instances_[instance].frames) {
    if (frame.sprite != assets::MapSpriteFrame::BlankSprite && frame.sprite >= 0 &&
        static_cast<std::size_t>(frame.sprite) < sprites_.size()) {
      const assets::MapSprite& sprite = sprites_[static_cast<std::size_t>(frame.sprite)];
      return glm::vec2{static_cast<float>(sprite.width), static_cast<float>(sprite.height)};
    }
  }
  return std::nullopt;
}

void MapDepthLayers::setPosition(std::size_t instance, glm::vec2 position) {
  if (instance < instances_.size()) {
    instances_[instance].position = position;
  }
}

void MapDepthLayers::setOffset(std::size_t instance, glm::vec2 offset) {
  if (instance < instances_.size()) {
    instances_[instance].offset = offset;
  }
}

glm::vec2 MapDepthLayers::offset(std::size_t instance) const {
  return instance < instances_.size() ? instances_[instance].offset : glm::vec2{0.0F};
}

} // namespace dwe::render
