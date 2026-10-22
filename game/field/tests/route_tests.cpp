#include <dwe/assets/file_system.hpp>
#include <dwe/assets/map_document.hpp>
#include <dwe/assets/map_table.hpp>
#include <dwe/assets/new_game_story.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/game/field/field.hpp>
#include <dwe/game/field/grid.hpp>
#include <dwe/game/field/rules.hpp>
#include <dwe/game/field/script_warps.hpp>
#include <dwe/test_support/user_assets.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "content_fixture.hpp"

// The vertical slice's route on the real extracted content: File City (twna01) to Jijimon's house (room08) and back,
// driven headlessly with scripted intents, both as stick directions (keyboard, gamepad) and as click-to-move targets.
// docs/reference/campo.md says how the maps were identified.

using dwe::assets::CollisionCell;
using dwe::assets::MapDocument;
using dwe::game::field::Field;
using dwe::game::field::FieldInput;
using dwe::game::field::MapChange;
using dwe::game::field::testing::mountContent;

namespace {

constexpr float Tick = 1.0F / 60.0F;
// Each leg must end well within this.
constexpr int MaxTicksPerLeg = 60 * 60;
// The stick aims at the next waypoint once this close to the current one: less than a tick's run.
constexpr float WaypointReach = 15.0F;

template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
    return result ? std::string{} : result.error().message;
}

enum class Drive : std::uint8_t { Stick, Click };

struct Content {
    dwe::assets::DiskFileSystem disk;
    std::optional<dwe::assets::VirtualFileSystem> vfs;
};

float partnerDistance(const Field& field) {
    const glm::vec3 delta = field.partner().position - field.player().position;
    return std::hypot(delta.x, delta.z);
}

bool isWarpValue(std::int32_t value) {
    return value >= 110 && value < 120;
}

// A path to the nearest cell with trigger `value` that does not cross other warps.
std::vector<glm::vec3> pathToTrigger(const Field& field, std::uint8_t value) {
    const dwe::assets::MapCollision& collision = field.collision();
    std::vector<CollisionCell> goals;
    for (std::int32_t row = 0; row < dwe::assets::MapCollision::Size; ++row) {
        for (std::int32_t column = 0; column < dwe::assets::MapCollision::Size; ++column) {
            if (collision.trigger({.column = column, .row = row}) == value) {
                goals.push_back({.column = column, .row = row});
            }
        }
    }
    return dwe::game::field::findPath(
        collision, field.player().position, goals, field.rules().player.radius,
        [&](CollisionCell cell) { return !isWarpValue(collision.trigger(cell).value_or(0)); });
}

// Walks to a trigger with the given input style until the field asks for a map change.
std::optional<MapChange> walkToTrigger(Field& field, std::uint8_t value, Drive drive) {
    std::vector<glm::vec3> path = pathToTrigger(field, value);
    if (path.empty()) {
        FAIL("no path to trigger ", int{value}, " in ", field.mapId());
        return std::nullopt;
    }
    if (drive == Drive::Click) {
        field.setInput({.run = true, .moveTarget = path.back()});
        field.setInput({.run = true});
        CHECK(field.hasMoveTarget());
    }
    float warpPartnerDistance = 0.0F;
    for (int tick = 0; tick < MaxTicksPerLeg; ++tick) {
        if (drive == Drive::Stick) {
            // As a gamepad would: point the stick, relative to the camera, at the next waypoint of the path.
            const glm::vec3 position = field.player().position;
            while (path.size() > 1 && glm::distance(position, path.front()) <= WaypointReach) {
                path.erase(path.begin());
            }
            const glm::vec3 aim = path.front() - position;
            const glm::vec3 forward = field.cameraForward();
            const glm::vec3 right{-forward.z, 0.0F, forward.x};
            const glm::vec2 direction = glm::normalize(glm::vec2{aim.x, aim.z});
            const glm::vec2 stick{glm::dot(direction, glm::vec2{right.x, right.z}),
                                  glm::dot(direction, glm::vec2{forward.x, forward.z})};
            field.setInput({.stick = stick, .run = true});
        }
        const bool wasLeaving = field.mapChangePending();
        field.tick(Tick);
        if (!wasLeaving && field.mapChangePending()) {
            // The partner kept up: it is within running distance of the player when the warp fires (then the player
            // walks on alone while the screen fades out).
            warpPartnerDistance = partnerDistance(field);
        }
        if (auto change = field.takeMapChange()) {
            MESSAGE(field.mapId(), ": trigger ", change->trigger, " after ", tick + 1, " ticks; partner at ",
                    warpPartnerDistance, " (teleports ", field.partnerTeleports(), ")");
            const auto& partner = field.rules().partner;
            CHECK(warpPartnerDistance < 2.0F * partner.runDistance * partner.mover.radius);
            CHECK(field.partnerTeleports() == 0);
            return change;
        }
    }
    const glm::vec3 position = field.player().position;
    FAIL("the player never reached trigger ", int{value}, " in ", field.mapId(), "; it is at (", position.x, ", ",
         position.z, ")");
    return std::nullopt;
}

