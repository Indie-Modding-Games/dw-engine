#include <dwe/assets/map_document.hpp>
#include <dwe/core/log.hpp>
#include <dwe/game/story/script_motion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <string>
#include <type_traits>
#include <utility>

#include "motion_messages.hpp"

namespace dwe::game::story {

namespace messages = motion_messages;

namespace {

// wait_for_entity's ids beyond the characters (script_interp.c:1332-1393): every slot, the camera,
// the slots of the map's objects, and the partner's sync.
constexpr std::int64_t WaitAll = 0xFF;
constexpr std::int64_t WaitCamera = 0xC8;
constexpr std::int64_t WaitPartnerSync = 0xC9;
constexpr std::int64_t WaitFirstObject = 0xCA;
constexpr std::int64_t WaitLastObject = 0xD4;
constexpr std::int64_t FirstObjectSlot = 11;
// move_object_to's slots start here (script_interp.c: SCRIPT_OP_MOVE_OBJECT_TO adds 0xc).
constexpr std::int64_t FirstSpriteSlot = 12;
constexpr std::int64_t WaitClearedPartnerSync = 0xD5;
// The training overlay's load: the engine has no overlays, so it is always loaded.
constexpr std::int64_t WaitTrainingLoaded = 0xD6;

// The slots below this are the characters', which go back to their first animation when a turn or
// a walk ends (script_interp.c: tickScriptedMovement).
constexpr std::int64_t FirstNonCharacterSlot = 10;
constexpr std::int64_t RestingAnimation = 0;

constexpr float Turn = 2.0F * std::numbers::pi_v<float>;

// A point of the original's ground (x, z) in world space, at the height `y`.
glm::vec3 groundPoint(std::int64_t x, std::int64_t z, float y) {
  const glm::vec3 world =
      core::fromOriginalAxes({static_cast<float>(x), 0.0F, static_cast<float>(z)});
  return {world.x, y, world.z};
}

// The walk clip of a walk animation: the player's walks start at clip 2, the others' at clip 2 in
// steps of 2 (script_interp.c: scriptSetEntityWalking).
std::int64_t walkClip(std::int64_t character, std::int64_t animation) {
  return character == PlayerScriptId ? animation + 2 : (animation + 1) * 2;
}

// The low byte of `value` read as a signed byte, as the scripts keep some operands.
std::int64_t signedByte(std::int64_t value) {
  constexpr std::int64_t ByteMask = 0xFF;
  constexpr std::int64_t SignBit = 0x80;
  constexpr std::int64_t ByteRange = 0x100;
  const std::int64_t low = value & ByteMask;
  return low >= SignBit ? low - ByteRange : low;
}

// A division as the original's processor gives it: by zero it gives -1, or 1 for a negative
// dividend, as its div does without a check (overworld.c: tickMoveObjectTo has none).
std::int32_t divideLikeOriginal(std::int32_t dividend, std::int32_t divisor) {
  if (divisor == 0) {
    return dividend >= 0 ? -1 : 1;
  }
  return dividend / divisor;
}

// One coordinate of tickMoveObjectTo's step: it stops at the target once past it, and a zero step
// lands on it.
std::int32_t spriteStep(std::int32_t value, std::int32_t target, std::int32_t step) {
  const std::int32_t next = value + step;
  if (step > 0) {
    return next >= target ? target : next;
  }
  if (step < 0) {
    return next <= target ? target : next;
  }
  return target;
}

// The world axis and sign of the original's axis (0 x, 1 y, 2 z).
glm::vec3 worldAxis(std::int64_t axis) {
  constexpr std::int64_t AxisY = 1;
  constexpr std::int64_t AxisZ = 2;
  if (axis == AxisY) {
    return {0.0F, 1.0F, 0.0F};
  }
  if (axis == AxisZ) {
    return {0.0F, 0.0F, 1.0F};
  }
  return {1.0F, 0.0F, 0.0F};
}

// Whether a coordinate moving by `step` reached `target`, as tickEntityMoveTo tests it: a step
// down ends at or below the target, any other at or above (tamer.c:1150-1170).
bool reached(float value, float target, float step) {
  return step < 0.0F ? value <= target : value >= target;
}

// The yaw `turn` closer to `wanted`, the short way (overworld.c: getRotationDifference,
// rotateEntity); nothing once it gets there.
std::optional<float> turnToward(float yaw, float wanted, float turn) {
  const float difference = std::remainder(wanted - yaw, Turn);
  if (std::abs(difference) <= turn) {
    return std::nullopt;
  }
  return yaw + (difference < 0.0F ? -turn : turn);
}

} // namespace

ScriptMotion::ScriptMotion(ScriptActorsPort& actors, MotionRules rules)
    : actors_{&actors}, rules_{rules} {}

std::optional<std::int64_t> ScriptMotion::slotFor(std::string_view order,
                                                  std::int64_t character) const {
  const std::optional<std::int64_t> slot = actors_->slotOf(character);
  if (!slot) {
    core::logDebug("{}", messages::formatMessage(messages::NotOnMap, order, character));
  }
  return slot;
}

void ScriptMotion::fill(std::int64_t slot, std::int64_t character, Move move) {
  core::logDebug("{}",
                 messages::formatMessage(messages::SlotFilled, move.index(), slot, character));
  slots_.insert_or_assign(slot,
                          Slot{.character = character, .move = move, .cameraFrom = std::nullopt});
}

void ScriptMotion::lookAt(std::int64_t character, std::int64_t target) {
  if (const auto slot = slotFor("entity_look_at_entity", character)) {
    fill(*slot, character, LookAt{.target = target, .point = std::nullopt});
  }
}

void ScriptMotion::setRotation(std::int64_t character, std::int64_t rotation) {
  if (const auto slot = slotFor("entity_set_rotation", character)) {
    fill(*slot, character,
         SetRotation{.yaw = assets::yawFromOriginal(static_cast<std::int32_t>(rotation))});
  }
}

void ScriptMotion::walk(std::string_view order, std::int64_t character,
                        std::optional<std::int64_t> target, glm::vec3 point, std::int64_t animation,
                        bool withCamera) {
  const auto slot = slotFor(order, character);
  if (!slot) {
    return;
  }
  const std::int64_t clip = walkClip(character, animation);
  actors_->animate(character, clip);
  core::logDebug("{}", messages::formatMessage(messages::WalkClip, character, animation, clip,
                                               actors_->groundSpeed(character, clip)));
  fill(*slot, character,
       Walk{.target = target, .point = point, .clip = clip, .withCamera = withCamera});
}

void ScriptMotion::walkTo(std::int64_t character, std::int64_t x, std::int64_t z,
                          std::int64_t animation) {
  walk("entity_walk_to", character, std::nullopt, groundPoint(x, z, actors_->position(character).y),
       animation, false);
}

void ScriptMotion::walkToCharacter(std::int64_t character, std::int64_t animation,
                                   std::int64_t target) {
  walk("entity_walk_to_entity", character, target, {}, animation, false);
}

void ScriptMotion::walkToWithCamera(std::int64_t character, std::int64_t x, std::int64_t z,
                                    std::int64_t animation) {
  walk("entity_walk_to_with_camera", character, std::nullopt,
       groundPoint(x, z, actors_->position(character).y), animation, true);
}

void ScriptMotion::walkToCharacterWithCamera(std::int64_t character, std::int64_t animation,
                                             std::int64_t target) {
  walk("entity_walk_to_entity_with_camera", character, target, {}, animation, true);
}

void ScriptMotion::slide(std::string_view order, std::int64_t character, MoveTo move) {
  if (const auto slot = slotFor(order, character)) {
    fill(*slot, character, move);
  }
}

void ScriptMotion::moveTo(std::int64_t character, std::int64_t x, std::int64_t z,
                          std::int64_t frames) {
  slide("entity_move_to", character,
        MoveTo{.target = std::nullopt,
               .point = groundPoint(x, z, actors_->position(character).y),
               .frames = frames,
               .perFrame = std::nullopt,
               .withCamera = false});
}

void ScriptMotion::moveToCharacter(std::int64_t character, std::int64_t target,
                                   std::int64_t frames) {
  core::logDebug("{}", messages::formatMessage(messages::MoveToCharacter, "entity_move_to_entity"));
  slide("entity_move_to_entity", character,
        MoveTo{.target = target,
               .point = {},
               .frames = frames,
               .perFrame = std::nullopt,
               .withCamera = false});
}

void ScriptMotion::moveToWithCamera(std::int64_t character, std::int64_t x, std::int64_t z,
                                    std::int64_t frames) {
  core::logDebug("{}", std::string{messages::MoveToWithCamera});
  slide("entity_move_to_with_camera", character,
        MoveTo{.target = std::nullopt,
               .point = groundPoint(x, z, actors_->position(character).y),
               .frames = frames,
               .perFrame = std::nullopt,
               .withCamera = true});
}

void ScriptMotion::moveToCharacterWithCamera(std::int64_t character, std::int64_t target,
                                             std::int64_t frames) {
  core::logDebug("{}", messages::formatMessage(messages::MoveToCharacter,
                                               "entity_move_to_entity_with_camera"));
  slide("entity_move_to_entity_with_camera", character,
        MoveTo{.target = target,
               .point = {},
               .frames = frames,
               .perFrame = std::nullopt,
               .withCamera = true});
}

void ScriptMotion::slideAlong(std::string_view order, std::int64_t character, std::int64_t position,
                              std::int64_t axis, std::int64_t frames, bool withCamera) {
  if (const auto slot = slotFor(order, character)) {
    // The original's y and z point the other way from the world's (core::fromOriginalAxes).
    const float sign = glm::dot(worldAxis(axis), core::fromOriginalAxes(worldAxis(axis)));
    fill(*slot, character,
         MoveToAxis{.axis = axis,
                    .position = sign * static_cast<float>(position),
                    .frames = frames,
                    .perFrame = std::nullopt,
                    .withCamera = withCamera});
  }
}

void ScriptMotion::moveToAxis(std::int64_t character, std::int64_t position, std::int64_t axis,
                              std::int64_t frames) {
  slideAlong("entity_move_to_axis", character, position, axis, frames, false);
}

void ScriptMotion::moveToAxisWithCamera(std::int64_t character, std::int64_t position,
                                        std::int64_t axis, std::int64_t frames) {
  slideAlong("entity_move_to_axis_with_camera", character, position, axis, frames, true);
}

void ScriptMotion::resetOrigin(std::int64_t character) {
  core::logDebug("{}", messages::formatMessage(messages::ResetOrigin, character));
  actors_->resetOrigin(character);
}

void ScriptMotion::moveMapSprite(std::int64_t slot, std::int64_t object, std::int64_t steps,
                                 std::int64_t x, std::int64_t y) {
  // The steps are a signed byte (overworld.c: tickMoveObjectTo).
  const std::int64_t signedSteps = signedByte(steps);
  fill(FirstSpriteSlot + slot, object,
       SpriteMove{.target = {static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)},
                  .steps = signedSteps,
                  .perFrame = std::nullopt});
}

