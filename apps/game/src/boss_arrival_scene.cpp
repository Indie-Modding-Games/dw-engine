#include "boss_arrival_scene.hpp"

#include <dwe/assets/asset_id.hpp>
#include <dwe/core/log.hpp>
#include <dwe/game/field/field.hpp>
#include <dwe/game/story/story_ports.hpp>
#include <dwe/scene/components.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <numbers>
#include <utility>
#include <vector>

#include "field_scene.hpp"

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
    play(arrival_.frame(point));
  }
}

void BossArrivalScene::play(const game::field::BossArrivalCues& cues) {
  if (cues.stopSounds) {
    stopSounds();
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
}

} // namespace dwe::app
