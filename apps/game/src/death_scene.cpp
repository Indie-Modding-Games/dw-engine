#include "death_scene.hpp"

#include <dwe/assets/map_document.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/math.hpp>
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

// Not in the original: a last clip that never ends (a model whose clip repeats) gives the field
// back after this many frames instead of holding it forever.
constexpr std::int32_t ClipWaitGuardFrames = 200;
// Slot 8 plays the scene's bank (playSound2(8, n)).
constexpr std::int64_t SceneSoundSlot = 8;
// fadeoutCLUT1's full level: the palettes are black.
constexpr float PaletteLevelMax = 255.0F;
// The lives box's sounds are the UI's, in slot 0 (ui.c: createAnimatedUIBox, renderUIBoxAnim).
constexpr std::int64_t BoxSoundSlot = 0;
// MURD_createLivesBox grows the box from the partner's middle: half its height over its feet.
constexpr float PartnerMiddle = 0.5F;
constexpr double Percent = 100.0;
constexpr screens::WindowFraction WindowMiddle{.x = 50.0, .y = 50.0};
// The evolution's images: the iris and the flashes are SPOT256, the sparks DEFEFF's palette 1.
constexpr std::string_view GlowImageSource = "texture/etcdat/spot256";
constexpr std::string_view SparkImageSource = "texture/etcdat/defeff?palette=1";
// TAMA.TMD: the four eggs, six pieces each.
constexpr std::string_view EggModel = "model/etcdat/tama";
// MURD_renderIris closes by 0x23.
constexpr std::int32_t LostLifeIrisEnd = 0x23;

// dooa.c: DOOA_renderDissolve draws the iris until frame 36, closing by 0x21.
constexpr std::int32_t DissolveIrisEnd = 0x21;
// MURD_renderScene and DOOA_renderDissolve draw it while the frame is below 0x24.
constexpr std::int32_t IrisUntil = 0x24;
// DOOA_tickDissolve: the camera comes round to the partner (phase 1, 0x23 to 0x55), is put almost
// all the way round it where it stands for the scene (0x56: frame 0x63 of 0 to 0x64) and goes back
// to the map's view along the same path (phase 3, frame 0x2C4 - f of 0x149 to 0x17B).
constexpr std::int32_t CameraInFrom = 0x23;
constexpr std::int32_t CameraInTo = 0x55;
constexpr std::int32_t CameraSetTo = 0x64;
constexpr std::int32_t CameraSetAt = 0x63;
constexpr std::int32_t CameraBackFrom = 0x149;
constexpr std::int32_t CameraBackTo = 0x17B;
constexpr std::int32_t CameraBackMirror = 0x2C4;
// DOOA_updateCutsceneCamera and DOOA_getOrbitPosition: the camera stands height * 5 + 1200 away at
// the partner's facing plus 0x638, 400 plus twice the height over half its height, and ends with
// VIEWPORT_DISTANCE 1000 and the picture lerp(0, 20, 0, 200, height) lower.
constexpr std::int32_t OrbitTurn = 0x638;
constexpr float OrbitPerHeight = 5.0F;
constexpr float OrbitDistance = 1200.0F;
constexpr float OrbitRise = 400.0F;
constexpr float OrbitFocalLength = 1000.0F;
constexpr std::int32_t OrbitDropMax = 20;
constexpr std::int32_t OrbitDropHeight = 200;
// The sound of the breaking body stops at 0x8C.
constexpr std::int32_t BreakSoundStop = 0x8C;
// Phase 0x64 (0xBF to 0xD0): DOOA_DISSOLVE_SCALE_CURVE squashes and stretches the partner, which
// leaps to 100 over its head once the curve passes 300.
constexpr std::array<std::int32_t, 32> ScaleCurve{
    100, 94,  89,  85,  82,  80,  79,  79,  100, 120, 139, 157, 174, 190, 205, 219,
    232, 244, 255, 265, 274, 282, 289, 295, 300, 350, 395, 435, 470, 500, 525, 545};
constexpr std::int32_t ScaleLast = 0x1F;
constexpr std::int32_t ScaleLeap = 0x12C;
constexpr std::int32_t LeapAbove = 100;
constexpr std::int32_t Hundred = 100;
constexpr std::int32_t ScaleOne = 4096;
// The partner's bones a mask can hide.
constexpr std::uint32_t BitsPerMask = 64;
constexpr std::int32_t FullShare = 16;
// The first scene's effects roll from the original's first rand() state.
constexpr std::uint32_t FirstEffectSeed = 0x3039;

// A linear step from `start` at t0 to `end` at t1 (effect_common.c: lerp, inside its range).
std::int32_t lerpFrames(std::int32_t start, std::int32_t end, std::int32_t t0, std::int32_t t1,
                        std::int32_t t) {
  if (t1 == t0) {
    return end;
  }
  const std::int32_t clamped = std::clamp(t, std::min(t0, t1), std::max(t0, t1));
  return start + ((end - start) * (clamped - t0) / (t1 - t0));
}