void ScriptMotion::turnMapObject(std::int64_t object, std::int64_t direction) {
  // script_interp.c: SCRIPT_OP_ROTATE_3D_OBJECT keeps the direction as a signed byte.
  const std::int64_t signedDirection = signedByte(direction);
  fill(FirstObjectSlot, object, MapObjectTurn{.direction = signedDirection});
}

void ScriptMotion::cameraTo(std::int64_t frames, std::int64_t x, std::int64_t z) {
  // map.c: tickCameraMoveTo aims at the player's height.
  const float y = actors_->position(PlayerScriptId).y;
  fill(CameraSlot, PlayerScriptId,
       CameraPan{.target = std::nullopt, .point = groundPoint(x, z, y), .frames = frames});
}

void ScriptMotion::cameraToCharacter(std::int64_t character, std::int64_t frames) {
  fill(CameraSlot, character, CameraPan{.target = character, .point = {}, .frames = frames});
}

void ScriptMotion::playAnimation(std::int64_t character, std::int64_t animation) {
  core::logDebug("{}", messages::formatMessage(messages::Animate, character, animation));
  if (actors_->slotOf(character)) {
    actors_->animate(character, animation);
  }
}

std::function<bool()> ScriptMotion::waitFor(std::int64_t what) {
  std::int64_t slot = 0;
  if (what == WaitAll) {
    core::logDebug("{}", messages::formatMessage(messages::WaitFor, what, "every slot"));
    return [this] { return partnerSynced() && slots_.empty(); };
  }
  if (what == WaitPartnerSync || what == WaitClearedPartnerSync) {
    if (what == WaitClearedPartnerSync) {
      partnerSync_ = false;
    }
    core::logDebug("{}", messages::formatMessage(messages::WaitFor, what, "the partner's sync"));
    return [this] { return partnerSynced(); };
  }
  if (what == WaitTrainingLoaded) {
    return [] { return true; };
  }
  if (what == WaitCamera) {
    slot = CameraSlot;
  } else if (what >= WaitFirstObject && what <= WaitLastObject) {
    slot = what - WaitFirstObject + FirstObjectSlot;
  } else if (const auto character = actors_->slotOf(what)) {
    slot = *character;
  } else {
    core::logWarning("{}", messages::formatMessage(messages::WaitNoCharacter, what));
    return [] { return true; };
  }
  core::logDebug("{}", messages::formatMessage(messages::WaitFor, what, slot));
  return [this, slot] { return !slots_.contains(slot); };
}

