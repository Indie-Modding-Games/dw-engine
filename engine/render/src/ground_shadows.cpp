#include <dwe/render/ground_shadows.hpp>

#include <cmath>

namespace dwe::render {

namespace {

// The sun's shadow at its darkest, against the round one's: lighter, so the two overlap softly at
// the feet.
constexpr float SunShadowShade = RoundShadowShade * 0.7F;
// Its width in radii, and how far back under the feet it starts.
constexpr float SunShadowWidth = 1.5F;
constexpr float SunShadowTuck = 0.5F;

} // namespace

MapBillboard roundShadow(const ShadowCaster& caster, const ShadowSpot& spot) {
  return {.centre = caster.feet,
          .size = glm::vec2{2.0F * caster.radius},
          .flat = true,
          .texture = spot.texture,
          .texel = spot.texel,
          .texels = spot.texels,
          .blend = assets::SpriteBlend::Subtract,
          .color = glm::vec3{RoundShadowShade}};
}

std::optional<MapBillboard> sunShadow(const ShadowCaster& caster, const SunShadow& sun,
                                      const ShadowSpot& spot) {
  if (sun.strength <= 0.0F || sun.length <= 0.0F || caster.radius <= 0.0F) {
    return std::nullopt;
  }
  const float length = sun.length * caster.radius;
  const float reach = (length * 0.5F) - (SunShadowTuck * caster.radius);
  const glm::vec3 direction{sun.direction.x, 0.0F, sun.direction.y};
  return MapBillboard{.centre = caster.feet + (direction * reach),
                      .size = {length, SunShadowWidth * caster.radius},
                      .flat = true,
                      .texture = spot.texture,
                      .texel = spot.texel,
                      .texels = spot.texels,
                      .blend = assets::SpriteBlend::Subtract,
                      .color = glm::vec3{SunShadowShade * sun.strength},
                      .turn = std::atan2(sun.direction.y, sun.direction.x)};
}

} // namespace dwe::render
