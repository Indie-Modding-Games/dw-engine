#include <doctest/doctest.h>

#include "coop_snapshot_interpolation.hpp"

using namespace dwe::app;
using dwe::game::coop::EntityState;
using dwe::game::coop::Snapshot;
using dwe::net::Millis;

TEST_CASE("client presentation plays each entity smoothly on the host's clock, shortest yaw") {
  CoopSnapshotInterpolation buffer;
  // World ticks 10 and 11 were sent 50 ms apart and arrived 20 ms after.
  REQUIRE(buffer.push(Snapshot{.mapId = "field/main",
                               .worldTick = 10,
                               .entities = {{.networkId = 1, .x = 0, .yaw = 65000}}},
                      Millis{520}));
  REQUIRE(buffer.push(Snapshot{.mapId = "field/main",
                               .worldTick = 11,
                               .acknowledgedCommand = 8,
                               .entities = {{.networkId = 1, .x = 100, .yaw = 500}}},
                      Millis{570}));

  // Drawn 100 ms behind: at 645 the host's 525, halfway between the two.
  const auto middle = buffer.sample(Millis{645});
  REQUIRE(middle);
  REQUIRE(middle->entities.size() == 1);
  CHECK(middle->worldTick == 11);
  CHECK(middle->acknowledgedCommand == 8);
  CHECK(middle->entities.front().position.x == doctest::Approx(50.0F));
  const bool yawNearZero =
      middle->entities.front().yaw < 0.1F || middle->entities.front().yaw > 6.1F;
  CHECK(yawNearZero);
  // The frames in between move it on by small steps, whatever the frame rate.
  float last = 0.0F;
  for (std::int64_t now = 620; now <= 670; now += 7) {
    const float x = buffer.sample(Millis{now})->entities.front().position.x;
    CHECK(x >= last);
    CHECK(x - last <= 15.0F);
    last = x;
  }
  CHECK(buffer.sample(Millis{0})->entities.front().position.x == 0.0F);
}

TEST_CASE("client presentation rejects old snapshots, drops gone entities and resets on a map") {
  CoopSnapshotInterpolation buffer;
  REQUIRE(buffer.push(Snapshot{.mapId = "field/a",
                               .worldTick = 20,
                               .entities = {{.networkId = 1, .x = 4}, {.networkId = 3, .x = 9}}},
                      Millis{1000}));
  REQUIRE(buffer.push(
      Snapshot{.mapId = "field/a", .worldTick = 21, .entities = {{.networkId = 1, .x = 8}}},
      Millis{1050}));
  CHECK_FALSE(buffer.push(
      Snapshot{.mapId = "field/a", .worldTick = 19, .entities = {{.networkId = 1, .x = 100}}},
      Millis{1060}));
  // Entity 3 left with snapshot 21: nothing of it is drawn.
  const auto afterLeave = buffer.sample(Millis{2000});
  REQUIRE(afterLeave);
  REQUIRE(afterLeave->entities.size() == 1);
  CHECK(afterLeave->entities.front().position.x == 8.0F);
  REQUIRE(buffer.push(
      Snapshot{.mapId = "field/b", .worldTick = 22, .entities = {{.networkId = 1, .x = 50}}},
      Millis{1100}));
  const auto afterMapChange = buffer.sample(Millis{1100});
  REQUIRE(afterMapChange);
  CHECK(afterMapChange->mapId == "field/b");
  CHECK(afterMapChange->entities.front().position.x == 50.0F);
}

TEST_CASE("a guest's presence query lists the players the newest snapshot shows on a map") {
  using dwe::game::coop::EntityRole;
  using dwe::game::coop::PlayerId;
  CoopSnapshotInterpolation buffer;
  CHECK(buffer.playersOn("field/a").empty());
  REQUIRE(
      buffer.push(Snapshot{.mapId = "field/a",
                           .worldTick = 1,
                           .entities = {{.networkId = 1, .owner = 0, .role = EntityRole::Player},
                                        {.networkId = 2, .owner = 0, .role = EntityRole::Partner},
                                        {.networkId = 5, .owner = 2, .role = EntityRole::Player}}},
                  Millis{0}));
  CHECK(buffer.playersOn("field/a") == std::vector<PlayerId>{0, 2});
  CHECK(buffer.playersOn("field/b").empty());
}

TEST_CASE("a late snapshot fills a track in time but never takes the guest back a map (#358)") {
  CoopSnapshotInterpolation buffer;
  // Snapshot 2 shows a turn (z 140) and arrives after 3, before the playback reached its time.
  REQUIRE(buffer.push(
      Snapshot{.mapId = "field/a", .worldTick = 1, .entities = {{.networkId = 1, .z = 0}}},
      Millis{80}));
  REQUIRE(buffer.push(
      Snapshot{.mapId = "field/a", .worldTick = 3, .entities = {{.networkId = 1, .z = 20}}},
      Millis{180}));
  CHECK_FALSE(buffer.push(
      Snapshot{.mapId = "field/a", .worldTick = 2, .entities = {{.networkId = 1, .z = 140}}},
      Millis{185}));
  const auto filled = buffer.sample(Millis{100 + 30 + 100});
  REQUIRE(filled);
  CHECK(filled->worldTick == 3);
  CHECK(filled->entities.front().position.z == doctest::Approx(140.0F));
  // The host went on to another map; a snapshot of the map before, late, is not drawn.
  REQUIRE(buffer.push(
      Snapshot{.mapId = "field/b", .worldTick = 5, .entities = {{.networkId = 1, .x = 7}}},
      Millis{280}));
  CHECK_FALSE(buffer.push(
      Snapshot{.mapId = "field/a", .worldTick = 4, .entities = {{.networkId = 1, .x = 99}}},
      Millis{285}));
  const auto after = buffer.sample(Millis{400});
  REQUIRE(after);
  CHECK(after->mapId == "field/b");
  CHECK(after->entities.front().position.x == 7.0F);
}
