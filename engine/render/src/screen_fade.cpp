#include <dwe/render/screen_fade.hpp>

#include <algorithm>
#include <utility>

namespace dwe::render {

glm::vec4 screenFadeColor(float darkness) {
  const float grey = std::clamp(darkness, 0.0F, 1.0F);
  return {grey, grey, grey, 1.0F};
}

core::Result<ScreenFade> ScreenFade::create(Renderer& renderer) {
  auto quads = QuadBatch::create(renderer, rhi::BlendMode::Subtractive);
  if (!quads) {
    return std::unexpected{quads.error()};
  }
  return ScreenFade{std::move(*quads), renderer.whiteTexture()};
}

void ScreenFade::setDarkness(float darkness) {
  darkness_ = std::clamp(darkness, 0.0F, 1.0F);
}

void ScreenFade::draw(DrawContext& context) {
  if (darkness_ <= 0.0F) {
    return;
  }
  // The quad follows the target, so it covers the window at any size.
  quads_.clear();
  quads_.add({.position = {0.0F, 0.0F},
              .size = {static_cast<float>(context.extent().width),
                       static_cast<float>(context.extent().height)},
              .uvMin = {0.0F, 0.0F},
              .uvMax = {1.0F, 1.0F},
              .color = screenFadeColor(darkness_),
              .texture = white_,
              .sampler = rhi::SamplerKind::NearestClamp});
  quads_.draw(context);
}

} // namespace dwe::render
