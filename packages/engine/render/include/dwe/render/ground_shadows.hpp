#pragma once

#include <dwe/core/math.hpp>
#include <dwe/render/map_renderer.hpp>
#include <dwe/render/scene_light.hpp>

#include <cstddef>
#include <optional>

namespace dwe::render {

// A character standing on the field, for its shadows: the point under it and its species' radius
// (the original's species table).
struct ShadowCaster {
  glm::vec3 feet{0.0F};
  float radius = 0.0F;
};

// The round spot the shadows are drawn with: a billboard texture (MapRenderer::addBillboardTexture,
// ETCDAT/SPOT256.TIM) and its rectangle in texels.
struct ShadowSpot {
  std::size_t texture = 0;
  glm::vec2 texel{0.0F};
  glm::vec2 texels{0.0F};
};

// How dark the original's shadow is: drop_shadow.c: renderDropShadow subtracts the spot with the
// colour 0x30 over 0x80.
inline constexpr float RoundShadowShade = 0x30 / 128.0F;

// The original's shadow under a character (drop_shadow.c: renderDropShadow): a square of twice the
// radius on each side, flat at the feet's height and centred under them, the spot subtracted.
MapBillboard roundShadow(const ShadowCaster& caster, const ShadowSpot& spot);

// An improvement over it (the option «Sombras», sun): a softer shadow stretched from the feet the
// way the sun casts it (sunAt), as wide as the character and as long as the sun says; nothing
// when the sun casts none. Drawn with the round one, which stays under the feet.
std::optional<MapBillboard> sunShadow(const ShadowCaster& caster, const SunShadow& sun,
                                      const ShadowSpot& spot);

} // namespace dwe::render
