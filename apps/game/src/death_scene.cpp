#include "death_scene.hpp"

#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>

#include <algorithm>
#include <array>
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
    : session_(desc.session), scene_(desc.scene), ui_(desc.ui), species_(desc.species),
      story_(desc.story), scenes_(desc.scenes), transition_(desc.transition) {}

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
      // The new baby is drawn on the rebirth's frame, not now.
      baby_ = session_->partnerView().species;
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
    showStage();
    frame_ = 0;
    setPhase(Phase::Rebirth);
    return;
  }
  if (pending.kind == SceneKind::Death && lives_ > 0) {
    // partner.c: partnerTickDying (natural death): the death's sound bank, then DOOA_tick.
    core::logDebug("[port:death] partnerTickDying (partner.c:640): natural death");
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
    port->playSound(SceneSoundSlot, sound);
  }
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
    if (scene_->partnerClipFinished() || waitFrames_ >= ClipWaitGuardFrames) {
      finish();
    }
    break;
  case Phase::Idle:
  case Phase::AwaitRebirth:
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
  // MURD_tickScene (murd.c:188). The iris is not drawn yet.
  const game::partner::LostLifeScene& rules = rules_.lostLife;
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
  setDeathMap();
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
  // DOOA_tickDissolve (dooa.c:303). The iris, the wireframe shards, the squash, the rising light,
  // the egg and the cutscene cameras are not drawn yet.
  const game::partner::DeathScene& rules = rules_.death;
  // A partner with lives reaches the map frame from 0; one without starts on it and moves on first
  // (dooa.c:341: the check runs after the frame is counted).
  if (frame_ == (lives_ > 0 ? rules.mapFrame : rules.mapFrame + 1)) {
    // Phase 0 ends: the wake map, and the partner alone on black.
    enterWakeMap();
    scene_->playPartnerClip(lives_ > 0 ? rules.clips.alive : rules.clips.lastLife);
    stage_ = {};
    stage_.sceneryHidden = true;
    showStage();
  }
  if (reached(rules.standFrame) && lives_ > 0) {
    scene_->playPartnerClip(rules.clips.burst);
  }
  if (reached(rules.burst.frame)) {
    playSound(rules.burst.sound);
  }
  if (reached(rules.sink.frame)) {
    playSound(rules.sink.sound);
  }
  if (reached(rules.vanish.frame)) {
    scene_->playPartnerClip(rules.clips.vanish);
    stage_.partnerHidden = true;
    showStage();
    playSound(rules.vanish.sound);
  }
  if (reached(rules.rise.frame)) {
    playSound(rules.rise.sound);
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
  // DOOA_tickRebirth (dooa.c:1020). The egg's shards, flashes and sparkles are not drawn yet.
  const game::partner::DeathScene& rules = rules_.death;
  if (reached(rules.glow.frame)) {
    playSound(rules.glow.sound);
  }
  if (reached(rules.hatch.frame)) {
    playSound(rules.hatch.sound);
  }
  if (reached(rules.swapFrame)) {
    // reincarnatePartner: the new baby; the scenery and the tamer come back from black.
    core::logDebug("[port:death] reincarnatePartner (dooa.c:1103): draw {}", baby_);
    if (!baby_.empty()) {
      if (auto shown = scene_->showPartnerSpecies(baby_); !shown) {
        core::logWarning("{}", core::formatMessage(messages::PartnerModelNotShown, baby_,
                                                   shown.error().message));
      }
    }
    stage_ = {};
    stage_.partnerHidden = true;
    showStage();
  }
  if (frame_ >= rules.swapFrame && frame_ < rules.shownFrame) {
    // fadeoutCLUT1 and the models' palettes from black (255) to their colours.
    fade_ = static_cast<float>(lerpFrames(static_cast<std::int32_t>(PaletteLevelMax), 0,
                                          rules.swapFrame, rules.shownFrame, frame_)) /
            PaletteLevelMax;
  }
  if (reached(rules.shownFrame)) {
    fade_ = 0.0F;
    stage_ = {};
    showStage();
    scene_->playPartnerClip(rules.clips.reborn);
    playSound(rules.sparkleSound);
  }
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
  session_->useSceneSounds(std::nullopt);
  scene_->playPartnerClip(std::nullopt);
  stage_ = {};
  showStage();
  fade_ = 0.0F;
  livesBox_.remove();
  showLivesBox();
  setPhase(Phase::Idle);
  sceneOver_ = sceneOver_ || storyScene_;
  storyScene_ = false;
}

} // namespace dwe::app
