#include <dwe/assets/content_check.hpp>
#include <dwe/assets/map_document.hpp>
#include <dwe/assets/map_patch.hpp>
#include <dwe/assets/map_table.hpp>
#include <dwe/assets/map_warps.hpp>
#include <dwe/assets/virtual_file_system.hpp>

#include <doctest/doctest.h>

#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "map_documents.hpp"

using dwe::assets::AssetId;
using dwe::assets::MapPatchText;
using dwe::assets::MemoryFileSystem;
using dwe::assets::PackLocation;
using dwe::assets::PackOrigin;
using dwe::assets::VirtualFileSystem;
using dwe::assets::testing::fixtureMap;

namespace {

template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
  return result ? std::string{} : result.error().message;
}

MapPatchText patchOf(std::string text, std::string pack = "mod_a", bool baseRights = false) {
  return {.pack = std::move(pack),
          .baseRights = baseRights,
          .file = "mods/patch.yaml",
          .text = std::move(text)};
}

constexpr std::string_view Objects = R"(
  sprites:
  - {tex_x: 96, tex_y: 40, width: 16, height: 8, position: {x: 1, y: 2, z: 3}, clut: -1, transparency: 4}
  instances:
  - frames:
    - {sprite: 0, time: 1}
    x: 392
    y: 0
    flag: 1)";

constexpr std::string_view ExitZeroToRoom = R"(warps:
- exit: 0
  target_exit: 5
  spawn: {x: 100, y: 0, z: -200}
)";

} // namespace

TEST_CASE("a map patch changes one exit field by field and leaves the others") {
  const std::array patches{patchOf(std::string{ExitZeroToRoom})};
  const auto map = dwe::assets::patchMapDocument(fixtureMap(), patches);
  REQUIRE_MESSAGE(map.has_value(), errorOf(map));
  const dwe::assets::MapWarp* exit = map->warp(0);
  REQUIRE(exit != nullptr);
  CHECK(exit->targetExit == 5);
  CHECK(exit->spawn == glm::ivec3{100, 0, -200});
  CHECK(exit->rotation == 3072);
  CHECK(exit->targetMapId == "room08");
  const dwe::assets::MapExitPatch* patch = map->exitPatch(0);
  REQUIRE(patch != nullptr);
  CHECK(patch->pack == "mod_a");
  CHECK_FALSE(patch->baseRights);
  CHECK(map->exitPatch(1) == nullptr);
  CHECK(map->warps.size() == 10);
  CHECK(map->patches == std::vector<std::string>{"mod_a"});
}

TEST_CASE("map patches apply bottom to top, each over the one below") {
  const std::array patches{
      patchOf("camera: {projection: 1900}\nlights:\n- {index: 1, color: {r: 9, g: 8, b: 7}}\n"
              "warps:\n- {exit: 2, target_exit: 1}\n",
              "mod_a"),
      patchOf("warps:\n- {exit: 2, target_exit: 4, rotation: 1024}\n", "mod_b")};
  const auto map = dwe::assets::patchMapDocument(fixtureMap(), patches);
  REQUIRE_MESSAGE(map.has_value(), errorOf(map));
  CHECK(map->camera.projection == 1900);
  CHECK(map->camera.viewpoint == glm::ivec3{0, -4200, -5900});
  CHECK(map->lights[1].color.r == 9);
  CHECK(map->lights[1].direction == glm::ivec3{1, 1, 1});
  CHECK(map->warp(2)->targetExit == 4);
  CHECK(map->warp(2)->rotation == 1024);
  REQUIRE(map->exitPatch(2) != nullptr);
  CHECK(map->exitPatch(2)->pack == "mod_b");
  CHECK(map->patchedExits.size() == 1);
  CHECK(map->patches == std::vector<std::string>{"mod_a", "mod_b"});
}

