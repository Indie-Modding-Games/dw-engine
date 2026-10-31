#include <dwe/assets/map_images.hpp>
#include <dwe/core/log.hpp>
#include <dwe/render/map_camera.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

#include "field_scene.hpp"

// What the technique effects draw as the original's own primitives rather than sprites or models
// (battle_effect.c: BTL_renderScrollingBackground, BTL_renderParallaxSprites,
// BTL_renderScreenSprite, BTL_renderScreenFade, BTL_renderScreenOverlay, BTL_renderEFELine,
// BTL_renderWireframeGrid, BTL_renderWireframeBox, BTL_renderRibbonStrip), as scene primitives.
namespace dwe::app {

namespace {

using game::battle::EffectBlend;
using game::battle::EffectRibbon;
using game::battle::EffectScreenLine;
using game::battle::EffectScreenQuad;
using game::battle::EffectVector;

constexpr std::size_t Channels = 4;
constexpr float ColorOne = 128.0F;
constexpr float FlatColorOne = 255.0F;
constexpr auto Opaque = std::byte{0xFF};
constexpr auto Clear = std::byte{0};
// The original's average mode (B/2 + F/2) and its quarter-added one (B + F/4).
constexpr float HalfAlpha = 0.5F;
constexpr float QuarterAlpha = 0.25F;

// BTL_RIBBON_UVS: a ribbon quad's texels, with the texture's third palette row (clut + 0x80).
constexpr std::array<glm::vec2, 4> RibbonTexels = {glm::vec2{104.0F, 0.0F}, glm::vec2{135.0F, 0.0F},
                                                   glm::vec2{104.0F, 31.0F},
                                                   glm::vec2{135.0F, 31.0F}};
constexpr std::int32_t RibbonClutRow = 2;

// The world's axes are the console's with y and z the other way (core::fromOriginalAxes).
glm::vec3 worldOf(const EffectVector& v) {
  return {static_cast<float>(v.x), static_cast<float>(-v.y), static_cast<float>(-v.z)};
}

// A primitive's colour bytes, 128 is 1.
glm::vec3 colorOf(const EffectVector& color) {
  return glm::vec3{static_cast<float>(color.x), static_cast<float>(color.y),
                   static_cast<float>(color.z)} /
         ColorOne;
}

// An untextured primitive's colour bytes, 255 is 1.
glm::vec3 flatColorOf(const EffectVector& color) {
  return glm::vec3{static_cast<float>(color.x), static_cast<float>(color.y),
                   static_cast<float>(color.z)} /
         FlatColorOne;
}

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
FieldScene::effectPrimTexture(const std::string& move, std::int32_t palette, TexelPart part,
                              const game::battle::EffectPalettes& palettes) {
  const game::battle::EffectPaletteRow* rewritten = rewrittenRow(palettes, palette);
  const auto key = std::tuple{move, palette, part,
                              rewritten != nullptr ? std::optional{*rewritten} : std::nullopt};
  if (const auto found = effectPrimTextures_.find(key); found != effectPrimTextures_.end()) {
    return found->second;
  }
  std::optional<PrimTexture> texture;
  auto image =
      effectSheetImage(*files_, std::format("effect/{}/texture", move), palette, rewritten);
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

// What the effects drew as the original's own primitives: the ribbons in the world, then the quads
// on the screen, back to front as the ordering table draws them (the far entries first and, within
// an entry, the quad added last first: AddPrim puts it at the head), each at its entry's view
// depth. A quad's opaque texels and its texels with STP are two primitives: the console mixes only
// the second.
void FieldScene::addEffectPrims(std::vector<ScenePrim>& prims) {
  // A textured quad of a move's texture from its four corners (top left, top right, bottom left,
  // bottom right) and texels.
  struct Quad {
    std::array<ScenePrimCorner, 4> corners{};
    std::array<glm::vec2, 4> texels{};
    glm::vec3 tint{1.0F};
    std::int32_t clutRow = 0;
    EffectBlend blend = EffectBlend::Opaque;
  };
  const auto add = [&](const SceneEffect& effect, const Quad& quad) {
    const auto part = [&](TexelPart texels, Mix mix) {
      const std::optional<PrimTexture> texture =
          effectPrimTexture(effect.move, quad.clutRow, texels, effect.palettes);
      if (!texture) {
        return;
      }
      ScenePrim prim{.shape = ScenePrim::Shape::Quad,
                     .corners = quad.corners,
                     .color = glm::vec4{quad.tint, mix.alpha},
                     .texture = texture->id,
                     .blend = mix.blend};
      for (std::size_t i = 0; i < prim.corners.size(); ++i) {
        prim.corners.at(i).uv = quad.texels.at(i) / texture->size;
      }
      prims.push_back(prim);
    };
    if (quad.blend == EffectBlend::Opaque) {
      part(TexelPart::All, {});
    } else {
      part(TexelPart::Solid, {});
      part(TexelPart::SemiTransparent, mixOf(quad.blend));
    }
  };
  const render::MapCamera& camera = mapRenderer_->camera();
  const glm::mat4 toClip = camera.projection() * camera.view();

  struct Entry {
    const EffectScreenQuad* quad = nullptr;
    const EffectScreenLine* line = nullptr;
    const SceneEffect* effect = nullptr;
    std::int32_t order = 0;
    std::size_t index = 0;
  };
  std::vector<Entry> quads;
  for (const SceneEffect& effect : effects_) {
    if (const auto* quad = std::get_if<EffectScreenQuad>(&effect.shape)) {
      quads.push_back(
          {.quad = quad, .effect = &effect, .order = quad->order, .index = quads.size()});
    } else if (const auto* line = std::get_if<EffectScreenLine>(&effect.shape)) {
      quads.push_back(
          {.line = line, .effect = &effect, .order = line->order, .index = quads.size()});
    } else if (const auto* ribbon = std::get_if<EffectRibbon>(&effect.shape)) {
      // battle_effect.c: BTL_renderRibbonStrip: a POLY_GT4 between each two pairs of points, its
      // top corners in half the colour and its bottom ones in the colour, black at the ribbon's
      // ends.
      const glm::vec4 full{colorOf(ribbon->color), 1.0F};
      const glm::vec4 half{
          colorOf(
              {.x = ribbon->color.x >> 1, .y = ribbon->color.y >> 1, .z = ribbon->color.z >> 1}),
          1.0F};
      const glm::vec4 black{0.0F, 0.0F, 0.0F, 1.0F};
      for (std::size_t i = 1; i < EffectRibbon::Points; ++i) {
        const bool first = i == 1;
        const bool last = i + 1 == EffectRibbon::Points;
        const std::array points{ribbon->top.at(i - 1), ribbon->top.at(i), ribbon->bottom.at(i - 1),
                                ribbon->bottom.at(i)};
        const std::array shades{first ? black : half, last ? black : half, first ? black : full,
                                last ? black : full};
        Quad strip{.texels = RibbonTexels, .clutRow = RibbonClutRow, .blend = EffectBlend::Add};
        for (std::size_t k = 0; k < strip.corners.size(); ++k) {
          strip.corners.at(k) = {.clip = toClip * glm::vec4{worldOf(points.at(k)), 1.0F},
                                 .shade = shades.at(k)};
        }
        add(effect, strip);
      }
    }
  }
  std::ranges::sort(quads, [](const Entry& a, const Entry& b) {
    return a.order != b.order ? a.order > b.order : a.index > b.index;
  });
  for (const Entry& entry : quads) {
    const float viewDepth =
        std::max(static_cast<float>(entry.order) * render::OrderingTableStep, render::MapNearClip);
    const glm::vec4 onAxis = camera.projection() * glm::vec4{0.0F, 0.0F, -viewDepth, 1.0F};
    // The screen's centre at that depth: the corners move from it in original pixels.
    const glm::vec4 centre{0.0F, 0.0F, onAxis.z / onAxis.w, 1.0F};
    if (entry.line != nullptr) {
      // A LINE_F2: one pixel wide, the renderer's white texture in the colour.
      const EffectScreenLine& line = *entry.line;
      const Mix mix = mixOf(line.blend);
      ScenePrim prim{.shape = ScenePrim::Shape::Line,
                     .color = glm::vec4{flatColorOf(line.color), mix.alpha},
                     .texture = renderer_->whiteTexture(),
                     .blend = mix.blend};
      prim.corners.at(0) = {
          .clip = centre,
          .offset = {static_cast<float>(line.from.x), static_cast<float>(line.from.y)}};
      prim.corners.at(1) = {
          .clip = centre, .offset = {static_cast<float>(line.to.x), static_cast<float>(line.to.y)}};
      prims.push_back(prim);
      continue;
    }
    const EffectScreenQuad& screen = *entry.quad;
    Quad quad{.tint = colorOf(screen.color), .clutRow = screen.clutRow, .blend = screen.blend};
    if (!screen.textured) {
      // A POLY_F4: the renderer's white texture in the colour, the whole quad mixed by its blend.
      const Mix mix = mixOf(screen.blend);
      ScenePrim prim{.shape = ScenePrim::Shape::Quad,
                     .color = glm::vec4{flatColorOf(screen.color), mix.alpha},
                     .texture = renderer_->whiteTexture(),
                     .blend = mix.blend};
      for (std::size_t i = 0; i < prim.corners.size(); ++i) {
        prim.corners.at(i) = {.clip = centre,
                              .offset = {static_cast<float>(screen.corners.at(i).x),
                                         static_cast<float>(screen.corners.at(i).y)}};
      }
      prims.push_back(prim);
      continue;
    }
    for (std::size_t i = 0; i < quad.corners.size(); ++i) {
      const bool right = (i & 1U) != 0;
      const bool bottom = (i & 2U) != 0;
      quad.corners.at(i) = {.clip = centre,
                            .offset = {static_cast<float>(screen.corners.at(i).x),
                                       static_cast<float>(screen.corners.at(i).y)}};
      quad.texels.at(i) = {static_cast<float>(screen.u + (right ? screen.texelWidth : 0)),
                           static_cast<float>(screen.v + (bottom ? screen.texelHeight : 0))};
    }
    add(*entry.effect, quad);
  }
}

} // namespace dwe::app
