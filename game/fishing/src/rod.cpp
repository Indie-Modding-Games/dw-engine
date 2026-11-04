#include <dwe/core/log.hpp>
#include <dwe/game/fishing/fishing.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include "fishing_math.hpp"

// The rod, the bobber and the line (fish.c: FISH_tickRod, FISH_tickBobberPhysics,
// FISH_updateTension).
namespace dwe::game::fishing {

namespace {

// The player's animations while fishing (FISH_tickRod: startAnimation).
constexpr std::int32_t StandAnimation = 0;
constexpr std::int32_t TakeRodAnimation = 0x1b;
constexpr std::int32_t HoldRodAnimation = 0x1c;
constexpr std::int32_t WindUpAnimation = 0x1d;
constexpr std::int32_t ThrowAnimation = 0x1e;
constexpr std::int32_t RestAnimation = 0x1f;
constexpr std::int32_t ReelAnimation = 0x20;
constexpr std::int32_t FightRestAnimation = 0x21;
constexpr std::int32_t FightReelAnimation = 0x22;

// FISH_bobberCommand: back to the hand, into the water, a nibble, a bite (small and big fish), and
// the hook lost.
constexpr std::int32_t ToHandCommand = 4;
constexpr std::int32_t SinkCommand = 7;
constexpr std::int32_t NibbleCommand = 2;
constexpr std::int32_t BiteCommand = 0xb;
constexpr std::int32_t BigNibbleCommand = 0xc;
constexpr std::int32_t LostCommand = 0xe;
// FISH_bobberCommand: floating, the bobber bobs (ripple -2) one frame in 60.
constexpr std::int32_t BobEvery = 60;
constexpr std::int32_t BobRipple = -2;
// The ripples of those commands (FISH_spawnRipple's kinds) and the one that scares the fish.
constexpr std::int32_t SinkRipple = -1;
constexpr std::int32_t NibbleRipple = -3;
constexpr std::int32_t BiteRipple = -4;
constexpr float SinkScare = 100.0F;
constexpr float BaitScare = 100.0F;
constexpr float LandedScare = 1.0F;
constexpr std::int32_t BaitSplashScale = 75;
constexpr std::int32_t LandedSplashScale = 10;

// FISH_tickRod: the player turns 0x40 a frame; the camera button throws at full power.
constexpr std::int32_t TurnStep = 0x40;
// The bobber swings from the rod before the throw, then flies (FISH_tickBobberPhysics: the drag
// is 300/4096 of the speed a frame).
constexpr float SwingGravity = 30.0F;
constexpr float FlightGravity = 3.0F;
constexpr float Drag = 300.0F / static_cast<float>(FixedOne);
// FISH_tickBaitSprite: the bait hangs this far below the bobber along the line.
constexpr float BaitBelowBobber = 100.0F;
// FISH_moveBaitToRodTip: a catch flies to the hand this fast.
constexpr float CatchToHandSpeed = 40.0F;
// Reeling while fighting: the rod's pull doubled, less 1 to 10 by the fish's size, and half of it
// moves the bobber.
constexpr float FightPullScale = 2.0F;
constexpr Range FightPullLoss{.min = 1, .max = 10};
constexpr float FightPullShare = 0.5F;
// The messages after a lost line wait this many frames.
constexpr std::int32_t LostMessageFrame = 10;
// FISH_tickRod, state 0x11AF: the sea serpent is ashore within this distance of its target.
constexpr float SerpentAshore = 50.0F;
// The tension level a fish pulls at, by whether it runs and whether the player reels.
constexpr std::int32_t HeldShift = 2;
// FISH_tickRod's camera: the view follows the bobber 16 pixels a frame and lets it wander 40
// across and 30 up or down from the centre; it centres the player at the same speed; it follows
// a landed catch 32 a frame; the d-pad moves it 5 a frame (FISH_panCameraWithDPad).
constexpr std::int32_t PanRadius = 0x10;
constexpr std::int32_t CatchPanRadius = 0x20;
constexpr glm::ivec2 BobberDeadZone{0x28, 0x1E};
constexpr std::int32_t DPadStep = 5;

} // namespace

Fishing::Fishing(FishingRules rules, FishingTables tables, FishingPlace place)
    : rules_{rules}, tables_{std::move(tables)}, place_{std::move(place)} {}

void Fishing::emit(FishingEventKind kind, std::int32_t value) {
  out_.events.push_back({.kind = kind, .value = value});
}

void Fishing::emitAt(FishingEventKind kind, std::int32_t value, glm::vec3 position) {
  FishingEvent event{.kind = kind, .value = value};
  event.position = position;
  out_.events.push_back(event);
}

void Fishing::sound(std::int32_t sound) {
  emit(FishingEventKind::Sound, sound);
}

void Fishing::animate(std::int32_t animation) {
  emit(FishingEventKind::Animation, animation);
}

void Fishing::message(FishingMessage message) {
  // FISH_showTextBox's messages scroll the view to the bobber while they show.
  emitAt(FishingEventKind::Message, static_cast<std::int32_t>(message), bobber_.position);
}

std::optional<glm::vec3> Fishing::bait() const {
  if (!baitShown_) {
    return std::nullopt;
  }
  return baitPosition_;
}

bool Fishing::withinCastRange(glm::vec3 position, std::int32_t extra) const {
  return groundDistance(input_->player, position) <= static_cast<float>(place_.castRange + extra);
}

void Fishing::start(Rod rod) {
  rod_ = rod;
  core::logDebug("[port:fishing] FISH_loadFishing (fish.c:4551): the {} rod",
                 rod == Rod::Amazing ? "amazing" : "old");
  bobberShown_ = false;
  lineShown_ = false;
  baitShown_ = false;
  bobber_ = {};
  // FISH_clearBobberAnim.
  bobberAnimation_.reset();
  if (hooked_) {
    hookedEscaped_ = true;
    hooked_->state = FishState::Escaping;
    hooked_->yaw += std::numbers::pi_v<float>;
  }
  toState(RodState::ToPlayer);
}

void Fishing::chooseBait(std::optional<std::int32_t> bait) {
  baitMenuOpen_ = false;
  baitChosen_ = bait;
}

FishingOutput Fishing::tick(const FishingInput& input, const FishingView& view,
                            ruleset::Random& random) {
  input_ = &input;
  view_ = &view;
  random_ = &random;
  out_ = {};
  previousLineStart_ = lineStart_;
  lineStart_ = input.lineStart;
  if (std::exchange(throwFromTip_, false) && state_ == RodState::Flying) {
    bobber_.position += lineStart_ - previousLineStart_;
  }
  if (!disabled_) {
    if (state_ != RodState::Off) {
      if (!camera_) {
        // FISH_enableCameraControl: the view stays where the field left it until the rod moves it.
        camera_ = view.viewCentre();
      }
      tickRod();
    }
    tickWater();
  }
  ++frame_;
  input_ = nullptr;
  view_ = nullptr;
  random_ = nullptr;
  return std::exchange(out_, {});
}

void Fishing::toState(RodState state) {
  state_ = state;
  framesOnState_ = -1;
}

void Fishing::giveUp() {
  if (!input_->hasBait) {
    toState(RodState::Ending);
    return;
  }
  animate(StandAnimation);
  bobberShown_ = false;
  baitShown_ = false;
  // FISH_initBobberAnim.
  bobberAnimation_.reset();
  lineShown_ = false;
  bobber_.mode = 0;
  toState(RodState::ToPlayer);
}

void Fishing::cast() {
  bobber_.position = input_->bobberRest;
  if (baitEaten_) {
    giveUp();
    return;
  }
  animate(HoldRodAnimation);
  bobberCommand(ToHandCommand);
  bobber_.mode = 1;
  lineShown_ = true;
  baitShown_ = true;
  moveBaitToHand(0.0F);
  toState(RodState::ToBobber);
}

void Fishing::release() {
  animate(ThrowAnimation);
  const std::int32_t power = framesOnState_ + rules_.cast.powerBonus;
  bobber_.mode = 3;
  bobber_.position = lineStart_;
  throwFromTip_ = true;
  bobber_.lineLength = 0.0F;
  bobber_.gravity = FlightGravity;
  const std::int32_t speed = remapClamped(
      power, {.min = 0, .max = rules_.cast.powerMax},
      {.min = rules_.cast.minSpeed,
       .max = rod_ == Rod::Amazing ? rules_.cast.amazingRodSpeed : rules_.cast.oldRodSpeed});
  glm::vec2 facing{input_->playerFacing.x, input_->playerFacing.z};
  facing = glm::length(facing) > 0.0F ? glm::normalize(facing) : glm::vec2{0.0F, -1.0F};
  bobber_.velocity = glm::vec3{facing.x, 0.0F, facing.y} * static_cast<float>(speed);
  core::logDebug("[port:fishing] FISH_tickRod release (fish.c:3866): power {} speed {}", power,
                 speed);
  sound(rules_.sounds.cast);
  toState(RodState::Flying);
}

void Fishing::splash() {
  baitShown_ = false;
  bobber_.position.y = 0.0F;
  if (!onWater(bobber_.position)) {
    cast();
    return;
  }
  sound(rules_.sounds.splash);
  bobberCommand(SinkCommand);
  bobber_.mode = 0;
  toState(RodState::Waiting);
}

void Fishing::reelBobber(float scale, float minus, float share) {
  const glm::vec3 toTip = lineStart_ - bobber_.position;
  const glm::vec2 heading{toTip.x, toTip.z};
  if (glm::length(heading) <= 0.0F) {
    return;
  }
  const glm::vec2 direction = glm::normalize(heading);
  const glm::vec3 moved = lineStart_ - previousLineStart_;
  const float pull = (std::trunc(glm::dot(glm::vec2{moved.x, moved.z}, direction)) * scale) - minus;
  if (pull > 0.0F) {
    bobber_.position += glm::vec3{direction.x, 0.0F, direction.y} * std::trunc(pull * share);
  }
}

void Fishing::checkBobber() {
  if (!onWater(bobber_.position) || withinCastRange(bobber_.position, 0)) {
    cast();
    return;
  }
  // With the camera button held the d-pad moves the view and Cancel waits.
  if (input_->cameraHeld) {
    panWithDPad();
    return;
  }
  panToBobber();
  if (input_->cancelPressed) {
    cast();
  }
}

void Fishing::waiting() {
  const FishingInput& in = *input_;
  if (in.confirmHeld) {
    if (in.confirmPressed || !in.animationPlaying) {
      animate(ReelAnimation);
    }
    reelBobber(1.0F, 0.0F, 1.0F);
    const JunkRules& junk = rules_.junk;
    if (baitEaten_ && !tables_.junk.empty() && framesOnState_ >= junk.after && junk.every != 0 &&
        framesOnState_ % junk.every == 0 && roll(1, junk.roll) <= junk.chance) {
      const std::int32_t r = roll(0, junk.pick);
      const auto found =
          std::ranges::find_if(tables_.junk, [r](const JunkItem& j) { return r <= j.upTo; });
      bait_ = found == tables_.junk.end() ? tables_.junk.back().item : found->item;
      core::logDebug("[port:fishing] FISH_tickRod (fish.c:3984): item {} got stuck on the hook",
                     bait_);
      sound(rules_.sounds.lost);
      message(FishingMessage::Snagged);
      toState(RodState::Snagged);
      return;
    }
  } else {
    if (in.confirmReleased || !in.animationPlaying) {
      animate(RestAnimation);
    }
    if (!baitEaten_) {
      if (const std::optional<std::size_t> fish = findFishToBite()) {
        hookFish(*fish);
        toState(RodState::Hooked);
        return;
      }
    }
  }
  checkBobber();
}

std::int32_t Fishing::updateTension() {
  const TensionRules& rules = rules_.tension;
  const std::int32_t size = hooked_ ? hooked_->size : rules.sizes.min;
  const bool running = hooked_ && hooked_->state == FishState::Running;
  const std::int32_t pull = (pressedFrames_ > 0 ? HeldShift : 0) + (running ? 1 : 0);
  const std::array<Range, 4> deltas{rules.slack, rules.fishRunning, rules.held, rules.heldRunning};
  tension_.level += remapClamped(size, rules.sizes, deltas[static_cast<std::size_t>(pull)]);
  tension_.level = std::clamp(tension_.level, -tension_.limit, tension_.limit);
  tension_.color = tensionColor(tension_.level, tension_.limit);
  if (tension_.level == -tension_.limit) {
    return 2;
  }
  if (tension_.level == tension_.limit) {
    const std::int32_t steps =
        rules.snapSteps.max + 1 - remapClamped(size, rules.sizes, rules.snapSteps);
    if (++tension_.framesAtLimit == steps * rules.snapFramesPerStep) {
      return 1;
    }
  } else {
    tension_.framesAtLimit = 0;
  }
  return 0;
}

void Fishing::reeling() {
  const FishingInput& in = *input_;
  if (!hooked_) {
    tension_ = {};
    splash();
    return;
  }
  const std::int32_t hookedSize = hooked_->size;
  if (in.confirmHeld) {
    pressedFrames_ = std::max(pressedFrames_, 0) + 1;
    if (in.confirmPressed || !in.animationPlaying) {
      animate(FightReelAnimation);
    }
    reelBobber(FightPullScale,
               static_cast<float>(remapClamped(hookedSize, rules_.tension.sizes, FightPullLoss)),
               FightPullShare);
  } else {
    pressedFrames_ = std::min(pressedFrames_, 0) - 1;
    if (in.confirmReleased) {
      animate(FightRestAnimation);
    }
  }
  const std::optional<glm::vec2> screen = visibleScreen(bobber_.position);
  if ((screen && !isWaterOnScreen(*screen)) || withinCastRange(bobber_.position, hookedSize)) {
    landed();
    return;
  }
  switch (updateTension()) {
  case 1:
    hookedEscapes();
    tension_ = {};
    sound(rules_.sounds.lost);
    lineShown_ = false;
    bobberCommand(LostCommand);
    toState(RodState::LineCut);
    return;
  case 2:
    hookedEscapes();
    tension_ = {};
    sound(rules_.sounds.lost);
    lineShown_ = true;
    bobberCommand(LostCommand);
    toState(RodState::HookLost);
    return;
  default:
    break;
  }
  if (in.cancelPressed) {
    hookedEscapes();
    tension_ = {};
    splash();
    return;
  }
  if (in.cameraHeld) {
    panWithDPad();
  } else {
    panToBobber();
  }
}

void Fishing::landed() {
  tension_ = {};
  if (!hooked_) {
    return;
  }
  if (hooked_->kind == SeaSerpentKind) {
    hooked_->state = FishState::Landing;
    hooked_->framesOnState = -1;
    toState(RodState::SeaSerpent);
    return;
  }
  caughtKind_ = hooked_->kind;
  caughtSize_ = hooked_->size;
  bait_ = tables_.kinds[static_cast<std::size_t>(caughtKind_)].item;
  hooked_.reset();
  core::logDebug("[port:fishing] FISH_tickRod landed (fish.c:4166): kind {} size {}", caughtKind_,
                 caughtSize_);
  sound(rules_.sounds.landed);
  baitShown_ = true;
  baitPosition_ = bobber_.position;
  spawnRipple(0, bobber_.position, LandedScare, 0, place_.splash * LandedSplashScale);
  bobber_.mode = 0;
  toState(RodState::Landing);
}

void Fishing::showCatch() {
  moveBaitToHand(0.0F);
  FishingEvent event{.kind = FishingEventKind::FishCaught,
                     .value = bait_,
                     .fishKind = caughtKind_,
                     .size = caughtSize_};
  event.position = baitPosition_;
  out_.events.push_back(event);
  sound(rules_.sounds.item);
  finish();
}

void Fishing::finish() {
  animate(HoldRodAnimation);
  bobberCommand(ToHandCommand);
  bobber_.mode = 1;
  lineShown_ = true;
  out_.playerRotation = 0;
  baitShown_ = true;
  moveBaitToHand(0.0F);
  toState(RodState::Ending);
}

void Fishing::endFishing() {
  animate(StandAnimation);
  state_ = RodState::Off;
  lineShown_ = false;
  bobberShown_ = false;
  baitShown_ = false;
  baitMenuOpen_ = false;
  tension_ = {};
  bobberAnimation_.reset();
  // FISH_endFishing: FISH_applySavedCameraScroll and setCameraFollowPlayer. The field's camera
  // centres the player, who has not moved, which is where the view was when the rod came out.
  camera_.reset();
  emit(FishingEventKind::Ended);
}

bool Fishing::panTo(glm::vec3 world, std::int32_t radius, glm::ivec2 deadZone) {
  if (!camera_) {
    return false;
  }
  const std::optional<glm::vec2> screen = view_->toScreen(world);
  if (!screen) {
    return false;
  }
  const glm::ivec2 target{static_cast<std::int32_t>(std::lround(screen->x)),
                          static_cast<std::int32_t>(std::lround(screen->y))};
  const std::optional<glm::ivec2> next =
      panCameraTo(*camera_, target, view_->viewRange(), radius, deadZone);
  if (!next) {
    return false;
  }
  camera_ = *next;
  return true;
}

bool Fishing::panToBobber() {
  return panTo(bobber_.position, PanRadius, BobberDeadZone);
}

// The pan back to the player (fish.c:1013): the feet in the middle, no dead zone.
bool Fishing::panToPlayer() {
  return panTo(input_->player, PanRadius, glm::ivec2{0});
}

void Fishing::panWithDPad() {
  if (!camera_) {
    return;
  }
  const glm::ivec2 direction{(input_->turnRight ? 1 : 0) - (input_->turnLeft ? 1 : 0),
                             (input_->lookDown ? 1 : 0) - (input_->lookUp ? 1 : 0)};
  if (const std::optional<glm::ivec2> next =
          panCameraWithDPad(*camera_, direction, view_->viewRange(), DPadStep)) {
    camera_ = *next;
  }
}

void Fishing::scrollToNotice(glm::vec3 target, const FishingView& view) {
  if (!camera_) {
    return;
  }
  if (const std::optional<glm::vec2> screen = view.toScreen(target)) {
    const glm::ivec2 point{static_cast<std::int32_t>(std::lround(screen->x)),
                           static_cast<std::int32_t>(std::lround(screen->y))};
    if (const std::optional<glm::ivec2> next =
            panCameraTo(*camera_, point, view.viewRange(), PanRadius, glm::ivec2{0})) {
      camera_ = *next;
    }
  }
}

void Fishing::tickRod() {
  const FishingInput& in = *input_;
  const std::int32_t turn = (in.turnLeft ? -TurnStep : 0) + (in.turnRight ? TurnStep : 0);
  switch (state_) {
  case RodState::ToPlayer:
    if (!panToPlayer()) {
      toState(RodState::ChoosingBait);
    }
    break;
  case RodState::ChoosingBait:
    if (framesOnState_ == 0) {
      baitMenuOpen_ = true;
      baitChosen_.reset();
      emit(FishingEventKind::OpenBaitMenu);
      break;
    }
    if (baitMenuOpen_) {
      break;
    }
    if (!baitChosen_ || !*baitChosen_) {
      toState(RodState::Ending);
      break;
    }
    bait_ = **baitChosen_;
    baitEaten_ = false;
    bobberShown_ = false;
    animate(TakeRodAnimation);
    toState(RodState::Readying);
    break;
  case RodState::Readying:
    out_.playerTurn += turn;
    if (!in.animationPlaying) {
      bobberShown_ = true;
      cast();
    }
    break;
  case RodState::ToBobber:
    if (!panToBobber()) {
      toState(RodState::Aiming);
    }
    break;
  case RodState::Aiming:
    moveBaitToHand(0.0F);
    if (in.cameraHeld) {
      panWithDPad();
      break;
    }
    if (in.confirmPressed) {
      animate(WindUpAnimation);
      bobber_.mode = 2;
      bobber_.velocity = glm::vec3{0.0F};
      bobber_.lineLength = glm::length(bobber_.position - lineStart_);
      bobber_.gravity = SwingGravity;
      toState(RodState::WindingUp);
      break;
    }
    if (in.cancelPressed) {
      baitShown_ = false;
      toState(RodState::Ending);
      break;
    }
    out_.playerTurn += turn;
    panToBobber();
    break;
  case RodState::WindingUp:
    if (in.confirmPressed) {
      release();
      break;
    }
    if (in.cameraPressed) {
      framesOnState_ = rules_.cast.powerMax;
      release();
      break;
    }
    panToBobber();
    if (!in.animationPlaying) {
      cast();
    }
    break;
  case RodState::Flying:
    // The view jumps to keep the flying bobber in sight, then the d-pad may move it.
    panTo(bobber_.position, 0, BobberDeadZone);
    if (in.cameraHeld) {
      panWithDPad();
    } else {
      panToBobber();
    }
    if (in.confirmPressed) {
      bobber_.velocity.x = 0.0F;
      bobber_.velocity.z = 0.0F;
    }
    if (bobber_.position.y > 0.0F) {
      if (in.cancelPressed) {
        cast();
      }
      break;
    }
    splash();
    break;
  case RodState::Waiting:
    waiting();
    break;
  case RodState::Hooked:
    panToBobber();
    if (hookedEscaped_ || !hooked_) {
      message(FishingMessage::BaitTaken);
      splash();
      break;
    }
    if (in.confirmPressed) {
      if (!tryStrike()) {
        message(baitEaten_ ? FishingMessage::TooLate : FishingMessage::TooSoon);
        splash();
        break;
      }
      message(FishingMessage::Hooked);
      tension_ = {.shown = true,
                  .limit = rod_ == Rod::Amazing ? rules_.tension.amazingRodLimit
                                                : rules_.tension.oldRodLimit};
      pressedFrames_ = 0;
      toState(RodState::Reeling);
      break;
    }
    if (in.cancelPressed) {
      hookedEscapes();
      splash();
    }
    break;
  case RodState::Reeling:
    reeling();
    break;
  case RodState::Landing: {
    const bool moving = moveBaitToHand(CatchToHandSpeed);
    bobber_.position = baitPosition_;
    const bool panning = panTo(bobber_.position, CatchPanRadius, BobberDeadZone);
    if (!moving && !panning) {
      showCatch();
    }
    break;
  }
  case RodState::Ending:
    if (!panToPlayer()) {
      endFishing();
    }
    break;
  case RodState::LineCut:
  case RodState::HookLost:
    if (framesOnState_ == LostMessageFrame) {
      sound(rules_.sounds.notice);
      if (state_ == RodState::LineCut) {
        message(FishingMessage::LineCut);
        toState(RodState::Ending);
      } else {
        message(FishingMessage::HookLost);
        splash();
      }
    }
    break;
  case RodState::Snagged: {
    baitShown_ = true;
    moveBaitToHand(0.0F);
    FishingEvent event{.kind = FishingEventKind::JunkCaught, .value = bait_};
    event.position = baitPosition_;
    out_.events.push_back(event);
    sound(rules_.sounds.item);
    finish();
    break;
  }
  case RodState::SeaSerpent:
    if (panToPlayer()) {
      break;
    }
    if (hooked_ && groundDistance(glm::vec3{0.0F}, hooked_->position) >= SerpentAshore) {
      break;
    }
    {
      const glm::vec3 ashore = hooked_ ? hooked_->position : bobber_.position;
      core::logDebug("[port:fishing] FISH_tamerTick (fish.c:4618): the sea serpent is ashore");
      hooked_.reset();
      endFishing();
      disabled_ = true;
      emitAt(FishingEventKind::SeaSerpentLanded, rules_.seaSerpent.sceneSection, ashore);
    }
    return;
  case RodState::Off:
    return;
  }
  ++framesOnState_;
  tickBobber();
}

bool Fishing::moveBaitToHand(float maxStep) {
  const glm::vec3 target = input_->baitHold;
  const glm::vec3 offset = target - baitPosition_;
  const float distance = glm::length(offset);
  if (maxStep > 0.0F && maxStep < distance) {
    baitPosition_ += offset / distance * maxStep;
    return true;
  }
  baitPosition_ = target;
  return false;
}

void Fishing::bobberCommand(std::int32_t command) {
  emit(FishingEventKind::Bobber, command);
  switch (command) {
  case ToHandCommand:
    bobber_.position = input_->bobberRest;
    bobberAnimation_.command(BobberAnimation::Command::ToHand);
    break;
  case SinkCommand:
    spawnRipple(SinkRipple, bobber_.position, SinkScare, 0, FixedOne);
    bobberAnimation_.command(BobberAnimation::Command::Sink);
    break;
  case NibbleCommand:
    sound(rules_.sounds.nibble);
    spawnRipple(NibbleRipple, bobber_.position, 0.0F, 0, FixedOne);
    bobberAnimation_.command(BobberAnimation::Command::Nibble);
    break;
  case BiteCommand:
  case BigNibbleCommand:
    // The original sends both to the bite's dip (its big nibble's own state is never entered).
    sound(rules_.sounds.bite);
    spawnRipple(BiteRipple, bobber_.position, 0.0F, 0, FixedOne);
    bobberAnimation_.command(BobberAnimation::Command::Bite);
    break;
  case LostCommand:
    bobberAnimation_.command(BobberAnimation::Command::Lost);
    break;
  default:
    break;
  }
}

void Fishing::tickBobberAnimation() {
  bool bobs = false;
  if (bobberAnimation_.floating()) {
    // The bobber drifts a unit now and then, and rings the water once in a while.
    if (roll(1, 2) == 1) {
      bobber_.position.x += static_cast<float>(roll(-1, 1));
      bobber_.position.z += static_cast<float>(roll(-1, 1));
    }
    bobs = roll(1, BobEvery) == 1;
    if (bobs) {
      spawnRipple(BobRipple, bobber_.position, 0.0F, 0, FixedOne);
    }
  }
  bobberAnimation_.tick(bobs);
}

void Fishing::tickBait(glm::vec3 previous) {
  if (!baitShown_) {
    return;
  }
  const glm::vec3 alongLine = bobber_.position - lineStart_;
  const float length = glm::length(alongLine);
  baitPosition_ =
      bobber_.position + (length > 0.0F ? alongLine / length * BaitBelowBobber : glm::vec3{0.0F});
  // The bait dips into the water while the bobber flies: a ripple that scares the fish.
  if (previous.y <= 0.0F || baitPosition_.y > 0.0F || state_ != RodState::Flying) {
    return;
  }
  if (onWater({baitPosition_.x, 0.0F, baitPosition_.z})) {
    spawnRipple(0, baitPosition_, BaitScare, 0, place_.splash * BaitSplashScale);
  }
}

void Fishing::tickBobber() {
  tickBobberAnimation();
  Bobber& b = bobber_;
  if (b.mode == 1) {
    b.position = input_->bobberRest;
    return;
  }
  if (b.mode < 2) {
    return;
  }
  const glm::vec3 old = b.position;
  for (int axis = 0; axis < 3; ++axis) {
    float& v = b.velocity[axis];
    const float slow = std::trunc(v * Drag);
    // At rest the drag still takes one unit toward zero.
    float step = slow;
    if (step == 0.0F && v != 0.0F) {
      step = v > 0.0F ? 1.0F : -1.0F;
    }
    v -= step;
  }
  b.velocity.y -= b.gravity;
  const auto keepOnLine = [&] {
    const glm::vec3 fromTip = b.position - lineStart_;
    const float length = glm::length(fromTip);
    if (b.lineLength > 0.0F && length > b.lineLength) {
      b.position = lineStart_ + (fromTip / length * b.lineLength);
      return true;
    }
    return false;
  };
  if (keepOnLine()) {
    b.velocity += b.position - old;
  }
  b.position += b.velocity;
  keepOnLine();
  if (!visibleScreen({b.position.x, 0.0F, b.position.z})) {
    b.position.x = old.x;
    b.position.z = old.z;
    b.velocity.x = 0.0F;
    b.velocity.z = 0.0F;
  }
  tickBait(baitPosition_);
}

} // namespace dwe::game::fishing