std::optional<MapDocument> loadMap(const dwe::assets::VirtualFileSystem& vfs, std::string_view id) {
    auto map = dwe::assets::loadMapDocument(vfs, id);
    if (!map) {
        FAIL(errorOf(map));
        return std::nullopt;
    }
    return std::move(*map);
}

// Enters a map and checks the spawn and the partner.
bool enter(Field& field, const std::optional<MapDocument>& map, std::int32_t exit) {
    if (!map || map->warp(exit) == nullptr || !field.enterMap(*map, exit)) {
        FAIL("cannot enter exit ", exit);
        return false;
    }
    const dwe::assets::SpawnPoint spawn = dwe::assets::warpSpawn(*map->warp(exit));
    CHECK(field.mapId() == map->id);
    CHECK(glm::distance(field.player().position, spawn.position) < 1e-3F);
    CHECK(field.player().yaw == doctest::Approx(spawn.yaw));
    // The partner came along and appears next to the player.
    CHECK(partnerDistance(field) <= field.rules().partner.spawnDistance + 1.0F);
    return true;
}

void runRoute(const dwe::assets::VirtualFileSystem& vfs, Drive drive) {
    auto rules = dwe::game::field::loadFieldRules(vfs);
    auto warps = dwe::game::field::loadFieldWarps(vfs);
    auto table = dwe::assets::loadMapTable(vfs);
    if (!rules || !warps || !table) {
        FAIL(errorOf(rules), errorOf(warps), errorOf(table));
        return;
    }
    // The route's maps, by the names shown when they load.
    const dwe::assets::MapEntry* town = table->find("twna01");
    const dwe::assets::MapEntry* house = table->find("room08");
    if (town == nullptr || house == nullptr) {
        FAIL("the map table has no twna01 or room08");
        return;
    }
    CHECK(table->displayName(*town) == "File City");
    CHECK(table->displayName(*house) == "Jijimon's house");

    // The door back is a warp of room08's script: field/script_warps and the new game's triggers come from dwx.
    auto scriptWarps = dwe::game::field::loadScriptWarps(vfs);
    auto story = dwe::assets::loadNewGameStory(vfs);
    if (!scriptWarps || !story) {
        FAIL(errorOf(scriptWarps), errorOf(story));
        return;
    }
    if (scriptWarps->empty() || !*story) {
        FAIL("the content has no field/script_warps or data/new_game_story: extract it again with this dwx");
        return;
    }
    dwe::game::field::TriggerState triggers = [set = (*story)->triggersSet](std::int32_t trigger) {
        return std::optional<bool>{std::ranges::binary_search(set, trigger)};
    };
    dwe::scene::World world;
    Field field{world, *rules, std::move(*scriptWarps), std::move(triggers)};
    CHECK(warps->startMap == "twna01");
    const auto townMap = loadMap(vfs, warps->startMap);
    if (!enter(field, townMap, warps->startExit)) {
        return;
    }

    std::vector<std::string> visited{field.mapId()};
    // File City -> Jijimon's house: twna01's trigger 112 is its exit 2, which leads to room08's exit 0.
    auto change = walkToTrigger(field, 112, drive);
    if (!change) {
        return;
    }
    CHECK(change->map == "room08");
    CHECK(change->exit == 0);
    if (!enter(field, loadMap(vfs, change->map), change->exit)) {
        return;
    }
    visited.push_back(field.mapId());

    // And back: room08 has no warps; its door (trigger 51) is a warp of its script to twna01's exit 2, taken while
    // trigger 203 is clear, as it is in a new game.
    change = walkToTrigger(field, 51, drive);
    if (!change) {
        return;
    }
    CHECK(change->map == "twna01");
    CHECK(change->exit == 2);
    if (!enter(field, townMap, change->exit)) {
        return;
    }
    visited.push_back(field.mapId());
    CHECK(visited == std::vector<std::string>{"twna01", "room08", "twna01"});

    // Back in File City, in front of the house: the player can walk away and the partner follows.
    field.setInput({.stick = {0.0F, -1.0F}, .run = false});
    for (int i = 0; i < 60; ++i) {
        field.tick(Tick);
    }
    field.setInput({});
    for (int i = 0; i < 180; ++i) {
        field.tick(Tick);
    }
    CHECK_FALSE(field.mapChangePending());
    CHECK(partnerDistance(field) < rules->partner.runDistance * rules->partner.mover.radius);
}

} // namespace

TEST_CASE("the user's disc, extracted: the route with stick directions") {
    const dwe::assets::DiskFileSystem disk;
    const auto vfs = mountContent(disk);
    if (!vfs) {
        MESSAGE("DWE_CONTENT is not set: skipping the route on extracted content");
        return;
    }
    runRoute(*vfs, Drive::Stick);
}

TEST_CASE("the user's disc, extracted: the route with click-to-move") {
    const dwe::assets::DiskFileSystem disk;
    const auto vfs = mountContent(disk);
    if (!vfs) {
        MESSAGE("DWE_CONTENT is not set: skipping the route on extracted content");
        return;
    }
    runRoute(*vfs, Drive::Click);
}
