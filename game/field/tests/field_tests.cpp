#include <dwe/core/log.hpp>
#include <dwe/game/field/field.hpp>
#include <dwe/game/field/grid.hpp>
#include <dwe/game/field/script_warps.hpp>
#include <dwe/ruleset/game_clock.hpp>
#include <dwe/test_support/script_warp_fixtures.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fixture.hpp"

using dwe::assets::CollisionCell;
using dwe::assets::MapCollision;
using dwe::assets::MapDocument;
using dwe::game::field::Field;
using dwe::game::field::FieldInput;
using dwe::game::field::MapChange;
using dwe::game::field::testing::fill;
using dwe::game::field::testing::originalRules;
using dwe::game::field::testing::roomMap;
using dwe::scene::Motion;

namespace {

constexpr float Tick = 1.0F / 60.0F;
constexpr float Pi = std::numbers::pi_v<float>;
constexpr std::uint8_t Blocked = MapCollision::BlockedBit;

void run(Field& field, float seconds, float tick = Tick) {
    const int ticks = static_cast<int>(std::lround(seconds / tick));
    for (int i = 0; i < ticks; ++i) {
        field.tick(tick);
    }
}

dwe::assets::SpawnPoint at(float x, float z, float yaw = 0.0F) {
    return {.position = {x, 0.0F, z}, .yaw = yaw};
}

float partnerDistance(const Field& field) {
    const glm::vec3 delta = field.partner().position - field.player().position;
    return std::hypot(delta.x, delta.z);
}

} // namespace

TEST_CASE("the stick moves the player relative to the map's camera") {
    dwe::scene::World world;
    Field field{world, originalRules()};
    const MapDocument map = roomMap("room");
    field.enterMapAt(map, at(0.0F, 0.0F));
    // The camera looks toward world -Z, like File City's: up on the stick goes there.
    CHECK(glm::distance(field.cameraForward(), glm::vec3{0.0F, 0.0F, -1.0F}) < 1e-5F);

    field.setInput({.stick = {0.0F, 1.0F}, .run = true});
    run(field, 1.0F);
    CHECK(field.player().position.z == doctest::Approx(-1000.0F).epsilon(1e-3));
    CHECK(field.player().position.x == doctest::Approx(0.0F));
    CHECK(field.player().motion == Motion::Run);
    CHECK(std::abs(field.player().yaw) == doctest::Approx(Pi));

    field.setInput({.stick = {1.0F, 0.0F}, .run = false});
    run(field, 1.0F);
    CHECK(field.player().position.x == doctest::Approx(400.0F).epsilon(1e-3));
    CHECK(field.player().motion == Motion::Walk);
    CHECK(field.player().yaw == doctest::Approx(Pi / 2.0F));

    field.setInput({.stick = {0.1F, 0.1F}}); // inside the dead zone
    run(field, 0.5F);
    CHECK(field.player().position.x == doctest::Approx(400.0F).epsilon(1e-3));
    CHECK(field.player().motion == Motion::Idle);

    // The scene components follow.
    const auto& transform = world.registry().get<dwe::scene::Transform>(field.playerEntity());
    CHECK(transform.position == field.player().position);
    CHECK(world.registry().get<dwe::scene::ModelRef>(field.playerEntity()).asset == "digimon/species_g/model");
    CHECK(world.first<dwe::scene::PartnerTag>() == field.partnerEntity());
}

TEST_CASE("the same input gives the same move at any tick rate") {
    auto map = roomMap("room");
    fill(map, {.column = 60, .row = 30}, {.column = 60, .row = 69}, Blocked);
    glm::vec3 reference{};
    bool first = true;
    for (const float tick : {1.0F / 20.0F, 1.0F / 30.0F, 1.0F / 60.0F, 1.0F / 120.0F, 1.0F / 240.0F}) {
        dwe::scene::World world;
        Field field{world, originalRules()};
        field.enterMapAt(map, at(0.0F, 0.0F));
        field.setInput({.stick = {0.8F, 0.6F}, .run = true}); // toward the wall at x 1000, sliding along it
        run(field, 1.2F, tick);
        const glm::vec3 end = field.player().position;
        CHECK(end.x == doctest::Approx(900.0F).epsilon(1e-4));
        if (first) {
            reference = end;
            first = false;
        }
        CHECK(glm::distance(end, reference) < 0.5F);
    }
}