const char* phaseName(std::uint8_t phase) {
  constexpr std::array<const char*, 8> Names{
      "idle",          "lost life alone", "lost life fade out", "lost life fade in", "dissolve",
      "await rebirth", "rebirth",         "rebirth finish"};
  return phase < Names.size() ? Names.at(phase) : "?";
}

} // namespace

DeathScene::DeathScene(const DeathSceneDesc& desc)
    : session_(desc.session), fishing_(desc.fishing), scene_(desc.scene), ui_(desc.ui),
      species_(desc.species), story_(desc.story), scenes_(desc.scenes),
      transition_(desc.transition) {}

std::vector<PartnerEvent> DeathScene::take(std::span<const PartnerEvent> events) {
  if (!session_->partnerRules().life.scenes || transition_.frameSeconds <= 0.0F) {
    return {events.begin(), events.end()};
  }
  // The story asks for the scenes itself (play).
  const bool byStory = scenes_ != nullptr && scenes_->playsDeath();
  std::vector<PartnerEvent> rest;
  for (const PartnerEvent& event : events) {
    if (byStory && (event.kind == EventKind::LostLife || event.kind == EventKind::Died)) {
      rest.push_back(event);
      continue;
    }
    switch (event.kind) {
    case EventKind::LostLife:
      // tamer.c: tamerTickBattleLostLife and tamerTickSicknessLostLife run MURD_tick; with no lives
      // left it leads into the death (murd.c:308).
      if (event.value > 0) {
        pending_.push_back({.kind = SceneKind::LostLife, .lives = event.value});
      } else {
        lastLifeLost_ = true;
      }
      rest.push_back(event);
      break;
    case EventKind::Died:
      pending_.push_back({.kind = SceneKind::Death, .lives = lastLifeLost_ ? 0 : 1});
      lastLifeLost_ = false;
      rest.push_back(event);
      break;
    case EventKind::Reborn:
      // The new baby is drawn on the rebirth's frame, not now; its egg is the baby's place.
      baby_ = session_->partnerView().species;
      eggSlot_ = static_cast<std::int32_t>(event.value);
      core::logDebug("[port:death] reborn as '{}' from egg {}", baby_, eggSlot_);
      break;
    default:
      rest.push_back(event);
      break;
    }
  }
  return rest;
}

bool DeathScene::play(game::story::PartnerScene scene) {
  using game::story::PartnerScene;
  if (!session_->partnerRules().life.scenes || transition_.frameSeconds <= 0.0F) {
    return false;
  }
  switch (scene) {
  case PartnerScene::SicknessLostLife:
  case PartnerScene::BattleLostLife:
    // tamer.c: tamerTickSicknessLostLife and tamerTickBattleLostLife run MURD_tick, which goes on
    // into the death with no lives left.
    pending_.push_back(
        {.kind = SceneKind::LostLife, .lives = session_->partner().state().life.lives});
    break;
  case PartnerScene::Dying:
    pending_.push_back({.kind = SceneKind::Death, .lives = 1});
    break;
  case PartnerScene::LostAllLives:
    pending_.push_back({.kind = SceneKind::Rebirth, .lives = 0});
    break;
  default:
    return false;
  }
  storyScene_ = true;
  return true;
}

void DeathScene::tick(double seconds) {
  if (phase_ == Phase::Idle || phase_ == Phase::AwaitRebirth) {
    if (pending_.empty()) {
      return;
    }
    const Pending next = pending_.front();
    pending_.pop_front();
    begin(next);
  }
  pendingSeconds_ += seconds;
  const double frameSeconds = transition_.frameSeconds;
  while (phase_ != Phase::Idle && pendingSeconds_ >= frameSeconds) {
    pendingSeconds_ -= frameSeconds;
    step();
  }
}

