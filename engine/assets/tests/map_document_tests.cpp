#include <dwe/assets/map_document.hpp>
#include <dwe/assets/virtual_file_system.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <format>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

#include "map_documents.hpp"

using dwe::assets::CollisionCell;
using dwe::assets::CollisionKind;
using dwe::assets::MapCollision;
using dwe::assets::MapDocument;
using dwe::assets::MapWarp;
using dwe::assets::MemoryFileSystem;
using dwe::assets::VirtualFileSystem;
using dwe::assets::testing::fixtureMap;

namespace {

constexpr float Pi = std::numbers::pi_v<float>;

// The error of a failed result, or an empty text. doctest builds messages even for passing checks.
template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
  return result ? std::string{} : result.error().message;
}

constexpr std::string_view FixtureObjects = R"(
  sprites:
  - tex_x: 96
    tex_y: 40
    width: 16
    height: 8
    position: {x: 1, y: 2, z: 5000}
    clut: -1
    transparency: 4
  instances:
  - frames:
    - sprite: 0
      time: 1
    - sprite: -2
      time: 30
    x: 392
    y: 0
    flag: 1)";

constexpr std::string_view FixtureNpc = R"(
- type_id: 117
  digimon: "species_f"
  follow_mode: 1
  position: {x: -161, y: 0, z: -2451}
  rotation: {x: 0, y: 3584, z: 0}
  tracking_range: 1000
  unknown_12: 0
  script_id: 5
  hp: 500
  mp: 500
  current_hp: 500
  current_mp: 500
  off: 50
  def: 50
  speed: 50
  brain: 50
  bits: 100
  charge_mode: 1
  unknown_2a: 1
  moves: [46, 51, 53, 255]
  moves_prio: [50, 30, 20, 0]
  flee: {x: 2718, y: 0, z: 202}
  ai_sections: [0, -1, -1, -1, -1, -1, -1, -1]
  waypoints:
  - {x: 10, y: 0, z: 0})";

} // namespace

TEST_CASE("a map document keeps every field of the data as it is") {
  const auto map = dwe::assets::parseMapDocument(fixtureMap(FixtureObjects, FixtureNpc));
  REQUIRE_MESSAGE(map.has_value(), errorOf(map));

  CHECK(map->camera.viewpoint == glm::ivec3{0, -4200, -5900});
  CHECK(map->camera.reference == glm::ivec3{0, 0, -250});
  CHECK(map->camera.projection == 1800);
  CHECK(map->lights[0].direction == glm::ivec3{0, 160, 90});
  CHECK(map->lights[0].color.g == 170);
  CHECK(map->regions.b[3] == 4);

  CHECK(map->tiles.width == 3);
  CHECK(map->tiles.height == 2);
  CHECK(map->tiles.cells == std::vector<std::int32_t>{0, 1, -1, 2, 3, 4});
  REQUIRE(map->images.size() == 1);
  CHECK(map->images[0].size == 66000);

  if (!map->objects) {
    FAIL("the map has no objects");
    return;
  }
  REQUIRE(map->objects->sprites.size() == 1);
  CHECK(map->objects->sprites[0].position == glm::ivec3{1, 2, 5000});
  CHECK(map->objects->sprites[0].clut == -1);
  REQUIRE(map->objects->instances.size() == 1);
  REQUIRE(map->objects->instances[0].frames.size() == 2);
  CHECK(map->objects->instances[0].frames[1].sprite == dwe::assets::MapSpriteFrame::BlankSprite);
  CHECK(map->objects->instances[0].flag == dwe::assets::MapObjectInstance::HiddenFlag);

  REQUIRE(map->warps.size() == 10);
  const MapWarp* exit0 = map->warp(0);
  REQUIRE(exit0 != nullptr);
  CHECK(exit0->targetMapId == std::optional<std::string>{"room08"});
  CHECK(exit0->targetExit == 2);
  CHECK_FALSE(map->warp(1)->targetMapId.has_value());
  CHECK(map->warp(10) == nullptr);

  REQUIRE(map->npcs.size() == 1);
  CHECK(map->npcs[0].digimon == std::optional<std::string>{"species_f"});
  CHECK(map->npcs[0].moves[3] == 255);
  CHECK(map->npcs[0].aiSections[1] == -1);
  CHECK(map->npcs[0].waypoints.size() == 1);
  CHECK(map->digimonCount == 1);

  CHECK(map->collision.kind({.column = 0, .row = 5}) == CollisionKind::Blocked);
  CHECK(map->collision.kind({.column = 5, .row = 5}) == CollisionKind::Free);
  CHECK(map->collision.trigger({.column = 10, .row = 20}) == std::optional<std::uint8_t>{110});
  CHECK(map->collision.trigger({.column = 11, .row = 20}) == std::optional<std::uint8_t>{51});
  CHECK(map->collision.kind({.column = -1, .row = 5}) == CollisionKind::Blocked);
  CHECK(map->collision.kind({.column = 5, .row = 100}) == CollisionKind::Blocked);
}