TEST_CASE("stepping on a warp trigger asks for a map change once") {
    MapDocument town = roomMap("town");
    fill(town, {.column = 50, .row = 40}, {.column = 52, .row = 41}, 112); // exit 2, at z -1000 to -800
    town.warps[2] = {
        .exit = 2, .spawn = {0, 0, 300}, .rotation = 0, .targetMap = 218, .targetMapId = "house", .targetExit = 0};
    MapDocument house = roomMap("house");
    house.warps[0] = {.exit = 0,
                      .spawn = {-102, 0, -1282},
                      .rotation = 2300,
                      .targetMap = -1,
                      .targetMapId = std::nullopt,
                      .targetExit = -1};

    dwe::scene::World world;
    Field field{world, originalRules()};
    REQUIRE(field.enterMap(town, 2).has_value());
    // Exit 2 spawns at the original's (0, 0, 300): world z -300, facing rotation 0 = world +Z.
    CHECK(glm::distance(field.player().position, glm::vec3{0.0F, 0.0F, -300.0F}) < 1e-3F);
    CHECK(field.player().yaw == doctest::Approx(0.0F));
    // The partner appears 200 units behind it (original z + 200 = world z - 200), facing it.
    CHECK(glm::distance(field.partner().position, glm::vec3{0.0F, 0.0F, -500.0F}) < 1e-3F);
    CHECK(std::abs(field.partner().yaw) == doctest::Approx(0.0F));

    field.setInput({.stick = {0.0F, 1.0F}});
    std::optional<MapChange> change;
    for (int i = 0; i < 600 && !change; ++i) {
        field.tick(Tick);
        change = field.takeMapChange();
    }
    if (!change) {
        FAIL("no map change");
        return;
    }
    CHECK(change->map == "house");
    CHECK(change->exit == 0);
    CHECK(change->trigger == 112);
    CHECK(field.player().position.z < -800.0F);
    CHECK(field.mapChangePending());
    CHECK_FALSE(field.takeMapChange().has_value());

    // Nothing moves until the app enters the new map.
    const glm::vec3 waiting = field.player().position;
    run(field, 0.5F);
    CHECK(field.player().position == waiting);
    CHECK(field.player().motion == Motion::Idle);

    REQUIRE(field.enterMap(house, change->exit).has_value());
    CHECK(field.mapId() == "house");
    CHECK_FALSE(field.mapChangePending());
    CHECK(glm::distance(field.player().position, glm::vec3{-102.0F, 0.0F, 1282.0F}) < 1e-3F);
    CHECK(dwe::assets::yawToOriginal(field.player().yaw) == 2300);
    CHECK(partnerDistance(field) == doctest::Approx(200.0F).epsilon(1e-3));
    CHECK_FALSE(field.enterMap(house, 12).has_value());
}

TEST_CASE("the field keeps the region lists of the map it entered, for how the partner feels about the map") {
    constexpr std::int32_t NoRegion = dwe::assets::MapRegions::NoRegion;
    MapDocument town = roomMap("town");
    town.regions = {.a = {1, 2, NoRegion, NoRegion}, .b = {3, NoRegion, NoRegion, NoRegion}};
    MapDocument house = roomMap("house");
    house.regions = {.a = {4, NoRegion, NoRegion, NoRegion}, .b = {5, 6, NoRegion, NoRegion}};
    dwe::scene::World world;
    Field field{world, originalRules()};
    field.enterMapAt(town, at(0.0F, 0.0F));
    CHECK(field.regions().a == town.regions.a);
    CHECK(field.regions().b == town.regions.b);
    field.enterMapAt(house, at(0.0F, 0.0F));
    CHECK(field.regions().a == house.regions.a);
    CHECK(field.regions().b == house.regions.b);
}

TEST_CASE("a trigger fires when stepped onto, not while standing on it") {
    MapDocument town = roomMap("town");
    fill(town, {.column = 50, .row = 50}, {.column = 50, .row = 50}, 112);
    town.warps[2].targetMapId = "house";
    town.warps[2].targetExit = 0;
    fill(town, {.column = 55, .row = 50}, {.column = 55, .row = 50}, 54); // a script trigger

    dwe::scene::World world;
    Field field{world, originalRules()};
    field.enterMapAt(town, at(50.0F, 50.0F)); // spawned on the warp
    run(field, 0.5F);
    CHECK_FALSE(field.mapChangePending());

    field.setInput({.stick = {1.0F, 0.0F}}); // walk east over the script trigger
    bool stepped = false;
    for (int i = 0; i < 120; ++i) {
        field.tick(Tick);
        for (const auto& step : field.triggerSteps()) {
            CHECK(step.trigger == 54);
            CHECK(step.cell == CollisionCell{.column = 55, .row = 50});
            stepped = true;
        }
    }
    CHECK(stepped);
    CHECK_FALSE(field.mapChangePending());

    field.setInput({.stick = {-1.0F, 0.0F}, .run = true}); // back west onto the warp
    run(field, 1.0F);
    CHECK(field.mapChangePending());
}

