#include <dwe/game/battle/player_retreat.hpp>

#include <cstdint>
#include <optional>
#include <vector>

#include "fixture.hpp"

using namespace battle_test;
using dwe::assets::CollisionCell;

namespace {

// An open ground seen from above: a screen pixel is eight world units, x right and z down.
class TopDownGround final : public BattleGround {
public:
  static constexpr std::int32_t UnitsPerPixel = 8;
  [[nodiscard]] std::optional<ScreenPoint> screenPointOf(Point ground,
                                                         std::int32_t height) const override {
    return ScreenPoint{.x = ground.x / UnitsPerPixel, .y = (ground.z - height) / UnitsPerPixel};
  }
  [[nodiscard]] bool blocksStep(Point /*from*/, Point /*to*/,
                                std::int32_t /*radius*/) const override {
    return true;
  }
};

constexpr Facing FacingAway{.x = 0, .z = -FacingScale};

// Steps until it stops, at most `limit` frames; the frames it took.
std::int32_t runUntilStopped(PlayerRetreat& retreat, const BattleGround* ground, Point partner,
                             std::int32_t limit = 200) {
  std::int32_t frames = 0;
  while (retreat.running() && frames < limit) {
    ++frames;
    retreat.step(ground, partner);
  }
  return frames;
}

} // namespace

TEST_CASE("the player runs back to the cell it entered the map on and turns to the partner") {
  const TimingRules& timing = originalRules().timing;
  // The forest fight of the oracle: from z 2785 of the original (world -2785) toward the entry
  // cell around z 3585; the original stops at 3534 after 15 steps of 50, on entering that cell.
  const CollisionCell entry{.column = 52, .row = 14};
  const Point partner{.x = 272, .z = -3129};
  PlayerRetreat retreat({}, entry, {.x = 264, .z = -2785}, FacingAway, timing);
  CHECK(runUntilStopped(retreat, nullptr, partner) == 16);
  CHECK_FALSE(retreat.running());
  CHECK(retreat.position().z <= -3532);
  CHECK(retreat.position().z >= -3536);
  CHECK(retreat.position().x >= 250);
  CHECK(retreat.position().x <= 253);
  // It faces the partner, back the way it came.
  CHECK(retreat.facing().z > 0);
  CHECK_FALSE(retreat.step(nullptr, partner));
}

TEST_CASE("the player follows its way back cell by cell before heading for the entry") {
  const TimingRules& timing = originalRules().timing;
  // Way back: east to (55, 50), then north to (55, 46); the entry is west of that.
  const std::vector<CollisionCell> way{{.column = 55, .row = 50}, {.column = 55, .row = 46}};
  const CollisionCell entry{.column = 51, .row = 46};
  PlayerRetreat retreat(way, entry, {.x = 50, .z = 50}, FacingAway, timing);
  bool passedFirst = false;
  std::int32_t frames = 0;
  while (retreat.running() && frames < 200) {
    ++frames;
    retreat.step(nullptr, {});
    passedFirst = passedFirst || retreat.position().x >= 500;
    if (retreat.wayLeft() == 2) {
      // Still heading east along z 50.
      CHECK(retreat.position().z == 50);
    }
  }
  CHECK(passedFirst);
  CHECK(retreat.wayLeft() == 0);
  CHECK_FALSE(retreat.running());
  const Point end = retreat.position();
  CHECK(end.x >= 100);
  CHECK(end.x < 200);
  CHECK(end.z >= -400);
  CHECK(end.z < -300);
}

TEST_CASE("the player stops where its next step would leave the combat area, walls or not") {
  const TimingRules& timing = originalRules().timing;
  const TopDownGround ground;
  // The entry is far east; the smaller area is 300 pixels wide, 150 each side, 1200 units here.
  const CollisionCell entry{.column = 90, .row = 50};
  PlayerRetreat retreat({}, entry, {.x = 0, .z = 50}, FacingAway, timing);
  runUntilStopped(retreat, &ground, {.x = -400, .z = 50});
  CHECK_FALSE(retreat.running());
  const std::int32_t edge = (timing.retreatEntryAreaWidth / 2) * TopDownGround::UnitsPerPixel;
  CHECK(retreat.position().x + timing.retreatRadius <= edge + TopDownGround::UnitsPerPixel);
  CHECK(retreat.position().x + timing.retreatRadius + timing.retreatStep > edge);
  CHECK(retreat.facing().x < 0);
}
