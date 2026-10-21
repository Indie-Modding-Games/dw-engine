#include <dwe/assets/file_system.hpp>
#include <dwe/assets/map_document.hpp>
#include <dwe/assets/map_table.hpp>
#include <dwe/assets/map_warps.hpp>
#include <dwe/assets/pack_manifest.hpp>
#include <dwe/assets/virtual_file_system.hpp>

#include <doctest/doctest.h>

#include <array>
#include <format>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "map_documents.hpp"

namespace {

using dwe::assets::MapDocument;
using dwe::assets::MapTable;
using dwe::assets::PackOrigin;
using dwe::assets::VirtualFileSystem;
using dwe::assets::WarpIssue;
using dwe::assets::WarpProblem;

template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
    return result ? std::string{} : result.error().message;
}

// Exit 0 of the fixture map, whose trigger (110) is in its collision.
constexpr std::string_view FixtureExit = "  target_map: 118\n  target_map_id: \"room08\"\n";

// A pack's new map, which the engine numbers after the base's.
constexpr std::string_view CaveRow = R"(- id: "my_mod:cave"
  filename: "CAVE"
  images_8bpp: 0
  images_4bpp: 0
  flags: 64
  sound_id: 0
  fixed_time_of_day: true
  has_npcs: false
  doors: null
  toilet: null
  loading_name: 0
)";

// The fixture map with exit 0 naming its target as `exit` does.
std::string mapWithExit(std::string_view exit) {
    std::string text = dwe::assets::testing::fixtureMap();
    const std::size_t at = text.find(FixtureExit);
    REQUIRE(at != std::string::npos);
    text.replace(at, FixtureExit.size(), exit);
    return text;
}

// The base pack's maps (twna01 104, room08 118, map_119 119), the engine's own, and a mod that adds my_mod:cave
// (120), whose map data is the fixture map with exit 0 as `caveExit` names it. `fakeBase` adds a mod that declares the
// base layer and replaces twna01's data.
class Packs {
public:
    explicit Packs(std::string_view caveExit, bool fakeBase = false) {
        memory_.addFile("packs/base/pack.yaml", "id: base\nversion: 1.0.0\nlayer: base\n");
        memory_.addFile("packs/base/data/maps.yaml", std::string{dwe::assets::testing::FixtureTable});
        memory_.addFile("packs/base/map/twna01/data.yaml", dwe::assets::testing::fixtureMap());
        memory_.addFile("packs/my_mod/pack.yaml", "id: my_mod\nversion: 1.0.0\nlayer: mods\n");
        memory_.addFile("packs/my_mod/data/maps.yaml", std::format("maps:\n{}", CaveRow));
        memory_.addFile("packs/my_mod/map/my_mod/cave/data.yaml", mapWithExit(caveExit));
        memory_.addFile("packs/fake/pack.yaml", "id: fake\nversion: 1.0.0\nlayer: base\npriority: 10\n");
        memory_.addFile("packs/fake/map/twna01/data.yaml", mapWithExit(caveExit));
        std::vector<dwe::assets::PackLocation> locations{
            {.fileSystem = &memory_, .root = "packs/base", .origin = PackOrigin::Engine},
            {.fileSystem = &memory_, .root = "packs/my_mod", .origin = PackOrigin::Mod},
        };
        if (fakeBase) {
            locations.push_back({.fileSystem = &memory_, .root = "packs/fake", .origin = PackOrigin::Mod});
        }
        auto vfs = VirtualFileSystem::mount(locations);
        REQUIRE_MESSAGE(vfs.has_value(), errorOf(vfs));
        vfs_ = std::make_unique<VirtualFileSystem>(std::move(*vfs));
        auto maps = dwe::assets::loadMapTable(*vfs_);
        REQUIRE_MESSAGE(maps.has_value(), errorOf(maps));
        maps_ = std::move(*maps);
    }

    // The map's document with its warps resolved, and their problems.
    [[nodiscard]] std::pair<MapDocument, std::vector<WarpIssue>> resolved(std::string_view mapId) const {
        auto map = dwe::assets::loadMapDocument(*vfs_, mapId);
        REQUIRE_MESSAGE(map.has_value(), errorOf(map));
        auto issues = dwe::assets::resolveWarpTargets(*map, maps_, dwe::assets::isBaseMapData(*vfs_, mapId));
        return {std::move(*map), std::move(issues)};
    }

    // Exit 0 of the base map, named as `exit` does, resolved as the base pack's.
    [[nodiscard]] std::vector<WarpIssue> baseExit(std::string_view exit) const {
        auto map = dwe::assets::parseMapDocument(mapWithExit(exit));
        REQUIRE_MESSAGE(map.has_value(), errorOf(map));
        return dwe::assets::resolveWarpTargets(*map, maps_, true);
    }