namespace {

// The door of a house: stepping on cells of value 51 takes the player to exit 2 of town while trigger 203 is clear.
dwe::game::field::ScriptWarp houseDoor() {
    using dwe::game::field::GuardBranch;
    using dwe::game::field::GuardTest;
    return {.map = "house",
            .via = dwe::game::field::WarpVia::Step,
            .section = 51,
            .targetMap = "town",
            .targetExit = 2,
            .scriptGuard = {},
            .guard = {.terms = {GuardBranch{.tests = {GuardTest{.test = "trigger", .trigger = 203, .clear = true}},
                                            .holds = true}},
                      .unstructured = false},
            .setsFlags = {},
            .source = "test"};
}

dwe::game::field::TriggerState triggersSet(std::vector<std::int32_t> set) {
    return [set = std::move(set)](std::int32_t trigger) -> std::optional<bool> {
        return std::ranges::contains(set, trigger);
    };
}

// Walks toward the camera (world +Z) from the middle of the house until a map change or 5 seconds.
std::optional<MapChange> walkSouth(Field& field, const MapDocument& house) {
    field.enterMapAt(house, at(0.0F, 1200.0F));
    field.setInput({.stick = {0.0F, -1.0F}});
    std::optional<MapChange> change;
    for (int i = 0; i < 300 && !change; ++i) {
        field.tick(Tick);
        change = field.takeMapChange();
    }
    return change;
}

} // namespace

TEST_CASE("a warp from a script whose triggers say it is taken works like a map warp") {
    MapDocument house = roomMap("house");
    fill(house, {.column = 46, .row = 65}, {.column = 53, .row = 66}, 51);
    dwe::scene::World world;
    Field field{world, originalRules(), {.warps = {houseDoor()}, .facts = triggersSet({})}};
    const auto change = walkSouth(field, house);
    if (!change) {
        FAIL("no map change");
        return;
    }
    CHECK(change->map == "town");
    CHECK(change->exit == 2);
    CHECK(change->trigger == 51);

    // With trigger 203 set the script does something else: the cell is a trigger step like any other.
    dwe::scene::World later;
    Field after{later, originalRules(), {.warps = {houseDoor()}, .facts = triggersSet({203})}};
    CHECK_FALSE(walkSouth(after, house).has_value());
}

TEST_CASE("a warp from a script the field cannot decide is not taken") {
    MapDocument house = roomMap("house");
    fill(house, {.column = 46, .row = 65}, {.column = 53, .row = 66}, 51);
    dwe::game::field::ScriptWarp door = houseDoor();
    door.guard.terms.emplace_back(dwe::game::field::GuardChoice{.arm = 1, .selection = true});
    dwe::scene::World world;
    Field field{world, originalRules(), {.warps = {door}, .facts = triggersSet({})}};
    CHECK_FALSE(walkSouth(field, house).has_value());
    // Nor without the triggers.
    dwe::scene::World unknown;
    Field blind{unknown, originalRules(), {.warps = {houseDoor()}}};
    CHECK_FALSE(walkSouth(blind, house).has_value());
}

TEST_CASE("a warp behind a recursion the extraction cut is taken under the vanilla policy and logged once") {
    static std::vector<std::string> lines;
    static const bool added = [] {
        dwe::core::addLogSink(
            [](dwe::core::LogLevel /*level*/, std::string_view message) { lines.emplace_back(message); });
        return true;
    }();
    static_cast<void>(added);
    lines.clear();
    constexpr std::string_view Site = "script 63, offset 0x0120";

    MapDocument house = roomMap("house");
    fill(house, {.column = 46, .row = 65}, {.column = 53, .row = 66}, 51);
    dwe::game::field::ScriptWarp door = houseDoor();
    door.guard.terms.emplace_back(dwe::game::field::GuardRecursion{.site = std::string{Site}});
    dwe::scene::World world;
    Field field{world, originalRules(), {.warps = {door}, .facts = triggersSet({})}};
    CHECK(walkSouth(field, house).has_value());
    CHECK(walkSouth(field, house).has_value());
    CHECK(std::ranges::count_if(lines, [Site](const std::string& line) { return line.contains(Site); }) == 1);

    dwe::scene::World strict;
    Field denied{strict,
                 originalRules(),
                 {.warps = {door},
                  .facts = triggersSet({}),
                  .writeTrigger = {},
                  .unknownPolicy = dwe::game::field::UnknownPolicy::Deny}};
    CHECK_FALSE(walkSouth(denied, house).has_value());
}