TEST_CASE("a map document without objects has none") {
  const auto map = dwe::assets::parseMapDocument(fixtureMap());
  REQUIRE_MESSAGE(map.has_value(), errorOf(map));
  CHECK_FALSE(map->objects.has_value());
  CHECK(map->npcs.empty());
}

TEST_CASE("a broken map document says where") {
  SUBCASE("an unknown key") {
    const auto map = dwe::assets::parseMapDocument(fixtureMap() + "extra: 1\n");
    REQUIRE_FALSE(map.has_value());
    CHECK(map.error().message.find("unknown key 'extra'") != std::string::npos);
  }
  SUBCASE("a cell outside the legend") {
    std::string text = fixtureMap();
    text.replace(text.rfind('.'), 1, "Z");
    const auto map = dwe::assets::parseMapDocument(text);
    REQUIRE_FALSE(map.has_value());
    CHECK(map.error().message.find("not in the legend") != std::string::npos);
    CHECK(map.error().message.find("line ") != std::string::npos);
  }
  SUBCASE("a short tile row") {
    std::string text = fixtureMap();
    text.replace(text.find("\"2 3 4\""), 7, "\"2 3\"");
    const auto map = dwe::assets::parseMapDocument(text);
    REQUIRE_FALSE(map.has_value());
    CHECK(map.error().message.find("3 tiles") != std::string::npos);
  }
  SUBCASE("a number that is not one") {
    std::string text = fixtureMap();
    text.replace(text.find("1800"), 4, "18x0");
    const auto map = dwe::assets::parseMapDocument(text);
    REQUIRE_FALSE(map.has_value());
    CHECK(map.error().message.find("'18x0'") != std::string::npos);
  }
}

TEST_CASE("the hex collision encoding gives the same cells") {
  std::string text = fixtureMap();
  const std::size_t start = text.find("collision:");
  std::string hex =
      "collision:\n  width: 100\n  height: 100\n  encoding: \"hex\"\n  legend: []\n  rows:\n";
  for (int row = 0; row < MapCollision::Size; ++row) {
    std::string cells;
    for (int column = 0; column < MapCollision::Size; ++column) {
      if (row == 3 && column == 4) {
        cells += "6e";
      } else {
        cells += column == 0 ? std::string_view{"80"} : std::string_view{"00"};
      }
    }
    hex += std::format("  - \"{}\"\n", cells);
  }
  text.replace(start, std::string::npos, hex);
  const auto map = dwe::assets::parseMapDocument(text);
  REQUIRE_MESSAGE(map.has_value(), errorOf(map));
  CHECK(map->collision.trigger({.column = 4, .row = 3}) == std::optional<std::uint8_t>{110});
  CHECK(map->collision.isBlocked({.column = 0, .row = 50}));
  CHECK(map->collision.kind({.column = 1, .row = 50}) == CollisionKind::Free);
}

TEST_CASE("collision cells follow the original's grid in world space") {
  // map_collision.c: column x / 100 + 50, row 50 - z / 100 in the original's axes, where z is the
  // world's -z.
  using dwe::assets::collisionCellAt;
  CHECK(collisionCellAt({0.0F, 0.0F, 0.0F}) == CollisionCell{.column = 50, .row = 50});
  CHECK(collisionCellAt({99.9F, 0.0F, 99.9F}) == CollisionCell{.column = 50, .row = 50});
  CHECK(collisionCellAt({-0.1F, 0.0F, -0.1F}) == CollisionCell{.column = 49, .row = 49});
  CHECK(collisionCellAt({-4999.0F, 0.0F, 4999.0F}) == CollisionCell{.column = 0, .row = 99});
  // An original spawn of the US data (twna01, exit 1): x -2450, z -2525 is column 25, row 75, as
  // getTileTrigger.
  CHECK(collisionCellAt(dwe::assets::toWorldPosition({-2450, 0, -2525})) ==
        CollisionCell{.column = 25, .row = 75});

  const CollisionCell cell{.column = 25, .row = 75};
  CHECK(dwe::assets::collisionCellMin(cell) == glm::vec3{-2500.0F, 0.0F, 2500.0F});
  CHECK(dwe::assets::collisionCellCenter(cell) == glm::vec3{-2450.0F, 0.0F, 2550.0F});
  CHECK(collisionCellAt(dwe::assets::collisionCellCenter(cell)) == cell);
}