bool ScriptMotion::partnerSynced() const {
  if (!partnerSync_ && !syncConnected_) {
    core::logWarning("{}", std::string{messages::SyncUnconnected});
    return true;
  }
  return partnerSync_;
}

void ScriptMotion::scriptBegins() {
  core::logDebug("{}", messages::formatMessage(messages::SlotsCleared, slots_.size()));
  // The original leaves a character it stops here walking on its animation until the script lets
  // go; the engine stops it, as it has no root motion to keep.
  for (const auto& [index, slot] : slots_) {
    if (index < FirstNonCharacterSlot && actors_->slotOf(slot.character)) {
      actors_->drive(slot.character, {});
    }
  }
  slots_.clear();
  partnerSync_ = true;
}

void ScriptMotion::scriptFrame() {
  // Each slot in order, as tickScriptedMovements does; a slot a move fills while ticking waits
  // for the next frame.
  for (auto it = slots_.begin(); it != slots_.end();) {
    if (!tick(it->second)) {
      ++it;
      continue;
    }
    core::logDebug("{}",
                   messages::formatMessage(messages::SlotDone, it->first, it->second.character));
    const bool resting = it->first < FirstNonCharacterSlot &&
                         !std::holds_alternative<MoveTo>(it->second.move) &&
                         !std::holds_alternative<MoveToAxis>(it->second.move);
    if (resting && actors_->slotOf(it->second.character)) {
      actors_->animate(it->second.character, RestingAnimation);
    }
    it = slots_.erase(it);
  }
}

