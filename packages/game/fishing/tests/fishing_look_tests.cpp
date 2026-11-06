#include <dwe/game/fishing/look.hpp>
#include <dwe/game/fishing/view.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

using namespace dwe::game::fishing;

// The lake's map (FISHING_MAP_DATA of mayo06).
constexpr FishScales LakeFish{.small = 0x155, .large = 0xAA};

std::vector<std::int32_t> framesOf(BobberAnimation& bobber, int frames, bool bobs = false) {
  std::vector<std::int32_t> out;
  for (int i = 0; i < frames; ++i) {
    bobber.tick(bobs);
    out.push_back(bobber.frame());
  }
  return out;
}

float angleBetween(glm::vec3 a, glm::vec3 b) {
  return std::acos(std::clamp(glm::dot(glm::normalize(a), glm::normalize(b)), -1.0F, 1.0F));
}

} // namespace

TEST_CASE("bobber: hangs on frame 1, sinks through 1 to 5 and floats on 3") {
  BobberAnimation bobber;
  CHECK(framesOf(bobber, 2) == std::vector<std::int32_t>{0, 0});
  bobber.command(BobberAnimation::Command::ToHand);
  CHECK(framesOf(bobber, 3) == std::vector<std::int32_t>{1, 1, 1});
  bobber.command(BobberAnimation::Command::Sink);
  CHECK(framesOf(bobber, 9) == std::vector<std::int32_t>{1, 2, 3, 4, 5, 4, 3, 3, 3});
  CHECK(bobber.floating());
}

TEST_CASE("bobber: bobs on 4 and 3 for three frames each, dips at a nibble") {
  BobberAnimation bobber;
  bobber.command(BobberAnimation::Command::Sink);
  framesOf(bobber, 8);
  REQUIRE(bobber.floating());
  bobber.tick(true);
  CHECK(bobber.frame() == 3);
  CHECK(framesOf(bobber, 8) == std::vector<std::int32_t>{4, 4, 4, 3, 3, 3, 3, 3});
  CHECK(bobber.floating());
  bobber.command(BobberAnimation::Command::Nibble);
  CHECK(framesOf(bobber, 6) == std::vector<std::int32_t>{4, 5, 4, 3, 3, 3});
}

TEST_CASE("bobber: a bite pulls it under until the hook is lost") {
  BobberAnimation bobber;
  bobber.command(BobberAnimation::Command::Bite);
  CHECK(framesOf(bobber, 7) == std::vector<std::int32_t>{4, 5, 6, 7, -1, -1, -1});
  CHECK_FALSE(bobberSprite(bobber.frame(), Rod::Old));
  bobber.command(BobberAnimation::Command::Lost);
  CHECK(framesOf(bobber, 7) == std::vector<std::int32_t>{7, 6, 5, 4, 3, 3, 3});
}

TEST_CASE("bobber: each rod has its own sprites") {
  CHECK_FALSE(bobberSprite(0, Rod::Old));
  CHECK(bobberSprite(1, Rod::Old) == 0x11U);
  CHECK(bobberSprite(7, Rod::Old) == 0x17U);
  CHECK(bobberSprite(1, Rod::Amazing) == 0x18U);
  const FishingSprite& old = fishingSprite(0x11);
  CHECK(old.u0 == 0x80);
  CHECK(old.v0 == 0x8C);
  CHECK(old.palette == 2);
}

TEST_CASE("fish: small and large frames by phase, scale by size, shade by opacity") {
  Fish small{.kind = 0, .size = 50, .opacity = OpaqueFish};
  small.animationPhase = 4;
  const auto swim = fishLook(small, false, LakeFish);
  REQUIRE(swim);
  CHECK(swim->sprite == 34U);
  CHECK(swim->scale == 50 * 0x155);
  CHECK(swim->shade == 30);
  Fish large{.kind = 3, .size = 150, .opacity = OpaqueFish / 2};
  large.animationPhase = 15;
  const auto big = fishLook(large, false, LakeFish);
  REQUIRE(big);
  CHECK(big->sprite == 45U);
  CHECK(big->scale == 150 * 0xAA);
  CHECK(big->shade == 15);
  large.animationPhase = 7;
  const auto fight = fishLook(large, true, LakeFish);
  REQUIRE(fight);
  CHECK(fight->sprite == 42U);
  Fish serpent{.kind = SeaSerpentKind, .size = 300, .opacity = OpaqueFish};
  CHECK_FALSE(fishLook(serpent, false, LakeFish));
}

TEST_CASE("fish: the hooked fish struggles once past pulling") {
  Fish fish;
  fish.state = FishState::Nibbling;
  CHECK_FALSE(hookedStruggles(fish));
  fish.state = FishState::Pulling;
  CHECK_FALSE(hookedStruggles(fish));
  fish.state = FishState::Struggling;
  CHECK(hookedStruggles(fish));
  fish.state = FishState::Escaping;
  CHECK(hookedStruggles(fish));
}

TEST_CASE("sprites: corners from the origin, texels of the image") {
  // The small fish: 15 x 23 texels from (8, 5).
  const FishingSprite& fish = fishingSprite(33);
  const auto corners =
      spriteCorners(fish, glm::vec3{100.0F, 0.0F, 0.0F}, glm::vec3{1.0F, 0.0F, 0.0F},
                    glm::vec3{0.0F, 0.0F, 1.0F}, 2 * 0x1000);
  CHECK(corners[0].x == doctest::Approx(84.0F));
  CHECK(corners[0].z == doctest::Approx(-10.0F));
  CHECK(corners[3].x == doctest::Approx(114.0F));
  CHECK(corners[3].z == doctest::Approx(36.0F));
  const auto uvs = spriteUvs(fish);
  CHECK(uvs[0].x == doctest::Approx(0.0F));
  CHECK(uvs[0].y == doctest::Approx((0xAC - SpriteImageTop) / 116.0F));
  CHECK(uvs[3].x == doctest::Approx(15.0F / 256.0F));
}

TEST_CASE("rod: four segments of 613 units, bent toward the bobber by the tension") {
  const glm::mat4 hand = glm::translate(glm::mat4{1.0F}, glm::vec3{0.0F, 100.0F, 0.0F});
  const RodPose straight = rodPose(hand, glm::vec3{0.0F, 0.0F, -2000.0F}, 0);
  const glm::vec3 foot{straight.joints[0][3]};
  CHECK(glm::length(straight.tip - foot) == doctest::Approx(613.0F).epsilon(0.01));
  CHECK(glm::vec3{straight.joints[4][3]} == straight.tip);
  // Off the rod's line, the bobber pulls the tip toward it as far as the bend lets it.
  const glm::vec3 bobber = foot + glm::vec3{600.0F, -300.0F, 0.0F};
  const RodPose rest = rodPose(hand, bobber, 0);
  const RodPose bent = rodPose(hand, bobber, 512);
  CHECK(angleBetween(bent.tip - foot, bobber - foot) <
        angleBetween(rest.tip - foot, bobber - foot));
  CHECK(glm::length(bent.tip - foot) < 613.0F);
  Tension tension;
  CHECK(rodBend(tension) == 0x80);
  tension = {.shown = true, .level = 0, .limit = 5000};
  CHECK(rodBend(tension) == 0x100);
  tension.level = 5000;
  CHECK(rodBend(tension) == 0x200);
}
