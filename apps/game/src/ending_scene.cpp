#include "ending_scene.hpp"

#include <dwe/core/log.hpp>

#include <cmath>

#include "field_scene.hpp"

namespace dwe::app {
namespace {

// The original's species table, entry 0 (the player's model): 17 bones.
constexpr std::int32_t PlayerBones = 17;
// Slot 8 holds the ending's bank (routine 52: loadMapSounds2).
constexpr std::int64_t SceneSoundSlot = 8;

std::int32_t playerBones(const assets::DigimonTable* species) {
  if (species == nullptr || species->digimon.empty() || species->digimon.front().boneCount < 2) {
    return PlayerBones;
  }
  return species->digimon.front().boneCount;
}

} // namespace

EndingScene::EndingScene(const EndingSceneDesc& desc)
    : session_(desc.session), scene_(desc.scene), frameSeconds_(desc.frameSeconds),
      effects_(playerBones(desc.species), FieldEffectsSeed) {}

bool EndingScene::play(game::story::PartnerScene scene) {
  if (scene != game::story::PartnerScene::Ending || scene_ == nullptr) {
    return false;
  }
  // tamer.c: tamerTickEnding, sub state 0: ENDI_tickEnding's first call. The player's shadow goes
  // too (PLAYER_SHADOW_ENABLED = 0), which the engine does not draw yet.
  core::logDebug("[port:ending] tamerTickEnding (tamer.c:1687): ENDI_tickEnding starts");
  effects_.start();
  running_ = true;
  over_ = false;
  pendingSeconds_ = 0.0;
  playSounds();
  show();
  return true;
}

void EndingScene::tick(double seconds) {
  if (!running_) {
    return;
  }
  pendingSeconds_ += seconds;
  while (running_ && pendingSeconds_ >= frameSeconds_) {
    pendingSeconds_ -= frameSeconds_;
    step();
  }
}

// One frame: the player's ending tick reads the frame ENDI_updateEnding left (sub state 1), then
// the scene's object runs and the sparks are drawn.
void EndingScene::step() {
  if (effects_.over()) {
    core::logDebug("[port:ending] tamerTickEnding (tamer.c:1692): over, the story's sync bit");
    running_ = false;
    over_ = true;
    return;
  }
  const EndingSparkPlacer placer = [this](std::int32_t bone, OriginalPoint offset,
                                          std::int32_t scale) {
    return place(bone, offset, scale);
  };
  effects_.tickFrame(placer);
  playSounds();
  show();
}

void EndingScene::show() {
  FieldStage stage = scene_->stage();
  stage.playerAdditive = true;
  stage.playerFade = effects_.fade();
  stage.playerScale =
      glm::vec3{static_cast<float>(effects_.scale()) / static_cast<float>(EndingScaleOne)};
  scene_->setStage(stage);
  const EndingSparkPlacer placer = [this](std::int32_t bone, OriginalPoint offset,
                                          std::int32_t scale) {
    return place(bone, offset, scale);
  };
  scene_->fieldEffects().setGlows(effects_.glows(placer));
}

void EndingScene::playSounds() {
  game::story::SoundPort* port = session_ != nullptr ? session_->sound() : nullptr;
  for (const EndingSound& sound : effects_.takeSounds()) {
    if (port == nullptr) {
      continue;
    }
    if (sound.kind == EndingSound::Kind::Play) {
      if (const std::optional<std::int64_t> held =
              port->playHeldSound(SceneSoundSlot, sound.sound)) {
        heldSounds_.push_back(*held);
      }
    } else {
      for (const std::int64_t held : heldSounds_) {
        port->stopHeldSound(held);
      }
      heldSounds_.clear();
    }
  }
}

// A spark on a bone: the bone's matrix carries the player's scale about its feet, which the offset,
// already scaled once, takes again (ENDI_renderParticles).
std::optional<OriginalPoint> EndingScene::place(std::int32_t bone, OriginalPoint offset,
                                                std::int32_t scale) const {
  const std::optional<glm::vec3> point =
      scene_->playerNodePoint(static_cast<std::uint32_t>(bone), glm::vec3{offset});
  if (!point) {
    return std::nullopt;
  }
  const glm::vec3 feet = scene_->field().player().position;
  const float times = static_cast<float>(scale) / static_cast<float>(EndingScaleOne);
  const glm::vec3 world = feet + ((*point - feet) * times);
  return OriginalPoint{static_cast<std::int32_t>(std::lround(world.x)),
                       static_cast<std::int32_t>(std::lround(-world.y)),
                       static_cast<std::int32_t>(std::lround(-world.z))};
}

} // namespace dwe::app
