#include <dwe/game/field/field_items.hpp>

#include <doctest/doctest.h>
#include <glm/vec3.hpp>

#include <vector>

using dwe::game::field::Chest;
using dwe::game::field::ChestTrayOut;
using dwe::game::field::FieldItems;

namespace {

FieldItems itemsWithRoom() {
  FieldItems items{{.groundSlots = 1, .chestSlots = 2, .reachCells = 1}};
  items.enterMap("ogre01");
  return items;
}

const dwe::assets::ItemTable& table() {
  static const dwe::assets::ItemTable items = [] {
    dwe::assets::ItemEntry entry;
    entry.id = "item";
    return dwe::assets::ItemTable{std::vector{entry}};
  }();
  return items;
}

Chest ogreChest(bool taken) {
  return {.item = table().handleOf(table().entries().front()),
          .position = {-1235.0F, 0.0F, -1587.0F},
          .rotation = 3072,
          .trigger = 670,
          .taken = taken,
          .tray = 0};
}

} // namespace

TEST_CASE("map_object.c tickOpenChestTray and tickCloseChestTray: the drawer slides 20 a frame") {
  FieldItems items = itemsWithRoom();
  REQUIRE(items.spawnChest(ogreChest(false)));
  CHECK(items.chests()[0]->tray == 0);
  // Out: 0 -> -160 in eight frames, done on the eighth.
  for (int frame = 1; frame < 8; ++frame) {
    CHECK_FALSE(items.slideTrayOut(0));
    CHECK(items.chests()[0]->tray == -20 * frame);
  }
  CHECK(items.slideTrayOut(0));
  CHECK(items.chests()[0]->tray == ChestTrayOut);
  // Already out: done at once and stays at -160.
  CHECK(items.slideTrayOut(0));
  CHECK(items.chests()[0]->tray == ChestTrayOut);
  // In: back to 0 in eight frames.
  for (int frame = 1; frame < 8; ++frame) {
    CHECK_FALSE(items.slideTrayIn(0));
  }
  CHECK(items.slideTrayIn(0));
  CHECK(items.chests()[0]->tray == 0);
  // A slot without a chest has nothing to slide.
  CHECK(items.slideTrayOut(1));
}

TEST_CASE("map_object.c spawnChest: a taken chest shows its drawer out, at its cell's centre") {
  FieldItems items = itemsWithRoom();
  REQUIRE(items.spawnChest(ogreChest(true)));
  CHECK(items.chests()[0]->tray == ChestTrayOut);
  // ogre01's chest: the script's (-1235, 0, 1587) stands at the original's (-1250, 0, 1550).
  const glm::vec3 place = dwe::game::field::chestPlace(*items.chests()[0]);
  CHECK(place.x == doctest::Approx(-1250.0F));
  CHECK(place.y == doctest::Approx(0.0F));
  CHECK(place.z == doctest::Approx(-1550.0F));
}

TEST_CASE("tamer.c checkChestCollision: a chest opens from four cells in front of it") {
  FieldItems items = itemsWithRoom();
  // ogre01's chest, rotation 0xc00: its cell is (37, 34), opened from column 41.
  REQUIRE(items.spawnChest(ogreChest(false)));
  const dwe::assets::CollisionCell reach = dwe::game::field::chestReachCell(*items.chests()[0]);
  CHECK(reach.column == 41);
  CHECK(reach.row == 34);
  CHECK(items.chestInReach({.column = 40, .row = 35}) == 0U);
  CHECK(items.chestInReach({.column = 42, .row = 33}) == 0U);
  // Its own cell and the closed ones around it are out of reach.
  CHECK_FALSE(items.chestInReach({.column = 37, .row = 34}).has_value());
  CHECK_FALSE(items.chestInReach({.column = 39, .row = 34}).has_value());
}