TEST_CASE("a warp the extraction gives behind a recursion is taken under the vanilla policy and logged once") {
    static std::vector<std::string> lines;
    static const bool added = [] {
        dwe::core::addLogSink(
            [](dwe::core::LogLevel /*level*/, std::string_view message) { lines.emplace_back(message); });
        return true;
    }();
    static_cast<void>(added);
    lines.clear();

    const auto warps = dwe::game::field::parseScriptWarps(dwe::test_support::RecursionWarpYaml);
    REQUIRE(warps.has_value());
    MapDocument house = roomMap(std::string{dwe::test_support::RecursionWarpHouse});
    fill(house, {.column = 46, .row = 65}, {.column = 53, .row = 66}, 51);
    dwe::scene::World world;
    Field field{world, originalRules(), {.warps = *warps, .facts = triggersSet({})}};
    const auto change = walkSouth(field, house);
    if (!change) {
        FAIL("no map change");
        return;
    }
    CHECK(change->map == dwe::test_support::RecursionWarpTown);
    CHECK(change->exit == 2);
    CHECK(walkSouth(field, house).has_value());
    const std::string_view site = dwe::test_support::RecursionWarpSite;
    CHECK(std::ranges::count_if(lines, [site](const std::string& line) { return line.contains(site); }) == 1);
}

TEST_CASE("a warp from a script on a confirm cell needs the button there") {
    MapDocument house = roomMap("house");
    fill(house, {.column = 49, .row = 49}, {.column = 51, .row = 51}, 80);
    dwe::game::field::ScriptWarp sign = houseDoor();
    sign.via = dwe::game::field::WarpVia::Confirm;
    sign.section = 80;
    dwe::scene::World world;
    Field field{world, originalRules(), {.warps = {sign}, .facts = triggersSet({})}};
    field.enterMapAt(house, at(0.0F, 0.0F));
    field.setInput({});
    run(field, 0.5F);
    CHECK_FALSE(field.mapChangePending());
    field.setInput({.confirm = true});
    field.tick(Tick);
    CHECK(field.mapChangePending());
}

TEST_CASE("the partner follows, keeps its distance and stops") {
    dwe::scene::World world;
    Field field{world, originalRules()};
    const MapDocument map = roomMap("room");
    field.enterMapAt(map, at(-1500.0F, 0.0F, Pi / 2.0F));
    const float radius = field.rules().partner.mover.radius;

    field.setInput({.stick = {1.0F, 0.0F}, .run = true});
    bool ran = false;
    for (int i = 0; i < 150; ++i) {
        field.tick(Tick);
        ran = ran || field.partner().motion == Motion::Run;
    }
    CHECK(ran);
    // It runs slower than the player (870 against 1000 per second), so it falls behind but keeps up.
    CHECK(field.partner().position.x > 0.0F);
    CHECK(partnerDistance(field) < 1000.0F);

    field.setInput({});
    run(field, 3.0F);
    CHECK(field.partner().motion == Motion::Idle);
    CHECK(partnerDistance(field) < (field.rules().partner.walkDistance * radius) + 1.0F);
    CHECK(partnerDistance(field) > (field.rules().partner.walkDistance * radius) - 50.0F);
    // While waiting it looks at the player.
    const glm::vec3 toPlayer = field.player().position - field.partner().position;
    CHECK(field.partner().yaw == doctest::Approx(dwe::assets::yawOfDirection(toPlayer)).epsilon(1e-3));
    CHECK(field.partnerTeleports() == 0);
}

TEST_CASE("the partner follows the player's trail around a wall") {
    auto map = roomMap("room");
    // A wall across the room at z 0 to 100 with a door at its east end (x 1500 to 2000).
    fill(map, {.column = 30, .row = 50}, {.column = 64, .row = 50}, Blocked);
    dwe::scene::World world;
    Field field{world, originalRules()};
    field.enterMapAt(map, at(-1000.0F, -1000.0F));

    // Run east to the door, south through it, then west on the other side.
    const auto go = [&](glm::vec2 stick, float seconds) {
        field.setInput({.stick = stick, .run = true});
        run(field, seconds);
    };
    go({1.0F, 0.0F}, 2.7F);  // x -1000 -> 1700
    go({0.0F, -1.0F}, 2.2F); // z -1000 -> 1200
    go({-1.0F, 0.0F}, 2.5F); // x 1700 -> -800
    field.setInput({});
    run(field, 6.0F);
    CHECK(field.partner().position.z > 100.0F); // on the player's side of the wall
    CHECK(partnerDistance(field) < (field.rules().partner.walkDistance * field.rules().partner.mover.radius) + 1.0F);
    CHECK(field.partnerTeleports() == 0);
}

TEST_CASE("a stuck partner is placed behind the player") {
    auto map = roomMap("room");
    fill(map, {.column = 35, .row = 35}, {.column = 41, .row = 41}, Blocked);
    fill(map, {.column = 37, .row = 37}, {.column = 39, .row = 39}, 0); // a closed pocket
    dwe::scene::World world;
    Field field{world, originalRules()};
    field.enterMapAt(map, at(0.0F, 0.0F, 0.0F));
    dwe::scene::place(world.registry().get<dwe::scene::Transform>(field.partnerEntity()), {-1150.0F, 0.0F, -1150.0F},
                      0.0F);
    run(field, 3.0F);
    CHECK(field.partnerTeleports() >= 1);
    CHECK(partnerDistance(field) < 400.0F);
    // Behind the player, who faces +Z.
    CHECK(field.partner().position.z < field.player().position.z);
}