void DeathScene::begin(const Pending& pending) {
  const std::optional<game::partner::LifeScenes>& scenes = session_->partnerRules().life.scenes;
  if (!scenes.has_value()) {
    // The scenes went from the ruleset since the event came: the baby is drawn at once.
    if (!baby_.empty() && !scene_->showPartnerSpecies(baby_)) {
      core::logWarning("{}", core::formatMessage(messages::PartnerModelNotShown, baby_, ""));
    }
    return;
  }
  rules_ = *scenes;
  lives_ = pending.lives;
  livesBox_.remove();
  pendingSeconds_ = 0.0;
  stage_ = {};
  if (pending.kind == SceneKind::Rebirth) {
    // partner.c: partnerTickDying2: the rebirth, on black, once the story's death script is over.
    core::logDebug("[port:death] partnerTickDying2 (call_partner_routine 4): the rebirth");
    session_->useSceneSounds(rules_.death.soundBank);
    stage_.sceneryHidden = true;
    stage_.partnerHidden = true;
    // The wake map's host, shown since the death's hostFrame, stays on black (nothing hides
    // ENTITY_TABLE[2] again before DOOA_tickRebirth brings everyone back at swapFrame).
    stage_.shownNpc = wakeHost();
    showStage();
    if (phase_ != Phase::AwaitRebirth) {
      // The death's shards wait for the rebirth when the story played both.
      effects_.start(FirstEffectSeed + effectSeed_++);
    }
    frame_ = 0;
    setPhase(Phase::Rebirth);
    return;
  }
  if (pending.kind == SceneKind::Death && lives_ > 0) {
    // partner.c: partnerTickDying (natural death): the water of a fishing map goes at once
    // (deinitializeFishing: its fish, sea serpent and ripples are gone until a map loads), then
    // the death's sound bank and DOOA_tick.
    core::logDebug("[port:death] partnerTickDying (partner.c:640): natural death");
    if (fishing_ != nullptr) {
      fishing_->leaveWater();
    }
    startDissolve(true);
    return;
  }
  // murd.c: MURD_tick; a death with no lives left starts here too and goes on to the dissolve.
  core::logDebug("[port:death] MURD_tick (murd.c:756): lives={}", lives_);
  if (lives_ != 0) {
    setDeathMap();
  }
  frame_ = 0;
  setPhase(Phase::LostLifeAlone);
}

void DeathScene::setPhase(Phase phase) {
  core::logDebug("[port:death] {} -> {} at frame {}", phaseName(static_cast<std::uint8_t>(phase_)),
                 phaseName(static_cast<std::uint8_t>(phase)), frame_);
  phase_ = phase;
}

void DeathScene::playSound(std::int32_t sound) {
  if (game::story::SoundPort* port = session_->sound()) {
    if (const std::optional<std::int64_t> held = port->playHeldSound(SceneSoundSlot, sound)) {
      heldSounds_.push_back(*held);
    }
  }
}

void DeathScene::stopSounds() {
  if (game::story::SoundPort* port = session_->sound()) {
    for (const std::int64_t held : heldSounds_) {
      port->stopHeldSound(held);
    }
  }
  heldSounds_.clear();
}

float DeathScene::partnerHeight() const {
  const assets::DigimonRow* row =
      species_ != nullptr ? species_->find(session_->partnerView().species) : nullptr;
  return row != nullptr ? static_cast<float>(row->height) : 0.0F;
}

float DeathScene::partnerRadius() const {
  const assets::DigimonRow* row =
      species_ != nullptr ? species_->find(session_->partnerView().species) : nullptr;
  return row != nullptr ? static_cast<float>(row->radius) : 0.0F;
}

void DeathScene::showIris(std::int32_t end) {
  // MURD_renderIris and DOOA_renderIrisWindow: around the partner's middle; once it is no larger
  // than the partner (MURD_isDigimonLargerThanIris, DOOA_hasIrisClosed), the partner is drawn over
  // it (MURD_renderDigimon at entry 0x21).
  iris_.clear();
  const std::optional<rhi::TextureId> spot = scene_->sceneImage(GlowImageSource);
  if (frame_ >= IrisUntil || !spot) {
    return;
  }
  const render::MapCamera& map = scene_->mapCamera();
  const ScenePrimFrame frame{
      .view = map.view(), .projection = map.projection(), .pixelScale = map.scale()};
  const float height = partnerHeight();
  const glm::vec3 middle =
      scene_->field().partner().position + glm::vec3{0.0F, height * PartnerMiddle, 0.0F};
  const float depth = frame.depth(middle);
  if (depth <= 0.0F) {
    return;
  }
  const std::int32_t radius = irisRadius(0, end, frame_);
  // getDistance(radius * 2, height, radius * 2), seen at the projection distance.
  const float across = 2.0F * partnerRadius();
  const float size = std::sqrt((across * across * 2.0F) + (height * height));
  const float pixels = size * map.desc().focalLength / depth;
  glm::vec4 centre = frame.clip(middle);
  if (irisWithinPartner(radius, pixels)) {
    // Behind the partner, which MURD_renderDigimon draws again over it, and over the rest: the
    // scenery is no longer drawn, so that nothing nearer than the partner's back shows through.
    stage_.sceneryHidden = true;
    const glm::vec3 eye{glm::inverse(frame.view)[3]};
    const glm::vec4 back = frame.clip(middle + (glm::normalize(middle - eye) * size));
    centre.z = back.z / back.w * centre.w;
  } else {
    centre = clipInFront(centre);
  }
  iris_ = irisPrims(centre, radius, *spot);
}

