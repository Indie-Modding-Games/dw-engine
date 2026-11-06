#include <dwe/core/original_math.hpp>
#include <dwe/game/fishing/look.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace dwe::game::fishing {

namespace {

// FISH_SPRITES (the fishing's image; the bobbers from 17, the fish from 33).
constexpr std::array<FishingSprite, FishingSpriteCount> Sprites{{
    {.u0 = 0xD0, .v0 = 0xB4, .u1 = 0xDF, .v1 = 0xC3, .originX = 8, .originY = 10, .palette = 0},
    {.u0 = 0x80, .v0 = 0xD4, .u1 = 0x9F, .v1 = 0xE3, .originX = 16, .originY = 8, .palette = 0},
    {.u0 = 0xA0, .v0 = 0xD4, .u1 = 0xBF, .v1 = 0xE3, .originX = 16, .originY = 8, .palette = 0},
    {.u0 = 0xC0, .v0 = 0xD4, .u1 = 0xDF, .v1 = 0xE3, .originX = 16, .originY = 8, .palette = 0},
    {.u0 = 0x80, .v0 = 0xE4, .u1 = 0x9F, .v1 = 0xF3, .originX = 16, .originY = 8, .palette = 0},
    {.u0 = 0x80, .v0 = 0xB4, .u1 = 0x8F, .v1 = 0xC3, .originX = 8, .originY = 8, .palette = 0},
    {.u0 = 0x90, .v0 = 0xB4, .u1 = 0x9F, .v1 = 0xC3, .originX = 8, .originY = 8, .palette = 0},
    {.u0 = 0xA0, .v0 = 0xB4, .u1 = 0xAF, .v1 = 0xC3, .originX = 8, .originY = 8, .palette = 0},
    {.u0 = 0xB0, .v0 = 0xB4, .u1 = 0xBF, .v1 = 0xC3, .originX = 8, .originY = 8, .palette = 0},
    {.u0 = 0xC0, .v0 = 0xB4, .u1 = 0xCF, .v1 = 0xC3, .originX = 8, .originY = 8, .palette = 0},
    {.u0 = 0x80, .v0 = 0xB4, .u1 = 0x8F, .v1 = 0xC3, .originX = 8, .originY = 8, .palette = 0},
    {.u0 = 0x90, .v0 = 0xC4, .u1 = 0x9F, .v1 = 0xD3, .originX = 8, .originY = 8, .palette = 0},
    {.u0 = 0xA0, .v0 = 0xC4, .u1 = 0xAF, .v1 = 0xD3, .originX = 8, .originY = 8, .palette = 0},
    {.u0 = 0xB0, .v0 = 0xC4, .u1 = 0xBF, .v1 = 0xD3, .originX = 8, .originY = 8, .palette = 0},
    {.u0 = 0xC0, .v0 = 0xC4, .u1 = 0xCF, .v1 = 0xD3, .originX = 8, .originY = 8, .palette = 0},
    {.u0 = 0xD0, .v0 = 0xC4, .u1 = 0xDF, .v1 = 0xD3, .originX = 8, .originY = 8, .palette = 0},
    {.u0 = 0xE0, .v0 = 0xC4, .u1 = 0xEF, .v1 = 0xD3, .originX = 8, .originY = 8, .palette = 0},
    {.u0 = 0x80, .v0 = 0x8C, .u1 = 0x8F, .v1 = 0x9B, .originX = 8, .originY = 10, .palette = 2},
    {.u0 = 0x90, .v0 = 0x8C, .u1 = 0x9F, .v1 = 0x9B, .originX = 8, .originY = 10, .palette = 2},
    {.u0 = 0xA0, .v0 = 0x8C, .u1 = 0xAF, .v1 = 0x9B, .originX = 8, .originY = 10, .palette = 2},
    {.u0 = 0xB0, .v0 = 0x8C, .u1 = 0xBF, .v1 = 0x9B, .originX = 8, .originY = 10, .palette = 2},
    {.u0 = 0xC0, .v0 = 0x8C, .u1 = 0xCF, .v1 = 0x9B, .originX = 8, .originY = 10, .palette = 2},
    {.u0 = 0xD0, .v0 = 0x8C, .u1 = 0xDF, .v1 = 0x9B, .originX = 8, .originY = 10, .palette = 2},
    {.u0 = 0xE0, .v0 = 0x8C, .u1 = 0xEF, .v1 = 0x9B, .originX = 8, .originY = 10, .palette = 2},
    {.u0 = 0x80, .v0 = 0x9C, .u1 = 0x8F, .v1 = 0xB3, .originX = 8, .originY = 18, .palette = 2},
    {.u0 = 0x90, .v0 = 0x9C, .u1 = 0x9F, .v1 = 0xB3, .originX = 8, .originY = 18, .palette = 2},
    {.u0 = 0xA0, .v0 = 0x9C, .u1 = 0xAF, .v1 = 0xB3, .originX = 8, .originY = 18, .palette = 2},
    {.u0 = 0xB0, .v0 = 0x9C, .u1 = 0xBF, .v1 = 0xB3, .originX = 8, .originY = 18, .palette = 2},
    {.u0 = 0xC0, .v0 = 0x9C, .u1 = 0xCF, .v1 = 0xB3, .originX = 8, .originY = 18, .palette = 2},
    {.u0 = 0xD0, .v0 = 0x9C, .u1 = 0xDF, .v1 = 0xB3, .originX = 8, .originY = 18, .palette = 2},
    {.u0 = 0xE0, .v0 = 0x9C, .u1 = 0xEF, .v1 = 0xB3, .originX = 8, .originY = 18, .palette = 2},
    {.u0 = 0x10, .v0 = 0x8C, .u1 = 0x58, .v1 = 0xAB, .originX = 34, .originY = 19, .palette = 1},
    {.u0 = 0x00, .v0 = 0x9C, .u1 = 0x03, .v1 = 0xA1, .originX = 0, .originY = 0, .palette = 1},
    {.u0 = 0x00, .v0 = 0xAC, .u1 = 0x0F, .v1 = 0xC3, .originX = 8, .originY = 5, .palette = 2},
    {.u0 = 0x10, .v0 = 0xAC, .u1 = 0x1F, .v1 = 0xC3, .originX = 8, .originY = 5, .palette = 2},
    {.u0 = 0x20, .v0 = 0xAC, .u1 = 0x2F, .v1 = 0xC3, .originX = 8, .originY = 5, .palette = 2},
    {.u0 = 0x00, .v0 = 0xC4, .u1 = 0x0F, .v1 = 0xDB, .originX = 8, .originY = 5, .palette = 2},
    {.u0 = 0x10, .v0 = 0xC4, .u1 = 0x1F, .v1 = 0xDB, .originX = 8, .originY = 5, .palette = 2},
    {.u0 = 0x00, .v0 = 0x8C, .u1 = 0x0F, .v1 = 0x9B, .originX = 8, .originY = 5, .palette = 2},
    {.u0 = 0x30, .v0 = 0xAC, .u1 = 0x3F, .v1 = 0xDB, .originX = 8, .originY = 3, .palette = 2},
    {.u0 = 0x40, .v0 = 0xAC, .u1 = 0x4F, .v1 = 0xDB, .originX = 8, .originY = 3, .palette = 2},
    {.u0 = 0x50, .v0 = 0xAC, .u1 = 0x5F, .v1 = 0xDB, .originX = 8, .originY = 3, .palette = 2},
    {.u0 = 0x60, .v0 = 0xAC, .u1 = 0x77, .v1 = 0xDB, .originX = 12, .originY = 3, .palette = 2},
    {.u0 = 0x77, .v0 = 0xAC, .u1 = 0x60, .v1 = 0xDB, .originX = 13, .originY = 3, .palette = 2},
    {.u0 = 0x5F, .v0 = 0xAC, .u1 = 0x50, .v1 = 0xDB, .originX = 9, .originY = 3, .palette = 2},
    {.u0 = 0x4F, .v0 = 0xAC, .u1 = 0x40, .v1 = 0xDB, .originX = 9, .originY = 3, .palette = 2},
    {.u0 = 0x68, .v0 = 0x8C, .u1 = 0x77, .v1 = 0x9B, .originX = 8, .originY = 16, .palette = 4},
    {.u0 = 0x68, .v0 = 0x9C, .u1 = 0x77, .v1 = 0xAB, .originX = 8, .originY = 16, .palette = 4},
    {.u0 = 0x58, .v0 = 0x8C, .u1 = 0x67, .v1 = 0x9B, .originX = 8, .originY = 16, .palette = 4},
}};

// The original's fixed point: 4096 is one, and a turn.
constexpr std::int32_t One = 0x1000;
constexpr float OneF = 4096.0F;

// FISH_bobberCommand: the old rod's bobber frames, then the amazing rod's.
constexpr std::size_t OldRodBobbers = 0x11;
constexpr std::size_t AmazingRodBobbers = 0x18;

// FISH_drawFishSprite: a fish of this size or more is a large one. Its frames by its animation
// phase (MAIN_D_801349A4, MAIN_D_801349AC), and struggling (MAIN_D_801349B0, MAIN_D_801349B8),
// when its scale is fixed (the same as the maps'). Its shade is its opacity's 30 out of 128.
constexpr std::int32_t LargeFish = 100;
constexpr std::array<std::size_t, 8> LargeSwim{39, 40, 41, 40, 39, 45, 44, 45};
constexpr std::array<std::size_t, 4> SmallSwim{33, 34, 33, 35};
constexpr std::array<std::size_t, 6> LargeStruggle{41, 42, 40, 44, 43, 45};
constexpr std::array<std::size_t, 6> SmallStruggle{34, 36, 34, 35, 37, 35};
constexpr FishScales StruggleScales{.small = 0x155, .large = 0xAA};
constexpr std::int32_t ShadeOfOpaque = 30;

// FISH_updateRodPose: the grip turns the hand's bone a quarter turn about y and x, then toward
// (-6, 0x34, 0x127); the rod's foot is (1, 19, -48) from the bone in that grip. Each joint is this
// far from the one before (FISH_D_8007A594); the middle three bend a quarter of the way each.
constexpr glm::ivec3 GripTurn{0x400, 0x400, 0};
constexpr glm::ivec3 GripToward{-6, 0x34, 0x127};
constexpr glm::vec3 RodFoot{1.0F, 19.0F, -48.0F};
constexpr std::array<float, RodJoints> JointLengths{252.0F, 185.0F, 101.0F, 75.0F, 0.0F};
constexpr std::int32_t BendShift = 2;
constexpr std::int32_t RestBend = 0x80;
constexpr std::int32_t FullBendShift = 9;

// The original's axes: y and z point the other way.
const glm::mat3 Flip{glm::vec3{1.0F, 0.0F, 0.0F}, glm::vec3{0.0F, -1.0F, 0.0F},
                     glm::vec3{0.0F, 0.0F, -1.0F}};

glm::mat3 fromRows(glm::vec3 a, glm::vec3 b, glm::vec3 c) {
  return glm::transpose(glm::mat3{a, b, c});
}

float sine(std::int32_t angle) {
  return static_cast<float>(core::original::rsin(angle)) / OneF;
}

float cosine(std::int32_t angle) {
  return static_cast<float>(core::original::rcos(angle)) / OneF;
}

// libgte RotMatrixYXZ: Ry · Rx · Rz.
glm::mat3 rotYXZ(glm::ivec3 angles) {
  const float sx = sine(angles.x);
  const float cx = cosine(angles.x);
  const float sy = sine(angles.y);
  const float cy = cosine(angles.y);
  const float sz = sine(angles.z);
  const float cz = cosine(angles.z);
  const glm::mat3 aboutX = fromRows({1.0F, 0.0F, 0.0F}, {0.0F, cx, -sx}, {0.0F, sx, cx});
  const glm::mat3 aboutY = fromRows({cy, 0.0F, sy}, {0.0F, 1.0F, 0.0F}, {-sy, 0.0F, cy});
  const glm::mat3 aboutZ = fromRows({cz, -sz, 0.0F}, {sz, cz, 0.0F}, {0.0F, 0.0F, 1.0F});
  return aboutY * aboutX * aboutZ;
}

std::int32_t centred(std::int32_t angle) {
  angle &= One - 1;
  return angle >= One / 2 ? angle - One : angle;
}

// vecmath.c: toEulerAngles: the yaw and pitch that point along a direction.
glm::ivec3 toEulerAngles(glm::ivec3 direction) {
  const auto ground =
      static_cast<std::int32_t>(std::sqrt((static_cast<double>(direction.x) * direction.x) +
                                          (static_cast<double>(direction.z) * direction.z)));
  return {centred(-core::original::ratan2(direction.y, ground)),
          centred(core::original::ratan2(direction.x, direction.z)), 0};
}

std::int32_t towardZero(std::int32_t value, std::int32_t limit) {
  return value > 0 ? std::min(value, limit) : std::max(value, limit);
}

} // namespace

