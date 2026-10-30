#include <dwe/game/field/partner_follow.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace {

using dwe::assets::ClipMotion;
using dwe::assets::ClipMotions;
using dwe::assets::RootStep;
using dwe::game::field::OriginalPoint;
using dwe::game::field::PartnerFollow;

// The first partner's standing, walking and running clips (agum: animations 0, 2 and 4), their
// root steps a frame (side, forward).
ClipMotions agumonClips() {
  return {
      {0, ClipMotion{.steps = std::vector<RootStep>(17), .repeatFrom = 1}},
      {2, ClipMotion{.steps = {{0, 0},
                               {2, 25},
                               {3, 25},
                               {0, 15},
                               {-1, 15},
                               {-1, 30},
                               {-1, 30},
                               {-1, 20},
                               {-1, 20},
                               {-2, 25},
                               {-3, 25},
                               {0, 15},
                               {1, 15},
                               {1, 30},
                               {1, 30},
                               {1, 20},
                               {1, 20}},
                     .repeatFrom = 1}},
      {4, ClipMotion{.steps = {{0, 0},
                               {2, 30},
                               {2, 20},
                               {1, 25},
                               {1, 40},
                               {-2, 80},
                               {-4, 75},
                               {-2, 30},
                               {-2, 20},
                               {-1, 25},
                               {-1, 40},
                               {2, 80},
                               {4, 75}},
                     .repeatFrom = 1}},
  };
}

// A frame on an open map with only the player around.
PartnerFollow::Frame frameWith(OriginalPoint player) {
  PartnerFollow::Frame frame;
  frame.player = player;
  return frame;
}

struct Expected {
  std::int32_t x;
  std::int32_t z;
  std::int32_t rotation;
  std::int32_t clip;
};

} // namespace

TEST_CASE("the partner follows the player frame by frame as the original's integers do") {
  // An integer model of map.c: partnerTickCollision, partner.c: partnerTickWalking and anim.c:
  // tickAnimation, checked against a trace of the original on the first map (field_walk_seen),
  // gives these on an open map: the player stands, walks east 50 a frame for 18 frames, then
  // south. The partner stands until it is out of its walking distance and its idle clip ends a
  // pass, runs, walks a moment (closeness 1 from a run starts the walk at once) and, out of reach
  // again, finishes the walk's pass before it runs again.
  constexpr std::array<Expected, 60> Frames{
      {{256, -1088, 0, 0},     {256, -1088, 0, 0},     {256, -1088, 3935, 0},
       {256, -1088, 3791, 0},  {256, -1088, 3672, 0},  {256, -1088, 3580, 0},
       {256, -1088, 3508, 0},  {256, -1088, 3452, 0},  {256, -1088, 3407, 0},
       {256, -1088, 3371, 4},  {284, -1099, 3342, 4},  {303, -1104, 3317, 4},
       {327, -1112, 3298, 4},  {365, -1123, 3279, 4},  {441, -1148, 3262, 4},
       {513, -1171, 3242, 4},  {542, -1180, 3221, 4},  {561, -1186, 3204, 4},
       {585, -1192, 3190, 4},  {624, -1199, 3178, 4},  {702, -1217, 3235, 4},
       {774, -1239, 3304, 4},  {802, -1251, 3381, 4},  {820, -1261, 3453, 4},
       {839, -1276, 3519, 4},  {869, -1303, 3577, 4},  {920, -1365, 3632, 4},
       {920, -1365, 3682, 2},  {935, -1386, 3726, 2},  {949, -1406, 3767, 2},
       {956, -1419, 3803, 2},  {961, -1434, 3833, 2},  {971, -1462, 3854, 2},
       {980, -1491, 3874, 2},  {985, -1510, 3893, 2},  {990, -1529, 3908, 2},
       {994, -1554, 3922, 2},  {998, -1579, 3933, 2},  {1001, -1594, 3942, 2},
       {1005, -1608, 3953, 2}, {1013, -1637, 3963, 2}, {1019, -1666, 3974, 2},
       {1024, -1686, 3981, 2}, {1028, -1706, 3990, 2}, {1028, -1706, 3996, 4},
       {1034, -1736, 4002, 4}, {1038, -1755, 4009, 4}, {1043, -1780, 4015, 4},
       {1048, -1820, 4020, 4}, {1055, -1899, 4024, 4}, {1059, -1974, 4026, 4},
       {1060, -2005, 4027, 4}, {1060, -2025, 4030, 4}, {1062, -2050, 4031, 4},
       {1065, -2090, 4035, 4}, {1074, -2169, 4038, 4}, {1084, -2243, 4041, 4},
       {1088, -2273, 4046, 4}, {1092, -2293, 4050, 4}, {1094, -2318, 4054, 4}}};
  PartnerFollow follow{120, agumonClips()};
  follow.place({.x = 256, .z = -1088}, 0);
  for (std::size_t frame = 0; frame < Frames.size(); ++frame) {
    const auto walked = static_cast<std::int32_t>(frame == 0 ? 0 : frame - 1);
    const OriginalPoint player{.x = 256 + (50 * std::min(walked, 18)),
                               .z = -1285 - (50 * std::max(0, walked - 18))};
    follow.tick(frameWith(player));
    const Expected& expected = Frames.at(frame);
    INFO("frame " << frame);
    CHECK(follow.position() == OriginalPoint{.x = expected.x, .z = expected.z});
    CHECK(follow.rotation() == expected.rotation);
    CHECK(follow.clip().id == expected.clip);
  }
}

TEST_CASE("a partner within its stopping distance stands and looks at the player") {
  PartnerFollow follow{120, agumonClips()};
  follow.place({.x = 0, .z = 0}, 0);
  for (int frame = 0; frame < 40; ++frame) {
    follow.tick(frameWith({.x = 0, .z = -250}));
  }
  CHECK(follow.clip().id == dwe::game::field::PartnerIdleClip);
  CHECK(follow.position() == OriginalPoint{.x = 0, .z = 0});
  // Looking at the player, straight down -z: the original's 0 (and its _atan's 4095 off it).
  CHECK(((follow.rotation() + 1) & 0xFFF) <= 2);
}

TEST_CASE("the partner's cell and its centre are the original's") {
  CHECK(PartnerFollow::cellOf({.x = 0, .z = 0}) ==
        dwe::assets::CollisionCell{.column = 50, .row = 50});
  CHECK(PartnerFollow::cellOf({.x = -1, .z = 1}) ==
        dwe::assets::CollisionCell{.column = 49, .row = 49});
  CHECK(PartnerFollow::cellOf({.x = 150, .z = -150}) ==
        dwe::assets::CollisionCell{.column = 51, .row = 51});
  CHECK(PartnerFollow::centreOf({.column = 51, .row = 51}) == OriginalPoint{.x = 150, .z = -150});
}