void DeathScene::showEffects() {
  // The body's bones the effects draw as wireframe are not drawn by the field.
  stage_.partnerHiddenBones = 0;
  const EvolutionBody& body = scene_->partnerBody();
  if (effects_.bodyShare() != FullShare) {
    for (std::uint32_t bone = 0; bone < body.boneCount() && bone < BitsPerMask; ++bone) {
      stage_.partnerHiddenBones |= std::uint64_t{1} << bone;
    }
  }
  stage_.prims.clear();
  showStage();
  const render::MapCamera& map = scene_->mapCamera();
  DeathView view{
      .frame = {.view = map.view(), .projection = map.projection(), .pixelScale = map.scale()},
      .focalLength = map.desc().focalLength,
      .white = scene_->whiteTexture(),
      .glow = scene_->sceneImage(GlowImageSource),
      .spark = scene_->sceneImage(SparkImageSource)};
  if (stage_.camera) {
    view.frame.view = stage_.camera->view;
    view.frame.projection = stage_.camera->projection;
    view.focalLength = cameraFocal_;
  }
  std::vector<ScenePrim> prims = effects_.draw(body, view);
  prims.insert(prims.end(), iris_.begin(), iris_.end());
  stage_.prims = std::move(prims);
  showStage();
}

void DeathScene::moveCamera(glm::vec3 feet, float yaw, std::int32_t start, std::int32_t end,
                            std::int32_t frame) {
  // DOOA_updateCutsceneCamera: out of its frames it leaves the camera as it is. The first half
  // turns the view to the partner and centres the picture; the second moves the eye round it,
  // lowers the picture and brings the projection distance to 1000.
  if (frame < start || frame > end) {
    return;
  }
  const float height = partnerHeight();
  const glm::vec3 target = feet + glm::vec3{0.0F, height / 2.0F, 0.0F};
  const float distance = (height * OrbitPerHeight) + OrbitDistance;
  const float angle = (static_cast<float>(OrbitTurn) * 2.0F * std::numbers::pi_v<float> /
                       static_cast<float>(core::OriginalAnglePerTurn)) -
                      yaw;
  const glm::vec3 eye =
      target +
      core::fromOriginalAxes({distance * std::sin(angle), 0.0F, distance * std::cos(angle)}) +
      glm::vec3{0.0F, OrbitRise + (2.0F * height), 0.0F};
  const std::int32_t mid = (start + end) / 2;
  SceneView shown = startView_;
  float recentre = 1.0F;
  float drop = 0.0F;
  if (frame <= mid) {
    const float t =
        mid > start ? static_cast<float>(frame - start) / static_cast<float>(mid - start) : 1.0F;
    shown.target = glm::mix(startView_.target, target, t);
    recentre = t;
  } else {
    const float t = static_cast<float>(frame - mid) / static_cast<float>(end - mid);
    shown.eye = glm::mix(startView_.eye, eye, t);
    shown.target = target;
    const std::int32_t lower =
        originalLerp(0, OrbitDropMax, 0, OrbitDropHeight, static_cast<std::int32_t>(height));
    drop = static_cast<float>(originalLerp(0, lower, mid, end, frame));
    shown.focalLength = glm::mix(startView_.focalLength, OrbitFocalLength, t);
  }
  const glm::mat4 centred = centredProjection(shown.focalLength, 0, drop);
  const glm::mat4 projection = (startView_.projection * (1.0F - recentre)) + (centred * recentre);
  const glm::mat4 view = glm::lookAt(shown.eye, shown.target, glm::vec3{0.0F, 1.0F, 0.0F});
  stage_.camera = render::Camera{.view = view, .projection = projection, .viewport = {}};
  cameraFocal_ = shown.focalLength;
}

glm::mat4 DeathScene::centredProjection(float focalLength, std::int32_t offsetX,
                                        float offsetY) const {
  const render::MapCamera& map = scene_->mapCamera();
  const glm::vec2 visible = map.visibleSize();
  const glm::vec2 viewport = map.viewportSize();
  const float fovY = 2.0F * std::atan((visible.y / 2.0F) / focalLength);
  // DRAWING_OFFSET_X right of the screen's centre moves the picture right and DRAWING_OFFSET_Y
  // below it moves the picture down (clip space Y is down).
  return glm::translate(glm::mat4{1.0F}, glm::vec3{2.0F * static_cast<float>(offsetX) / visible.x,
                                                   2.0F * offsetY / visible.y, 0.0F}) *
         render::perspective(fovY, viewport.x / viewport.y, render::MapNearClip,
                             render::MapFarClip);
}

