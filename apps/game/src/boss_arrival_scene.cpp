#include "boss_arrival_scene.hpp"

#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/map_document.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/math.hpp>
#include <dwe/game/field/field.hpp>
#include <dwe/game/story/story_ports.hpp>
#include <dwe/scene/components.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "evolution_effects.hpp"
#include "field_scene.hpp"
#include "scene_prims.hpp"

namespace dwe::app {

namespace {

// The original's field runs at 20 frames a second (main.c: gameLoop waits VSync(3)).
constexpr double OriginalFrameSeconds = 1.0 / 20.0;
// script_interp.c:1846: routine 51 reads ETCHI\BOSS_EFE.TMD for the rings.
constexpr std::string_view RingModel = "model/etchi/boss_efe";
// EAB_startBuildup: EAB_setModelColor(0x3c, 0x3c, 0x3c) on the rings' textured primitives, where
// 0x80 draws the texture as it is.
constexpr float RingShade = static_cast<float>(0x3c) / static_cast<float>(0x80);
// The sounds' slot: the boss bank routine 51 loaded.
constexpr std::int64_t SoundSlot = 8;
// 4096 for the model's own size.
constexpr float ScaleOne = 4096.0F;
// EAB_calculateCameraOrbit looks 100 over the boss's root bone.
constexpr float OrbitLookUp = 100.0F;
// EAB_renderParticle draws a side only when both ends are 0x20 to 0x1000 deep (the projected depth
// over 16).
constexpr float ParticleNearest = 0x20 * 16.0F;
constexpr float ParticleFarthest = 0x1000 * 16.0F;
constexpr float ChannelMax = 255.0F;
// The arrival's first NPC when no charge named the boss: ENTITY_TABLE[2].
constexpr std::size_t FirstNpc = 0;

} // namespace

BossArrivalScene::BossArrivalScene(FieldScene& scene) : scene_{&scene} {}

void BossArrivalScene::startBuildup(const session::BossBuildupStarted& request) {
  leaveMapIfChanged();
  character_ = request.character;
  seconds_ = 0.0;
  play(arrival_.startBuildup());
}

std::optional<scene::Entity> BossArrivalScene::boss() const {
  if (!character_ || !std::in_range<std::int32_t>(*character_)) {
    return std::nullopt;
  }
  return scene_->field().npcs().entityOf(static_cast<std::int32_t>(*character_));
}

void BossArrivalScene::startArrival() {
  leaveMapIfChanged();
  if (!character_ && !scene_->map().npcs.empty()) {
    character_ = scene_->map().npcs.at(FirstNpc).scriptId;
  }
  waiting_ = true;
  play(arrival_.startArrival());
}

bool BossArrivalScene::takeOver() {
  if (!waiting_ || arrival_.arriving()) {
    return false;
  }
  // tamerTickMachinedramonSpawn sets SOME_SCRIPT_SYNC_BIT once EAB_tick reads -1.
  waiting_ = false;
  return true;
}

void BossArrivalScene::leaveMapIfChanged() {
  const std::string& mapId = scene_->field().mapId();
  if (mapId == mapId_) {
    return;
  }
  mapId_ = mapId;
  if (arrival_.charging() || arrival_.arriving()) {
    core::logDebug("[port:story] the boss's charge goes with the map");
  }
  stopSounds();
  arrival_.clear();
  character_.reset();
  if (std::exchange(staged_, false)) {
    scene_->setStage({});
  }
}

void BossArrivalScene::advance(double seconds) {
  leaveMapIfChanged();
  if (!arrival_.charging() && !arrival_.arriving()) {
    seconds_ = 0.0;
    return;
  }
  const std::optional<scene::Entity> entity = boss();
  const glm::vec3 point =
      entity ? scene_->world().registry().get<scene::Transform>(*entity).position : glm::vec3{0.0F};
  seconds_ += seconds;
  while (seconds_ >= OriginalFrameSeconds) {
    seconds_ -= OriginalFrameSeconds;
    play(arrival_.frame(point, finishedAnimation()));
  }
}

std::optional<std::int32_t> BossArrivalScene::finishedAnimation() const {
  const std::optional<scene::Entity> entity = boss();
  if (!entity || !character_ || !scene_->clipPlayedThrough(*entity)) {
    return std::nullopt;
  }
  return scene_->field().npcs().scriptedAnimationOf(static_cast<std::int32_t>(*character_));
}

void BossArrivalScene::play(const game::field::BossArrivalCues& cues) {
  if (cues.stopSounds) {
    stopSounds();
  }
  if (cues.animation && character_ && std::in_range<std::int32_t>(*character_)) {
    // startAnimation on the boss, as a script's play_animation (StoryStage::animate).
    game::field::Field& field = scene_->field();
    const auto scriptId = static_cast<std::int32_t>(*character_);
    if (field.npcs().setScriptedAnimation(scriptId, *cues.animation)) {
      if (const std::optional<scene::Entity> entity = boss()) {
        field.animateByScript(*entity, static_cast<std::uint32_t>(*cues.animation));
      }
    }
  }
  if (cues.bossSoundSlot) {
    if (const std::optional<scene::Entity> entity = boss()) {
      scene_->setNpcSoundSlot(*entity, *cues.bossSoundSlot);
    }
  }
  if (sound_ == nullptr) {
    return;
  }
  if (cues.heldSound) {
    held_ = sound_->playHeldSound(SoundSlot, *cues.heldSound);
  }
  if (cues.sound) {
    sound_->playSound(SoundSlot, *cues.sound);
  }
}

void BossArrivalScene::stopSounds() {
  if (const std::optional<std::int64_t> held = std::exchange(held_, std::nullopt);
      held && sound_ != nullptr) {
    sound_->stopHeldSound(*held);
  }
}

void BossArrivalScene::show() {
  const std::optional<scene::Entity> entity = boss();
  std::vector<scene::Entity> hidden;
  if (entity && arrival_.bossHidden()) {
    hidden.push_back(*entity);
  }
  scene_->setHiddenNpcs(std::move(hidden));
  // EAB_renderBuildupRing and EAB_renderSpawnRing: turned half a turn, at the boss's place.
  std::vector<FieldScene::PropPlace> rings;
  if (entity) {
    const glm::vec3 place = scene_->world().registry().get<scene::Transform>(*entity).position;
    const glm::mat4 turned = glm::rotate(glm::translate(glm::mat4{1.0F}, place),
                                         std::numbers::pi_v<float>, glm::vec3{0.0F, 1.0F, 0.0F});
    for (const auto& ring : arrival_.rings()) {
      if (!ring) {
        continue;
      }
      const game::field::BossRingScale scale = game::field::bossRingScale(*ring);
      const float across = static_cast<float>(scale.across) / ScaleOne;
      rings.push_back(
          {.world = glm::scale(
               turned, glm::vec3{across, static_cast<float>(scale.high) / ScaleOne, across}),
           .joints = {},
           .tint = glm::vec4{glm::vec3{RingShade}, 1.0F}});
    }
  }
  if (auto id = assets::AssetId::parse(RingModel)) {
    scene_->setPosedProps(*id, std::move(rings));
  }
  showStage(entity);
}

std::optional<render::Camera> BossArrivalScene::orbitCamera(scene::Entity entity) const {
  const std::optional<game::field::BossOrbit> orbit = arrival_.orbit();
  if (!orbit) {
    return std::nullopt;
  }
  // EAB_calculateCameraOrbit in the original's axes (y down), turned into world space: the boss's
  // root bone 100 up, the eye `distance` away at the angle plus the boss's facing, `height` up.
  const scene::Transform& transform = scene_->world().registry().get<scene::Transform>(entity);
  const glm::vec3 root = scene_->entityNodePoint(entity, 0).value_or(transform.position);
  const glm::vec3 target = root + glm::vec3{0.0F, OrbitLookUp, 0.0F};
  const float angle = (static_cast<float>(orbit->angle) * 2.0F * std::numbers::pi_v<float> /
                       static_cast<float>(core::OriginalAnglePerTurn)) -
                      transform.yaw;
  const auto distance = static_cast<float>(orbit->distance);
  const glm::vec3 eye =
      target +
      core::fromOriginalAxes({distance * std::sin(angle), 0.0F, distance * std::cos(angle)}) +
      glm::vec3{0.0F, static_cast<float>(orbit->height), 0.0F};
  return render::Camera{.view = glm::lookAt(eye, target, glm::vec3{0.0F, 1.0F, 0.0F}),
                        .projection = scene_->mapCamera().projection(),
                        .viewport = {}};
}

void BossArrivalScene::showStage(std::optional<scene::Entity> entity) {
  if (!arrival_.arriving()) {
    if (std::exchange(staged_, false)) {
      scene_->setStage({});
    }
    return;
  }
  FieldStage stage;
  if (arrival_.othersHidden()) {
    stage.sceneryHidden = true;
    stage.partnerHidden = true;
    stage.shownNpc = entity;
    stage.shownProps = {std::string{RingModel}};
  }
  stage.camera = entity ? orbitCamera(*entity) : std::nullopt;
  const render::Camera seen =
      stage.camera.value_or(render::Camera{.view = scene_->mapCamera().view(),
                                           .projection = scene_->mapCamera().projection(),
                                           .viewport = {}});
  const ScenePrimFrame frame{
      .view = seen.view, .projection = seen.projection, .pixelScale = scene_->mapCamera().scale()};
  // model.c: renderDigimon draws the boss (entity 2) with renderWireframed while
  // ENTITY1_WIREFRAME_TOTAL is not 16: of its faces, those of the solid shares textured, the
  // others as lines of one grey an object.
  if (entity && arrival_.othersHidden() &&
      arrival_.solidShares() != game::field::BossArrival::SolidShares) {
    stage.shownNpcWired = true;
    const std::vector<BodyFace> faces = scene_->npcFaces(*entity);
    std::vector<SortedPrim> wired;
    for (auto first = faces.begin(); first != faces.end();) {
      const auto last = std::find_if(
          first, faces.end(), [&](const BodyFace& face) { return face.bone != first->bone; });
      const auto grey = WireGreyMin + static_cast<std::int32_t>(
                                          wireRandom_.next() %
                                          static_cast<std::uint32_t>(WireGreyMax - WireGreyMin));
      addWiredFaces(std::span{first, last}, arrival_.solidShares(), grey, frame,
                    scene_->whiteTexture(), wired, [this](const BodyFace& face) {
                      return glm::vec4{scene_->modelLight(face.normal), 1.0F};
                    });
      first = last;
    }
    std::vector<ScenePrim> sorted = sortedPrims(std::move(wired));
    stage.prims.insert(stage.prims.end(), sorted.begin(), sorted.end());
  }
  // EAB_renderParticle: four lines round the point, sorted far (0xf9f), opaque.
  for (const game::field::BossParticle& particle : arrival_.particles()) {
    const auto half = static_cast<float>(game::field::bossParticleHalfSide(particle));
    const std::array corners{particle.point + glm::vec3{half, 0.0F, half},
                             particle.point + glm::vec3{half, 0.0F, -half},
                             particle.point + glm::vec3{-half, 0.0F, -half},
                             particle.point + glm::vec3{-half, 0.0F, half}};
    const std::array<std::int32_t, 3> rgb = game::field::bossParticleColour(particle);
    const glm::vec4 colour{static_cast<float>(rgb[0]) / ChannelMax,
                           static_cast<float>(rgb[1]) / ChannelMax,
                           static_cast<float>(rgb[2]) / ChannelMax, 1.0F};
    const auto shown = [&frame](glm::vec3 point) {
      const float depth = frame.depth(point);
      return depth > ParticleNearest && depth < ParticleFarthest;
    };
    for (std::size_t i = 0; i < corners.size(); ++i) {
      const glm::vec3 from = corners.at(i);
      const glm::vec3 to = corners.at((i + 1) % corners.size());
      if (!shown(from) || !shown(to)) {
        continue;
      }
      ScenePrim line{
          .shape = ScenePrim::Shape::Line, .color = colour, .texture = scene_->whiteTexture()};
      line.corners[0].clip = clipBehind(frame.clip(from));
      line.corners[1].clip = clipBehind(frame.clip(to));
      stage.prims.push_back(line);
    }
  }
  // EAB_renderFlash: the whole screen less the level, in front of everything (subtractive).
  if (const std::optional<std::int32_t> blackout = arrival_.blackout(); blackout && *blackout > 0) {
    ScenePrim quad{.shape = ScenePrim::Shape::Quad,
                   .color = glm::vec4{glm::vec3{static_cast<float>(*blackout) / ChannelMax}, 1.0F},
                   .texture = scene_->whiteTexture(),
                   .blend = ScenePrimBlend::Subtractive};
    quad.corners[0].clip = {-1.0F, -1.0F, 0.0F, 1.0F};
    quad.corners[1].clip = {1.0F, -1.0F, 0.0F, 1.0F};
    quad.corners[2].clip = {-1.0F, 1.0F, 0.0F, 1.0F};
    quad.corners[3].clip = {1.0F, 1.0F, 0.0F, 1.0F};
    stage.prims.push_back(quad);
  }
  scene_->setStage(stage);
  staged_ = true;
}

} // namespace dwe::app
