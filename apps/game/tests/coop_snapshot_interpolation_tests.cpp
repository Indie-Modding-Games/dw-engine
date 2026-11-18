#include <doctest/doctest.h>

#include "coop_snapshot_interpolation.hpp"

using namespace dwe::app;
using dwe::game::coop::EntityState;
using dwe::game::coop::Snapshot;

TEST_CASE("client presentation interpolates authoritative entities and shortest yaw") {
  CoopSnapshotInterpolation buffer;
  REQUIRE(buffer.push(Snapshot{.mapId = "field/main",
                               .worldTick = 10,
                               .entities = {{.networkId = 1, .x = 0, .yaw = 65000}}}));
  REQUIRE(buffer.push(Snapshot{.mapId = "field/main",
                               .worldTick = 11,
                               .acknowledgedCommand = 8,
                               .entities = {{.networkId = 1, .x = 100, .yaw = 500}}}));

  const auto middle = buffer.sample(0.5F);
  REQUIRE(middle);
  REQUIRE(middle->entities.size() == 1);
  CHECK(middle->worldTick == 11);
  CHECK(middle->acknowledgedCommand == 8);
  CHECK(middle->entities.front().position.x == 50.0F);
  const bool yawNearZero =
      middle->entities.front().yaw < 0.1F || middle->entities.front().yaw > 6.1F;
  CHECK(yawNearZero);
  CHECK(buffer.sample(-1.0F)->entities.front().position.x == 0.0F);
  CHECK(buffer.sample(2.0F)->entities.front().position.x == 100.0F);
}

TEST_CASE("client presentation rejects old snapshots and resets history on map changes") {
  CoopSnapshotInterpolation buffer;
  REQUIRE(buffer.push(
      Snapshot{.mapId = "field/a", .worldTick = 20, .entities = {{.networkId = 1, .x = 4}}}));
  REQUIRE(buffer.push(
      Snapshot{.mapId = "field/a", .worldTick = 21, .entities = {{.networkId = 1, .x = 8}}}));
  CHECK_FALSE(buffer.push(
      Snapshot{.mapId = "field/a", .worldTick = 19, .entities = {{.networkId = 1, .x = 100}}}));
  REQUIRE(buffer.push(
      Snapshot{.mapId = "field/b", .worldTick = 1, .entities = {{.networkId = 1, .x = 50}}}));
  const auto afterMapChange = buffer.sample(0.0F);
  REQUIRE(afterMapChange);
  CHECK(afterMapChange->mapId == "field/b");
  CHECK(afterMapChange->entities.front().position.x == 50.0F);
}