void DeathScene::squash() {
  // Phase 0x64: the partner squashes, stretches up thin and, past 300 on the curve, leaps.
  const game::partner::DeathScene& rules = rules_.death;
  const std::int32_t from = rules.sink.frame;
  const std::int32_t to = rules.vanish.frame - 1;
  const std::int32_t index = originalLerp(0, ScaleLast, from, to, frame_);
  const std::int32_t curve =
      ScaleCurve.at(static_cast<std::size_t>(std::clamp(index, 0, ScaleLast)));
  const auto stretch = [](std::int32_t height) {
    return (((height - Hundred) * 2 * Hundred) / Hundred) + Hundred;
  };
  std::int32_t work = stretch(curve);
  std::int32_t tall = (work * ScaleOne) / Hundred;
  std::int32_t wide = ((10000 / work) * ScaleOne) / Hundred;
  wide = originalLerp(wide, wide * 50 / Hundred, from, to, frame_);
  if (curve > ScaleLeap) {
    work = stretch(ScaleLeap - (curve - ScaleLeap));
    tall = (work * ScaleOne) / Hundred;
    const float leap = static_cast<float>(static_cast<std::int32_t>(partnerHeight()) + LeapAbove);
    scene_->field().placeAt(scene_->field().partnerEntity(),
                            glm::vec3{0.0F, leap, 0.0F} + savedFeet_, sceneYaw_);
  }
  stage_.partnerScale =
      glm::vec3{static_cast<float>(wide), static_cast<float>(tall), static_cast<float>(wide)} /
      static_cast<float>(ScaleOne);
}

void DeathScene::showStage() {
  scene_->setStage(stage_);
}

void DeathScene::setShade(std::int32_t shade) {
  fade_ = game::field::shadeDarkness(transition_, shade);
}

bool DeathScene::reached(std::int32_t frame) const {
  return frame_ == frame;
}

void DeathScene::step() {
  // The scenes count their frame first (scene->timer++, seq->frame++); the boxes, which the
  // original ticks as objects of their own, before the scene's moments.
  frame_ += 1;
  livesBox_.step();
  switch (phase_) {
  case Phase::LostLifeAlone:
  case Phase::LostLifeFadeOut:
  case Phase::LostLifeFadeIn:
    stepLostLife();
    break;
  case Phase::Dissolve:
    stepDissolve();
    break;
  case Phase::Rebirth:
    stepRebirth();
    break;
  case Phase::RebirthFinish:
    waitFrames_ += 1;
    effects_.tickObjects();
    showEffects();
    if (scene_->partnerClipFinished() || waitFrames_ >= ClipWaitGuardFrames) {
      finish();
    }
    break;
  case Phase::AwaitRebirth:
    // The shards stay where they hang until the rebirth (the effect lives on in DOOA_tick's
    // objects while the story's script runs).
    effects_.tickObjects();
    showEffects();
    break;
  case Phase::Idle:
    break;
  }
  showLivesBox();
}

screens::WindowFraction DeathScene::partnerPoint() const {
  const assets::DigimonRow* row =
      species_ != nullptr ? species_->find(session_->partnerView().species) : nullptr;
  glm::vec3 middle = scene_->field().partner().position;
  if (row != nullptr) {
    middle.y += static_cast<float>(row->height) * PartnerMiddle;
  }
  const std::optional<glm::vec2> fraction = scene_->worldToWindowFraction(middle);
  if (!fraction) {
    return WindowMiddle;
  }
  return {.x = fraction->x * Percent, .y = fraction->y * Percent};
}

void DeathScene::showLivesBox() {
  if (const std::optional<std::int64_t> sound = livesBox_.takeSound()) {
    if (game::story::SoundPort* port = session_->sound()) {
      port->playSound(BoxSoundSlot, *sound);
    }
  }
  if (ui_ == nullptr) {
    return;
  }
  if (!livesBox_.shown()) {
    ui_->hide(LivesBoxDocument);
    return;
  }
  screens::publish(*ui_, livesBox_.settings(), screens::LivesBoxRevisionSetting);
  ui_->show(LivesBoxDocument);
}