TEST_CASE("item.c spawnDroppedItems: a drop on a wall lies on the nearest free cell") {
  using dwe::assets::CollisionCell;
  using dwe::assets::collisionCellAt;
  using dwe::assets::collisionCellCenter;
  using dwe::game::field::dropCell;
  // A wall six cells thick, columns 46 to 51, free to the west from column 45 and to the east
  // from 52.
  dwe::assets::MapCollision collision;
  for (std::int32_t row = 40; row <= 60; ++row) {
    for (std::int32_t column = 46; column <= 51; ++column) {
      collision.setValue({.column = column, .row = row}, 0x80);
    }
  }
  const auto item = table().handleOf(table().entries().front());

  SUBCASE("on a free cell it stays where the enemy stood, on the ground") {
    FieldItems items = itemsWithRoom();
    REQUIRE(items.drop(item, {275.0F, 30.0F, 40.0F}, collision));
    const auto& lying = items.ground()[0];
    REQUIRE(lying);
    CHECK(lying->cell == CollisionCell{.column = 52, .row = 50});
    CHECK(lying->position.x == doctest::Approx(275.0F));
    CHECK(lying->position.y == doctest::Approx(0.0F));
    CHECK(lying->position.z == doctest::Approx(40.0F));
  }
  SUBCASE("inside the wall it moves to the centre of the nearest free cell") {
    FieldItems items = itemsWithRoom();
    // Column 51 is blocked; column 52, one cell east, is the nearest free one.
    const glm::vec3 inWall{120.0F, 0.0F, 40.0F};
    REQUIRE(collision.isBlocked(collisionCellAt(inWall)));
    REQUIRE(items.drop(item, inWall, collision));
    const auto& lying = items.ground()[0];
    REQUIRE(lying);
    CHECK(lying->cell == CollisionCell{.column = 52, .row = 50});
    CHECK_FALSE(collision.isBlocked(lying->cell));
    const glm::vec3 centre = collisionCellCenter(lying->cell);
    CHECK(lying->position.x == doctest::Approx(centre.x));
    CHECK(lying->position.z == doctest::Approx(centre.z));
    CHECK(lying->position.y == doctest::Approx(0.0F));
  }
  SUBCASE("deep in the wall it still finds the open side, and nothing when there is none") {
    // From column 47 the free column 45 (two cells west) is nearer than column 52 (five east).
    CHECK(dropCell(collision, collisionCellCenter({.column = 47, .row = 50})) ==
          CollisionCell{.column = 45, .row = 50});
    dwe::assets::MapCollision walled;
    for (std::int32_t row = 30; row <= 70; ++row) {
      for (std::int32_t column = 30; column <= 70; ++column) {
        walled.setValue({.column = column, .row = row}, 0x80);
      }
    }
    CHECK_FALSE(dropCell(walled, collisionCellCenter({.column = 50, .row = 50})).has_value());
  }
}

TEST_CASE("map.c changeMap: another map clears the last one's ground and chests, its own stay") {
  FieldItems items{{.groundSlots = 2, .chestSlots = 2, .reachCells = 1}};
  items.enterMap("room08");
  const dwe::assets::ItemHandle item = table().handleOf(table().entries().front());
  REQUIRE(items.spawn(item, {.column = 48, .row = 62}, 0.0F));
  REQUIRE(items.spawnChest(ogreChest(false)));
  REQUIRE_FALSE(items.ground().empty());

  // The map change: nothing of room08 is left to draw over the next map's fade in.
  items.enterMap("twna01");
  CHECK(items.ground().empty());
  CHECK(items.chests().empty());

  // The new map's head script spawns its own; naming the same map again keeps them.
  REQUIRE(items.spawnChest(ogreChest(true)));
  items.enterMap("twna01");
  REQUIRE(items.chests().size() == 1);
  CHECK(items.chests().front()->taken);
}

TEST_CASE("map.c changeMap: a load of the same map clears the last load's ground and chests") {
  FieldItems items{{.groundSlots = 11, .chestSlots = 2, .reachCells = 1}};
  const dwe::assets::ItemHandle item = table().handleOf(table().entries().front());
  items.loadMap("trop06", 1);
  REQUIRE(items.spawn(item, {.column = 67, .row = 42}, 0.0F));
  REQUIRE(items.spawnChest(ogreChest(false)));
  // Naming the map, or the same load again, keeps them.
  items.enterMap("trop06");
  items.loadMap("trop06", 1);
  REQUIRE(items.ground().size() == 1);
  REQUIRE(items.chests().size() == 1);

  // A script's warp back onto trop06 (the forest's trap, script 18): a new load, nothing stays,
  // and the head script's roll lays its items on free ground instead of over the last ones.
  items.loadMap("trop06", 2);
  CHECK(items.ground().empty());
  CHECK(items.chests().empty());
  REQUIRE(items.spawn(item, {.column = 67, .row = 42}, 0.0F));
  CHECK(items.ground().size() == 1);
}