    [[nodiscard]] const MapTable& maps() const { return maps_; }
    [[nodiscard]] const VirtualFileSystem& files() const { return *vfs_; }

private:
    dwe::assets::MemoryFileSystem memory_;
    std::unique_ptr<VirtualFileSystem> vfs_;
    MapTable maps_;
};

} // namespace

TEST_CASE("the base pack's warps name their target both ways, and lead to it") {
    const Packs packs{FixtureExit};
    const auto [map, issues] = packs.resolved("twna01");
    CHECK(issues.empty());
    CHECK(map.warp(0)->targetMapId == "room08");
    CHECK_FALSE(map.warp(1)->targetMapId);
    // An id with no index, or an index of a map with no id, is not what the extractor writes.
    CHECK(packs.baseExit("  target_map: -1\n  target_map_id: \"room08\"\n").front().problem ==
          WarpProblem::BaseNeedsBoth);
    CHECK(packs.baseExit("  target_map: 118\n  target_map_id: null\n").front().problem == WarpProblem::BaseNeedsBoth);
    // A raw index outside the table leads nowhere, as in the original.
    CHECK(packs.baseExit("  target_map: 300\n  target_map_id: null\n").empty());
    // An id next to an index outside the table, or an id of no map next to a good index, is a mistake.
    const auto outside = packs.baseExit("  target_map: 300\n  target_map_id: \"room08\"\n");
    REQUIRE(outside.size() == 1);
    CHECK(outside.front().problem == WarpProblem::Disagree);
    CHECK(dwe::assets::warpIssueMessage("twna01", outside.front()).contains("300, no map of the original"));
    const auto unknown = packs.baseExit("  target_map: 118\n  target_map_id: \"room09\"\n");
    REQUIRE(unknown.size() == 1);
    CHECK(unknown.front().problem == WarpProblem::UnknownTarget);
    CHECK(dwe::assets::disablesWarp(unknown.front().problem));
}

TEST_CASE("only the engine's own pack of the base layer is the base pack, never a mod that declares that layer") {
    const Packs packs{"  target_map: -1\n  target_map_id: \"room08\"\n", true};
    CHECK_FALSE(dwe::assets::isBaseMapData(packs.files(), "twna01"));
    CHECK_FALSE(dwe::assets::isBaseMapData(packs.files(), "my_mod:cave"));
    // The mod's twna01 names its target by id alone, as a mod may: no problem.
    const auto [map, issues] = packs.resolved("twna01");
    CHECK(issues.empty());
    CHECK(map.warp(0)->targetMapId == "room08");
    CHECK(dwe::assets::isBaseMapData(Packs{FixtureExit}.files(), "twna01"));
}

TEST_CASE("a pack's warp may name its target by id alone, even a pack's map") {
    const Packs packs{"  target_map: -1\n  target_map_id: \"my_mod:cave\"\n"};
    const auto [map, issues] = packs.resolved("my_mod:cave");
    CHECK(issues.empty());
    CHECK(map.warp(0)->targetMapId == "my_mod:cave");
}

TEST_CASE("a pack's warp with the original's index alone leads to the base's map of that index") {
    SUBCASE("target_map_id null") {
        const Packs packs{"  target_map: 118\n  target_map_id: null\n"};
        const auto [map, issues] = packs.resolved("my_mod:cave");
        CHECK(issues.empty());
        CHECK(map.warp(0)->targetMapId == "room08");
    }
    SUBCASE("target_map_id left out") {
        const Packs packs{"  target_map: 118\n"};
        const auto [map, issues] = packs.resolved("my_mod:cave");
        CHECK(issues.empty());
        CHECK(map.warp(0)->targetMapId == "room08");
    }
    SUBCASE("never a pack's map, whose index is the engine's: that warp is dead") {
        const Packs packs{"  target_map: 120\n  target_map_id: null\n"};
        const dwe::assets::MapEntry* cave = packs.maps().find("my_mod:cave");
        REQUIRE(cave != nullptr);
        CHECK(cave->index == 120);
        CHECK_FALSE(cave->originalIndex);
        CHECK(packs.maps().findByOriginalIndex(cave->index) == nullptr);
        const auto [map, issues] = packs.resolved("my_mod:cave");
        REQUIRE(issues.size() == 1);
        CHECK(issues.front().problem == WarpProblem::Dead);
        CHECK_FALSE(map.warp(0)->targetMapId);
        CHECK(dwe::assets::warpIssueMessage("my_mod:cave", issues.front()).contains("target_map (120)"));
    }
}