TEST_CASE("click-to-move walks around walls to the target") {
    auto map = roomMap("room");
    fill(map, {.column = 50, .row = 30}, {.column = 50, .row = 65}, Blocked); // gap at z 1600 to 2000
    fill(map, {.column = 45, .row = 68}, {.column = 46, .row = 69}, 113);     // an unrelated warp near the gap
    map.warps[3].targetMapId = "elsewhere";
    dwe::scene::World world;
    Field field{world, originalRules()};
    field.enterMapAt(map, at(-1000.0F, 0.0F));

    const glm::vec3 target{1000.0F, 0.0F, 0.0F};
    field.setInput({.moveTarget = target});
    CHECK(field.hasMoveTarget());
    CHECK_FALSE(field.path().empty());
    field.setInput({});
    for (int i = 0; i < 60 * 20 && field.hasMoveTarget(); ++i) {
        field.tick(Tick);
        CHECK_FALSE(field.mapChangePending());
    }
    CHECK_FALSE(field.hasMoveTarget());
    CHECK(glm::distance(field.player().position, target) <= field.rules().input.arriveDistance + 1.0F);
    run(field, 0.2F);
    CHECK(field.player().motion == Motion::Idle);

    // A click on a wall goes to the nearest free cell; the stick cancels a target.
    field.setInput({.moveTarget = glm::vec3{50.0F, 0.0F, 0.0F}});
    CHECK(field.hasMoveTarget());
    field.setInput({.stick = {0.0F, 1.0F}});
    field.tick(Tick);
    CHECK_FALSE(field.hasMoveTarget());
    // A click that cannot be reached does nothing.
    fill(map, {.column = 50, .row = 66}, {.column = 50, .row = 69}, Blocked);
    field.enterMapAt(map, at(-1000.0F, 0.0F));
    field.setInput({.moveTarget = target});
    CHECK_FALSE(field.hasMoveTarget());
}

TEST_CASE("a click on a warp walks onto it") {
    MapDocument town = roomMap("town");
    fill(town, {.column = 50, .row = 40}, {.column = 52, .row = 41}, 112);
    town.warps[2].targetMapId = "house";
    town.warps[2].targetExit = 0;
    dwe::scene::World world;
    Field field{world, originalRules()};
    field.enterMapAt(town, at(-1000.0F, 1000.0F));
    field.setInput({.run = true, .moveTarget = dwe::assets::collisionCellCenter({.column = 51, .row = 40})});
    field.setInput({.run = true});
    std::optional<MapChange> change;
    for (int i = 0; i < 60 * 10 && !change; ++i) {
        field.tick(Tick);
        change = field.takeMapChange();
    }
    if (!change) {
        FAIL("no map change");
        return;
    }
    CHECK(change->map == "house");
}

TEST_CASE("a walk to a door keeps off the cells whose script changes the map") {
    MapDocument house = roomMap("house");
    constexpr std::uint8_t Door = 112;
    constexpr std::uint8_t ScriptStep = 51;
    constexpr std::uint8_t ExitWithoutMap = 113;
    // A band of script step cells across the room between the player and the door, open at both ends.
    fill(house, {.column = 40, .row = 50}, {.column = 59, .row = 51}, ScriptStep);
    fill(house, {.column = 49, .row = 35}, {.column = 51, .row = 36}, Door);
    fill(house, {.column = 35, .row = 60}, {.column = 36, .row = 61}, ExitWithoutMap);
    house.warps[2].targetMapId = "town";
    house.warps[2].targetExit = 0;
    dwe::scene::World world;
    Field field{world, originalRules(), {.warps = {houseDoor()}, .facts = triggersSet({})}};
    field.enterMapAt(house, at(0.0F, 1000.0F));
    CHECK(field.warpsOn(Door));
    CHECK(field.warpsOn(ScriptStep));
    CHECK_FALSE(field.warpsOn(ExitWithoutMap));
    CHECK_FALSE(field.warpsOn(0));

    // The autotest's walk_to plans like this: every cell that changes the map now is out, but the goal's.
    std::vector<CollisionCell> goals;
    for (std::int32_t column = 49; column <= 51; ++column) {
        for (std::int32_t row = 35; row <= 36; ++row) {
            goals.push_back({.column = column, .row = row});
        }
    }
    const MapCollision& collision = field.collision();
    const std::vector<glm::vec3> path = dwe::game::field::findPath(
        collision, field.player().position, goals, field.rules().player.radius, [&](CollisionCell cell) {
            const std::int32_t value = collision.trigger(cell).value_or(0);
            return !field.warpsOn(value) || value == std::int32_t{Door};
        });
    REQUIRE_FALSE(path.empty());
    CHECK(collision.trigger(dwe::assets::collisionCellAt(path.back())) == Door);
    constexpr float SampleStep = 10.0F;
    glm::vec3 from = field.player().position;
    for (const glm::vec3& to : path) {
        const auto samples = static_cast<int>(std::ceil(glm::distance(from, to) / SampleStep));
        for (int i = 0; i <= samples; ++i) {
            const float along = samples == 0 ? 0.0F : static_cast<float>(i) / static_cast<float>(samples);
            CHECK(collision.trigger(dwe::assets::collisionCellAt(glm::mix(from, to, along))) != ScriptStep);
        }
        from = to;
    }

    // Once its triggers say the script does something else, the band is floor like any other.
    dwe::scene::World later;
    Field after{later, originalRules(), {.warps = {houseDoor()}, .facts = triggersSet({203})}};
    after.enterMapAt(house, at(0.0F, 1000.0F));
    CHECK_FALSE(after.warpsOn(ScriptStep));
    CHECK(after.warpsOn(Door));
}

