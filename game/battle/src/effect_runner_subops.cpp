#include <dwe/game/battle/effect_runner.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "effect_math.hpp"

// The sub-ops of the effect scripts that move, turn and draw (battle_effect.c and efe.c), as
// EffectRunner members: the arguments come in push order.
namespace dwe::game::battle {

namespace {

namespace fx = effect_math;

using assets::EffectWidth;

constexpr std::int32_t FullCircle512 = 0x1FF;
constexpr std::int32_t Quarter512 = 0x80;
constexpr std::int32_t AngleMask = 0xFFF;
constexpr std::int32_t QuarterTurn = 0x400;
constexpr std::int32_t HalfTurn = 0x800;
constexpr std::int32_t FullTurn = 0x1000;
constexpr std::int32_t FixedShift = 12;
constexpr std::int32_t ByteMask = 0xFF;
// EfeInstance: the frame, then the transform's position and rotation.
constexpr std::int32_t PositionOffset = 4;
constexpr std::int32_t RotationOffset = 0x10;
constexpr std::int32_t VectorBytes = 12;
constexpr std::int32_t Int32Bytes = 4;
constexpr std::int32_t PairBytes = 8;
constexpr std::int32_t Int16Bytes = 2;
// BTL_setTransformToSourceBone moves the point 25 units forward.
constexpr std::int32_t SourceBoneForward = 25;
// BTL_renderCenteredSprite: no turn, semi-transparent with rate 1 (add), the first palette row.
constexpr std::int32_t CenteredAttribute = 5;
// GsSPRITE attribute bits 28 to 31 as the scripts give them (attribute << 28): the rate, the
// semi-transparency switch and the display-off bit.
constexpr std::int32_t RateMask = 3;
constexpr std::int32_t SemiTransparentBit = 4;
constexpr std::int32_t HiddenBit = 8;
// The scripts' -1 for "no keyframes" (BTL_initializeUVAnim).
constexpr std::int32_t NoKeyframes = -1;
// The frame of a free instance.
constexpr std::int32_t FreeInstance = -1;
// EfeUvKeyframe: s16 frames, s8 du, s8 dv, after an s32 count.
constexpr std::int32_t KeyframeBytes = 4;

EffectBlend blendOf(std::int32_t attribute) {
  if ((attribute & SemiTransparentBit) == 0) {
    return EffectBlend::Opaque;
  }
  return static_cast<EffectBlend>(attribute & RateMask);
}

// The sign-extended angle from -2048 to 2048 the steering sub-ops turn by.
std::int32_t centred(std::int32_t angle) {
  angle &= AngleMask;
  return angle >= HalfTurn + 1 ? angle - FullTurn : angle;
}

// Turns toward `wanted`, at most `limit` either way.
std::int32_t limited(std::int32_t wanted, std::int32_t limit) {
  if (wanted > 0) {
    return wanted < limit ? wanted : limit;
  }
  return -wanted < limit ? wanted : -limit;
}

// The ground direction of an EfeVector after turning it -yaw about y (RotMatrixZYX with only y).
EffectVector intoYaw(const EffectVector& d, std::int32_t yaw) {
  return fx::toShort(
      fx::apply(fx::rotZYX({.x = 0, .y = fx::toShort(-yaw), .z = 0}), fx::toShort(d)));
}

} // namespace

std::optional<EffectBody> EffectRunner::bodyOf(std::optional<FighterId> fighter) const {
  if (bodies_ == nullptr || !fighter) {
    return std::nullopt;
  }
  return bodies_->body(*fighter);
}

std::optional<EffectMatrix> EffectRunner::nodeOf(std::optional<FighterId> fighter,
                                                 std::int32_t index) const {
  if (bodies_ == nullptr || !fighter) {
    return std::nullopt;
  }
  if (std::optional<EffectMatrix> node = bodies_->node(*fighter, index)) {
    return node;
  }
  // A fighter the app does not draw: its place on the ground.
  const std::optional<EffectBody> body = bodies_->body(*fighter);
  if (!body) {
    return std::nullopt;
  }
  EffectMatrix place;
  place.t = body->location;
  return place;
}

std::int32_t EffectRunner::instanceAddress(std::int32_t offset) const {
  return current_ ? static_cast<std::int32_t>(*current_) + offset : -1;
}

EffectVector EffectRunner::vectorAt(const Slot& slot, std::int32_t address) const {
  return {.x = read(slot, address, EffectWidth::Int32),
          .y = read(slot, address + Int32Bytes, EffectWidth::Int32),
          .z = read(slot, address + PairBytes, EffectWidth::Int32)};
}

EffectVector EffectRunner::instancePosition(const Slot& slot) const {
  return vectorAt(slot, instanceAddress(PositionOffset));
}

void EffectRunner::setVector(Slot& slot, std::int32_t address, const EffectVector& value) const {
  write(slot, address, EffectWidth::Int32, value.x);
  write(slot, address + Int32Bytes, EffectWidth::Int32, value.y);
  write(slot, address + PairBytes, EffectWidth::Int32, value.z);
}

// The sub-ops that place and turn the running instance (EfeInstance.transform).
bool EffectRunner::transformOp(Slot& slot, std::string_view name,
                               const std::vector<std::int32_t>& args) {
  const std::int32_t position = instanceAddress(PositionOffset);
  const std::int32_t rotation = instanceAddress(RotationOffset);
  SubEffect& self = context(slot);
  const auto rotationY = [&](std::optional<FighterId> fighter) {
    const std::optional<EffectBody> body = bodyOf(fighter);
    return body ? body->rotationY : 0;
  };
  // BTL_setTransformToBoneOffset, BTL_setTransformToBoneMatrix: the source's bone of the
  // bone-offset entry, with the entry's offset in the bone's space.
  const auto boneOffsetPoint = [&]() -> std::pair<EffectVector, EffectMatrix> {
    const assets::EffectBoneOffset entry = self.boneOffset.value_or(assets::EffectBoneOffset{});
    const EffectMatrix m = nodeOf(self.source, entry.bone).value_or(EffectMatrix{});
    EffectVector at = fx::toShort(
        fx::apply(m, {.x = entry.offset[0], .y = entry.offset[1], .z = entry.offset[2]}));
    at = {.x = at.x + m.t.x, .y = at.y + m.t.y, .z = at.z + m.t.z};
    return {at, m};
  };
  // BTL_setTransformToSourceBone, BTL_setTransformToTargetBone: a node's point, moved `forward`
  // along the fighter's facing, facing as the fighter does.
  const auto nodeForward = [&](std::optional<FighterId> fighter, std::int32_t node,
                               std::int32_t forward) {
    const EffectVector t = nodeOf(fighter, node).value_or(EffectMatrix{}).t;
    const std::int32_t yaw = rotationY(fighter);
    const std::int32_t s1 = fx::sin512(-(yaw >> 3));
    const std::int32_t s2 = fx::sin512(-(((yaw >> 3) + Quarter512) & FullCircle512));
    setVector(slot, position,
              {.x = t.x + ((forward * s1) >> FixedShift),
               .y = t.y,
               .z = t.z + ((forward * s2) >> FixedShift)});
    setVector(slot, rotation, {.x = 0, .y = yaw, .z = 0});
  };

  if (name == "init_transform") {
    // battle_effect.c: BTL_initializeEFETransform: the parent's transform, or the bone offset's.
    if (previous_) {
      const auto parent = static_cast<std::int32_t>(*previous_);
      setVector(slot, position, vectorAt(slot, parent + PositionOffset));
      setVector(slot, rotation, vectorAt(slot, parent + RotationOffset));
    } else {
      setVector(slot, position, boneOffsetPoint().first);
      setVector(slot, rotation, {.x = 0, .y = rotationY(self.source), .z = 0});
    }
  } else if (name == "set_transform_to_bone_offset") {
    setVector(slot, position, boneOffsetPoint().first);
    setVector(slot, rotation, {.x = 0, .y = rotationY(self.source), .z = 0});
  } else if (name == "set_transform_to_bone_matrix") {
    const auto [at, m] = boneOffsetPoint();
    setVector(slot, position, at);
    setVector(slot, rotation, fx::toShort(fx::eulerYXZ(m)));
  } else if (name == "set_transform_to_source_bone") {
    nodeForward(self.source, args[0], SourceBoneForward);
  } else if (name == "set_transform_to_target_bone") {
    nodeForward(self.target, args[0], args[1]);
  } else if (name == "copy_from_parent" || name == "copy_to_parent") {
    // BTL_copyFromParentTransform, BTL_copyToParentTransform: bytes between the instance's data
    // and its parent's (the first sizes only the latter knows).
    if (!previous_) {
      return true;
    }
    const bool fromParent = name == "copy_from_parent";
    const std::int32_t parent = static_cast<std::int32_t>(*previous_) + args[1];
    const std::int32_t source = fromParent ? parent : args[0];
    const std::int32_t destination = fromParent ? args[0] : parent;
    const std::int32_t size = args[2];
    if (size == VectorBytes || size == Int32Bytes || (size == PairBytes && !fromParent)) {
      for (std::int32_t at = 0; at < size; at += Int32Bytes) {
        write(slot, destination + at, EffectWidth::Int32,
              read(slot, source + at, EffectWidth::Int32));
      }
    } else if (size == Int16Bytes || size == 1) {
      const auto width = size == 1 ? EffectWidth::Int8 : EffectWidth::Int16;
      write(slot, destination, width, read(slot, source, width));
    }
  } else if (name == "face_point") {
    // BTL_rotateTransformTowardPoint.
    const EffectVector target = vectorAt(slot, args[0]);
    const EffectVector at = vectorAt(slot, position);
    const std::int32_t angle = fx::atan4096(target.x - at.x, target.z - at.z) - QuarterTurn;
    write(slot, rotation + Int32Bytes, EffectWidth::Int32, (-angle) & AngleMask);
  } else if (name == "steer_toward_point") {
    // BTL_steerTransformTowardPoint: turn at most max_turn toward the point on the ground, then
    // move speed units along the facing.
    const EffectVector target = vectorAt(slot, args[0]);
    EffectVector at = vectorAt(slot, position);
    EffectVector turn = vectorAt(slot, rotation);
    const EffectVector local =
        intoYaw({.x = target.x - at.x, .y = 0, .z = target.z - at.z}, turn.y);
    const std::int32_t wanted = centred(-(fx::atan4096(local.x, local.z) - QuarterTurn));
    turn.y += limited(wanted, args[2]);
    write(slot, rotation + Int32Bytes, EffectWidth::Int32, turn.y);
    const EffectVector step = fx::toShort(
        fx::apply(fx::rotZYX(fx::toShort(turn)), {.x = 0, .y = 0, .z = fx::toShort(-args[1] * 8)}));
    at = {.x = at.x + (step.x >> 3), .y = at.y + (step.y >> 3), .z = at.z + (step.z >> 3)};
    setVector(slot, position, at);
  } else if (name == "home_toward_point") {
    homeTowardPoint(slot, args);
  } else if (name == "get_scattered_spawn_position") {
    // BTL_getScatteredSpawnPosition: the bone of the bone offset (without a parent) or the
    // parent's position, scattered by up to half the range either way.
    EffectVector out;
    if (previous_) {
      out = vectorAt(slot, static_cast<std::int32_t>(*previous_) + PositionOffset);
    } else {
      const std::int16_t bone = self.boneOffset ? self.boneOffset->bone : std::int16_t{0};
      out = nodeOf(self.source, bone).value_or(EffectMatrix{}).t;
    }
    if (const std::int32_t range = args[1]; range > 0) {
      const auto scatter = [&] {
        return static_cast<std::int32_t>(randomDraw() % static_cast<std::uint32_t>(range)) -
               (range >> 1);
      };
      out.x += scatter();
      out.y += scatter();
      out.z += scatter();
    }
    setVector(slot, args[0], out);
  } else if (name == "center_on_entities") {
    // BTL_centerTransformOnEntities: the mean of the other fighters' points.
    EffectVector sum;
    std::int32_t count = 0;
    if (bodies_ != nullptr) {
      for (const FighterId fighter : bodies_->fighters()) {
        if (self.source == fighter) {
          continue;
        }
        if (const std::optional<EffectMatrix> m = bodies_->node(fighter, 0)) {
          sum = {.x = sum.x + m->t.x, .y = sum.y + m->t.y, .z = sum.z + m->t.z};
          ++count;
        }
      }
    }
    if (count > 0) {
      sum = {.x = sum.x / count, .y = sum.y / count, .z = sum.z / count};
    }
    setVector(slot, position, sum);
    setVector(slot, rotation, {});
  } else if (name == "polar_offset") {
    // BTL_calculatePolarOffset: x and z of a point at a radius and angle (512 per turn).
    const std::int32_t angle = args[1];
    const std::int32_t radius = args[2];
    write(slot, args[0] + PairBytes, EffectWidth::Int32,
          (radius * fx::sin512(Quarter512 - angle)) >> FixedShift);
    write(slot, args[0], EffectWidth::Int32,
          (radius * fx::cos512(Quarter512 - angle)) >> FixedShift);
  } else {
    return false;
  }
  return true;
}

// battle_effect.c: BTL_applyHomingMovement: turn the yaw and the pitch toward the point at most
// max_turn each, speed up while on course (130%) and slow down while turning (82%), then move.
void EffectRunner::homeTowardPoint(Slot& slot, const std::vector<std::int32_t>& args) {
  const std::int32_t position = instanceAddress(PositionOffset);
  const std::int32_t rotation = instanceAddress(RotationOffset);
  const EffectVector target = vectorAt(slot, args[0]);
  const std::int32_t minSpeed = args[1];
  const std::int32_t maxSpeed = args[2];
  const std::int32_t speedAt = args[4];
  const std::int32_t limit = args[5];
  EffectVector at = vectorAt(slot, position);
  EffectVector turn = vectorAt(slot, rotation);
  const EffectVector local =
      intoYaw({.x = target.x - at.x, .y = target.y - at.y, .z = target.z - at.z}, turn.y);
  const std::int32_t yaw = limited(centred(-(fx::atan4096(local.x, local.z) - QuarterTurn)), limit);
  turn.y += yaw;
  const std::int32_t pitch =
      limited(centred(fx::atan4096(local.y, local.z) - QuarterTurn - turn.z), limit);
  turn.z += pitch;
  if (const std::int32_t wrapped = turn.z & AngleMask; wrapped >= QuarterTurn && wrapped < 0xC00) {
    turn.z = HalfTurn - turn.z;
  }
  write(slot, rotation + Int32Bytes, EffectWidth::Int32, turn.y);
  write(slot, rotation + PairBytes, EffectWidth::Int32, turn.z);
  std::int32_t speed = read(slot, speedAt, EffectWidth::Int32);
  constexpr std::int32_t OnCourse = 40;
  constexpr std::int32_t SpeedUp = 130;
  constexpr std::int32_t SlowDown = 82;
  constexpr std::int32_t Percent = 100;
  if (limit * OnCourse / Percent >= std::abs(pitch) + std::abs(yaw)) {
    speed = std::min(speed * SpeedUp / Percent, maxSpeed);
  } else {
    speed = std::max(speed * SlowDown / Percent, minSpeed);
  }
  write(slot, speedAt, EffectWidth::Int32, speed);
  EffectVector step = fx::toShort(fx::apply(fx::rotZYX({.x = fx::toShort(turn.z), .y = 0, .z = 0}),
                                            {.x = 0, .y = 0, .z = fx::toShort(-speed << 3)}));
  step = fx::toShort(fx::apply(fx::rotZYX({.x = 0, .y = fx::toShort(turn.y), .z = 0}), step));
  setVector(slot, position,
            {.x = at.x + (step.x >> 3), .y = at.y + (step.y >> 3), .z = at.z + (step.z >> 3)});
}

// The sub-ops that compute with vectors and numbers.
bool EffectRunner::vectorOp(Slot& slot, std::string_view name,
                            const std::vector<std::int32_t>& args) {
  using Combine = std::int32_t (*)(std::int32_t, std::int32_t);
  struct PairOp {
    std::string_view name;
    Combine combine;
  };
  // BTL_addVectors ... BTL_shiftVectorsRight: a = a <op> b; a division by 0 leaves a.
  static constexpr std::array<PairOp, 6> PairOps{{
      {"add_vectors", [](std::int32_t a, std::int32_t b) { return a + b; }},
      {"subtract_vectors", [](std::int32_t a, std::int32_t b) { return a - b; }},
      {"multiply_vectors",
       [](std::int32_t a, std::int32_t b) {
         return static_cast<std::int32_t>(static_cast<std::int64_t>(a) * b);
       }},
      {"divide_vectors", [](std::int32_t a, std::int32_t b) { return b == 0 ? a : a / b; }},
      {"mask_vectors", [](std::int32_t a, std::int32_t b) { return a & b; }},
      {"shift_vectors_right",
       [](std::int32_t a, std::int32_t b) { return a >> (static_cast<std::uint32_t>(b) & 31U); }},
  }};
  // BTL_multiplyVectorByScalar, BTL_divideVectorByScalar, BTL_maskVectorByScalar.
  static constexpr std::array<PairOp, 3> ScalarOps{{
      {"multiply_vector",
       [](std::int32_t a, std::int32_t b) {
         return static_cast<std::int32_t>(static_cast<std::int64_t>(a) * b);
       }},
      {"divide_vector", [](std::int32_t a, std::int32_t b) { return b == 0 ? a : a / b; }},
      {"mask_vector", [](std::int32_t a, std::int32_t b) { return a & b; }},
  }};
  const auto apply3 = [&](Combine combine, const EffectVector& a, const EffectVector& b) {
    return EffectVector{.x = combine(a.x, b.x), .y = combine(a.y, b.y), .z = combine(a.z, b.z)};
  };
  if (const auto* op = std::ranges::find(PairOps, name, &PairOp::name); op != PairOps.end()) {
    setVector(slot, args[0], apply3(op->combine, vectorAt(slot, args[0]), vectorAt(slot, args[1])));
  } else if (const auto* scalar = std::ranges::find(ScalarOps, name, &PairOp::name);
             scalar != ScalarOps.end()) {
    setVector(slot, args[0],
              apply3(scalar->combine, vectorAt(slot, args[0]),
                     {.x = args[1], .y = args[1], .z = args[1]}));
  } else if (name == "copy_vector") {
    setVector(slot, args[0], vectorAt(slot, args[1]));
  } else if (name == "rotate_vector_zyx") {
    // efe.c: EFERotateVector: the vector turned by RotMatrixZYX, both as 16-bit vectors.
    const EffectVector turn = fx::toShort(vectorAt(slot, args[0]));
    setVector(slot, args[1],
              fx::toShort(fx::apply(fx::rotZYX(turn), fx::toShort(vectorAt(slot, args[1])))));
  } else if (name == "rotate_vector_yxz") {
    // BTL_rotateVectorByAngles: RotMatrixYXZ on the 32-bit vector (ApplyMatrixLV).
    const EffectVector turn = fx::toShort(vectorAt(slot, args[0]));
    setVector(slot, args[1], fx::apply(fx::rotYXZ(turn), vectorAt(slot, args[1])));
  } else if (name == "combine_rotations") {
    setVector(slot, args[0],
              fx::toShort(fx::combineRotations(fx::toShort(vectorAt(slot, args[0])),
                                               fx::toShort(vectorAt(slot, args[1])))));
  } else if (name == "normalize_rotation" || name == "normalize_rotation_2") {
    // BTL_normalizeRotationAngles(2): YXZ angles as RotMatrix's (matrixToEuler1) or YXZ ones
    // again (matrixToEuler2).
    const EffectMatrix m = fx::rotYXZ(fx::toShort(vectorAt(slot, args[1])));
    setVector(slot, args[0],
              fx::toShort(name == "normalize_rotation" ? fx::eulerXYZ(m) : fx::eulerYXZ(m)));
  } else if (name == "get_euler_angles") {
    setVector(slot, args[0], fx::toShort(fx::toEulerAngles(vectorAt(slot, args[1]))));
  } else if (name == "vector_length") {
    write(slot, args[0], EffectWidth::Int32, fx::distance(vectorAt(slot, args[1])));
  } else if (name == "sine" || name == "cosine") {
    // BTL_calculateSine, BTL_calculateCosine: scale * rsin(angle) / 4096.
    const std::int32_t angle = args[0] & AngleMask;
    const std::int32_t trig = name == "sine" ? fx::rsin(angle) : fx::rcos(angle);
    write(slot, args[2], EffectWidth::Int32, (args[1] * trig) >> FixedShift);
  } else if (name == "interpolate_value") {
    // BTL_interpolateValue: from start to end as t goes from t0 to t1, clamped.
    std::int32_t low = args[1];
    std::int32_t high = args[2];
    if (high < low) {
      std::swap(low, high);
    }
    const std::int32_t t = std::clamp(args[3], low, high);
    const std::int32_t start = args[4];
    const std::int32_t end = args[5];
    write(slot, args[0], EffectWidth::Int32,
          low == high ? start
                      : start + static_cast<std::int32_t>(static_cast<std::int64_t>(t - low) *
                                                          (end - start) / (high - low)));
  } else if (name == "interpolate_vector") {
    // BTL_interpolateVector: lerp on each axis.
    const EffectVector from = vectorAt(slot, args[0]);
    const EffectVector to = vectorAt(slot, args[1]);
    const auto at = [&](std::int32_t a, std::int32_t b) {
      return fx::lerp(a, b, args[2], args[3], args[4]);
    };
    setVector(slot, args[5], {.x = at(from.x, to.x), .y = at(from.y, to.y), .z = at(from.z, to.z)});
  } else {
    return false;
  }
  return true;
}

// The sub-ops that draw with the slot's texture and model, and their state.
bool EffectRunner::drawOp(Slot& slot, std::string_view name,
                          const std::vector<std::int32_t>& args) {
  const std::size_t index = static_cast<std::size_t>(&slot - slots_.data());
  const auto object = [&](std::int32_t value) -> std::optional<std::size_t> {
    if (value < 0 || std::cmp_greater_equal(value, slot.script.modelObjects)) {
      return std::nullopt;
    }
    return static_cast<std::size_t>(value);
  };
  if (name == "render_projected_sprite" || name == "render_centered_sprite") {
    // BTL_renderProjectedSprite, and BTL_renderCenteredSprite, which pushes width + 1, height + 1,
    // its centre as the pivot, no turn, attribute 5 and the first palette row.
    const bool centred = name == "render_centered_sprite";
    EffectSprite sprite;
    sprite.position = vectorAt(slot, args[0]);
    sprite.color = vectorAt(slot, args[1]);
    sprite.color = {.x = sprite.color.x & ByteMask,
                    .y = sprite.color.y & ByteMask,
                    .z = sprite.color.z & ByteMask};
    sprite.scaleX = args[2];
    sprite.scaleY = args[3];
    sprite.u = args[4];
    sprite.v = args[5];
    sprite.width = centred ? args[6] + 1 : args[6];
    sprite.height = centred ? args[7] + 1 : args[7];
    sprite.pivotX = centred ? args[6] / 2 : args[8];
    sprite.pivotY = centred ? args[7] / 2 : args[9];
    sprite.rotate = centred ? 0 : args[10];
    const std::int32_t attribute = centred ? CenteredAttribute : args[11];
    sprite.blend = blendOf(attribute);
    sprite.clutRow = centred ? 0 : args[12];
    if ((attribute & HiddenBit) == 0) {
      draws_.push_back({.slot = index, .shape = sprite});
    }
  } else if (name == "draw_tmd" || name == "draw_tmd_yxz") {
    // BTL_drawTMD (RotMatrix) and BTL_drawTMDYXZ (RotMatrixYXZ): the instance's rotation scaled
    // by the vector, at its position.
    const std::optional<std::size_t> which = object(args[0]);
    if (!which) {
      return true;
    }
    const EffectVector turn = fx::toShort(vectorAt(slot, instanceAddress(RotationOffset)));
    EffectModel model;
    model.object = args[0];
    model.transform = fx::scaled(name == "draw_tmd" ? fx::rotXYZ(turn) : fx::rotYXZ(turn),
                                 vectorAt(slot, args[1]));
    model.transform.t = vectorAt(slot, instanceAddress(PositionOffset));
    model.color = slot.objectColors[*which];
    model.uOffset = slot.uvAnims[*which].u;
    model.vOffset = slot.uvAnims[*which].v;
    draws_.push_back({.slot = index, .shape = model});
  } else if (name == "set_model_object_color") {
    // BTL_setEFEModelObjectColor: each primitive's colour bytes.
    if (const std::optional<std::size_t> which = object(args[0])) {
      const EffectVector color = vectorAt(slot, args[1]);
      slot.objectColors[*which] =
          EffectVector{.x = color.x & ByteMask, .y = color.y & ByteMask, .z = color.z & ByteMask};
    }
  } else if (name == "init_uv_anim") {
    // BTL_initializeUVAnim: an object outside the model ends the script and fails a loading slot.
    const std::optional<std::size_t> which = object(args[0]);
    if (!which) {
      slot.failed = slot.failed || !current_;
      abort();
      return true;
    }
    if (args[2] == NoKeyframes) {
      return true;
    }
    UvAnim& anim = slot.uvAnims[*which];
    anim.keyframes = static_cast<std::uint32_t>(args[2]);
    anim.count = read(slot, args[2], EffectWidth::Int32);
    anim.keyframe = 0;
    anim.keyframeFrame = 0;
    anim.frame = 1;
    anim.loopFrames = 0;
    for (std::int32_t k = 0; k < anim.count; ++k) {
      anim.loopFrames += read(slot, args[2] + Int32Bytes + (k * KeyframeBytes), EffectWidth::Int16);
    }
  } else if (name == "get_uv_anim_frame") {
    if (const std::optional<std::size_t> which = object(args[1])) {
      write(slot, args[0], EffectWidth::Int32, slot.uvAnims[*which].frame);
    }
  } else {
    return false;
  }
  return true;
}

// battle_effect.c: BTL_tickEFEUVAnimation: each keyframe moves the object's texels by its du, dv
// once its frames have passed; after the last the first comes again.
void EffectRunner::tickUvAnims(Slot& slot) {
  for (UvAnim& anim : slot.uvAnims) {
    if (anim.count == 0 || !anim.keyframes) {
      continue;
    }
    const auto keyframeAt = [&](std::int32_t k) {
      return static_cast<std::int32_t>(*anim.keyframes) + Int32Bytes + (k * KeyframeBytes);
    };
    ++anim.keyframeFrame;
    if (anim.keyframeFrame >= read(slot, keyframeAt(anim.keyframe), EffectWidth::Int16)) {
      anim.keyframeFrame = 0;
      anim.keyframe = anim.keyframe + 1 >= anim.count ? 0 : anim.keyframe + 1;
      anim.u += read(slot, keyframeAt(anim.keyframe) + 2, EffectWidth::Int8);
      anim.v += read(slot, keyframeAt(anim.keyframe) + 3, EffectWidth::Int8);
    }
    ++anim.frame;
    if (anim.loopFrames > 0) {
      anim.frame %= anim.loopFrames;
    }
  }
}

// The sub-ops that read the fighters.
bool EffectRunner::fighterOp(Slot& slot, std::string_view name,
                             const std::vector<std::int32_t>& args) {
  SubEffect& self = context(slot);
  const auto sizeOf = [&](std::optional<FighterId> fighter) {
    const std::optional<EffectBody> body = bodyOf(fighter);
    return body ? EffectVector{.x = body->radius, .y = body->height, .z = body->radius}
                : EffectVector{};
  };
  const auto boneTransform = [&](std::optional<FighterId> fighter) {
    // BTL_getSourceBoneTransform, BTL_getTargetBoneTransform: a node's point and its YXZ angles.
    const EffectMatrix m = nodeOf(fighter, args[0]).value_or(EffectMatrix{});
    setVector(slot, args[1], m.t);
    setVector(slot, args[2], fx::toShort(fx::eulerYXZ(m)));
  };
  if (name == "check_tech_compatibility") {
    // BTL_checkTechCompatibility: the entry of the source's original type; without one the effect
    // ends (EFE_ACTIVE_SECTION -1). Without a body the first entry stands in.
    const auto found = slot.script.boneOffsets.find(static_cast<std::uint32_t>(args[0]));
    if (found == slot.script.boneOffsets.end() || found->second.empty()) {
      return true;
    }
    const std::vector<assets::EffectBoneOffset>& entries = found->second;
    const std::optional<EffectBody> body = bodyOf(self.source);
    if (!body) {
      self.boneOffset = entries.front();
      return true;
    }
    const auto entry =
        std::ranges::find(entries, body->species, &assets::EffectBoneOffset::species);
    if (entry == entries.end()) {
      // The port also frees the instance the start spawned, which nothing would run or free.
      activeSection_.reset();
      setFrame(slot, current_, FreeInstance);
      abort();
      return true;
    }
    self.boneOffset = *entry;
  } else if (name == "get_target_position") {
    // BTL_copyTargetEntityPosition: the target's node 0.
    setVector(slot, args[0], nodeOf(self.target, 0).value_or(EffectMatrix{}).t);
  } else if (name == "get_source_size") {
    setVector(slot, args[0], sizeOf(self.source));
  } else if (name == "get_target_size") {
    setVector(slot, args[0], sizeOf(self.target));
  } else if (name == "get_source_bone_transform") {
    boneTransform(self.source);
  } else if (name == "get_target_bone_transform") {
    boneTransform(self.target);
  } else if (name == "select_next_target" || name == "select_random_target") {
    selectTarget(slot, name == "select_random_target", args[0]);
  } else if (name == "face_target" || name == "target_hit_entity") {
    // BTL_faceTargetEntity turns the attacker to its target, which the battle already does;
    // BTL_setTargetToHitEntity makes the hit fighter the target, which is the battle's target.
  } else {
    return false;
  }
  return true;
}

// battle_effect.c: BTL_selectNextTargetEntity: the next standing fighter but the source (its
// entity index, -1 after the last); BTL_selectRandomTargetEntity: one a few further on, the source
// too when allowed.
void EffectRunner::selectTarget(Slot& slot, bool random, std::int32_t argument) {
  SubEffect& self = context(slot);
  const std::vector<FighterId> fighters =
      bodies_ != nullptr ? bodies_->fighters() : std::vector<FighterId>{};
  const auto eligible = [&](FighterId fighter, bool allowSelf) {
    const std::optional<EffectBody> body = bodyOf(fighter);
    return body && body->standing && (allowSelf || self.source != fighter);
  };
  if (!random) {
    while (targetCursor_ < fighters.size() && !eligible(fighters[targetCursor_], false)) {
      ++targetCursor_;
    }
    if (targetCursor_ >= fighters.size()) {
      write(slot, argument, EffectWidth::Int32, -1);
      return;
    }
    self.target = fighters[targetCursor_];
    write(slot, argument, EffectWidth::Int32, static_cast<std::int32_t>(targetCursor_ + 1));
    ++targetCursor_;
    return;
  }
  constexpr std::uint32_t Skips = 8;
  const bool allowSelf = argument != 0;
  if (std::ranges::none_of(fighters, [&](FighterId f) { return eligible(f, allowSelf); })) {
    return;
  }
  for (std::uint32_t n = randomDraw() % Skips + 1; n > 0;) {
    targetCursor_ = (targetCursor_ + 1) % fighters.size();
    if (eligible(fighters[targetCursor_], allowSelf)) {
      --n;
    }
  }
  self.target = fighters[targetCursor_];
}

namespace {

// Whether the segment from (x0, z0) to (x1, z1) crosses the square of half-side `half` around
// (cx, cz) (Liang-Barsky clipping).
bool segmentCrossesSquare(std::int64_t x0, std::int64_t z0, std::int64_t x1, std::int64_t z1,
                          std::int64_t cx, std::int64_t cz, std::int64_t half) {
  double low = 0.0;
  double high = 1.0;
  const std::array<std::pair<double, double>, 4> edges{{
      {static_cast<double>(-(x1 - x0)), static_cast<double>(x0 - (cx - half))},
      {static_cast<double>(x1 - x0), static_cast<double>((cx + half) - x0)},
      {static_cast<double>(-(z1 - z0)), static_cast<double>(z0 - (cz - half))},
      {static_cast<double>(z1 - z0), static_cast<double>((cz + half) - z0)},
  }};
  for (const auto& [p, q] : edges) {
    if (p == 0.0) {
      if (q < 0.0) {
        return false;
      }
      continue;
    }
    const double t = q / p;
    if (p < 0.0) {
      low = std::max(low, t);
    } else {
      high = std::min(high, t);
    }
    if (low > high) {
      return false;
    }
  }
  return true;
}

// battle_effect.c: BTL_calculateAttackHitPosition: the point `radius` from the fighter's root
// toward `other`, at its height.
EffectVector bodyPointToward(const EffectBody& body, const EffectVector& other) {
  const double dx = static_cast<double>(other.x) - body.location.x;
  const double dz = static_cast<double>(other.z) - body.location.z;
  const double length = std::hypot(dx, dz);
  if (length == 0.0) {
    return body.location;
  }
  return {.x = body.location.x + static_cast<std::int32_t>(std::lround(dx * body.radius / length)),
          .y = body.location.y - body.height,
          .z = body.location.z + static_cast<std::int32_t>(std::lround(dz * body.radius / length))};
}

} // namespace

bool EffectRunner::touchOp(Slot& slot, std::string_view name,
                           const std::vector<std::int32_t>& args) {
  const bool box = name == "check_collision" || name == "apply_box_hit";
  const bool radius = name == "apply_radius_hit";
  const bool line = name == "apply_line_hit";
  const bool target = name == "check_target_collision";
  if (!box && !radius && !line && !target) {
    return false;
  }
  const EffectVector at = instancePosition(slot);
  SubEffect& self = context(slot);
  if (target) {
    // BTL_checkTargetCollision: within `radius` of the target's root on the ground; it marks
    // nothing.
    if (args.size() < 4) {
      return true;
    }
    const std::optional<EffectMatrix> root = nodeOf(self.target, 0);
    bool near = false;
    if (root) {
      const std::int64_t dx = at.x - root->t.x;
      const std::int64_t dz = at.z - root->t.z;
      const std::int64_t r = args[3];
      near = (dx * dx) + (dz * dz) <= r * r;
    }
    write(slot, args[2], EffectWidth::Int32, near ? 1 : 0);
    return true;
  }
  if (args.size() < 2) {
    return true;
  }
  const std::int32_t hitAddress = args[1];
  const EffectVector extent = box ? vectorAt(slot, args[0]) : EffectVector{};
  const std::optional<EffectBody> source = bodyOf(self.source);
  bool any = false;
  for (const FighterId fighter :
       bodies_ != nullptr ? bodies_->fighters() : std::vector<FighterId>{}) {
    if (fighter == self.source || std::ranges::contains(slot.hitTargets, fighter)) {
      continue;
    }
    const std::optional<EffectBody> body = bodies_->body(fighter);
    if (!body || !body->standing) {
      continue;
    }
    const std::int64_t dx = static_cast<std::int64_t>(at.x) - body->location.x;
    const std::int64_t dz = static_cast<std::int64_t>(at.z) - body->location.z;
    bool hit = false;
    if (box) {
      // aabb.c: findAABBHitEntity: the fighter's box is radius / 2 wide and its height tall, from
      // the ground up.
      const std::int64_t dy =
          static_cast<std::int64_t>(at.y) - (body->location.y - (body->height >> 1));
      hit = std::abs(dx) <= extent.x + (body->radius >> 1) &&
            std::abs(dy) <= extent.y + (body->height >> 1) &&
            std::abs(dz) <= extent.z + (body->radius >> 1);
    } else if (radius) {
      // BTL_applyRadiusAttackHit: the squares of the radii added, not the radii.
      const std::int64_t r = args[0];
      hit = (dx * dx) + (dz * dz) <=
            (r * r) + (static_cast<std::int64_t>(body->radius) * body->radius);
    } else if (source) {
      // BTL_applyLineAttackHit: the line from the instance to the source crosses the fighter's
      // square.
      hit = segmentCrossesSquare(at.x, at.z, source->location.x, source->location.z,
                                 body->location.x, body->location.z, body->radius);
    }
    if (!hit) {
      continue;
    }
    slot.hitTargets.push_back(fighter);
    // The box check registers the hit at the instance; the radius and line checks on the
    // fighter's body, its radius toward the source (BTL_calculateAttackHitPosition).
    const bool onBody = !box && source.has_value();
    touched_.push_back({.fighter = fighter,
                        .at = onBody ? bodyPointToward(*body, source->location) : at,
                        .onBody = onBody});
    any = true;
    // BTL_applyBoxAttackHit stops at the first fighter it hits.
    if (box) {
      break;
    }
  }
  write(slot, hitAddress, EffectWidth::Int32, any ? 1 : 0);
  return true;
}

} // namespace dwe::game::battle