TEST_CASE("a pack's warp with an id of no map and an index of the original leads there, and the id is reported") {
    const Packs packs{"  target_map: 118\n  target_map_id: \"my_mod:nowhere\"\n"};
    const auto [map, issues] = packs.resolved("my_mod:cave");
    REQUIRE(issues.size() == 1);
    CHECK(issues.front().problem == WarpProblem::UnknownIdByIndex);
    CHECK_FALSE(dwe::assets::disablesWarp(issues.front().problem));
    CHECK(map.warp(0)->targetMapId == "room08");
    CHECK(dwe::assets::warpIssueMessage("my_mod:cave", issues.front()).contains("'my_mod:nowhere' is no map"));
}

TEST_CASE("the editor's shape of a warp into a pack's map is a dead warp: named, disabled, never a crash") {
    // dw1fe writes target_map -1 and drops target_map_id for a target it cannot number; the exit's trigger is there.
    for (const std::string_view exit : {"  target_map: -1\n  target_map_id: null\n", "  target_map: -1\n"}) {
        const Packs packs{exit};
        const auto [map, issues] = packs.resolved("my_mod:cave");
        REQUIRE(issues.size() == 1);
        CHECK(issues.front().exit == 0);
        CHECK(issues.front().problem == WarpProblem::Dead);
        CHECK_FALSE(map.warp(0)->targetMapId);
        const std::string message = dwe::assets::warpIssueMessage("my_mod:cave", issues.front());
        CHECK(message.find("map 'my_mod:cave', exit 0") != std::string::npos);
        CHECK(message.find("may have dropped target_map_id") != std::string::npos);
    }
    // The other exits have no trigger: unused, as in the original.
    const Packs packs{"  target_map: -1\n  target_map_id: null\n"};
    CHECK(packs.resolved("my_mod:cave").second.size() == 1);
}

TEST_CASE("a pack's warp whose id and index disagree, or whose id names no map, leads nowhere") {
    SUBCASE("disagree") {
        const Packs packs{"  target_map: 104\n  target_map_id: \"room08\"\n"};
        const auto [map, issues] = packs.resolved("my_mod:cave");
        REQUIRE(issues.size() == 1);
        CHECK(issues.front().problem == WarpProblem::Disagree);
        CHECK_FALSE(map.warp(0)->targetMapId);
        CHECK(dwe::assets::warpIssueMessage("my_mod:cave", issues.front()).find("twna01") != std::string::npos);
    }
    SUBCASE("an unknown id") {
        const Packs packs{"  target_map: -1\n  target_map_id: \"my_mod:nowhere\"\n"};
        const auto [map, issues] = packs.resolved("my_mod:cave");
        REQUIRE(issues.size() == 1);
        CHECK(issues.front().problem == WarpProblem::UnknownTarget);
        CHECK_FALSE(map.warp(0)->targetMapId);
    }
}

TEST_CASE("a pack's full copy of data/maps, as an editor saves it, composes with one door changed") {
    // The base's entries restated with their indices, the doors section with one door moved, and the pack's new map
    // without an index.
    std::string copy{dwe::assets::testing::FixtureTable};
    const std::string door = "{model_id: 9, position: {x: 1925, y: 0, z: -915}, rotation: 1024}";
    const std::size_t at = copy.find(door);
    REQUIRE(at != std::string::npos);
    copy.replace(at, door.size(), "{model_id: 9, position: {x: 2000, y: 0, z: -915}, rotation: 1024}");
    const std::size_t doors = copy.find("doors:\n-");
    REQUIRE(doors != std::string::npos);
    copy.insert(doors, CaveRow);

    dwe::assets::MemoryFileSystem memory;
    memory.addFile("packs/base/pack.yaml", "id: base\nversion: 1.0.0\nlayer: base\n");
    memory.addFile("packs/base/data/maps.yaml", std::string{dwe::assets::testing::FixtureTable});
    memory.addFile("packs/my_mod/pack.yaml", "id: my_mod\nversion: 1.0.0\nlayer: mods\n");
    memory.addFile("packs/my_mod/data/maps.yaml", copy);
    const std::array<dwe::assets::PackLocation, 2> locations{{
        {.fileSystem = &memory, .root = "packs/base", .origin = PackOrigin::Engine},
        {.fileSystem = &memory, .root = "packs/my_mod", .origin = PackOrigin::Mod},
    }};
    auto vfs = VirtualFileSystem::mount(locations);
    REQUIRE_MESSAGE(vfs.has_value(), errorOf(vfs));
    const auto maps = dwe::assets::loadMapTable(*vfs);
    REQUIRE_MESSAGE(maps.has_value(), errorOf(maps));
    CHECK(maps->maps.size() == 4);
    CHECK(maps->findByOriginalIndex(118)->id == "room08");
    CHECK_FALSE(maps->find("my_mod:cave")->originalIndex);
    CHECK(maps->doorsOf(*maps->find("room08"))->doors.at(1).position.x == 2000);
}