TEST_CASE("a map patch sets rectangles of collision cells, after a whole grid") {
  const std::string grid = "collision:\n  grid:\n    width: 100\n    height: 100\n"
                           "    encoding: legend\n    legend:\n    - {cell: \".\", value: 0}\n"
                           "    rows:\n" +
                           [] {
                             std::string rows;
                             for (int row = 0; row < 100; ++row) {
                               rows += "    - \"" + std::string(100, '.') + "\"\n";
                             }
                             return rows;
                           }();

  SUBCASE("rectangles set every cell they cover") {
    const std::array patches{
        patchOf("collision:\n  cells:\n  - {column: 40, row: 50, width: 3, height: 2, value: 128}\n"
                "  - {column: 41, row: 51, value: 0}\n")};
    const auto map = dwe::assets::patchMapDocument(fixtureMap(), patches);
    REQUIRE_MESSAGE(map.has_value(), errorOf(map));
    CHECK(map->collision.value({.column = 40, .row = 50}) == 128);
    CHECK(map->collision.value({.column = 42, .row = 51}) == 128);
    CHECK(map->collision.value({.column = 41, .row = 51}) == 0);
    CHECK(map->collision.value({.column = 43, .row = 50}) == 0);
    CHECK(map->collision.value({.column = 10, .row = 20}) == 110);
  }

  SUBCASE("a higher patch's grid replaces the rectangles below it; its own apply over it") {
    const std::array patches{
        patchOf("collision:\n  cells:\n  - {column: 40, row: 50, value: 128}\n", "mod_a"),
        patchOf(grid + "  cells:\n  - {column: 1, row: 2, value: 51}\n", "mod_b")};
    const auto map = dwe::assets::patchMapDocument(fixtureMap(), patches);
    REQUIRE_MESSAGE(map.has_value(), errorOf(map));
    CHECK(map->collision.value({.column = 40, .row = 50}) == 0);
    CHECK(map->collision.value({.column = 0, .row = 0}) == 0);
    CHECK(map->collision.value({.column = 10, .row = 20}) == 0);
    CHECK(map->collision.value({.column = 1, .row = 2}) == 51);
  }
}

TEST_CASE("a map patch changes object sprites and instances by place") {
  const std::array patches{patchOf("objects:\n  sprites:\n  - {index: 0, clut: 3}\n"
                                   "  instances:\n  - {index: 0, x: 10, flag: 0}\n")};
  const auto map = dwe::assets::patchMapDocument(fixtureMap(Objects), patches);
  REQUIRE_MESSAGE(map.has_value(), errorOf(map));
  if (!map->objects) {
    FAIL("the map has no objects");
    return;
  }
  CHECK(map->objects->sprites[0].clut == 3);
  CHECK(map->objects->sprites[0].texX == 96);
  CHECK(map->objects->instances[0].x == 10);
  CHECK(map->objects->instances[0].flag == 0);
  CHECK(map->objects->instances[0].frames.size() == 1);

  const auto none = dwe::assets::patchMapDocument(fixtureMap(), patches);
  REQUIRE_FALSE(none.has_value());
  CHECK(none.error().message.contains("no objects"));
}

TEST_CASE("a map patch that names what the data does not have is an error naming its file") {
  const auto fails = [](std::string text, std::string_view expected) {
    const std::array patches{patchOf(std::move(text))};
    const auto map = dwe::assets::patchMapDocument(fixtureMap(), patches);
    REQUIRE_FALSE(map.has_value());
    CHECK_MESSAGE(map.error().message.starts_with("patch mods/patch.yaml: "), map.error().message);
    CHECK_MESSAGE(map.error().message.contains(expected), map.error().message);
  };
  fails("warps:\n- {exit: 12, target_exit: 0}\n", "no 'warps' with 'exit' 12");
  fails("npcs:\n- {index: 0, script_id: 3}\n", "no 'npcs' with 'index' 0");
  fails("lights:\n- {index: 3, color: {r: 1, g: 1, b: 1}}\n", "no 'lights' with 'index' 3");
  fails("warps:\n- {exit: 1, target_exit: 0}\n- {exit: 1, rotation: 0}\n", "changed twice");
  fails("warps:\n- {target_exit: 0}\n", "with its 'exit'");
  fails("warps:\n- {exit: 1, rotation: north}\n", "line 2");
  fails("warps:\n- {exit: 1, sideways: 1}\n", "sideways");
  fails("collision:\n  cells:\n  - {column: 98, row: 0, width: 3, value: 1}\n", "leave the");
  fails("music: 3\n", "music");
}