TEST_CASE("original rotations become yaws that face the same way") {
  using dwe::assets::forwardOfYaw;
  using dwe::assets::yawFromOriginal;
  // Rotation 0 faces the original's -z, the world's +z; 1024 faces the original's -x; 2048 the
  // original's +z.
  const glm::vec3 r0 = forwardOfYaw(yawFromOriginal(0));
  const glm::vec3 r1024 = forwardOfYaw(yawFromOriginal(1024));
  const glm::vec3 r2048 = forwardOfYaw(yawFromOriginal(2048));
  const glm::vec3 r3072 = forwardOfYaw(yawFromOriginal(3072));
  CHECK(glm::distance(r0, glm::vec3{0.0F, 0.0F, 1.0F}) < 1e-5F);
  CHECK(glm::distance(r1024, glm::vec3{-1.0F, 0.0F, 0.0F}) < 1e-5F);
  CHECK(glm::distance(r2048, glm::vec3{0.0F, 0.0F, -1.0F}) < 1e-5F);
  CHECK(glm::distance(r3072, glm::vec3{1.0F, 0.0F, 0.0F}) < 1e-5F);
  CHECK(yawFromOriginal(1024) == doctest::Approx(-Pi / 2.0F));

  for (const std::int32_t rotation : {0, 1, 512, 1024, 2048, 3700, 4095}) {
    CHECK(dwe::assets::yawToOriginal(yawFromOriginal(rotation)) == rotation);
    CHECK(dwe::assets::yawToOriginal(
              dwe::assets::yawOfDirection(forwardOfYaw(yawFromOriginal(rotation)))) == rotation);
  }
  CHECK(dwe::assets::yawToOriginal(yawFromOriginal(-1024)) == 3072);
}

TEST_CASE("a warp spawns in world space") {
  const MapWarp warp{.exit = 1,
                     .spawn = {-2500, 0, -2525},
                     .rotation = 3072,
                     .targetMap = 112,
                     .targetMapId = "test_station",
                     .targetExit = 1};
  const dwe::assets::SpawnPoint spawn = dwe::assets::warpSpawn(warp);
  CHECK(spawn.position == glm::vec3{-2500.0F, 0.0F, 2525.0F});
  CHECK(glm::distance(dwe::assets::forwardOfYaw(spawn.yaw), glm::vec3{1.0F, 0.0F, 0.0F}) < 1e-5F);
}

TEST_CASE("a map document is loaded by id with its backgrounds and textures") {
  MemoryFileSystem memory;
  memory.addFile("base/pack.yaml", "id: base\nversion: 1.0.0\nlayer: base\n");
  memory.addFile("base/map/twna01/data.yaml", fixtureMap());
  memory.addFile("base/map/twna01/background_night.png", "png");
  memory.addFile("base/map/twna01/background_day.png", "png");
  memory.addFile("base/map/twna01/background_palette3.png", "png");
  memory.addFile("base/map/twna01/background_dusk.png", "png");
  memory.addFile("base/map/twna01/texture8/001.png", "png");
  memory.addFile("base/map/twna01/texture8/000.png", "png");
  memory.addFile("base/map/twna01/texture8/000_meta.yaml", "x: 1");
  memory.addFile("base/map/twna01/texture4/000.png", "png");
  memory.addFile("base/map/twna011/background_day.png", "png");
  const std::vector<std::filesystem::path> roots{"base"};
  const auto vfs = VirtualFileSystem::mount(memory, roots);
  REQUIRE_MESSAGE(vfs.has_value(), errorOf(vfs));

  const auto map = dwe::assets::loadMapDocument(*vfs, "twna01");
  REQUIRE_MESSAGE(map.has_value(), errorOf(map));
  CHECK(map->id == "twna01");
  REQUIRE(map->assets.backgrounds.size() == 4);
  CHECK(map->assets.backgrounds[0].variant == "day");
  CHECK(map->assets.backgrounds[1].variant == "dusk");
  CHECK(map->assets.backgrounds[2].variant == "night");
  CHECK(map->assets.backgrounds[3].variant == "palette3");
  REQUIRE(map->assets.background("night") != nullptr);
  CHECK(map->assets.background("night")->str() == "map/twna01/background_night");
  CHECK(map->assets.background("noon") == nullptr);
  REQUIRE(map->assets.textures.size() == 3);
  CHECK(map->assets.textures[0].image.str() == "map/twna01/texture8/000");
  CHECK(map->assets.textures[0].meta.value_or(map->assets.textures[0].image).str() ==
        "map/twna01/texture8/000_meta");
  CHECK(map->assets.textures[1].index == 1);
  CHECK_FALSE(map->assets.textures[1].meta.has_value());
  CHECK(map->assets.textures[2].bitsPerPixel == 4);

  const auto missing = dwe::assets::loadMapDocument(*vfs, "nowhere");
  REQUIRE_FALSE(missing.has_value());
  CHECK(missing.error().message.find("nowhere") != std::string::npos);
  CHECK_FALSE(dwe::assets::loadMapDocument(*vfs, "Bad/Id").has_value());
}