void DeathScene::stepLostLife() {
  // MURD_tickScene (murd.c:188), with MURD_renderIris drawn over the field until it is closed.
  const game::partner::LostLifeScene& rules = rules_.lostLife;
  showIris(LostLifeIrisEnd);
  stage_.prims = iris_;
  showStage();
  if (reached(rules.irisFrames)) {
    // Phase 1: the partner alone on black (MURD_setOtherEntitiesVisible), and the lives box over it
    // (MURD_createLivesBox) with a slot per life the partner starts with.
    stage_.sceneryHidden = true;
    showStage();
    core::logDebug("[port:death] MURD_createLivesBox (murd.c:416): lives={}", lives_);
    livesBox_.open(partnerPoint(), lives_, session_->partnerRules().life.lives);
  }
  switch (phase_) {
  case Phase::LostLifeAlone:
    if (frame_ < rules.aloneUntil) {
      break;
    }
    if (lives_ == 0) {
      // Phase 2 with no life left: the box shrinks back to the partner (MURD_animateLivesBoxOut),
      // phase 0x64 waits for it to go, and in phase 0x65 the dissolve goes on from the lost life's
      // scene, the partner already gone.
      if (livesBox_.shown()) {
        if (!livesBox_.closing()) {
          core::logDebug("[port:death] MURD_animateLivesBoxOut (murd.c:438)");
          livesBox_.close(partnerPoint());
        }
        break;
      }
      startDissolve(false);
      break;
    }
    setPhase(Phase::LostLifeFadeOut);
    break;
  case Phase::LostLifeFadeOut:
    setShade(lerpFrames(0, rules.shadeMax, rules.aloneUntil, rules.fadeOutEnd, frame_));
    if (frame_ >= rules.fadeOutEnd) {
      // Phase 3 ends taking the box away (MURD_removeLivesBox).
      livesBox_.remove();
      // Phases 4 and 5: black, and the tamer wakes up on the wake map.
      setShade(rules.shadeHold);
      enterWakeMap();
      setPhase(Phase::LostLifeFadeIn);
    }
    break;
  case Phase::LostLifeFadeIn:
    if (frame_ < rules.mapBack) {
      setShade(rules.shadeHold);
      break;
    }
    if (reached(rules.mapBack)) {
      stage_ = {};
      showStage();
    }
    setShade(lerpFrames(rules.shadeMax, 0, rules.mapBack, rules.fadeInEnd, frame_));
    if (frame_ >= rules.fadeInEnd) {
      finish();
    }
    break;
  default:
    break;
  }
}

void DeathScene::startDissolve(bool livesLeft) {
  // DOOA_tick (dooa.c:1699): a partner with lives starts on frame 0 with its alive clip; one with
  // none starts at the map frame, already gone.
  const game::partner::DeathScene& rules = rules_.death;
  session_->useSceneSounds(rules.soundBank);
  // partnerTickDying (partner.c:664): the fishing stops while DOOA_tick runs.
  if (fishing_ != nullptr) {
    fishing_->setFishingDisabled();
  }
  setDeathMap();
  effects_.start(FirstEffectSeed + effectSeed_++);
  lives_ = livesLeft ? 1 : 0;
  if (livesLeft) {
    frame_ = 0;
    scene_->playPartnerClip(rules.clips.alive);
  } else {
    frame_ = rules.mapFrame;
    stage_.partnerHidden = true;
    showStage();
  }
  setPhase(Phase::Dissolve);
}