const FishingSprite& fishingSprite(std::size_t index) {
  return Sprites.at(index);
}

std::array<glm::vec2, 4> spriteUvs(const FishingSprite& sprite) {
  const auto at = [](std::int32_t u, std::int32_t v) {
    return glm::vec2{static_cast<float>(u), static_cast<float>(v - SpriteImageTop)} /
           SpriteImageSize;
  };
  return {at(sprite.u0, sprite.v0), at(sprite.u1, sprite.v0), at(sprite.u0, sprite.v1),
          at(sprite.u1, sprite.v1)};
}

std::array<glm::vec3, 4> spriteCorners(const FishingSprite& sprite, glm::vec3 origin,
                                       glm::vec3 right, glm::vec3 down, std::int32_t scale) {
  const float s = static_cast<float>(scale) / OneF;
  const float left = static_cast<float>(-sprite.originX) * s;
  const float top = static_cast<float>(-sprite.originY) * s;
  const float width = static_cast<float>(std::abs(sprite.u1 - sprite.u0)) * s;
  const float height = static_cast<float>(std::abs(sprite.v1 - sprite.v0)) * s;
  const auto at = [&](float x, float y) { return origin + (right * x) + (down * y); };
  return {at(left, top), at(left + width, top), at(left, top + height),
          at(left + width, top + height)};
}