void ScriptMotion::talkFrozen(std::int64_t talker) {
  const std::optional<std::int64_t> slot = actors_->slotOf(talker);
  if (!slot) {
    return;
  }
  core::logDebug("{}", messages::formatMessage(messages::TalkFacing, talker));
  fill(*slot, talker, LookAt{.target = PlayerScriptId, .point = std::nullopt});
  fill(0, PlayerScriptId, LookAt{.target = talker, .point = std::nullopt});
}

bool ScriptMotion::tick(Slot& slot) {
  if (const auto* turn = std::get_if<MapObjectTurn>(&slot.move)) {
    return actors_->turnMapObject(slot.character, turn->direction);
  }
  if (auto* sprite = std::get_if<SpriteMove>(&slot.move)) {
    return tick(slot.character, *sprite);
  }
  if (!std::holds_alternative<CameraPan>(slot.move) && !actors_->slotOf(slot.character)) {
    return true;
  }
  followWithCamera(slot);
  return std::visit(
      [&](auto& move) {
        using Kind = std::decay_t<decltype(move)>;
        if constexpr (std::is_same_v<Kind, SetRotation>) {
          // tamer.c: tickEntitySetRotation.
          actors_->turn(slot.character, move.yaw);
          return true;
        } else if constexpr (std::is_same_v<Kind, CameraPan>) {
          return tick(move);
        } else if constexpr (std::is_same_v<Kind, MapObjectTurn> ||
                             std::is_same_v<Kind, SpriteMove>) {
          return true;
        } else {
          return tick(slot.character, move);
        }
      },
      slot.move);
}