TEST_CASE("a map patch is checked on its own for what it names, not for the data") {
  CHECK(dwe::assets::checkMapPatch("warps:\n- {exit: 12, target_exit: 0}\n").has_value());
  CHECK(dwe::assets::checkMapPatch("{}").has_value());
  CHECK_FALSE(dwe::assets::checkMapPatch("warps:\n- {exit: -1}\n").has_value());
  CHECK_FALSE(
      dwe::assets::checkMapPatch("collision:\n  cells:\n  - {column: 0, row: 0}\n").has_value());
  CHECK_FALSE(dwe::assets::checkMapPatch("collision:\n  grid: {width: 100}\n").has_value());
  CHECK_FALSE(dwe::assets::checkMapPatch("camera: {zoom: 2}\n").has_value());
  CHECK_FALSE(dwe::assets::checkMapPatch("- 1\n").has_value());
}

TEST_CASE("a map's patch id sits next to its data, and both give the map back") {
  CHECK(dwe::assets::mapPatchId("twna01").value().str() == "map/twna01/patch");
  CHECK(dwe::assets::mapPatchId("example_mod:meadow").value().str() ==
        "map/example_mod/meadow/patch");
  const auto mapOf = [](std::string_view id) {
    return dwe::assets::mapIdOfAsset(AssetId::parse(id).value());
  };
  CHECK(mapOf("map/twna01/data") == "twna01");
  CHECK(mapOf("map/twna01/patch") == "twna01");
  CHECK(mapOf("map/example_mod/meadow/patch") == "example_mod:meadow");
  CHECK_FALSE(mapOf("map/twna01/texture8/000").has_value());
  CHECK_FALSE(mapOf("map/a/b/c/data").has_value());
  CHECK_FALSE(mapOf("data/maps").has_value());
  CHECK(dwe::assets::isMapPatchId(AssetId::parse("map/twna01/patch").value()));
  CHECK_FALSE(dwe::assets::isMapPatchId(AssetId::parse("map/twna01/data").value()));
}

TEST_CASE("loadMapDocument applies the patches of the data's pack and of the packs above it") {
  MemoryFileSystem memory;
  memory.addFile("base/pack.yaml", "id: base\nversion: 1.0.0\nlayer: base\n");
  memory.addFile("base/map/twna01/data.yaml", fixtureMap());
  memory.addFile("base/map/twna01/background_day.png", "png");
  memory.addFile("low/pack.yaml", "id: low\nversion: 1.0.0\nlayer: mods\n");
  memory.addFile("low/map/twna01/patch.yaml", "warps:\n- {exit: 0, target_exit: 7}\n");
  memory.addFile("high/pack.yaml", "id: high\nversion: 1.0.0\nlayer: mods\npriority: 2\n");
  memory.addFile("high/map/twna01/patch.yaml", "camera: {projection: 1234}\n");

  SUBCASE("every patch over the base's data, bottom to top") {
    const std::vector<std::filesystem::path> roots{"high", "base", "low"};
    const auto vfs = VirtualFileSystem::mount(memory, roots);
    REQUIRE_MESSAGE(vfs.has_value(), errorOf(vfs));
    const auto map = dwe::assets::loadMapDocument(*vfs, "twna01");
    REQUIRE_MESSAGE(map.has_value(), errorOf(map));
    CHECK(map->patches == std::vector<std::string>{"low", "high"});
    CHECK(map->warp(0)->targetExit == 7);
    CHECK(map->camera.projection == 1234);
    CHECK(map->assets.backgrounds.size() == 1);
  }

  SUBCASE("a pack's whole data drops the patches below it") {
    memory.addFile("mid/pack.yaml", "id: mid\nversion: 1.0.0\nlayer: mods\npriority: 1\n");
    memory.addFile("mid/map/twna01/data.yaml", fixtureMap());
    const std::vector<std::filesystem::path> roots{"base", "low", "mid", "high"};
    const auto vfs = VirtualFileSystem::mount(memory, roots);
    REQUIRE_MESSAGE(vfs.has_value(), errorOf(vfs));
    const auto map = dwe::assets::loadMapDocument(*vfs, "twna01");
    REQUIRE_MESSAGE(map.has_value(), errorOf(map));
    CHECK(map->patches == std::vector<std::string>{"high"});
    CHECK(map->warp(0)->targetExit == 2);
    CHECK(map->camera.projection == 1234);
  }

  SUBCASE("a patch that does not apply stops the map from loading") {
    memory.addFile("high/map/twna01/patch.yaml", "npcs:\n- {index: 4, script_id: 1}\n");
    const std::vector<std::filesystem::path> roots{"base", "high"};
    const auto vfs = VirtualFileSystem::mount(memory, roots);
    REQUIRE_MESSAGE(vfs.has_value(), errorOf(vfs));
    const auto map = dwe::assets::loadMapDocument(*vfs, "twna01");
    REQUIRE_FALSE(map.has_value());
    CHECK(map.error().message.contains("twna01"));
    CHECK(map.error().message.contains("high"));
  }
}

