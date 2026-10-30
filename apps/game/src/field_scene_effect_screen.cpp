#include <dwe/assets/map_images.hpp>
#include <dwe/core/log.hpp>
#include <dwe/render/map_camera.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

#include "field_scene.hpp"

// What the technique effects draw on the screen rather than in the world (battle_effect.c:
// BTL_renderScrollingBackground, BTL_renderParallaxSprites), as screen primitives over the scene.
namespace dwe::app {

namespace {

using game::battle::EffectBlend;
using game::battle::EffectScreenQuad;

constexpr std::size_t Channels = 4;
constexpr float ColorOne = 128.0F;
constexpr auto Opaque = std::byte{0xFF};
constexpr auto Clear = std::byte{0};
// The original's average mode (B/2 + F/2) and its quarter-added one (B + F/4).
constexpr float HalfAlpha = 0.5F;
constexpr float QuarterAlpha = 0.25F;

rhi::Extent extentOf(const assets::Image& image) {
  return {.width = image.width, .height = image.height};
}

// The pipeline and alpha that mix a quad's texels with STP as its blend does.
struct Mix {
  ScenePrimBlend blend = ScenePrimBlend::Alpha;
  float alpha = 1.0F;
};

Mix mixOf(EffectBlend blend) {
  switch (blend) {
  case EffectBlend::Average:
    return {.blend = ScenePrimBlend::Alpha, .alpha = HalfAlpha};
  case EffectBlend::Add:
    return {.blend = ScenePrimBlend::Additive};
  case EffectBlend::Subtract:
    return {.blend = ScenePrimBlend::Subtractive};
  case EffectBlend::AddQuarter:
    return {.blend = ScenePrimBlend::Additive, .alpha = QuarterAlpha};
  case EffectBlend::Opaque:
    break;
  }
  return {};
}

} // namespace

std::optional<FieldScene::PrimTexture>
FieldScene::effectPrimTexture(const std::string& move, std::int32_t palette, TexelPart part) {
  const auto key = std::tuple{move, palette, part};
  if (const auto found = effectPrimTextures_.find(key); found != effectPrimTextures_.end()) {
    return found->second;
  }
  std::optional<PrimTexture> texture;
  auto image = effectSheetImage(*files_, std::format("effect/{}/texture", move), palette);
  if (image) {
    // Colours with STP come at SemiTransparentAlpha, the others opaque, colour 0 clear.
    for (std::size_t alpha = Channels - 1; alpha < image->pixels.size(); alpha += Channels) {
      std::byte& texel = image->pixels[alpha];
      const bool stp = texel == std::byte{assets::SemiTransparentAlpha};
      const bool solid = texel == Opaque;
      switch (part) {
      case TexelPart::All:
        texel = stp || solid ? Opaque : Clear;
        break;
      case TexelPart::SemiTransparent:
        texel = stp ? Opaque : Clear;
        break;
      case TexelPart::Solid:
        texel = solid ? Opaque : Clear;
        break;
      }
    }
    if (auto made = renderer_->queueTexture(extentOf(*image), image->pixels)) {
      texture = PrimTexture{
          .id = *made,
          .size = {static_cast<float>(image->width), static_cast<float>(image->height)}};
    } else {
      core::logWarning("battle: no screen texture for {}: {}", move, made.error().message);
    }
  } else {
    core::logWarning("battle: no effect texture of {} (palette {}): {}", move, palette,
                     image.error().message);
  }
  effectPrimTextures_.emplace(key, texture);
  return texture;
}

// The quads the effects drew on the screen, back to front as the ordering table draws them: the
// far entries first and, within an entry, the quad added last first (AddPrim puts it at the head).
// Each sorts with the models at its entry's view depth. A quad's opaque texels and its texels
// with STP are two primitives: the console mixes only the second.
void FieldScene::addEffectScreenPrims(std::vector<ScenePrim>& prims) {
  struct Entry {
    const EffectScreenQuad* quad = nullptr;
    const std::string* move = nullptr;
    std::size_t index = 0;
  };
  std::vector<Entry> quads;
  for (const SceneEffect& effect : effects_) {
    if (const auto* quad = std::get_if<EffectScreenQuad>(&effect.shape)) {
      quads.push_back({.quad = quad, .move = &effect.move, .index = quads.size()});
    }
  }
  if (quads.empty()) {
    return;
  }
  std::ranges::sort(quads, [](const Entry& a, const Entry& b) {
    return a.quad->order != b.quad->order ? a.quad->order > b.quad->order : a.index > b.index;
  });
  const render::MapCamera& camera = mapRenderer_->camera();
  for (const Entry& entry : quads) {
    const EffectScreenQuad& quad = *entry.quad;
    const float viewDepth =
        std::max(static_cast<float>(quad.order) * render::OrderingTableStep, render::MapNearClip);
    const glm::vec4 onAxis = camera.projection() * glm::vec4{0.0F, 0.0F, -viewDepth, 1.0F};
    // The screen's centre at that depth: the corners move from it in original pixels.
    const glm::vec4 centre{0.0F, 0.0F, onAxis.z / onAxis.w, 1.0F};
    const glm::vec3 tint =
        glm::vec3{static_cast<float>(quad.color.x), static_cast<float>(quad.color.y),
                  static_cast<float>(quad.color.z)} /
        ColorOne;
    const auto add = [&](TexelPart part, Mix mix) {
      const std::optional<PrimTexture> texture = effectPrimTexture(*entry.move, quad.clutRow, part);
      if (!texture) {
        return;
      }
      ScenePrim prim{.shape = ScenePrim::Shape::Quad,
                     .color = glm::vec4{tint, mix.alpha},
                     .texture = texture->id,
                     .blend = mix.blend};
      for (std::size_t i = 0; i < prim.corners.size(); ++i) {
        const bool right = (i & 1U) != 0;
        const bool bottom = (i & 2U) != 0;
        const glm::vec2 texel{static_cast<float>(quad.u + (right ? quad.texelWidth : 0)),
                              static_cast<float>(quad.v + (bottom ? quad.texelHeight : 0))};
        prim.corners.at(i) = {.clip = centre,
                              .offset = {static_cast<float>(quad.corners.at(i).x),
                                         static_cast<float>(quad.corners.at(i).y)},
                              .uv = texel / texture->size};
      }
      prims.push_back(prim);
    };
    if (quad.blend == EffectBlend::Opaque) {
      add(TexelPart::All, {});
    } else {
      add(TexelPart::Solid, {});
      add(TexelPart::SemiTransparent, mixOf(quad.blend));
    }
  }
}

} // namespace dwe::app
