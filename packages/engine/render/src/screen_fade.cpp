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
  auto whiteQuads = QuadBatch::create(renderer, rhi::BlendMode::Additive);
  if (!whiteQuads) {
    return std::unexpected{whiteQuads.error()};
  }
  return ScreenFade{std::move(*quads), std::move(*whiteQuads), renderer.whiteTexture()};
}

void ScreenFade::setDarkness(float darkness) {
  darkness_ = std::clamp(darkness, 0.0F, 1.0F);
}

void ScreenFade::setWhiteness(float whiteness) {
  whiteness_ = std::clamp(whiteness, 0.0F, 1.0F);
}

void ScreenFade::cover(QuadBatch& quads, DrawContext& context, float amount, TextureId white) {
  // The quad follows the target, so it covers the window at any size.
  quads.clear();
  quads.add({.position = {0.0F, 0.0F},
             .size = {static_cast<float>(context.extent().width),
                      static_cast<float>(context.extent().height)},
             .uvMin = {0.0F, 0.0F},
             .uvMax = {1.0F, 1.0F},
             .color = screenFadeColor(amount),
             .texture = white,
             .sampler = rhi::SamplerKind::NearestClamp});
  quads.draw(context);
}

void ScreenFade::draw(DrawContext& context) {
  if (whiteness_ > 0.0F) {
    cover(whiteQuads_, context, whiteness_, white_);
  }
  if (darkness_ > 0.0F) {
    cover(quads_, context, darkness_, white_);
  }
}

} // namespace dwe::render
