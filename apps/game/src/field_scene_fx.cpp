#include <dwe/assets/image_palettes.hpp>
#include <dwe/assets/indexed_image.hpp>
#include <dwe/assets/map_images.hpp>
#include <dwe/core/log.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <iterator>
#include <optional>
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
constexpr std::size_t FighterParticlePalette = 2;
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
  FieldEffectView view{
      .frame = {.view = camera.view(),
                .projection = camera.projection(),
                .pixelScale = camera.scale()},
      .focalLength = camera.desc().focalLength,
      .images = {.clouds = stpSceneImage(EffectSheet, CloudPalette),
                 .particles = stpSceneImage(EffectSheet, ParticlePalette),
                 .butterfly = stpSceneImage(ButterflySheet, ButterflyPalette),
                 .glow = sceneImage(GlowImage),
                 .fighterParticles = stpSceneImage(EffectSheet, FighterParticlePalette)},
      .butterflyCentre = std::nullopt,
      .battle = nullptr};
  if (fieldEffects_.hasButterfly() && partnerConditionPoint_ && !stage_.partnerHidden) {
    view.butterflyCentre =
        nodePoint(*partnerActor_, partnerConditionPoint_->first, partnerConditionPoint_->second);
  }
  const FieldBattleParticles battle = battleParticles();
  if (hitParticles_) {
    view.battle = &battle;
  }
  return fieldEffects_.draw(view);
}

// efe.c: the battle's sparks and clouds as the field's, and each fighter particle on its node of
// the fighter's model as it is posed now (bone->posMatrix.workm.t).
FieldBattleParticles FieldScene::battleParticles() const {
  FieldBattleParticles out;
  if (!hitParticles_) {
    return out;
  }
  using game::battle::HitParticles;
  // Smooth motion draws them between the last frame's and this one's (battle_glide.hpp).
  const HitParticles* last = hitParticlesFrom_ ? &*hitParticlesFrom_ : nullptr;
  const auto sparks = hitParticles_->sparkSlots();
  for (std::size_t slot = 0; slot < sparks.size(); ++slot) {
    const std::optional<HitParticles::SparkFx>& spark = sparks[slot];
    const std::optional<glm::ivec3> color =
        spark ? FieldEffects::sparkColor(spark->kind) : std::nullopt;
    if (!color) {
      continue;
    }
    out.sparks.push_back({.position = {spark->position[0], spark->position[1], spark->position[2]},
                          .mode = static_cast<std::int32_t>(spark->spark),
                          .color = *color,
                          .state = spark->state,
                          .timer = spark->timer,
                          .endFrame = spark->endFrame,
                          .spread = spark->spread,
                          .behind = sparkBehind(last, slot, *spark, visualsProgress_)});
  }
  for (const HitParticles::Cloud& cloud : hitParticles_->clouds()) {
    out.clouds.push_back({.x = cloud.x,
                          .z = cloud.z,
                          .state = cloud.state,
                          .behind = cloudBehind(last, cloud, visualsProgress_)});
  }
  for (const std::optional<HitParticles::FighterParticle>& particle :
       hitParticles_->fighterParticleSlots()) {
    if (!particle || particle->fighter >= statusBodies_.size()) {
      continue;
    }
    const Actor* actor = fighterActor(statusBodies_[particle->fighter].npc);
    const std::optional<glm::vec3> node =
        actor != nullptr
            ? nodePoint(*actor, static_cast<std::uint32_t>(particle->node), glm::vec3{0.0F})
            : std::nullopt;
    if (node) {
      out.motes.push_back({.position = *node, .look = particle->look, .timer = particle->timer});
    }
  }
  return out;
}

std::optional<glm::vec3> FieldScene::partnerConditionPoint() const {
  if (!partnerConditionPoint_ || !partnerActor_) {
    return std::nullopt;
  }
  return nodePoint(*partnerActor_, partnerConditionPoint_->first, partnerConditionPoint_->second);
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

std::optional<std::int32_t> FieldScene::screenNumber() const {
  const assets::MapEntry* entry = mapTable_ ? mapTable_->find(map_.document.id) : nullptr;
  if (entry == nullptr || !entry->originalIndex) {
    return std::nullopt;
  }
  return entry->index;
}

std::optional<rhi::TextureId> FieldScene::mistTexture(std::int32_t palette) {
  if (const auto found = mistTextures_.find(palette); found != mistTextures_.end()) {
    return found->second;
  }
  std::optional<rhi::TextureId> texture;
  if (auto image = mistImage(map_.images, palette)) {
    if (auto made = renderer_->queueTexture({.width = image->width, .height = image->height},
                                            image->pixels)) {
      texture = *made;
    } else {
      core::logWarning("field: no texture for the mist of {}: {}", map_.document.id,
                       made.error().message);
    }
  } else {
    // Palettes with no colour (the cleared mist's) and maps without the picture draw nothing.
    core::logDebug("field: map {} has no mist picture with palette {}", map_.document.id, palette);
  }
  mistTextures_.emplace(palette, texture);
  return texture;
}

void FieldScene::releaseMistTextures() {
  for (const auto& [palette, texture] : mistTextures_) {
    if (texture) {
      renderer_->destroyTexture(*texture);
    }
  }
  mistTextures_.clear();
}

void FieldScene::addMist(std::vector<ScenePrim>& prims, double frameSeconds, bool paused) {
  const render::MapCamera& camera = mapRenderer_->camera();
  const glm::vec2 scroll = camera.scroll();
  const glm::ivec2 cameraNow{static_cast<std::int32_t>(std::floor(scroll.x)),
                             static_cast<std::int32_t>(std::floor(scroll.y))};
  const bool drawn = mistShown_ && !mapLayerHidden_ && !stage_.sceneryHidden && !stage_.camera;
  // map.c: renderMap runs renderMist once a frame of the original, 20 a second, the pause
  // menu's included (PLAYTIME_FRAMES counts on under it); the field's own pause stops it.
  const double originalFrame = field_->rules().transition.frameSeconds;
  if (!paused && originalFrame > 0.0) {
    mistSeconds_ += frameSeconds;
    while (mistSeconds_ >= originalFrame) {
      mistSeconds_ -= originalFrame;
      if (drawn) {
        ++mistFrames_;
        mist_.frame(cameraNow, mistFrames_ % 2 == 0);
      } else {
        mist_.keepCamera(cameraNow);
      }
    }
  }
  if (!drawn) {
    return;
  }
  // Between two frames of the original the layers keep up with the camera, as they would on its
  // next frame: no shudder against the background.
  const glm::vec2 shift = glm::vec2{mist_.camera()} - scroll;
  const render::PixelRect inside = camera.backgroundRect();
  const glm::vec2 centre = camera.viewportSize() * 0.5F;
  const float scale = camera.scale();
  const glm::vec2 areaMin = (inside.origin - centre) / scale;
  const glm::vec2 areaMax = (inside.origin + inside.size - centre) / scale;
  const MistTextures textures{.back = mistTexture(mistShown_->back),
                              .front = mistTexture(mistShown_->front)};
  std::ranges::move(mistPrims(mist_, textures, shift, areaMin, areaMax), std::back_inserter(prims));
}

} // namespace dwe::app