void ScriptMotion::followWithCamera(Slot& slot) {
  const bool withCamera = std::visit(
      [](const auto& move) {
        if constexpr (requires { move.withCamera; }) {
          return move.withCamera;
        } else {
          return false;
        }
      },
      slot.move);
  if (!withCamera) {
    return;
  }
  const glm::vec3 position = actors_->position(slot.character);
  if (slot.cameraFrom) {
    actors_->shiftCamera(position - *slot.cameraFrom);
  }
  slot.cameraFrom = position;
}

bool ScriptMotion::tick(std::int64_t character, LookAt& move) {
  // tamer.c: tickLookAtEntity takes the target's place on the first frame and turns from the next.
  if (!move.point) {
    if (!actors_->slotOf(move.target)) {
      core::logWarning("{}", messages::formatMessage(messages::TargetGone, character, move.target));
      return true;
    }
    move.point = actors_->position(move.target);
    return false;
  }
  const glm::vec3 position = actors_->position(character);
  const float wanted = assets::yawOfDirection(*move.point - position);
  const std::optional<float> next =
      turnToward(actors_->yaw(character), wanted, rules_.lookTurnPerFrame);
  actors_->turn(character, next.value_or(wanted));
  return !next.has_value();
}

bool ScriptMotion::tick(std::int64_t character, const Walk& move) {
  // tamer.c: tickEntityWalkTo faces the target every frame and ends on its cell; walking to a
  // character also ends on touching one. The walk animation moves the character meanwhile.
  glm::vec3 point = move.point;
  if (move.target) {
    if (!actors_->slotOf(*move.target)) {
      core::logWarning("{}",
                       messages::formatMessage(messages::TargetGone, character, *move.target));
      actors_->drive(character, {});
      return true;
    }
    point = actors_->position(*move.target);
  }
  const glm::vec3 position = actors_->position(character);
  const float speed = actors_->groundSpeed(character, move.clip);
  glm::vec3 toward = point - position;
  toward.y = 0.0F;
  const float yaw =
      glm::length(toward) > 0.0F ? assets::yawOfDirection(toward) : actors_->yaw(character);
  actors_->turn(character, yaw);
  // getModelTile's cells, as tickEntityWalkTo compares them: a point on a cell edge is in the cell
  // away from 0, where the walk overshoots to.
  const bool onCell = assets::originalCellAt(position) == assets::originalCellAt(point);
  const bool touching =
      move.target && actors_->wouldTouchCharacter(character, speed * rules_.frameSeconds);
  if (onCell || touching) {
    actors_->drive(character, {});
    return true;
  }
  actors_->drive(character, assets::forwardOfYaw(yaw) * speed);
  return false;
}

