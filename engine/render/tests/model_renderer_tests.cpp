#include <dwe/render/model_renderer.hpp>

#include <doctest/doctest.h>

using dwe::assets::AlphaMode;
using dwe::assets::ModelMaterial;
using dwe::assets::SemiTransparency;
using dwe::render::materialBlend;
using dwe::rhi::BlendMode;

namespace {

ModelMaterial material(AlphaMode mode, SemiTransparency blending) {
  ModelMaterial out;
  out.alphaMode = mode;
  out.blending = blending;
  return out;
}

} // namespace

TEST_CASE("opaque and masked materials draw with the opaque pass") {
  for (const AlphaMode mode : {AlphaMode::Opaque, AlphaMode::Mask}) {
    // The extracted models write blending 0 on every material; only BLEND ones use it.
    const auto blend = materialBlend(material(mode, SemiTransparency::Add));
    CHECK(blend.mode == BlendMode::Opaque);
    CHECK_FALSE(blend.translucent);
    CHECK(blend.alpha == 1.0F);
  }
}

TEST_CASE("semi-transparent materials use the original's equations") {
  const auto average = materialBlend(material(AlphaMode::Blend, SemiTransparency::Average));
  CHECK(average.mode == BlendMode::Alpha);
  CHECK(average.alpha == 0.5F);
  CHECK(average.translucent);
  const auto add = materialBlend(material(AlphaMode::Blend, SemiTransparency::Add));
  CHECK(add.mode == BlendMode::Additive);
  CHECK(add.alpha == 1.0F);
  const auto subtract = materialBlend(material(AlphaMode::Blend, SemiTransparency::Subtract));
  CHECK(subtract.mode == BlendMode::Subtractive);
  CHECK(subtract.alpha == 1.0F);
  const auto quarter = materialBlend(material(AlphaMode::Blend, SemiTransparency::AddQuarter));
  CHECK(quarter.mode == BlendMode::Additive);
  CHECK(quarter.alpha == 0.25F);
}

TEST_CASE("the default lighting leaves a lit face near the texel's own color") {
  const auto lighting = dwe::render::defaultModelLighting();
  const float up =
      lighting.ambient.x + (lighting.lights[0].color.x * -lighting.lights[0].direction.y);
  CHECK(up > 0.9F);
  CHECK(up < 1.3F);
}