namespace {

// A story's triggers the tests write and the field reads, live.
struct TestStory {
    std::set<std::int32_t> set;
    std::vector<std::pair<std::int32_t, bool>> writes;

    [[nodiscard]] dwe::game::field::TriggerState reader() const {
        return [this](std::int32_t trigger) -> std::optional<bool> { return set.contains(trigger); };
    }

    dwe::game::field::TriggerWriter writer() {
        return [this](std::int32_t trigger, bool on) {
            writes.emplace_back(trigger, on);
            if (on) {
                set.insert(trigger);
            } else {
                set.erase(trigger);
            }
        };
    }
};

} // namespace

TEST_CASE("a warp the field takes writes the triggers it sets, and the map's cells are decided again at once") {
    MapDocument house = roomMap("house");
    constexpr std::uint8_t OtherStep = 52;
    fill(house, {.column = 46, .row = 65}, {.column = 53, .row = 66}, 51);
    fill(house, {.column = 20, .row = 20}, {.column = 21, .row = 21}, OtherStep);
    dwe::game::field::ScriptWarp door = houseDoor();
    door.setsFlags = {{.trigger = 203, .set = true}, {.trigger = 7, .set = false}};
    // Another cell of the house leads out only while 203 is clear, as the door does.
    dwe::game::field::ScriptWarp window = houseDoor();
    window.section = OtherStep;
    TestStory story{.set = {7}, .writes = {}};
    dwe::scene::World world;
    Field field{
        world, originalRules(), {.warps = {door, window}, .facts = story.reader(), .writeTrigger = story.writer()}};
    field.enterMapAt(house, at(0.0F, 1200.0F));
    CHECK(field.warpsOn(OtherStep));
    const auto change = walkSouth(field, house);
    REQUIRE(change.has_value());
    CHECK(story.writes == std::vector<std::pair<std::int32_t, bool>>{{203, true}, {7, false}});
    // The walk out still sees the house: its cells follow the triggers the warp wrote.
    CHECK_FALSE(field.warpsOn(OtherStep));
}

TEST_CASE("after a trigger written outside the field, refreshing decides the map's step cells again") {
    MapDocument house = roomMap("house");
    fill(house, {.column = 46, .row = 65}, {.column = 53, .row = 66}, 51);
    TestStory story;
    dwe::scene::World world;
    Field field{world, originalRules(), {.warps = {houseDoor()}, .facts = story.reader()}};
    field.enterMapAt(house, at(0.0F, 1000.0F));
    CHECK(field.warpsOn(51));
    story.set.insert(203);
    field.refreshScriptWarps();
    CHECK_FALSE(field.warpsOn(51));
    story.set.erase(203);
    field.refreshScriptWarps();
    CHECK(field.warpsOn(51));
}

namespace {

// The house's door to the castle, which the story opens with trigger 203.
dwe::game::field::ScriptWarp castleDoor() {
    dwe::game::field::ScriptWarp door = houseDoor();
    door.targetMap = "castle";
    door.targetExit = 1;
    std::get<dwe::game::field::GuardBranch>(door.guard.terms.front()).tests.front().clear = false;
    door.setsFlags = {{.trigger = 9, .set = true}};
    return door;
}

// Ticks until a map change or 5 seconds.
std::optional<MapChange> tickToChange(Field& field) {
    std::optional<MapChange> change;
    for (int i = 0; i < 300 && !change; ++i) {
        field.tick(Tick);
        change = field.takeMapChange();
    }
    return change;
}

} // namespace