std::optional<std::size_t> bobberSprite(std::int32_t frame, Rod rod) {
  if (frame <= 0) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(frame - 1) +
         (rod == Rod::Old ? OldRodBobbers : AmazingRodBobbers);
}

bool hookedStruggles(const Fish& fish) {
  return fish.state != FishState::Nibbling && fish.state != FishState::Pulling;
}

std::optional<FishLook> fishLook(const Fish& fish, bool struggling, FishScales scales) {
  if (fish.kind == SeaSerpentKind) {
    return std::nullopt;
  }
  const std::int32_t phase = fish.animationPhase;
  const bool large = fish.size >= LargeFish;
  FishLook look;
  if (!struggling) {
    look.sprite = large ? LargeSwim.at(static_cast<std::size_t>((phase / 3) & 7))
                        : SmallSwim.at(static_cast<std::size_t>((phase / 4) & 3));
    look.scale = fish.size * (large ? scales.large : scales.small);
  } else {
    const auto step = static_cast<std::size_t>(static_cast<std::uint32_t>(phase) % 6U);
    look.sprite = large ? LargeStruggle.at(step) : SmallStruggle.at(step);
    look.scale = fish.size * (large ? StruggleScales.large : StruggleScales.small);
  }
  look.shade = fish.opacity * ShadeOfOpaque / OpaqueFish;
  return look;
}