void DeathScene::stepDissolve() {
  // DOOA_tickDissolve (dooa.c:303) with DOOA_renderDissolve's iris and the shard effect's objects.
  const game::partner::DeathScene& rules = rules_.death;
  game::field::Field& field = scene_->field();
  // A partner with lives reaches the map frame from 0; one without starts on it and moves on first
  // (dooa.c:341: the check runs after the frame is counted).
  if (frame_ == (lives_ > 0 ? rules.mapFrame : rules.mapFrame + 1)) {
    // Phase 0 ends: the wake map, and the partner alone on black, still where it stood and seen
    // from where the old map's camera stood.
    const render::MapCamera& map = scene_->mapCamera();
    startView_ = {.eye = map.desc().eye,
                  .target = map.desc().target,
                  .projection = map.projection(),
                  .focalLength = map.desc().focalLength};
    savedFeet_ = field.partner().position;
    savedYaw_ = field.partner().yaw;
    enterWakeMap();
    field.placeAt(field.partnerEntity(), savedFeet_, savedYaw_);
    scene_->playPartnerClip(lives_ > 0 ? rules.clips.alive : rules.clips.lastLife);
    // dooa.c:401: DOOA_setOtherEntitiesVisible(0), setMapLayerEnabled(0) and
    // DOOA_hideAllButPartner: the player and every NPC off the map (the partner goes at the vanish
    // frame), whoever was on it remembered for the rebirth.
    stage_ = {};
    stage_.sceneryHidden = true;
    stage_.camera = render::Camera{
        .view = glm::lookAt(startView_.eye, startView_.target, glm::vec3{0.0F, 1.0F, 0.0F}),
        .projection = startView_.projection,
        .viewport = {}};
    cameraFocal_ = startView_.focalLength;
    showStage();
  }
  // Phase 1: the camera comes round to the partner.
  moveCamera(savedFeet_, savedYaw_, CameraInFrom, CameraInTo, frame_);
  if (reached(rules.standFrame)) {
    // Phase 2: the partner stands at the wake map's origin facing the player, and the camera is put
    // round it, seen from the wake map's view.
    // The camera starts from the wake map's view, but the picture's centre is not the map's: the
    // original puts it at 0xA0, 0x78, or 0xBC, 0x78 on the story map (dooa.c: DOOA_tickDissolve
    // phase 0, MAIN_D_80135350 and MAIN_D_80135354).
    const render::MapCamera& map = scene_->mapCamera();
    startView_ = {.eye = map.desc().eye,
                  .target = map.desc().target,
                  .projection = centredProjection(map.desc().focalLength,
                                                  rules_.wakeMap.startOffsetX(deathMap_), 0.0F),
                  .focalLength = map.desc().focalLength};
    savedFeet_ = glm::vec3{0.0F};
    const glm::vec3 toPlayer = field.player().position - savedFeet_;
    sceneYaw_ = assets::yawOfDirection(toPlayer);
    field.placeAt(field.partnerEntity(), savedFeet_, sceneYaw_);
    moveCamera(savedFeet_, sceneYaw_, 0, CameraSetTo, CameraSetAt);
    if (lives_ > 0) {
      scene_->playPartnerClip(rules.clips.burst);
    }
  }
  if (reached(rules.burst.frame)) {
    playSound(rules.burst.sound);
  }
  if (reached(BreakSoundStop)) {
    stopSounds();
  }
  if (reached(rules.sink.frame)) {
    stopSounds();
    playSound(rules.sink.sound);
  }
  if (frame_ > rules.sink.frame && frame_ < rules.vanish.frame) {
    squash();
  }
  if (reached(rules.vanish.frame)) {
    scene_->playPartnerClip(rules.clips.vanish);
    stage_.partnerHidden = true;
    stage_.partnerScale = glm::vec3{1.0F};
    field.placeAt(field.partnerEntity(), savedFeet_, sceneYaw_);
    showStage();
    playSound(rules.vanish.sound);
  }
  if (reached(rules.rise.frame)) {
    playSound(rules.rise.sound);
  }
  if (reached(rules.hostFrame)) {
    // dooa.c: DOOA_tickDissolve phase 0x66: ENTITY_TABLE[2]->isOnMap = 1, the wake map's first
    // character (the elder in his house) on black with the partner's remains.
    stage_.shownNpc = wakeHost();
    showStage();
  }
  // Phase 3: the camera goes back to the wake map's view, which it then is.
  moveCamera(savedFeet_, sceneYaw_, CameraBackFrom, CameraBackTo, CameraBackMirror - frame_);
  if (frame_ > CameraBackTo) {
    stage_.camera.reset();
  }
  effects_.dissolveStep(frame_, scene_->partnerBody(),
                        {.feet = field.partner().position, .height = partnerHeight()});
  effects_.tickObjects();
  if (lives_ > 0) {
    showIris(DissolveIrisEnd);
  } else {
    iris_.clear();
  }
  showEffects();
  if (frame_ >= rules.endFrame) {
    stopSounds();
    // partnerTickDying (partner.c:673): DOOA_tick is over; the fishing comes back.
    if (fishing_ != nullptr) {
      fishing_->setFishingEnabled();
    }
  }
  if (frame_ >= rules.endFrame && storyScene_) {
    // partnerTickDying ends: the story's death script runs on the wake map (the egg box) and asks
    // for the rebirth (partnerTickDying2); the partner stays gone on black until then.
    core::logDebug("[port:death] DOOA_tick (dooa.c:1699): the death is over, the story goes on");
    sceneOver_ = true;
    setPhase(Phase::AwaitRebirth);
    return;
  }
  if (frame_ >= rules.endFrame) {
    // Without the story's death script, which runs between the two in the original, the rebirth
    // follows at once.
    core::logDebug("[port:death] DOOA_getSequenceState (dooa.c:1745): rebirth as {}", baby_);
    frame_ = 0;
    setPhase(Phase::Rebirth);
  }
}

