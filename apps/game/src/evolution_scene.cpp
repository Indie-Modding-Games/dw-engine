#include "evolution_scene.hpp"

#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/render/camera.hpp>
#include <dwe/render/map_camera.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <optional>
#include <utility>

#include "messages.hpp"

namespace dwe::app {
namespace {

using game::partner::EventKind;
using game::partner::PartnerEvent;

// The original's fixed point: 4096 is 1.
constexpr std::int32_t FixedOne = 4096;
constexpr float FixedOneF = 4096.0F;
// Not in the original: a scene clip that never ends (a model whose last clip repeats) gives the
// field back after this many frames instead of holding it forever.
constexpr std::int32_t ClipWaitGuardFrames = 200;
// Slot 8 plays the scene's bank (playSound2(8, n)).
constexpr std::int64_t SceneSoundSlot = 8;

// effect_common.c: lerp, kept whole: past t1 the result wraps instead of stopping at `end`.
std::int32_t originalLerp(std::int32_t start, std::int32_t end, std::int32_t t0, std::int32_t t1,
                          std::int32_t t) {
  if (t1 == t0) {
    return 0;
  }
  const std::int32_t range = end - start;
  std::int32_t result = (range * (t - t0)) / (t1 - t0);
  if (range >= 0) {
    result %= range + 1;
    if (result < 0) {
      result += range;
    }
  } else {
    result %= range - 1;
    if (result > 0) {
      result += range;
    }
  }
  return result + start;
}

float progress(std::int32_t from, std::int32_t to, std::int32_t frame) {
  return std::clamp(static_cast<float>(frame - from) / static_cast<float>(to - from), 0.0F, 1.0F);
}

const char* phaseName(std::uint8_t phase) {
  constexpr std::array<const char*, 8> Names{"idle",    "glowing",  "gathering", "dimming",
                                             "closing", "bursting", "restoring", "finishing"};
  return phase < Names.size() ? Names.at(phase) : "?";
}

} // namespace

EvolutionScene::EvolutionScene(const EvolutionSceneDesc& desc)
    : session_(desc.session), scene_(desc.scene), species_(desc.species),
      evolution_(desc.evolution), frameSeconds_(desc.frameSeconds),
      drawn_(desc.session->partnerView().species) {}

std::vector<PartnerEvent> EvolutionScene::take(std::span<const PartnerEvent> events) {
  std::vector<PartnerEvent> rest;
  for (const PartnerEvent& event : events) {
    const std::string species = session_->partnerView().species;
    if (event.kind == EventKind::Evolved && evolution_ != nullptr &&
        evolution_->sceneRules() != nullptr && frameSeconds_ > 0.0) {
      start(species);
      continue;
    }
    if (event.kind == EventKind::Reborn) {
      drawn_ = species;
    }
    rest.push_back(event);
  }
  return rest;
}

void EvolutionScene::start(const std::string& target) {
  if (active()) {
    // A second evolution during the scene only changes the form it ends on.
    core::logDebug("[port:evolution] getEvoSequenceState (main/evl.c:77): retarget {} -> {}",
                   target_, target);
    target_ = target;
    return;
  }
  target_ = target;
  rules_ = *evolution_->sceneRules();
  core::logDebug("[port:evolution] partnerTickEvolving (partner.c:741): scene {} -> {}", drawn_,
                 target_);
  stage_ = {};
  pendingSeconds_ = 0.0;
  burstClipPlaying_ = false;
  // getEvoSequenceState: the idle clip, the scene's sound bank in slot 8.
  scene_->playPartnerClip(rules_.clips.idle);
  session_->useSceneSounds(rules_.soundBank);
  setPhase(Phase::Glowing);
}

void EvolutionScene::tick(double seconds) {
  if (!active()) {
    return;
  }
  pendingSeconds_ += seconds;
  while (active() && pendingSeconds_ >= frameSeconds_) {
    pendingSeconds_ -= frameSeconds_;
    step();
  }
}

void EvolutionScene::setPhase(Phase phase) {
  core::logDebug("[port:evolution] EVL_tickEvoSequence (evl.c:345): {} -> {} at frame {}",
                 phaseName(static_cast<std::uint8_t>(phase_)),
                 phaseName(static_cast<std::uint8_t>(phase)), frame_);
  phase_ = phase;
  if (phase == Phase::Glowing || phase == Phase::Gathering || phase == Phase::Dimming) {
    frame_ = 0;
  }
}

void EvolutionScene::playSound(std::int32_t sound) {
  if (game::story::SoundPort* port = session_->sound()) {
    port->playSound(SceneSoundSlot, sound);
  }
}

void EvolutionScene::step() {
  // evl.c:376: the burst clip goes on with the ready one once it ends.
  if (burstClipPlaying_ && scene_->partnerClipFinished()) {
    burstClipPlaying_ = false;
    scene_->playPartnerClip(rules_.clips.ready);
  }
  switch (phase_) {
  case Phase::Glowing:
    // tickEvoSequenceLoading state 0 (main/evl.c:41); the condition bubble is not drawn yet.
    if (frame_ >= rules_.waitFrames) {
      scene_->playPartnerClip(rules_.clips.ready);
      setPhase(Phase::Gathering);
    }
    break;
  case Phase::Gathering:
    if (frame_ >= rules_.waitFrames) {
      setPhase(Phase::Dimming);
    }
    break;
  case Phase::Dimming:
    // State 0: the map's palettes fade to black over these frames; here the scenery goes at their
    // end.
    if (frame_ >= rules_.dimFrames) {
      const render::MapCamera& map = scene_->mapCamera();
      mapEye_ = map.desc().eye;
      mapTarget_ = map.desc().target;
      mapProjection_ = map.projection();
      mapFocalLength_ = map.desc().focalLength;
      camera_ = {.eye = mapEye_, .target = mapTarget_};
      focalLength_ = mapFocalLength_;
      drop_ = 0.0F;
      recentre_ = 0.0F;
      stage_.sceneryHidden = true;
      scene_->setStage(stage_);
      setPhase(Phase::Closing);
    }
    break;
  case Phase::Closing:
    stepClosing();
    break;
  case Phase::Bursting:
    stepBursting();
    break;
  case Phase::Restoring:
    restore();
    break;
  case Phase::Finishing:
    waitFrames_ += 1;
    if (scene_->partnerClipFinished() || waitFrames_ >= ClipWaitGuardFrames) {
      finish();
      return;
    }
    break;
  case Phase::Idle:
    return;
  }
  frame_ += 1;
}

void EvolutionScene::stepClosing() {
  // EVL_updateEvoCamera (evl.c:702): aim at the partner, move in, then circle it.
  const game::evolution::EvolutionSceneCamera& rules = rules_.camera;
  const Size size = sizeOf(drawn_);
  if (frame_ >= rules_.dimFrames && frame_ <= rules_.cameraMoveEnd) {
    orbitAngle_ = rules.startAngle;
    const Orbit orbit = orbitAt(size);
    if (frame_ <= rules_.cameraAimEnd) {
      const float t = progress(rules_.dimFrames, rules_.cameraAimEnd, frame_);
      camera_.target = glm::mix(mapTarget_, orbit.target, t);
      recentre_ = t;
    } else {
      const float t = progress(rules_.cameraAimEnd, rules_.cameraMoveEnd, frame_);
      camera_.eye = glm::mix(mapEye_, orbit.eye, t);
      const std::int32_t drop = originalLerp(0, rules.maxDrop, 0, rules.dropHeight,
                                             static_cast<std::int32_t>(size.height));
      drop_ = static_cast<float>(
          originalLerp(0, drop, rules_.cameraAimEnd, rules_.cameraMoveEnd, frame_));
      focalLength_ = glm::mix(mapFocalLength_, static_cast<float>(rules.focalLength), t);
    }
  } else if (frame_ > rules_.cameraMoveEnd) {
    orbitAngle_ += rules.angleStep;
    camera_ = orbitAt(size);
  }
  showCamera();
  if (frame_ == rules_.burstFrame) {
    for (const std::int32_t sound : rules_.burstSounds) {
      playSound(sound);
    }
    scene_->playPartnerClip(rules_.clips.burst);
    burstClipPlaying_ = true;
    setPhase(Phase::Bursting);
  }
}

void EvolutionScene::stepBursting() {
  // State 2 (evl.c:410); its particles, shards and flashes are not drawn yet.
  const game::evolution::EvolutionSceneCamera& rules = rules_.camera;
  const Size size = grownSize();
  if (frame_ > rules_.orbitEase.from) {
    orbitAngle_ +=
        frame_ > rules_.orbitEase.to
            ? rules.angleStep
            : originalLerp(0, rules.angleStep, rules_.orbitEase.from, rules_.orbitEase.to, frame_);
    camera_ = orbitAt(size);
    showCamera();
  }
  if (frame_ >= rules_.grow.from && frame_ < rules_.grow.to) {
    const Size from = sizeOf(drawn_);
    const Size to = sizeOf(target_);
    const auto ratio = [](float next, float now) {
      return now > 0.0F ? static_cast<std::int32_t>((next * FixedOneF) / now) : FixedOne;
    };
    const auto scaleAt = [&](std::int32_t target) {
      return static_cast<float>(
                 originalLerp(FixedOne, target, rules_.grow.from, rules_.grow.to, frame_)) /
             FixedOneF;
    };
    const float across = scaleAt(ratio(to.radius, from.radius));
    stage_.partnerScale = {across, scaleAt(ratio(to.height, from.height)), across};
    scene_->setStage(stage_);
  }
  if (frame_ == rules_.roarFrame) {
    playSound(rules_.roarSound);
  }
  if (frame_ == rules_.swapFrame) {
    drawNewForm();
  }
  if (frame_ >= rules_.endFrame) {
    // State 3: the map's camera comes back.
    stage_.camera.reset();
    scene_->setStage(stage_);
    setPhase(Phase::Restoring);
  }
}

void EvolutionScene::drawNewForm() {
  // EVL_applyEvolution (evl.c:1332): the new form, at its own size, idle.
  core::logDebug("[port:evolution] EVL_applyEvolution (evl.c:1332): draw {} at frame {}", target_,
                 frame_);
  if (auto shown = scene_->showPartnerSpecies(target_); !shown) {
    core::logWarning(
        "{}", core::formatMessage(messages::PartnerModelNotShown, target_, shown.error().message));
  }
  drawn_ = target_;
  stage_.partnerScale = glm::vec3{1.0F};
  scene_->setStage(stage_);
  burstClipPlaying_ = false;
  scene_->playPartnerClip(rules_.clips.idle);
}

void EvolutionScene::restore() {
  // State 3 to 4 (evl.c:569): the scenery and the tamer come back, the map's sound bank, and the
  // partner plays its last clip.
  stage_ = {};
  scene_->setStage(stage_);
  session_->useSceneSounds(std::nullopt);
  scene_->playPartnerClip(rules_.clips.finish);
  waitFrames_ = 0;
  setPhase(Phase::Finishing);
}

void EvolutionScene::finish() {
  // partnerTickEvolving (partner.c:774): the game time and the player go on.
  core::logDebug("[port:evolution] partnerTickEvolving (partner.c:774): scene ends as {}", drawn_);
  if (drawn_ != target_) {
    drawNewForm();
  }
  scene_->playPartnerClip(std::nullopt);
  scene_->setStage({});
  setPhase(Phase::Idle);
}

EvolutionScene::Size EvolutionScene::sizeOf(std::string_view species) const {
  const assets::DigimonRow* row = species_ != nullptr ? species_->find(species) : nullptr;
  if (row == nullptr) {
    return {};
  }
  return {.radius = static_cast<float>(row->radius), .height = static_cast<float>(row->height)};
}

EvolutionScene::Size EvolutionScene::grownSize() const {
  const Size from = sizeOf(drawn_);
  const Size to = sizeOf(target_);
  if (frame_ < rules_.grow.from) {
    return from;
  }
  if (frame_ >= rules_.grow.to) {
    return to;
  }
  const auto at = [&](float start, float end) {
    return static_cast<float>(originalLerp(static_cast<std::int32_t>(start),
                                           static_cast<std::int32_t>(end), rules_.grow.from,
                                           rules_.grow.to, frame_));
  };
  return {.radius = at(from.radius, to.radius), .height = at(from.height, to.height)};
}

EvolutionScene::Orbit EvolutionScene::orbitAt(Size size) const {
  // EVL_calculateCameraVectors (evl.c:834) in the original's axes (Y down), turned into world
  // space: the camera looks at half the partner's height from its root, from `distance` away at the
  // orbit angle plus the partner's facing, raised by rise plus twice the height.
  const game::evolution::EvolutionSceneCamera& rules = rules_.camera;
  const game::field::CharacterState partner = scene_->field().partner();
  const float distance =
      (std::max(size.radius, size.height) * static_cast<float>(rules.distancePerSize)) +
      static_cast<float>(rules.distance);
  const float angle = (static_cast<float>(orbitAngle_) * 2.0F * std::numbers::pi_v<float> /
                       static_cast<float>(core::OriginalAnglePerTurn)) -
                      partner.yaw;
  const glm::vec3 target = partner.position + glm::vec3{0.0F, size.height / 2.0F, 0.0F};
  const glm::vec3 offset =
      core::fromOriginalAxes({distance * std::sin(angle), 0.0F, distance * std::cos(angle)});
  const glm::vec3 rise{0.0F, static_cast<float>(rules.rise) + (2.0F * size.height), 0.0F};
  return {.eye = target + offset + rise, .target = target};
}

void EvolutionScene::showCamera() {
  const render::MapCamera& map = scene_->mapCamera();
  const glm::vec2 visible = map.visibleSize();
  const glm::vec2 viewport = map.viewportSize();
  const float fovY = 2.0F * std::atan((visible.y / 2.0F) / focalLength_);
  // DRAWING_OFFSET_Y below the screen's centre moves the picture down (clip space Y is down).
  const glm::mat4 centred =
      glm::translate(glm::mat4{1.0F}, glm::vec3{0.0F, 2.0F * drop_ / visible.y, 0.0F}) *
      render::perspective(fovY, viewport.x / viewport.y, render::MapNearClip, render::MapFarClip);
  const glm::mat4 projection = (mapProjection_ * (1.0F - recentre_)) + (centred * recentre_);
  const glm::mat4 view = glm::lookAt(camera_.eye, camera_.target, glm::vec3{0.0F, 1.0F, 0.0F});
  stage_.camera = render::Camera{.view = view, .projection = projection, .viewport = {}};
  scene_->setStage(stage_);
}

} // namespace dwe::app