TEST_CASE("free exploration opens a cell the story keeps closed, and logs it") {
    static std::vector<std::string> lines;
    static const bool added = [] {
        dwe::core::addLogSink(
            [](dwe::core::LogLevel /*level*/, std::string_view message) { lines.emplace_back(message); });
        return true;
    }();
    static_cast<void>(added);
    lines.clear();
    MapDocument house = roomMap("house");
    fill(house, {.column = 46, .row = 65}, {.column = 53, .row = 66}, 51);
    dwe::scene::World world;
    Field field{world, originalRules(), {.warps = {houseDoor()}, .facts = triggersSet({203}), .explore = true}};
    const auto change = walkSouth(field, house);
    if (!change) {
        FAIL("no map change");
        return;
    }
    CHECK(change->map == "town");
    CHECK(std::ranges::any_of(lines, [](const std::string& line) { return line.contains("free exploration opens"); }));

    // A place without data is not offered.
    dwe::scene::World filtered;
    Field noTown{
        filtered,
        originalRules(),
        {.warps = {houseDoor()}, .facts = triggersSet({203}), .explore = true, .hasMap = [](std::string_view map) {
             return map != "town";
         }}};
    CHECK_FALSE(walkSouth(noTown, house).has_value());
}

TEST_CASE("free exploration lets the player choose among a cell's places, starting on the story's, or stay") {
    MapDocument house = roomMap("house");
    fill(house, {.column = 46, .row = 65}, {.column = 53, .row = 66}, 51);
    TestStory story;
    dwe::scene::World world;
    Field field{world,
                originalRules(),
                {.warps = {castleDoor(), houseDoor()},
                 .facts = story.reader(),
                 .writeTrigger = story.writer(),
                 .explore = true}};
    CHECK_FALSE(walkSouth(field, house).has_value());
    const auto& found = field.destinationChoice();
    if (!found) {
        FAIL("no destination choice");
        return;
    }
    const auto& choice = *found;
    CHECK(choice.map == "house");
    CHECK(choice.cell == 51);
    REQUIRE(choice.places.size() == 2);
    CHECK(choice.places[0].map == "castle");
    CHECK(choice.places[1].map == "town");
    CHECK(choice.vanilla == std::optional<std::size_t>{1});

    // Staying: the player is on the cell, which does not fire again until stepped onto.
    field.chooseDestination(std::nullopt);
    CHECK_FALSE(field.destinationChoice().has_value());
    field.setInput({});
    CHECK_FALSE(tickToChange(field).has_value());
    CHECK_FALSE(field.destinationChoice().has_value());

    // Choosing the castle takes it and writes its triggers.
    CHECK_FALSE(walkSouth(field, house).has_value());
    REQUIRE(field.destinationChoice().has_value());
    field.chooseDestination(7);
    REQUIRE(field.destinationChoice().has_value());
    field.chooseDestination(0);
    CHECK_FALSE(field.destinationChoice().has_value());
    const auto change = tickToChange(field);
    if (!change) {
        FAIL("no map change");
        return;
    }
    CHECK(change->map == "castle");
    CHECK(change->exit == 1);
    CHECK(story.writes == std::vector<std::pair<std::int32_t, bool>>{{9, true}});
}

TEST_CASE("free exploration turns on and off while playing, and the story's conditions decide again") {
    MapDocument house = roomMap("house");
    fill(house, {.column = 46, .row = 65}, {.column = 53, .row = 66}, 51);
    dwe::scene::World world;
    Field field{world, originalRules(), {.warps = {houseDoor()}, .facts = triggersSet({203})}};
    field.enterMapAt(house, at(0.0F, 1000.0F));
    CHECK_FALSE(field.explores());
    CHECK_FALSE(field.warpsOn(51));
    field.setExplore(true);
    CHECK(field.warpsOn(51));
    field.setExplore(false);
    CHECK_FALSE(field.warpsOn(51));
    CHECK_FALSE(walkSouth(field, house).has_value());
}