std::int32_t rodBend(const Tension& tension) {
  if (!tension.shown || tension.limit <= 0) {
    return RestBend;
  }
  return ((tension.level + tension.limit) << FullBendShift) / (tension.limit * 2);
}

RodPose rodPose(const glm::mat4& hand, glm::vec3 bobber, std::int32_t bend) {
  // In the original's axes from here on.
  glm::mat3 turn =
      Flip * glm::mat3{hand} * Flip * rotYXZ(GripTurn) * rotYXZ(toEulerAngles(GripToward));
  glm::vec3 at = (Flip * glm::vec3{hand[3]}) + (turn * RodFoot);
  const glm::vec3 local = glm::transpose(turn) * ((Flip * bobber) - at);
  const glm::ivec3 toward{static_cast<std::int32_t>(local.x), static_cast<std::int32_t>(local.y),
                          static_cast<std::int32_t>(local.z)};
  glm::ivec3 aim = toEulerAngles(toward);
  const std::int32_t side = core::original::ratan2(toward.y, toward.x);
  aim.x = towardZero(aim.x, -(bend * core::original::rsin(side)) >> 12);
  aim.y = towardZero(aim.y, (bend * core::original::rcos(side)) >> 12);
  RodPose pose;
  for (std::size_t joint = 0; joint < RodJoints; ++joint) {
    if (joint > 0 && joint + 1 < RodJoints) {
      turn = turn * rotYXZ(aim >> BendShift);
    }
    glm::mat4 world{Flip * turn * Flip};
    world[3] = glm::vec4{Flip * at, 1.0F};
    pose.joints.at(joint) = world;
    at += turn * glm::vec3{0.0F, 0.0F, JointLengths.at(joint)};
  }
  pose.tip = glm::vec3{pose.joints.back()[3]};
  return pose;
}

} // namespace dwe::game::fishing