bool ScriptMotion::tick(std::int64_t character, MoveTo& move) {
  // tamer.c: tickEntityMoveTo takes the step on the first frame and moves from the next; each axis
  // stops at the target once it gets there.
  const glm::vec3 position = actors_->position(character);
  if (!move.perFrame) {
    if (move.target) {
      // tamer.c: tickEntityMoveTo aims at where the target stands on the first frame.
      if (!actors_->slotOf(*move.target)) {
        core::logWarning("{}",
                         messages::formatMessage(messages::TargetGone, character, *move.target));
        return true;
      }
      move.point = actors_->position(*move.target);
      move.point.y = position.y;
    }
    glm::vec3 step = (move.point - position) / frames(move.frames);
    step.y = 0.0F;
    move.perFrame = step;
    actors_->drive(character, step / rules_.frameSeconds);
    return false;
  }
  const glm::vec3 step = *move.perFrame;
  const bool doneX = reached(position.x, move.point.x, step.x);
  const bool doneZ = reached(position.z, move.point.z, step.z);
  glm::vec3 clamped = position;
  glm::vec3 velocity = step / rules_.frameSeconds;
  if (doneX) {
    clamped.x = move.point.x;
    velocity.x = 0.0F;
  }
  if (doneZ) {
    clamped.z = move.point.z;
    velocity.z = 0.0F;
  }
  if (doneX || doneZ) {
    actors_->place(character, clamped);
  }
  actors_->drive(character, velocity);
  return doneX && doneZ;
}

bool ScriptMotion::tick(std::int64_t character, MoveToAxis& move) {
  // tamer.c: tickEntityMoveToAxis, the same along one axis.
  const glm::vec3 axis = worldAxis(move.axis);
  const glm::vec3 position = actors_->position(character);
  const float value = glm::dot(position, axis);
  if (!move.perFrame) {
    move.perFrame = (move.position - value) / frames(move.frames);
    actors_->drive(character, axis * (*move.perFrame / rules_.frameSeconds));
    return false;
  }
  if (!reached(value, move.position, *move.perFrame)) {
    return false;
  }
  actors_->place(character, position + axis * (move.position - value));
  actors_->drive(character, {});
  return true;
}

bool ScriptMotion::tick(CameraPan& move) {
  // map.c: checkCameraMovement starts the pan on the first frame and reports it done on the frame
  // after it gets there; the camera's target is taken when it starts (tickCameraMoveToEntity).
  if (move.started) {
    return !actors_->cameraPanning();
  }
  glm::vec3 point = move.point;
  if (move.target) {
    if (!actors_->slotOf(*move.target)) {
      core::logWarning("{}",
                       messages::formatMessage(messages::TargetGone, CameraSlot, *move.target));
      return true;
    }
    point = actors_->position(*move.target);
  }
  actors_->panCamera(point, frames(move.frames) * rules_.frameSeconds);
  move.started = true;
  return false;
}

bool ScriptMotion::tick(std::int64_t object, SpriteMove& move) {
  // overworld.c: tickMoveObjectTo takes the step and moves on the same frame, and ends once both
  // coordinates are on the target.
  const std::optional<glm::ivec2> position = actors_->mapSpritePosition(object);
  if (!position) {
    core::logWarning("{}", messages::formatMessage(messages::NoMapSprite, object));
    return true;
  }
  if (!move.perFrame) {
    const auto steps = static_cast<std::int32_t>(move.steps);
    move.perFrame = glm::ivec2{divideLikeOriginal(move.target.x - position->x, steps),
                               divideLikeOriginal(move.target.y - position->y, steps)};
  }
  const glm::ivec2 next{spriteStep(position->x, move.target.x, move.perFrame->x),
                        spriteStep(position->y, move.target.y, move.perFrame->y)};
  actors_->placeMapSprite(object, next);
  return next == move.target;
}

float ScriptMotion::frames(std::int64_t count) {
  if (count > 0) {
    return static_cast<float>(count);
  }
  core::logWarning("{}", messages::formatMessage(messages::NoFrames, "move", count));
  return 1.0F;
}

} // namespace dwe::game::story