TEST_CASE("a warp fades out while the player walks on, and fades in on the new map") {
    MapDocument town = roomMap("town");
    fill(town, {.column = 50, .row = 40}, {.column = 52, .row = 41}, 112);
    fill(town, {.column = 50, .row = 38}, {.column = 52, .row = 39}, Blocked); // a wall right behind the warp
    town.warps[2].targetMapId = "house";
    town.warps[2].targetExit = 0;
    MapDocument house = roomMap("house");
    house.warps[0] = {
        .exit = 0, .spawn = {0, 0, 0}, .rotation = 0, .targetMap = -1, .targetMapId = std::nullopt, .targetExit = -1};

    dwe::scene::World world;
    const auto rules = originalRules();
    const dwe::game::field::TransitionRules& transition = rules.transition;
    const auto darkness = [&](std::int32_t shade) { return dwe::game::field::shadeDarkness(transition, shade); };
    Field field{world, rules};
    field.enterMapAt(town, at(0.0F, -600.0F, Pi)); // facing world -Z, toward the warp
    field.setInput({.stick = {0.0F, 1.0F}});
    for (int i = 0; i < 120 && !field.mapChangePending(); ++i) {
        field.tick(Tick);
    }
    REQUIRE(field.mapChangePending());
    // fade.c: renderFadeOut's first frame: the 20 the last fade in left plus 7.
    CHECK(field.fadeLevel() == doctest::Approx(darkness(27)));
    CHECK_FALSE(field.mapNameShown().has_value());
    const float onWarp = field.player().position.z;

    // Frame 9 of the fade out: the player walks on at walk speed, through the wall, whatever the input says.
    field.setInput({.stick = {1.0F, 0.0F}, .run = true});
    run(field, 0.4F);
    CHECK(field.fadeLevel() == doctest::Approx(darkness(83)));
    CHECK_FALSE(field.mapNameShown().has_value());
    CHECK(field.player().motion == Motion::Walk);
    CHECK(field.player().position.z == doctest::Approx(onWarp - 160.0F).epsilon(0.05));
    // Frame 10: the new map's name appears.
    run(field, 0.05F);
    CHECK(field.fadeLevel() == doctest::Approx(darkness(90)));
    CHECK(field.mapNameShown() == std::optional<std::string_view>{"house"});
    CHECK_FALSE(field.takeMapChange().has_value());

    // The map loads when the fade out reaches frame 20: 19 frames after the warp.
    std::optional<MapChange> change;
    int ticks = 0;
    for (; ticks < 60 && !change; ++ticks) {
        field.tick(Tick);
        change = field.takeMapChange();
    }
    if (!change) {
        FAIL("no map change");
        return;
    }
    CHECK(ticks == 30);
    CHECK(field.fadeLevel() == doctest::Approx(1.0F));
    CHECK(field.mapNameShown() == std::optional<std::string_view>{"house"});

    // Game time stood still from the warp on.
    CHECK_FALSE(field.gameTimeRan());

    // The fade in: the player stands still, and game time with it, until its 20th frame.
    REQUIRE(field.enterMap(house, change->exit).has_value());
    CHECK_FALSE(field.mapNameShown().has_value());
    CHECK(field.fadeLevel() == doctest::Approx(1.0F));
    field.setInput({.stick = {0.0F, 1.0F}});
    run(field, 0.9F);
    CHECK(field.fadeLevel() == doctest::Approx(darkness(27)));
    CHECK(field.player().position == glm::vec3{0.0F});
    CHECK_FALSE(field.gameTimeRan());
    // Frame 20: the player moves again, under the fade's last shade, which goes on the next frame.
    run(field, 0.05F);
    CHECK(field.fadeLevel() == doctest::Approx(darkness(20)));
    CHECK(field.player().position != glm::vec3{0.0F});
    CHECK(field.gameTimeRan());
    run(field, 0.05F);
    CHECK(field.fadeLevel() == doctest::Approx(0.0F));
}

TEST_CASE("the game clock counts minutes while the player plays and wraps at midnight") {
    dwe::scene::World world;
    Field field{world, originalRules()};
    dwe::ruleset::GameClock clock{dwe::ruleset::ClockRules{}};
    // The owner of the game loop advances the clock by the ticks that ran game time.
    const auto play = [&](float seconds) {
        const int ticks = static_cast<int>(std::lround(seconds / Tick));
        for (int i = 0; i < ticks; ++i) {
            field.tick(Tick);
            if (field.gameTimeRan()) {
                clock.advance(Tick);
            }
        }
    };
    play(1.0F);
    // Before the first map nothing plays.
    CHECK(clock.time().hour == 8);
    CHECK(clock.time().minute == 0);
    field.enterMapAt(roomMap("town"), at(0.0F, 0.0F));
    play(90.0F);
    CHECK(clock.time().hour == 9);
    CHECK(clock.time().minute == 30);
    clock.jumpTo(23, 59);
    play(1.0F);
    CHECK(clock.time().hour == 0);
    CHECK(clock.time().minute == 0);
}

TEST_CASE("up on the stick follows the map's camera rounded to an eighth of a turn") {
    MapDocument map = roomMap("town");
    // A camera looking a little to the side of straight ahead (-Z in the world).
    map.camera.viewpoint = {100, -2000, 4000};
    map.camera.reference = {0, 0, 0};
    dwe::scene::World world;
    Field field{world, originalRules()};
    field.enterMapAt(map, at(0.0F, 0.0F));
    CHECK(std::abs(field.cameraForward().x) < 1e-5F);
    CHECK(std::abs(std::abs(field.cameraForward().z) - 1.0F) < 1e-5F);
}