TEST_CASE("an exit a mod's patch changes is checked by the mod's rules, not the base's") {
  MemoryFileSystem memory;
  memory.addFile("base/pack.yaml", "id: base\nversion: 1.0.0\nlayer: base\n");
  memory.addFile("base/map/twna01/data.yaml", fixtureMap());
  // Exit 0 by id alone: a pack's way to reach a map, a mistake in the base.
  constexpr std::string_view ById = "warps:\n- {exit: 0, target_map: -1}\n";
  memory.addFile("mod/pack.yaml", "id: mod\nversion: 1.0.0\nlayer: mods\n");
  memory.addFile("mod/map/twna01/patch.yaml", ById);
  memory.addFile("game/pack.yaml", "id: game\nversion: 1.0.0\nlayer: base\npriority: 1\n");
  memory.addFile("game/map/twna01/patch.yaml", ById);
  const auto table = dwe::assets::parseMapTable(dwe::assets::testing::FixtureTable);
  REQUIRE_MESSAGE(table.has_value(), errorOf(table));

  const auto issuesWith = [&](std::string_view patchPack) {
    const std::array locations{
        PackLocation{.fileSystem = &memory, .root = "base", .origin = PackOrigin::Engine},
        PackLocation{.fileSystem = &memory,
                     .root = std::filesystem::path{patchPack},
                     .origin = patchPack == "game" ? PackOrigin::Engine : PackOrigin::Mod}};
    const auto vfs = VirtualFileSystem::mount(locations);
    REQUIRE_MESSAGE(vfs.has_value(), errorOf(vfs));
    auto map = dwe::assets::loadMapDocument(*vfs, "twna01");
    REQUIRE_MESSAGE(map.has_value(), errorOf(map));
    const auto issues =
        dwe::assets::resolveWarpTargets(*map, *table, dwe::assets::isBaseMapData(*vfs, "twna01"));
    return std::pair{issues, map->warp(0)->targetMapId};
  };

  const auto [modIssues, modTarget] = issuesWith("mod");
  CHECK(modIssues.empty());
  CHECK(modTarget == "room08");

  const auto [gameIssues, gameTarget] = issuesWith("game");
  REQUIRE(gameIssues.size() == 1);
  CHECK(gameIssues[0].problem == dwe::assets::WarpProblem::BaseNeedsBoth);
  CHECK_FALSE(gameTarget.has_value());
}

TEST_CASE("the content check loads a patched map and finds patches without a map") {
  MemoryFileSystem memory;
  memory.addFile("base/pack.yaml", "id: base\nversion: 1.0.0\nlayer: base\n");
  memory.addFile("base/map/twna01/data.yaml", fixtureMap());
  memory.addFile("mod/pack.yaml", "id: mod\nversion: 1.0.0\nlayer: mods\n");
  memory.addFile("mod/map/twna01/patch.yaml", "warps:\n- {exit: 11, target_exit: 0}\n");
  memory.addFile("mod/map/nowhere/patch.yaml", "camera: {projection: 1}\n");
  const std::vector<std::filesystem::path> roots{"base", "mod"};
  const auto vfs = VirtualFileSystem::mount(memory, roots);
  REQUIRE_MESSAGE(vfs.has_value(), errorOf(vfs));
  const std::vector<dwe::assets::ContentProblem> problems = dwe::assets::checkContent(*vfs);
  REQUIRE(problems.size() == 2);
  CHECK(problems[0].id.str() == "map/nowhere/patch");
  CHECK(problems[0].message.contains("no data"));
  CHECK(problems[1].id.str() == "map/twna01/data");
  CHECK(problems[1].message.contains("exit' 11"));
}