void DeathScene::stepRebirth() {
  // DOOA_tickRebirth (dooa.c:1020) with DOOA_renderRebirth: the shards spiral up and close into the
  // egg, which fills in, flashes and breaks around the new baby (doo2.c).
  const game::partner::DeathScene& rules = rules_.death;
  game::field::Field& field = scene_->field();
  if (reached(1) && baby_.empty()) {
    // The story's rebirth (partnerTickDying2) has already made the partner the new baby; its egg
    // is the baby's place among the rebirth's babies.
    baby_ = session_->partnerView().species;
    const std::vector<game::partner::RebirthBaby>& babies =
        session_->partnerRules().life.rebirth.babies;
    const auto found = std::ranges::find(babies, baby_, &game::partner::RebirthBaby::digimon);
    eggSlot_ = found != babies.end() ? static_cast<std::int32_t>(found - babies.begin()) : 0;
  }
  if (reached(rules.glow.frame)) {
    stopSounds();
    playSound(rules.glow.sound);
  }
  if (reached(rules.hatch.frame)) {
    playSound(rules.hatch.sound);
  }
  if (reached(rules.swapFrame)) {
    // reincarnatePartner: the new baby at the map's origin; the scenery and the player come back
    // from black.
    core::logDebug("[port:death] reincarnatePartner (dooa.c:1103): draw {}", baby_);
    if (!baby_.empty()) {
      if (auto shown = scene_->showPartnerSpecies(baby_); !shown) {
        core::logWarning("{}", core::formatMessage(messages::PartnerModelNotShown, baby_,
                                                   shown.error().message));
      }
    }
    const glm::vec3 toPlayer = field.player().position;
    field.placeAt(field.partnerEntity(), glm::vec3{0.0F}, assets::yawOfDirection(toPlayer));
    // dooa.c:1108: DOOA_setOtherEntitiesVisible(1), ENTITY_TABLE[2] on, DOOA_showPlayerAndPartner
    // and then the partner off again (the egg stands for it until shownFrame): everyone who was on
    // the map before the death, the player included, is drawn again.
    stage_ = {};
    stage_.partnerHidden = true;
  }
  if (frame_ >= rules.swapFrame && frame_ < rules.shownFrame) {
    // fadeoutCLUT1 and the models' palettes from black (255) to their colours; the egg's light is
    // not faded.
    stage_.sceneryDarkness =
        static_cast<float>(lerpFrames(static_cast<std::int32_t>(PaletteLevelMax), 0,
                                      rules.swapFrame, rules.shownFrame, frame_)) /
        PaletteLevelMax;
  }
  if (reached(rules.shownFrame)) {
    stage_ = {};
    scene_->playPartnerClip(rules.clips.reborn);
  }
  effects_.rebirthStep(frame_, eggSlot_, scene_->sceneModelFaces(EggModel));
  effects_.tickObjects();
  // Each egg piece that breaks plays the sparkle (DOOA_tickRebirth: playSound(8, 7)).
  for (std::int32_t piece = effects_.takePieceBreaks(); piece > 0; --piece) {
    playSound(rules.sparkleSound);
  }
  showEffects();
  if (reached(rules.finishFrame)) {
    scene_->playPartnerClip(rules.clips.finish);
    waitFrames_ = 0;
    setPhase(Phase::RebirthFinish);
  }
}

void DeathScene::setDeathMap() {
  // MURD_tick (murd.c:780, with lives left) and DOOA_tick (dooa.c:1734) pick the map when their
  // scene starts (map.c:5400 setDeathMap).
  const game::partner::WakeMap& wake = rules_.wakeMap;
  bool storyMap = false;
  if (story_ != nullptr) {
    const game::story::StoryState& state = story_->state();
    for (const std::int32_t trigger : wake.storyTriggers) {
      storyMap = storyMap || state.trigger(trigger).value_or(false);
    }
    storyMap = storyMap || std::cmp_greater_equal(state.variable(wake.storyVariable).value_or(0),
                                                  wake.storyVariableAtLeast);
  }
  deathMap_ = storyMap ? wake.storyMap : wake.earlyMap;
  core::logDebug("[port:death] setDeathMap (map.c:5400): map={} exit={}", deathMap_, wake.exit);
}

std::optional<scene::Entity> DeathScene::wakeHost() const {
  // ENTITY_TABLE[2]: the map's first character.
  const auto& npcs = scene_->map().npcs;
  if (npcs.empty()) {
    return std::nullopt;
  }
  return scene_->field().npcs().entityOf(npcs.front().scriptId);
}

void DeathScene::enterWakeMap() {
  // map.c:5420 changeToDeathMap: the map the scene picked when it started. The map's load in the
  // background (waitForDeathMapLoading) has no counterpart: the field loads it when it enters it.
  // dooa.c:357 then calls setSomeDyingState (tamer state 6, partner state 8 sub state 2, the
  // camera put): the scene or the story's script already holds the player and the camera.
  const std::int32_t exit = rules_.wakeMap.exit;
  core::logDebug("[port:death] changeToDeathMap (map.c:5420): map={} exit={}", deathMap_, exit);
  if (auto entered = scene_->enterOriginalMap(deathMap_, exit); !entered) {
    core::logWarning(
        "{}", core::formatMessage(messages::WakeMapNotEntered, deathMap_, entered.error().message));
  }
}

void DeathScene::finish() {
  core::logDebug("[port:death] scene ends at frame {}", frame_);
  stopSounds();
  effects_.clear();
  iris_.clear();
  baby_.clear();
  session_->useSceneSounds(std::nullopt);
  scene_->playPartnerClip(std::nullopt);
  stage_ = {};
  showStage();
  fade_ = 0.0F;
  livesBox_.remove();
  showLivesBox();
  // partnerTickDying2 (partner.c:810): the rebirth is over; the fishing comes back.
  if (fishing_ != nullptr && (phase_ == Phase::Rebirth || phase_ == Phase::RebirthFinish)) {
    fishing_->setFishingEnabled();
  }
  setPhase(Phase::Idle);
  sceneOver_ = sceneOver_ || storyScene_;
  storyScene_ = false;
}

} // namespace dwe::app
