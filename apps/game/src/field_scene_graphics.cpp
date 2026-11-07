#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/map_document.hpp>
#include <dwe/core/log.hpp>
#include <dwe/render/ground_shadows.hpp>
#include <dwe/render/scene_grade.hpp>
#include <dwe/render/scene_light.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string_view>
#include <vector>

#include "field_scene.hpp"

// The field's enhanced graphics (docs/architecture/render.md, «Paso de luz y color»): the
// characters' shadows, the time of day's light and the vivid colours. None of it reads or changes
// a rule of the game, the collision or the random numbers.
namespace dwe::app {

namespace {

// drop_shadow.c: renderDropShadow: ETCDAT/SPOT256.TIM, uv 0x40, 0x80, 63 x 63 (the image the
// ground items' shadow uses, field_scene_ground_items.cpp).
constexpr glm::vec2 ShadowTexel{64.0F, 0.0F};
constexpr glm::vec2 ShadowTexels{63.0F, 63.0F};

// map_collision.c: getTileTrigger gives -1 on a cell of raw value 0x80 or 0xFF (a byte read as
// signed), and renderDropShadow draws no shadow there.
bool castsShadowOn(const assets::MapCollision& collision, glm::vec3 feet) {
  const std::uint8_t raw = collision.value(assets::originalCellAt(feet));
  return raw != 0x80 && raw != 0xFF;
}

// The vivid colours' grade: muted colours gain the most, the contrast stays gentle and only the
// brightest spots (lamps, sky, snow) glow.
constexpr float VividVibrance = 0.3F;
constexpr float VividContrast = 0.12F;
constexpr float VividBloom = 0.22F;

} // namespace

void FieldScene::setGraphics(const GraphicsOptions& options) {
  graphics_ = options;
  mapRenderer_->setSceneryQuality(options.sceneryQuality == SceneryQuality::Smooth
                                      ? render::MapRenderer::SceneryQuality::Smooth
                                      : render::MapRenderer::SceneryQuality::Upscaled);
  // The fighters and the techniques' models of a battle are the field's too.
  applyTextureFilter(*models_, options);
}

render::MapLightTable loadMapLights(const assets::VirtualFileSystem& files) {
  constexpr std::string_view Id = "field/lights";
  auto id = assets::AssetId::parse(Id);
  if (!id || files.resolve(*id) == nullptr) {
    core::logWarning("field: no {}: the maps have no point lights", Id);
    return {};
  }
  auto bytes = files.read(*id);
  if (!bytes) {
    core::logWarning("field: {}: {}", Id, bytes.error().message);
    return {};
  }
  auto table = render::parseMapLights(
      std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()});
  if (!table) {
    core::logWarning("field: {}", table.error().message);
    return {};
  }
  return std::move(*table);
}

render::Drawer* FieldScene::postDrawer() {
  const bool wanted = graphics_.enhancedLighting || graphics_.vividColors;
  const bool mapShown =
      mapRenderer_->hasMap() && !mapLayerHidden_ && !(stage_.sceneryHidden && !stage_.view);
  return wanted && mapShown ? grade_.get() : nullptr;
}

void FieldScene::drawShadows(float alpha) {
  std::vector<render::MapBillboard> shadows;
  const std::optional<std::size_t> texture =
      graphics_.shadows != ShadowMode::Off && !stage_.sceneryHidden && !mapLayerHidden_
          ? groundItemTexture(std::nullopt)
          : std::nullopt;
  if (!texture) {
    mapRenderer_->setGroundBillboards(shadows);
    return;
  }
  const render::ShadowSpot spot{.texture = *texture, .texel = ShadowTexel, .texels = ShadowTexels};
  const float hour =
      static_cast<float>(clock_->time().hour) + (static_cast<float>(clock_->time().minute) / 60.0F);
  const render::SunShadow sun = graphics_.shadows == ShadowMode::Sun
                                    ? render::sunAt(hour, schedule_, outdoor_)
                                    : render::SunShadow{};
  // The sun's shadows first, so the round ones under the feet go over them.
  std::vector<render::ShadowCaster> casters;
  const auto cast = [&](scene::Entity entity, float radius) {
    if (!world_.alive(entity) || radius <= 0.0F) {
      return;
    }
    const glm::vec3 feet = field_->drawnPose(entity, alpha).position;
    if (castsShadowOn(field_->collision(), feet)) {
      casters.push_back({.feet = feet, .radius = radius});
    }
  };
  // model.c draws one under every character it draws: the player (PLAYER_SHADOW_ENABLED,
  // set but in the ending), the partner and the NPCs, each the size of its species.
  if (stage_.playerFade > 0.0F) {
    cast(field_->playerEntity(), field_->rules().player.radius);
  }
  if (!stage_.partnerHidden) {
    cast(field_->partnerEntity(), field_->rules().partner.mover.radius);
  }
  std::map<scene::Entity, float> npcRadii;
  for (const game::field::NpcBody& body : field_->npcs().bodies()) {
    npcRadii.emplace(body.entity, body.half);
  }
  for (const std::unique_ptr<Actor>& actor : npcActors_) {
    if (std::ranges::contains(hiddenNpcs_, actor->entity)) {
      continue;
    }
    const auto radius = npcRadii.find(actor->entity);
    cast(actor->entity, radius != npcRadii.end() ? radius->second : field_->rules().player.radius);
  }
  for (const render::ShadowCaster& caster : casters) {
    if (auto stretched = render::sunShadow(caster, sun, spot)) {
      shadows.push_back(*stretched);
    }
  }
  for (const render::ShadowCaster& caster : casters) {
    shadows.push_back(render::roundShadow(caster, spot));
  }
  mapRenderer_->setGroundBillboards(shadows);
}

void FieldScene::gradeScene(double frameSeconds) {
  lightSeconds_ += frameSeconds;
  render::SceneGradeParams params;
  shownLights_.clear();
  if (graphics_.enhancedLighting && mapRenderer_->hasMap()) {
    const float hour = static_cast<float>(clock_->time().hour) +
                       (static_cast<float>(clock_->time().minute) / 60.0F);
    const render::DaylightGrade daylight = render::daylightAt(hour, schedule_, outdoor_);
    params.tint = daylight.tint;
    const auto lights = mapLights_.of(map_.document.id);
    for (std::size_t index = 0; index < lights.size(); ++index) {
      const render::MapLight& light = lights[index];
      constexpr float SeedStep = 1.7F;
      shownLights_.push_back(
          {.pixel = light.pixel,
           .radius = light.radius,
           .color = light.color,
           .strength = render::lightStrength(light, daylight, lightSeconds_,
                                             static_cast<float>(index) * SeedStep)});
    }
  }
  if (graphics_.vividColors) {
    params.vibrance = VividVibrance;
    params.contrast = VividContrast;
    params.bloom = VividBloom;
  }
  grade_->setParams(params);
  // A scene's own camera moves the models but not the painted background the lights sit on.
  grade_->setLights(shownLights_, stage_.camera ? nullptr : &mapRenderer_->camera());
}

} // namespace dwe::app
