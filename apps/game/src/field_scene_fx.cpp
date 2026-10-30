#include <dwe/assets/image_palettes.hpp>
#include <dwe/assets/indexed_image.hpp>
#include <dwe/assets/map_images.hpp>
#include <dwe/core/log.hpp>

#include <algorithm>
#include <format>
#include <string>
#include <vector>

#include "field_scene.hpp"

namespace dwe::app {

namespace {

// ETCDAT/ETCTIM.BIN entries 2 and 3 and ETCDAT/SPOT256.TIM (field_effects.hpp).
constexpr std::string_view EffectSheet = "texture/etcdat/etctim/002";
constexpr std::string_view ButterflySheet = "texture/etcdat/etctim/003";
constexpr std::size_t CloudPalette = 0;
constexpr std::size_t ParticlePalette = 1;
constexpr std::size_t ButterflyPalette = 2;
constexpr std::string_view GlowImage = "texture/etcdat/spot256";
// overworld.c: renderNinjamonEffect puts the smoke's objects in ordering-table entry 10.
constexpr std::int32_t SmokeOrderValue = 10;

core::Result<assets::Image> stpImage(const assets::VirtualFileSystem& files, std::string_view asset,
                                     std::size_t palette) {
  auto id = assets::AssetId::parse(asset);
  if (!id) {
    return std::unexpected{id.error()};
  }
  auto png = files.read(*id);
  if (!png) {
    return std::unexpected{png.error()};
  }
  auto indexed = assets::decodeIndexedPng(*png);
  if (!indexed) {
    return std::unexpected{indexed.error()};
  }
  auto metaId = assets::AssetId::parse(std::string{asset} + std::string{assets::ImageMetaSuffix});
  if (!metaId) {
    return std::unexpected{metaId.error()};
  }
  auto meta = files.read(*metaId);
  if (!meta) {
    return std::unexpected{meta.error()};
  }
  auto palettes = assets::parseImagePalettes(
      std::string_view{reinterpret_cast<const char*>(meta->data()), meta->size()});
  if (!palettes) {
    return std::unexpected{palettes.error()};
  }
  if (palette >= palettes->size()) {
    return core::fail(std::format("{} has {} palettes", asset, palettes->size()));
  }
  // colorFromOriginal keeps the STP bit as alpha 128.
  std::vector<assets::PaletteColor> colors;
  for (const std::uint16_t color : palettes->at(palette)) {
    colors.push_back(assets::colorFromOriginal(color));
  }
  return assets::applyPalette(*indexed, colors);
}

} // namespace

std::optional<rhi::TextureId> FieldScene::stpSceneImage(std::string_view asset,
                                                        std::size_t palette) {
  const std::string key = std::format("{}#stp{}", asset, palette);
  if (const auto found = sceneImages_.find(key); found != sceneImages_.end()) {
    return found->second;
  }
  std::optional<rhi::TextureId> texture;
  if (auto image = stpImage(*files_, asset, palette)) {
    if (auto made = renderer_->queueTexture({.width = image->width, .height = image->height},
                                            image->pixels)) {
      texture = *made;
    } else {
      core::logWarning("field: no texture for {}: {}", key, made.error().message);
    }
  } else {
    core::logWarning("field: no image {}: {}", key, image.error().message);
  }
  sceneImages_.emplace(key, texture);
  return texture;
}

std::vector<ScenePrim> FieldScene::drawEffects() {
  const render::MapCamera& camera = mapRenderer_->camera();
  FieldEffectView view{.frame = {.view = camera.view(),
                                 .projection = camera.projection(),
                                 .pixelScale = camera.scale()},
                       .focalLength = camera.desc().focalLength,
                       .images = {.clouds = stpSceneImage(EffectSheet, CloudPalette),
                                  .particles = stpSceneImage(EffectSheet, ParticlePalette),
                                  .butterfly = stpSceneImage(ButterflySheet, ButterflyPalette),
                                  .glow = sceneImage(GlowImage)},
                       .butterflyCentre = std::nullopt};
  if (fieldEffects_.hasButterfly() && partnerConditionPoint_ && !stage_.partnerHidden) {
    view.butterflyCentre =
        nodePoint(*partnerActor_, partnerConditionPoint_->first, partnerConditionPoint_->second);
  }
  return fieldEffects_.draw(view);
}

void FieldScene::startObjectSmoke() {
  const render::MapDepthLayers& layers = mapLayers();
  std::vector<glm::ivec2> places;
  for (std::size_t i = 0; i < std::min(ObjectSmoke::Objects, layers.instanceCount()); ++i) {
    places.emplace_back(*layers.position(i));
  }
  objectSmoke_.start(places);
  objectSmokeSeconds_ = 0.0;
}

void FieldScene::advanceObjectSmoke(double seconds) {
  if (!objectSmoke_.active()) {
    return;
  }
  render::MapDepthLayers& layers = mapLayers();
  const std::size_t count = std::min(ObjectSmoke::Objects, layers.instanceCount());
  const double frameSeconds = field_->rules().transition.frameSeconds;
  objectSmokeSeconds_ += seconds;
  while (objectSmoke_.active() && objectSmokeSeconds_ >= frameSeconds) {
    objectSmokeSeconds_ -= frameSeconds;
    std::vector<glm::ivec2> places;
    for (std::size_t i = 0; i < count; ++i) {
      places.emplace_back(*layers.position(i));
    }
    const bool done = objectSmoke_.frame(places);
    for (std::size_t i = 0; i < count; ++i) {
      layers.setPosition(i, glm::vec2{places[i]});
      layers.setOrderValue(i, SmokeOrderValue);
      if (done) {
        layers.setHidden(i, true);
      }
    }
  }
}

} // namespace dwe::app
