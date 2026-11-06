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
#include <memory>
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
using dwe::game::field::PartnerClip;
using dwe::game::field::PartnerIdleClip;
using dwe::game::field::PartnerWalkClip;
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

  // 20 frames of the original in a second: the first starts the run and does not move
  // (anim.c: startAnimation), the other 19 step 50 each.
  field.setInput({.stick = {0.0F, 1.0F}, .run = true});
  run(field, 1.0F);
  CHECK(field.player().position.z == doctest::Approx(-950.0F).epsilon(1e-3));
  CHECK(field.player().position.x == doctest::Approx(0.0F));
  CHECK(field.player().motion == Motion::Run);
  CHECK(std::abs(field.player().yaw) == doctest::Approx(Pi));

  // Walking is another clip: its first frame stands too, then 19 steps of 20.
  field.setInput({.stick = {1.0F, 0.0F}, .run = false});
  run(field, 1.0F);
  CHECK(field.player().position.x == doctest::Approx(380.0F).epsilon(1e-3));
  CHECK(field.player().motion == Motion::Walk);
  CHECK(field.player().yaw == doctest::Approx(Pi / 2.0F));

  field.setInput({.stick = {0.1F, 0.1F}}); // inside the dead zone
  run(field, 0.5F);
  CHECK(field.player().position.x == doctest::Approx(380.0F).epsilon(1e-3));
  CHECK(field.player().motion == Motion::Idle);

  // The scene components follow.
  const auto& transform = world.registry().get<dwe::scene::Transform>(field.playerEntity());
  CHECK(transform.position == field.player().position);
  CHECK(world.registry().get<dwe::scene::ModelRef>(field.playerEntity()).asset ==
        "digimon/species_g/model");
  CHECK(world.first<dwe::scene::PartnerTag>() == field.partnerEntity());
}

TEST_CASE("the same input gives the same move at any tick rate") {
  auto map = roomMap("room");
  fill(map, {.column = 60, .row = 30}, {.column = 60, .row = 69}, Blocked);
  glm::vec3 reference{};
  bool first = true;
  for (const float tick :
       {1.0F / 20.0F, 1.0F / 30.0F, 1.0F / 60.0F, 1.0F / 120.0F, 1.0F / 240.0F}) {
    dwe::scene::World world;
    Field field{world, originalRules()};
    field.enterMapAt(map, at(0.0F, 0.0F));
    field.setInput(
        {.stick = {0.8F, 0.6F}, .run = true}); // toward the wall at x 1000, sliding along it
    run(field, 1.2F, tick);
    const glm::vec3 end = field.player().position;
    // Steps of 40 along x: the one to 920 would put the probe past the wall at 1000, so the
    // player stops at 880 (map.c: entityCheckCollision tests the whole step) and slides.
    CHECK(end.x == doctest::Approx(880.0F).epsilon(1e-4));
    if (first) {
      reference = end;
      first = false;
    }
    CHECK(glm::distance(end, reference) < 0.5F);
  }
}

TEST_CASE("a loaded game puts the partner back where it stood, or next to the player if taken") {
  // main.c: initializeLoadedMap restores the partner's saved position.
  dwe::scene::World world;
  Field field{world, originalRules()};
  field.enterMapAt(roomMap("room"), at(0.0F, 0.0F), at(600.0F, 400.0F, 1.0F));
  CHECK(field.partner().position.x == doctest::Approx(600.0F));
  CHECK(field.partner().position.z == doctest::Approx(400.0F));
  CHECK(field.partner().yaw == doctest::Approx(1.0F));
  // Close to the player is no reason to move it: only an NPC is (entityCheckCollision).
  field.enterMapAt(roomMap("room"), at(0.0F, 0.0F), at(100.0F, 150.0F));
  CHECK(field.partner().position.x == doctest::Approx(100.0F));
  CHECK(field.partner().position.z == doctest::Approx(150.0F));

  // A spot off the room's free cells: next to the player, as a save without a partner place.
  field.enterMapAt(roomMap("room"), at(0.0F, 0.0F), at(5000.0F, 5000.0F));
  const float beside = partnerDistance(field);
  CHECK(beside > 0.0F);
  CHECK(beside < 400.0F);
  field.enterMapAt(roomMap("room"), at(0.0F, 0.0F));
  CHECK(partnerDistance(field) == doctest::Approx(beside));
}

TEST_CASE(
    "the player steps on the original's frames, turning on the press and stopping on release") {
  dwe::scene::World world;
  Field field{world, originalRules()};
  field.enterMapAt(roomMap("room"), at(0.0F, 0.0F));
  field.tick(Tick);
  // tamer.c: tamerTickWalkingState turns the player on the frame the pad is read; a press from
  // standing starts a frame on its own tick. The run starts there and does not move yet.
  field.setInput({.stick = {1.0F, 0.0F}, .run = true});
  field.tick(Tick);
  CHECK(field.player().yaw == doctest::Approx(Pi / 2.0F));
  CHECK(field.player().position.x == doctest::Approx(0.0F));
  CHECK(field.player().motion == Motion::Run);
  // A frame is three ticks: 50 on each frame's tick, nothing on the two between.
  std::vector<float> xs;
  for (int i = 0; i < 6; ++i) {
    field.tick(Tick);
    xs.push_back(field.player().position.x);
  }
  CHECK(xs == std::vector<float>{0.0F, 0.0F, 50.0F, 50.0F, 50.0F, 100.0F});
  // Let go: the next frame stands at once, without another step.
  field.setInput({});
  run(field, 3.0F * Tick);
  CHECK(field.player().position.x == doctest::Approx(100.0F));
  CHECK(field.player().motion == Motion::Idle);
  run(field, 0.5F);
  CHECK(field.player().position.x == doctest::Approx(100.0F));
}

TEST_CASE("the player and the partner are drawn gliding over a frame's ticks") {
  dwe::scene::World world;
  Field field{world, originalRules()};
  field.enterMapAt(roomMap("room"), at(0.0F, 0.0F));
  field.setInput({.stick = {1.0F, 0.0F}, .run = true});
  run(field, 4.0F * Tick); // the press's frame and the first step's
  const dwe::scene::Entity player = field.playerEntity();
  REQUIRE(field.player().position.x == doctest::Approx(50.0F));
  // The step's tick draws from where the last frame left it to a third of the way.
  CHECK(field.drawnPose(player, 0.0F).position.x == doctest::Approx(0.0F));
  CHECK(field.drawnPose(player, 1.0F).position.x == doctest::Approx(50.0F / 3.0F));
  field.tick(Tick);
  CHECK(field.drawnPose(player, 0.5F).position.x == doctest::Approx(25.0F));
  field.tick(Tick);
  CHECK(field.drawnPose(player, 1.0F).position.x == doctest::Approx(50.0F));
  // Settling (a tick the field does not run) draws it where it stands.
  field.settle();
  CHECK(field.drawnPose(player, 0.0F).position.x == doctest::Approx(50.0F));
  // Anything else that moves it is drawn as the scene draws a tick.
  field.tick(Tick);
  field.placeAt(player, {300.0F, 0.0F, 0.0F}, 0.0F);
  CHECK(field.drawnPose(player, 0.5F).position.x == doctest::Approx(300.0F));
}

TEST_CASE("the game's ticks, each starting with settle, keep the frame's glide going") {
  dwe::scene::World world;
  Field field{world, originalRules()};
  field.enterMapAt(roomMap("room"), at(0.0F, 0.0F));
  field.setInput({.stick = {1.0F, 0.0F}, .run = true});
  // As PlaySession::tick: settle, then the field's tick.
  const auto gameTick = [&] {
    field.settle();
    field.tick(Tick);
  };
  for (int i = 0; i < 4; ++i) {
    gameTick(); // the press's frame and the first step's
  }
  const dwe::scene::Entity player = field.playerEntity();
  const dwe::scene::Entity partner = field.partnerEntity();
  REQUIRE(field.player().position.x == doctest::Approx(50.0F));
  // Drawn at 120 Hz over three frames of steps: the same advance every draw, 50 a frame of 1/20 s.
  std::vector<float> drawn;
  for (int i = 0; i < 9; ++i) {
    drawn.push_back(field.drawnPose(player, 0.0F).position.x);
    drawn.push_back(field.drawnPose(player, 0.5F).position.x);
    gameTick();
  }
  for (std::size_t i = 1; i < drawn.size(); ++i) {
    CHECK(drawn[i] - drawn[i - 1] == doctest::Approx(50.0F / 6.0F));
  }
  // The partner follows and glides the same way: no draw stands still between two of its steps.
  std::vector<glm::vec3> partnerDrawn;
  std::vector<glm::vec3> partnerAt;
  for (int i = 0; i < 90; ++i) {
    gameTick();
    partnerDrawn.push_back(field.drawnPose(partner, 0.0F).position);
    partnerAt.push_back(world.registry().get<dwe::scene::Transform>(partner).position);
  }
  int walking = 0;
  for (std::size_t i = 0; i + 3 < partnerAt.size(); ++i) {
    if (partnerAt[i + 3] != partnerAt[i] && partnerAt[i] != partnerAt[0]) {
      ++walking;
      CHECK(glm::distance(partnerDrawn[i + 1], partnerDrawn[i]) > 1.0F);
    }
  }
  CHECK(walking > 30);
  // A tick without the field's (a menu) draws everybody where they stand and ends the glides.
  field.settle();
  CHECK(field.drawnPose(player, 0.5F).position.x == doctest::Approx(field.player().position.x));
  field.settle();
  field.tick(Tick);
  CHECK(field.drawnPose(player, 0.0F).position.x == doctest::Approx(field.player().position.x));
}

TEST_CASE("with smooth motion off the characters are drawn where their tick left them") {
  dwe::scene::World world;
  Field field{world, originalRules()};
  CHECK(field.smoothMotion());
  field.setSmoothMotion(false);
  field.enterMapAt(roomMap("room"), at(0.0F, 0.0F));
  field.setInput({.stick = {1.0F, 0.0F}, .run = true});
  run(field, 4.0F * Tick);
  const dwe::scene::Entity player = field.playerEntity();
  // The step's tick: the whole step at once, whatever the alpha, as the original's frame.
  CHECK(field.drawnPose(player, 0.0F).position.x == doctest::Approx(50.0F));
  CHECK(field.drawnPose(player, 0.5F).position.x == doctest::Approx(50.0F));
  field.tick(Tick);
  CHECK(field.drawnPose(player, 0.5F).position.x == doctest::Approx(50.0F));
  // The logic is the same either way.
  run(field, 2.0F * Tick);
  CHECK(field.player().position.x == doctest::Approx(100.0F));
}

TEST_CASE("against a wall straight ahead the player turns 0x50 aside and slides") {
  auto map = roomMap("room");
  // A wall across the way up (world -Z), from z -500 to -600.
  fill(map, {.column = 30, .row = 44}, {.column = 70, .row = 44}, Blocked);
  dwe::scene::World world;
  Field field{world, originalRules()};
  field.enterMapAt(map, at(0.0F, 0.0F, Pi)); // the partner behind, out of the way
  field.setInput({.stick = {0.0F, 1.0F}, .run = true});
  run(field, 0.6F);
  // map.c: entityCheckCollision tests the whole step: the step to -450 would put the probe in the
  // wall, so the player stops at -400, short of touching it.
  CHECK(field.player().position.z == doctest::Approx(-400.0F));
  // map.c: collisionGrace: up (0x800) is blocked, 0xC00 (right) is free: the player turns 0x50
  // toward it and moves the step's part along the wall, 50 * sin(0x50) a frame.
  CHECK(dwe::assets::yawToOriginal(field.player().yaw) == 0x850);
  const float x = field.player().position.x;
  CHECK(x > 0.0F);
  run(field, 0.3F);
  CHECK(field.player().position.x - x == doctest::Approx(6.0F * 50.0F * 0.1224F).epsilon(0.02));
  CHECK(field.player().position.z == doctest::Approx(-400.0F));
}

TEST_CASE("a placed partner stays where it was put, close to the player") {
  dwe::scene::World world;
  Field field{world, originalRules()};
  field.enterMapAt(roomMap("room"), at(0.0F, 0.0F));
  // A script let go: the partner would make room.
  field.holdForScript(true);
  field.tick(Tick);
  field.holdForScript(false);
  field.placeAt(field.partnerEntity(), {0.0F, 0.0F, 192.0F}, Pi);
  run(field, 1.0F);
  CHECK(field.partner().position.z == doctest::Approx(192.0F));
  CHECK(field.partner().position.x == doctest::Approx(0.0F));
}

TEST_CASE("stepping on a warp trigger asks for a map change once") {
  MapDocument town = roomMap("town");
  fill(town, {.column = 50, .row = 40}, {.column = 52, .row = 41},
       112); // exit 2, at z -1000 to -800
  town.warps[2] = {.exit = 2,
                   .spawn = {0, 0, 300},
                   .rotation = 0,
                   .targetMap = 218,
                   .targetMapId = "house",
                   .targetExit = 0};
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

TEST_CASE(
    "the field keeps the region lists of the map it entered, for how the partner feels about the map") {
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

TEST_CASE("the field reads its cells by the trigger bands of its rules") {
  MapDocument town = roomMap("town");
  fill(town, {.column = 50, .row = 50}, {.column = 50, .row = 50}, 32); // exit 2 in the new band
  town.warps[2].targetMapId = "house";
  town.warps[2].targetExit = 0;
  fill(town, {.column = 55, .row = 50}, {.column = 55, .row = 50}, 112); // no exit any more
  auto rules = originalRules();
  rules.triggers.exits = {.first = 30, .last = 39};

  dwe::scene::World world;
  Field field{world, rules};
  field.enterMapAt(town, at(53.0F, 50.0F));
  field.setInput({.stick = {1.0F, 0.0F}}); // east over 112: a cell that only runs its section
  bool stepped = false;
  for (int i = 0; i < 120; ++i) {
    field.tick(Tick);
    for (const auto& step : field.triggerSteps()) {
      CHECK(step.trigger == 112);
      stepped = true;
    }
  }
  CHECK(stepped);
  CHECK_FALSE(field.mapChangePending());

  field.setInput({.stick = {-1.0F, 0.0F}, .run = true}); // back west onto 32
  std::optional<MapChange> change;
  for (int i = 0; i < 300 && !change; ++i) {
    field.tick(Tick);
    change = field.takeMapChange();
  }
  if (!change) {
    FAIL("no map change");
    return;
  }
  CHECK(change->map == "house");
  CHECK(change->trigger == 32);
}

namespace {

// The door of a house: stepping on cells of value 51 takes the player to exit 2 of town while
// trigger 203 is clear.
dwe::game::field::ScriptWarp houseDoor() {
  using dwe::game::field::GuardBranch;
  using dwe::game::field::GuardTest;
  return {.map = "house",
          .via = dwe::game::field::WarpVia::Step,
          .section = 51,
          .targetMap = "town",
          .targetExit = 2,
          .scriptGuard = {},
          .guard = {.terms = {GuardBranch{
                        .tests = {GuardTest{.test = "trigger", .trigger = 203, .clear = true}},
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

TEST_CASE(
    "a warp behind a recursion the extraction cut is taken under the vanilla policy and logged once") {
  static std::vector<std::string> lines;
  static const bool added = [] {
    dwe::core::addLogSink([](dwe::core::LogLevel /*level*/, std::string_view message) {
      lines.emplace_back(message);
    });
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
  CHECK(std::ranges::count_if(
            lines, [Site](const std::string& line) { return line.contains(Site); }) == 1);

  dwe::scene::World strict;
  Field denied{strict,
               originalRules(),
               {.warps = {door},
                .facts = triggersSet({}),
                .writeTrigger = {},
                .unknownPolicy = dwe::game::field::UnknownPolicy::Deny}};
  CHECK_FALSE(walkSouth(denied, house).has_value());
}

TEST_CASE(
    "a warp the extraction gives behind a recursion is taken under the vanilla policy and logged once") {
  static std::vector<std::string> lines;
  static const bool added = [] {
    dwe::core::addLogSink([](dwe::core::LogLevel /*level*/, std::string_view message) {
      lines.emplace_back(message);
    });
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
  CHECK(std::ranges::count_if(
            lines, [site](const std::string& line) { return line.contains(site); }) == 1);
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
  // The original's _atan is good to about one of its 4096ths of a turn.
  const float off =
      std::remainder(field.partner().yaw - dwe::assets::yawOfDirection(toPlayer), 2.0F * Pi);
  CHECK(std::abs(off) <= 2.0F * 2.0F * Pi / 4096.0F);
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
  CHECK(partnerDistance(field) <
        (field.rules().partner.walkDistance * field.rules().partner.mover.radius) + 1.0F);
  CHECK(field.partnerTeleports() == 0);
}

TEST_CASE("a partner in a closed pocket walks through its wall to the player, as the original") {
  // map.c: partnerTickCollision ignores what entityCheckCollision says about the map (10): the
  // original's partner keeps out of walls only by its trail.
  auto map = roomMap("room");
  fill(map, {.column = 35, .row = 35}, {.column = 41, .row = 41}, Blocked);
  fill(map, {.column = 37, .row = 37}, {.column = 39, .row = 39}, 0); // a closed pocket
  dwe::scene::World world;
  Field field{world, originalRules()};
  field.enterMapAt(map, at(0.0F, 0.0F, 0.0F));
  dwe::scene::place(world.registry().get<dwe::scene::Transform>(field.partnerEntity()),
                    {-1150.0F, 0.0F, -1150.0F}, 0.0F);
  run(field, 3.0F);
  CHECK(field.partnerTeleports() == 0);
  CHECK(partnerDistance(field) <
        (field.rules().partner.walkDistance * field.rules().partner.mover.radius) + 1.0F);
}

TEST_CASE("a partner that cannot step for a second is placed behind the player") {
  // The engine's rescue: the original's goes by its camera (isTileOffScreen).
  dwe::scene::World world;
  Field field{world, originalRules()};
  field.enterMapAt(roomMap("room"), at(0.0F, 0.0F, 0.0F));
  field.setPartnerClips(
      {{2, {.steps = {{}, {}}, .repeatFrom = 1}}, {4, {.steps = {{}, {}}, .repeatFrom = 1}}});
  dwe::scene::place(world.registry().get<dwe::scene::Transform>(field.partnerEntity()),
                    {-1150.0F, 0.0F, -1150.0F}, 0.0F);
  run(field, 1.5F);
  CHECK(field.partnerTeleports() >= 1);
  CHECK(partnerDistance(field) < 400.0F);
  // Behind the player, who faces +Z.
  CHECK(field.partner().position.z < field.player().position.z);
}

TEST_CASE("a partner left too far comes back from the first cell out of view toward it") {
  // map.c: getClosestTileOffScreen: the original brings its partner from off screen; a sulking one
  // walks while the player runs and falls behind, and it must not be seen to jump.
  dwe::scene::World world;
  dwe::game::field::FieldRules rules = originalRules();
  rules.partner.teleportDistance = 1500.0F;
  Field field{world, rules};
  field.enterMapAt(roomMap("room"), at(0.0F, 0.0F, 0.0F));
  field.setPartnerClips(
      {{2, {.steps = {{}, {}}, .repeatFrom = 1}}, {4, {.steps = {{}, {}}, .repeatFrom = 1}}});
  // A view 650 units around the player.
  field.setOffScreenTest([&field](glm::vec3 point) {
    return glm::distance(glm::vec2{point.x, point.z},
                         glm::vec2{field.player().position.x, field.player().position.z}) > 650.0F;
  });
  dwe::scene::place(world.registry().get<dwe::scene::Transform>(field.partnerEntity()),
                    {1850.0F, 0.0F, 0.0F}, 0.0F);
  run(field, 0.1F);
  CHECK(field.partnerTeleports() == 1);
  // The first cell out of view on the partner's side, not behind the player.
  CHECK(field.partner().position.x >= 650.0F);
  CHECK(field.partner().position.x <= 750.0F);
  CHECK(std::abs(field.partner().position.z) <= 50.0F);

  // Far but in view, it is left where it is.
  field.setOffScreenTest([](glm::vec3) { return false; });
  dwe::scene::place(world.registry().get<dwe::scene::Transform>(field.partnerEntity()),
                    {1850.0F, 0.0F, 0.0F}, 0.0F);
  run(field, 0.1F);
  CHECK(field.partnerTeleports() == 1);
  CHECK(field.partner().position.x > 1800.0F);
}

TEST_CASE("a script's hold stands the partner on its idle clip until it lets go") {
  // overworld.c: setMovementEnabled(1, 1) from scriptPauseGame puts the partner in state 11;
  // partner.c: partnerTickIdle starts its clip 0 once and nothing else runs. Measured on the
  // original (04_jijimon_house_interior, talking to Tokomon while Agumon ran up): state 11 and
  // animation 0 at once, still on it and in place 400 frames later with a direction held.
  dwe::scene::World world;
  Field field{world, originalRules()};
  field.enterMapAt(roomMap("room"), at(-1500.0F, 0.0F, Pi / 2.0F));
  field.setInput({.stick = {1.0F, 0.0F}, .run = true});
  run(field, 1.5F);
  REQUIRE(field.partnerFollowClip());
  CHECK(field.partnerFollowClip()->id != PartnerIdleClip);

  const PartnerClip before = *field.partnerFollowClip();
  field.holdForScript(true);
  const std::optional<PartnerClip> held = field.partnerFollowClip();
  REQUIRE(held);
  CHECK(held->id == PartnerIdleClip);
  CHECK(held->starts != before.starts);
  const glm::vec3 where = field.partner().position;
  run(field, 10.0F);
  CHECK(field.partnerFollowClip() == held);
  CHECK(field.partner().position == where);

  // Let go, it follows again.
  field.holdForScript(false);
  run(field, 1.0F);
  CHECK(field.partner().position != where);
}

TEST_CASE("after a script the partner makes room by the player, then stops") {
  dwe::scene::World world;
  Field field{world, originalRules()};
  field.enterMapAt(roomMap("room"), at(0.0F, 0.0F, 0.0F));
  const auto& rules = field.rules();
  const float touching = rules.player.radius + rules.partner.mover.radius;
  const float stops = rules.partner.walkDistance * rules.partner.mover.radius;
  // Entering a map it stands spawnDistance behind the player and stays there, as the original.
  run(field, 1.0F);
  CHECK(partnerDistance(field) == doctest::Approx(rules.partner.spawnDistance));

  const auto releaseWithPartnerAt = [&](float x, float z) {
    field.holdForScript(true);
    dwe::scene::place(world.registry().get<dwe::scene::Transform>(field.partnerEntity()),
                      {x, 0.0F, z}, 0.0F);
    run(field, 0.5F);
    CHECK(partnerDistance(field) == doctest::Approx(std::hypot(x, z)));
    field.holdForScript(false);
    field.tick(Tick);
    CHECK(field.partner().motion == Motion::Walk);
    run(field, 2.0F);
    CHECK(field.partner().motion == Motion::Idle);
    CHECK(partnerDistance(field) > touching);
    CHECK(partnerDistance(field) < stops);
    // Then it looks back at the player.
    const glm::vec3 toPlayer = field.player().position - field.partner().position;
    CHECK(field.partner().yaw ==
          doctest::Approx(dwe::assets::yawOfDirection(toPlayer)).epsilon(1e-3));
  };

  SUBCASE("left against the player's side, it steps straight away") {
    releaseWithPartnerAt(60.0F, 0.0F);
    CHECK(field.partner().position.z == doctest::Approx(0.0F));
    CHECK(field.partner().position.x > 0.0F);
  }
  SUBCASE("left between the camera and the player, it also steps out of the camera's line") {
    // The map's camera looks toward -Z: +Z is nearer the camera.
    releaseWithPartnerAt(0.0F, 60.0F);
    CHECK(std::abs(field.partner().position.x) >= (touching * 0.75F) - 1.0F);
  }
  CHECK(field.partnerTeleports() == 0);
}

TEST_CASE("a care scene holds the player and walks the partner to a range, toward or away") {
  dwe::scene::World world;
  Field field{world, originalRules()};
  const MapDocument map = roomMap("room");
  field.enterMapAt(map, at(0.0F, 0.0F, 0.0F));
  auto& partner = world.registry().get<dwe::scene::Transform>(field.partnerEntity());

  // From far, it walks up to 360 (a take distance of 10).
  dwe::scene::place(partner, {-1000.0F, 0.0F, 0.0F}, 0.0F);
  field.holdForCare(true);
  field.walkPartnerToRange(360.0F);
  field.setInput({.stick = {1.0F, 0.0F}, .run = true});
  for (int i = 0; i < 200 && field.partnerWalking(); ++i) {
    field.tick(Tick);
  }
  CHECK_FALSE(field.partnerWalking());
  CHECK(partnerDistance(field) <= 360.0F);
  CHECK(partnerDistance(field) > 300.0F);
  // The player stayed put and the game time ran.
  CHECK(field.player().position.x == doctest::Approx(0.0F));
  CHECK(field.gameTimeRan());

  // From close, it walks away past the range.
  dwe::scene::place(partner, {-100.0F, 0.0F, 0.0F}, 0.0F);
  field.walkPartnerToRange(360.0F);
  for (int i = 0; i < 200 && field.partnerWalking(); ++i) {
    field.tick(Tick);
  }
  CHECK_FALSE(field.partnerWalking());
  CHECK(partnerDistance(field) > 360.0F);
  // The two face each other.
  const glm::vec3 toPlayer = field.player().position - field.partner().position;
  CHECK(field.partner().yaw ==
        doctest::Approx(dwe::assets::yawOfDirection(toPlayer)).epsilon(1e-3));

  // Let go, the player's run starts on the next frame and moves on the one after.
  field.holdForCare(false);
  run(field, 0.1F);
  CHECK(field.player().position.x > 0.0F);
}

TEST_CASE("a care hold walks a partner from between 2.5 and 3.5 radii to beside the player") {
  // partner.c: partnerTickSleep and partnerTickPraiseScold, sub states 0 and 1: the partner walks
  // straight up until getPartnerTamerCloseness gives 2 (nearer than 2.5 radii), whatever the follow
  // would do there (partnerTickWalking only walks once its condition clip has ended).
  dwe::scene::World world;
  Field field{world, originalRules()};
  const MapDocument map = roomMap("room");
  field.enterMapAt(map, at(0.0F, 0.0F, 0.0F));
  const float beside = field.rules().partner.walkDistance * field.rules().partner.mover.radius;
  const float sprint = field.rules().partner.runDistance * field.rules().partner.mover.radius;
  auto& partner = world.registry().get<dwe::scene::Transform>(field.partnerEntity());
  dwe::scene::place(partner, {-(beside + sprint) / 2.0F, 0.0F, 0.0F}, 0.0F);
  REQUIRE_FALSE(field.partnerBeside());

  field.holdForCare(true);
  field.walkPartnerToRange(beside);
  for (int i = 0; i < 200 && field.partnerWalking(); ++i) {
    field.tick(Tick);
  }
  CHECK_FALSE(field.partnerWalking());
  CHECK(field.partnerBeside());
  CHECK(field.player().position.x == doctest::Approx(0.0F));
}

TEST_CASE("a sleep's care hold stops the game time, the other care scenes keep it") {
  // partner.c: partnerTickSleep stops the game time from its first frame (stopGameTime), which
  // slides the clock and the bars off (clock.c: renderGameClock); feeding, the toilet, praise and
  // scold keep it running.
  dwe::scene::World world;
  Field field{world, originalRules()};
  const MapDocument map = roomMap("room");
  field.enterMapAt(map, at(0.0F, 0.0F, 0.0F));

  field.holdForCare(true, dwe::game::field::CareClock::Stops);
  field.tick(Tick);
  CHECK(field.heldForCare());
  CHECK_FALSE(field.gameTimeRan());
  field.holdForCare(false);
  field.tick(Tick);
  CHECK(field.gameTimeRan());

  field.holdForCare(true);
  field.tick(Tick);
  CHECK(field.gameTimeRan());
}

TEST_CASE("a care scene runs the partner to a point and pans the camera, given back on release") {
  dwe::scene::World world;
  Field field{world, originalRules()};
  const MapDocument map = roomMap("room");
  field.enterMapAt(map, at(0.0F, 0.0F, 0.0F));
  field.holdForCare(true);
  const glm::vec3 point{800.0F, field.partner().position.y, 600.0F};
  field.panCameraByScript(point, 1.0F);
  field.walkPartnerTo(point, Motion::Run);
  bool ran = false;
  for (int i = 0; i < 200 && field.partnerWalking(); ++i) {
    field.tick(Tick);
    ran = ran || field.partner().motion == Motion::Run;
  }
  CHECK(ran);
  CHECK_FALSE(field.partnerWalking());
  CHECK(field.partner().position.x == doctest::Approx(800.0F).epsilon(0.01));
  CHECK(field.partner().position.z == doctest::Approx(600.0F).epsilon(0.01));
  run(field, 1.0F);
  REQUIRE(field.scriptCamera().has_value());
  CHECK(field.scriptCamera().value_or(glm::vec3{}).x == doctest::Approx(800.0F));
  field.holdForCare(false);
  CHECK_FALSE(field.scriptCamera().has_value());
}

TEST_CASE("a care walk walks from its first frame, and let go on its point the partner walks on") {
  // partner.c: tickPartnerToilet: sub state 3 starts the walk clip 2 as it sets the walk back,
  // sub state 4 lets the script go on the point with the clip still on, and partnerTickWalking
  // goes on from it. Measured on the original (the toilet by the start's house): clip 2 from the
  // frame the toilet clip ends, still clip 2 when state 1 takes over, and clip 0 only once it
  // stops by the player.
  dwe::scene::World world;
  Field field{world, originalRules()};
  field.enterMapAt(roomMap("room"), at(0.0F, 0.0F, 0.0F));
  auto& partner = world.registry().get<dwe::scene::Transform>(field.partnerEntity());
  dwe::scene::place(partner, {900.0F, 0.0F, 300.0F}, 0.0F);
  field.holdForCare(true);
  const glm::vec3 point{900.0F, field.partner().position.y, 700.0F};
  field.walkPartnerTo(point, Motion::Walk);
  CHECK(field.partner().motion == Motion::Walk);
  for (int i = 0; i < 400 && field.partnerWalking(); ++i) {
    field.tick(Tick);
    REQUIRE(field.partner().motion == Motion::Walk);
  }
  REQUIRE_FALSE(field.partnerWalking());

  field.holdForCare(false);
  const std::optional<PartnerClip> clip = field.partnerFollowClip();
  REQUIRE(clip);
  CHECK(clip->id == PartnerWalkClip);
  CHECK(field.partner().motion == Motion::Walk);
  field.tick(Tick);
  REQUIRE(field.partnerFollowClip());
  CHECK(field.partnerFollowClip()->id != PartnerIdleClip);
}

TEST_CASE("a care walk to a point goes through walls and ends on the point's cell") {
  dwe::scene::World world;
  Field field{world, originalRules()};
  // tickPartnerToilet: the partner runs into the toilet's hut, whose cells are walls; the clip's
  // root motion carries it with no collision test, and tickEntityWalkTo ends on the point's cell.
  MapDocument map = roomMap("room");
  fill(map, {.column = 54, .row = 30}, {.column = 54, .row = 69}, Blocked);
  field.enterMapAt(map, at(0.0F, 0.0F, 0.0F));
  auto& partner = world.registry().get<dwe::scene::Transform>(field.partnerEntity());
  dwe::scene::place(partner, {0.0F, 0.0F, 300.0F}, 0.0F);
  field.holdForCare(true);
  // The point is in the middle of its cell: the walk ends once the partner steps into it.
  const glm::vec3 point{850.0F, field.partner().position.y, 50.0F};
  field.walkPartnerTo(point, Motion::Run);
  for (int i = 0; i < 200 && field.partnerWalking(); ++i) {
    field.tick(Tick);
  }
  CHECK_FALSE(field.partnerWalking());
  CHECK(dwe::assets::originalCellAt(field.partner().position) ==
        dwe::assets::originalCellAt(point));
  CHECK(field.partner().position.x < 850.0F);
}

TEST_CASE("a pile's scene runs the partner up to the player, and leaves it where it is if near") {
  dwe::scene::World world;
  Field field{world, originalRules()};
  const MapDocument map = roomMap("room");
  field.enterMapAt(map, at(0.0F, 0.0F, 0.0F));
  auto& partner = world.registry().get<dwe::scene::Transform>(field.partnerEntity());
  const float runRange = field.rules().partner.runDistance * field.rules().partner.mover.radius;

  dwe::scene::place(partner, {-1000.0F, 0.0F, 0.0F}, 0.0F);
  field.holdForCare(true);
  field.runPartnerUp();
  bool ran = false;
  for (int i = 0; i < 200 && field.partnerWalking(); ++i) {
    field.tick(Tick);
    ran = ran || field.partner().motion == Motion::Run;
  }
  CHECK(ran);
  CHECK_FALSE(field.partnerWalking());
  CHECK(partnerDistance(field) <= runRange);

  dwe::scene::place(partner, {-100.0F, 0.0F, 0.0F}, 0.0F);
  field.runPartnerUp();
  field.tick(Tick);
  CHECK_FALSE(field.partnerWalking());
  CHECK(field.partner().position.x == doctest::Approx(-100.0F));
  field.holdForCare(false);
}

TEST_CASE("click-to-move walks around walls to the target") {
  auto map = roomMap("room");
  fill(map, {.column = 50, .row = 30}, {.column = 50, .row = 65}, Blocked); // gap at z 1600 to 2000
  fill(map, {.column = 45, .row = 68}, {.column = 46, .row = 69},
       113); // an unrelated warp near the gap
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
  CHECK(glm::distance(field.player().position, target) <=
        field.rules().input.arriveDistance + 1.0F);
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

TEST_CASE("a click-to-move target never outlives a hold of the player") {
  auto map = roomMap("room");
  dwe::scene::World world;
  Field field{world, originalRules()};
  field.enterMapAt(map, at(-1000.0F, 0.0F));
  const glm::vec3 target{1000.0F, 0.0F, 0.0F};
  run(field, 0.5F); // past the entry's fade

  // A script (a talk) takes the player while he walks to a click: the target goes, and the player
  // stands still once the script lets go.
  field.setInput({.moveTarget = target});
  REQUIRE(field.hasMoveTarget());
  field.setInput({});
  field.tick(Tick);
  CHECK(field.playerFree());
  field.holdForScript(true);
  field.tick(Tick);
  CHECK_FALSE(field.hasMoveTarget());
  CHECK_FALSE(field.playerFree());
  // A click while held (the one that closes a textbox) is no walk order either.
  field.setInput({.moveTarget = target});
  CHECK_FALSE(field.hasMoveTarget());
  field.holdForScript(false);
  field.setInput({});
  const glm::vec3 released = field.player().position;
  run(field, 1.0F);
  CHECK(glm::distance(field.player().position, released) < 1.0F);

  // The same for a care scene (praise, feeding, the toilet) ...
  field.setInput({.moveTarget = target});
  REQUIRE(field.hasMoveTarget());
  field.holdForCare(true);
  CHECK_FALSE(field.hasMoveTarget());
  field.setInput({.moveTarget = target});
  CHECK_FALSE(field.hasMoveTarget());
  field.holdForCare(false);

  // ... and for whatever runs instead of the field (a menu, a gym course, a battle), which drops
  // it.
  field.setInput({.moveTarget = target});
  REQUIRE(field.hasMoveTarget());
  field.dropMoveTarget();
  CHECK_FALSE(field.hasMoveTarget());
  CHECK(field.path().empty());
}

TEST_CASE("a click on a warp walks onto it") {
  MapDocument town = roomMap("town");
  fill(town, {.column = 50, .row = 40}, {.column = 52, .row = 41}, 112);
  town.warps[2].targetMapId = "house";
  town.warps[2].targetExit = 0;
  dwe::scene::World world;
  Field field{world, originalRules()};
  field.enterMapAt(town, at(-1000.0F, 1000.0F));
  field.setInput(
      {.run = true, .moveTarget = dwe::assets::collisionCellCenter({.column = 51, .row = 40})});
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

TEST_CASE("a reload of the field's data brings new script map changes and rules") {
  MapDocument house = roomMap("house");
  constexpr std::uint8_t ScriptStep = 51;
  fill(house, {.column = 40, .row = 50}, {.column = 59, .row = 51}, ScriptStep);
  dwe::scene::World world;
  Field field{world, originalRules(), {.warps = {}, .facts = triggersSet({})}};
  field.enterMapAt(house, at(0.0F, 1000.0F));
  CHECK_FALSE(field.warpsOn(ScriptStep));

  dwe::game::field::FieldRules rules = originalRules();
  rules.transition.fadeInFrames += 3;
  field.reloadData(rules, {houseDoor()});
  CHECK(field.warpsOn(ScriptStep));
  CHECK(field.rules().transition.fadeInFrames == originalRules().transition.fadeInFrames + 3);

  field.reloadData(originalRules(), {});
  CHECK_FALSE(field.warpsOn(ScriptStep));
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

  // The autotest's walk_to plans like this: every cell that changes the map now is out, but the
  // goal's.
  std::vector<CollisionCell> goals;
  for (std::int32_t column = 49; column <= 51; ++column) {
    for (std::int32_t row = 35; row <= 36; ++row) {
      goals.push_back({.column = column, .row = row});
    }
  }
  const MapCollision& collision = field.collision();
  const std::vector<glm::vec3> path =
      dwe::game::field::findPath(collision, field.player().position, goals,
                                 field.rules().player.radius, [&](CollisionCell cell) {
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
      CHECK(collision.trigger(dwe::assets::collisionCellAt(glm::mix(from, to, along))) !=
            ScriptStep);
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

TEST_CASE(
    "a warp the field takes writes the triggers it sets, and the map's cells are decided again at once") {
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
  Field field{world,
              originalRules(),
              {.warps = {door, window}, .facts = story.reader(), .writeTrigger = story.writer()}};
  field.enterMapAt(house, at(0.0F, 1200.0F));
  CHECK(field.warpsOn(OtherStep));
  const auto change = walkSouth(field, house);
  REQUIRE(change.has_value());
  CHECK(story.writes == std::vector<std::pair<std::int32_t, bool>>{{203, true}, {7, false}});
  // The walk out still sees the house: its cells follow the triggers the warp wrote.
  CHECK_FALSE(field.warpsOn(OtherStep));
}

TEST_CASE(
    "after a trigger written outside the field, refreshing decides the map's step cells again") {
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
    dwe::core::addLogSink([](dwe::core::LogLevel /*level*/, std::string_view message) {
      lines.emplace_back(message);
    });
    return true;
  }();
  static_cast<void>(added);
  lines.clear();
  MapDocument house = roomMap("house");
  fill(house, {.column = 46, .row = 65}, {.column = 53, .row = 66}, 51);
  dwe::scene::World world;
  Field field{world,
              originalRules(),
              {.warps = {houseDoor()}, .facts = triggersSet({203}), .explore = true}};
  const auto change = walkSouth(field, house);
  if (!change) {
    FAIL("no map change");
    return;
  }
  CHECK(change->map == "town");
  CHECK(std::ranges::any_of(
      lines, [](const std::string& line) { return line.contains("free exploration opens"); }));

  // A place without data is not offered.
  dwe::scene::World filtered;
  Field noTown{filtered,
               originalRules(),
               {.warps = {houseDoor()},
                .facts = triggersSet({203}),
                .explore = true,
                .hasMap = [](std::string_view map) { return map != "town"; }}};
  CHECK_FALSE(walkSouth(noTown, house).has_value());
}

TEST_CASE(
    "free exploration lets the player choose among a cell's places, starting on the story's, or stay") {
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

TEST_CASE(
    "free exploration turns on and off while playing, and the story's conditions decide again") {
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
  fill(town, {.column = 50, .row = 38}, {.column = 52, .row = 39},
       Blocked); // a wall right behind the warp
  town.warps[2].targetMapId = "house";
  town.warps[2].targetExit = 0;
  MapDocument house = roomMap("house");
  house.warps[0] = {.exit = 0,
                    .spawn = {0, 0, 0},
                    .rotation = 0,
                    .targetMap = -1,
                    .targetMapId = std::nullopt,
                    .targetExit = -1};

  dwe::scene::World world;
  const auto rules = originalRules();
  const dwe::game::field::TransitionRules& transition = rules.transition;
  const auto darkness = [&](std::int32_t shade) {
    return dwe::game::field::shadeDarkness(transition, shade);
  };
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

  // Frame 9 of the fade out: the player walks on at walk speed, through the wall, whatever the
  // input says.
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
  // Frame 20: the player starts walking again, under the fade's last shade, which goes on the next
  // frame; the walk's first frame does not move (anim.c: startAnimation), the next one does.
  run(field, 0.05F);
  CHECK(field.fadeLevel() == doctest::Approx(darkness(20)));
  CHECK(field.player().motion == Motion::Walk);
  CHECK(field.gameTimeRan());
  run(field, 0.05F);
  CHECK(field.fadeLevel() == doctest::Approx(0.0F));
  CHECK(field.player().position != glm::vec3{0.0F});
}

TEST_CASE("a map change's fade protects the game from the warp until the fade in's end") {
  // fade.c: fadeToBlack sets FADE_PROTECTION on the fade out's first frame and renderFadeIn clears
  // it on the frame after the fade in's last shade, one frame after the player walks again.
  MapDocument town = roomMap("town");
  fill(town, {.column = 50, .row = 40}, {.column = 52, .row = 41}, 112);
  town.warps[2].targetMapId = "house";
  town.warps[2].targetExit = 0;
  MapDocument house = roomMap("house");
  house.warps[0] = {.exit = 0,
                    .spawn = {0, 0, 0},
                    .rotation = 0,
                    .targetMap = -1,
                    .targetMapId = std::nullopt,
                    .targetExit = -1};

  dwe::scene::World world;
  Field field{world, originalRules()};
  field.enterMapAt(town, at(0.0F, -600.0F, Pi));
  CHECK_FALSE(field.fadeProtected());
  field.setInput({.stick = {0.0F, 1.0F}});
  for (int i = 0; i < 120 && !field.mapChangePending(); ++i) {
    CHECK_FALSE(field.fadeProtected());
    field.tick(Tick);
  }
  REQUIRE(field.mapChangePending());
  CHECK(field.fadeProtected());
  std::optional<MapChange> change;
  for (int ticks = 0; ticks < 60 && !change; ++ticks) {
    field.tick(Tick);
    change = field.takeMapChange();
    CHECK(field.fadeProtected());
  }
  REQUIRE(change.has_value());
  REQUIRE(field.enterMap(house, change->exit).has_value());
  CHECK(field.fadeProtected());
  // Up to the fade in's 20th frame, when the player walks again, the protection holds.
  run(field, 0.95F);
  CHECK(field.playerFree());
  CHECK(field.fadeProtected());
  // The next frame draws no shade and ends it.
  run(field, 0.05F);
  CHECK(field.fadeLevel() == doctest::Approx(0.0F));
  CHECK_FALSE(field.fadeProtected());
}

TEST_CASE("a step cell is the one the original reads, the neighbour away from 0 on an edge") {
  // map_collision.c: getTileTrigger (getModelTile) truncates toward 0 and then takes one column
  // less for x < 0 and one row less for z > 0: frzl17's exit 1 puts the player on (-400, 1400), the
  // corner of four cells, and the curling's step cell (column 45, row 35) is the one it reads, so
  // the match starts on arrival.
  MapDocument rink = roomMap("rink");
  rink.warps.at(1).spawn = {-400, 0, 1400};
  const glm::vec3 spawn = dwe::assets::toWorldPosition({-400, 0, 1400});
  const CollisionCell start = dwe::assets::originalCellAt(spawn);
  CHECK(start == CollisionCell{.column = 45, .row = 35});
  CHECK_FALSE(start == dwe::assets::collisionCellAt(spawn));
  fill(rink, start, start, 51);

  dwe::scene::World world;
  Field field{world, originalRules()};
  REQUIRE(field.enterMap(rink, 1).has_value());
  field.startPlay();
  field.tick(Tick);
  CHECK(std::ranges::find(field.cellSections(), 51) != field.cellSections().end());
  // Standing on it, the cell asks again every frame (tamer.c: checkMapInteraction).
  field.tick(Tick);
  CHECK(std::ranges::find(field.cellSections(), 51) != field.cellSections().end());
}

TEST_CASE("play starts with a long fade in that holds no one, and a map change then stays dark") {
  // main.c: main: fadeFromBlack(0x28) after the first map: a new game's arrival cell runs on the
  // first frame, and its scripted map change fades out from the fade in's dark first shade
  // (fade.c: fadeToBlack keeps FADE_PROGRESS), so the first map never shows.
  MapDocument town = roomMap("town");
  const CollisionCell spawnCell = dwe::assets::collisionCellAt(glm::vec3{0.0F});
  fill(town, spawnCell, spawnCell, 54);

  dwe::scene::World world;
  const auto rules = originalRules();
  const dwe::game::field::TransitionRules& transition = rules.transition;
  CHECK(transition.startFadeInFrames == 40);
  Field field{world, rules};
  REQUIRE(field.enterMap(town, 0).has_value());
  field.startPlay();
  // fade.c: renderFadeIn's first frame of 40: 160 - 160 / 41.
  CHECK(field.fadeLevel() == doctest::Approx(dwe::game::field::shadeDarkness(transition, 157)));
  field.tick(Tick);
  CHECK(std::ranges::find(field.cellSections(), 54) != field.cellSections().end());
  CHECK(field.gameTimeRan());

  field.startScriptedMapChange({.map = "house", .exit = 0, .trigger = 54}, false);
  std::optional<MapChange> change;
  for (int i = 0; i < 120 && !change; ++i) {
    CHECK(field.fadeLevel() == doctest::Approx(1.0F));
    field.tick(Tick);
    change = field.takeMapChange();
  }
  CHECK(change.has_value());

  // The map change's fade in on the next map is the usual one of 20 frames (fadeFromBlack(20)).
  REQUIRE(field.enterMap(roomMap("house"), 0).has_value());
  CHECK(field.fadeLevel() == doctest::Approx(dwe::game::field::shadeDarkness(transition, 153)));
  run(field, 1.1F);
  CHECK(field.fadeLevel() == doctest::Approx(0.0F));
}

TEST_CASE("the fade in runs on while a battle holds the field") {
  // fade.c: renderFadeIn is a drawn object: it steps on every drawn frame, a battle's too, so a
  // battle started under the start of play's fade in does not stay dark.
  MapDocument town = roomMap("town");
  dwe::scene::World world;
  const auto rules = originalRules();
  const dwe::game::field::TransitionRules& transition = rules.transition;
  Field field{world, rules};
  REQUIRE(field.enterMap(town, 0).has_value());
  field.startPlay();
  CHECK(field.fadeLevel() == doctest::Approx(dwe::game::field::shadeDarkness(transition, 157)));
  // The battle ticks only the fade in: 40 frames of 20 a second and the one that ends it.
  for (int i = 0; i < 60; ++i) {
    field.tickFadeIn(Tick);
  }
  CHECK(field.fadeLevel() > 0.0F);
  for (int i = 0; i < 90; ++i) {
    field.tickFadeIn(Tick);
  }
  CHECK(field.fadeLevel() == doctest::Approx(0.0F));
  CHECK(field.shadeNow() == std::optional<std::int32_t>{40});
  // Nothing else moved: no cell ran, no game time passed.
  CHECK_FALSE(field.gameTimeRan());
}

TEST_CASE("a shade held by a battle stays, a map change fades on from it and a release fades in") {
  // battle_tick.c: handleCommands: a partner called back fades the screen to black (fadeToBlack)
  // while the battle still runs; the field keeps that shade until a map change or a fade in.
  MapDocument town = roomMap("town");
  dwe::scene::World world;
  const auto rules = originalRules();
  const dwe::game::field::TransitionRules& transition = rules.transition;
  Field field{world, rules};
  REQUIRE(field.enterMap(town, 0).has_value());
  field.startPlay();
  field.holdShade(transition.shadeMax);
  CHECK(field.shadeHeld());
  CHECK(field.shadeNow() == std::optional<std::int32_t>{transition.shadeMax});
  for (int i = 0; i < 30; ++i) {
    field.tickFadeIn(Tick);
  }
  CHECK(field.fadeLevel() == doctest::Approx(1.0F));

  SUBCASE("a scripted map change stays black") {
    field.startScriptedMapChange({.map = "house", .exit = 0, .trigger = 0}, false);
    CHECK_FALSE(field.shadeHeld());
    CHECK(field.fadeLevel() == doctest::Approx(1.0F));
    field.releaseShade();
    CHECK(field.fadeLevel() == doctest::Approx(1.0F));
  }
  SUBCASE("a release with no map change fades back in") {
    field.releaseShade();
    CHECK_FALSE(field.shadeHeld());
    CHECK(field.fadeLevel() > 0.0F);
    for (int i = 0; i < 60; ++i) {
      field.tickFadeIn(Tick);
    }
    CHECK(field.fadeLevel() == doctest::Approx(0.0F));
  }
}

TEST_CASE("a fade out started during a fade in goes on from the shade the fade in drew") {
  // fade.c: fadeToBlack adds its step to FADE_PROGRESS; a whole warp fade in leaves 20 there.
  const auto rules = originalRules();
  const dwe::game::field::TransitionRules& transition = rules.transition;
  CHECK(dwe::game::field::fadeInShade(transition, 1, 40) == 157);
  CHECK(dwe::game::field::fadeInShade(transition, 40, 40) == 40);
  CHECK(dwe::game::field::fadeOutShade(transition, 1, 157) == 160);
  CHECK(dwe::game::field::fadeOutShade(transition, 1, 40) == 47);
  CHECK(dwe::game::field::fadeOutShade(transition, 1) == 27);
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

TEST_CASE("free exploration takes the story's place by the original's warp, with its triggers") {
  using dwe::game::field::GuardBranch;
  using dwe::game::field::GuardTest;
  MapDocument house = roomMap("house");
  fill(house, {.column = 46, .row = 65}, {.column = 53, .row = 66}, 51);
  // Two paths to the town: the first, which the chooser shows, behind trigger 7, which is clear;
  // the second the original takes.
  dwe::game::field::ScriptWarp blocked = houseDoor();
  blocked.guard.terms = {
      GuardBranch{.tests = {GuardTest{.test = "trigger", .trigger = 7}}, .holds = true}};
  blocked.setsFlags = {{.trigger = 11, .set = true}};
  dwe::game::field::ScriptWarp original = houseDoor();
  original.setsFlags = {{.trigger = 12, .set = true}};
  TestStory story;
  dwe::scene::World world;
  Field field{world,
              originalRules(),
              {.warps = {castleDoor(), blocked, original},
               .facts = story.reader(),
               .writeTrigger = story.writer(),
               .explore = true}};
  CHECK_FALSE(walkSouth(field, house).has_value());
  const auto& found = field.destinationChoice();
  if (!found) {
    FAIL("no destination choice");
    return;
  }
  REQUIRE(found->places.size() == 2);
  CHECK(found->vanilla == std::optional<std::size_t>{1});
  field.chooseDestination(1);
  const auto change = tickToChange(field);
  if (!change) {
    FAIL("no map change");
    return;
  }
  CHECK(change->map == "town");
  CHECK(story.writes == std::vector<std::pair<std::int32_t, bool>>{{12, true}});
}

TEST_CASE(
    "click-to-move from a spawn on a script warp's cells walks off them without changing the map") {
  MapDocument house = roomMap("house");
  constexpr std::uint8_t ScriptStep = 51;
  // The player arrives inside a block of step cells whose warp is on: every cell around is the same
  // warp.
  fill(house, {.column = 47, .row = 47}, {.column = 53, .row = 53}, ScriptStep);
  dwe::scene::World world;
  Field field{world, originalRules(), {.warps = {houseDoor()}, .facts = triggersSet({})}};
  field.enterMapAt(house, {.position = dwe::assets::collisionCellCenter({.column = 50, .row = 50}),
                           .yaw = 0.0F});
  const glm::vec3 target = dwe::assets::collisionCellCenter({.column = 50, .row = 40});
  field.setInput({.moveTarget = target});
  REQUIRE(field.hasMoveTarget());
  field.setInput({});
  for (int i = 0; i < 60 * 10 && field.hasMoveTarget(); ++i) {
    field.tick(Tick);
    CHECK_FALSE(field.mapChangePending());
  }
  CHECK_FALSE(field.hasMoveTarget());
  CHECK(glm::distance(field.player().position, target) <=
        field.rules().input.arriveDistance + 1.0F);
}

namespace {

// Adds an NPC record standing at world (x, z) to the map; its slot is the record's index.
std::size_t addNpc(MapDocument& map, float x, float z, std::int32_t scriptId) {
  dwe::assets::MapNpc npc;
  npc.typeId = 1;
  npc.position = {static_cast<std::int32_t>(x), 0, static_cast<std::int32_t>(-z)};
  npc.scriptId = scriptId;
  npc.hp = 10;
  npc.currentHp = 10;
  map.npcs.push_back(npc);
  return map.npcs.size() - 1;
}

float groundDistance(glm::vec3 a, glm::vec3 b) {
  return std::hypot(a.x - b.x, a.z - b.z);
}

bool spawnNpc(Field& field, std::size_t slot) {
  return field.npcs().spawn(
      {.slot = slot, .species = 1, .autotalk = false, .model = "npc", .radius = 100.0F});
}

dwe::game::field::GroundSquare squareAt(glm::vec3 position, float half) {
  return {.centre = {position.x, position.z}, .half = half};
}

void placeAt(dwe::scene::World& world, dwe::scene::Entity entity, float x, float z) {
  dwe::scene::place(world.registry().get<dwe::scene::Transform>(entity), {x, 0.0F, z}, 0.0F);
}

} // namespace

TEST_CASE("the release rule leaves characters standing apart where they are") {
  dwe::scene::World world;
  Field field{world, originalRules()};
  field.enterMapAt(roomMap("room"), at(0.0F, 0.0F, 0.0F));
  run(field, 0.5F);
  const glm::vec3 player = field.player().position;
  const glm::vec3 partner = field.partner().position;
  CHECK(field.separateActors("test") == 0);
  CHECK(field.player().position == player);
  CHECK(field.partner().position == partner);
  CHECK(field.overlapEpisodes() == 0);
}

TEST_CASE("the release rule puts a partner left on the player on the nearest free spot") {
  dwe::scene::World world;
  Field field{world, originalRules()};
  field.enterMapAt(roomMap("room"), at(0.0F, 0.0F, 0.0F));
  const auto& rules = field.rules();
  placeAt(world, field.partnerEntity(), 40.0F, 10.0F);
  CHECK(field.separateActors("test") == 1);
  // The player stays; the partner touches it no more, close by and on the side it was.
  CHECK(field.player().position == glm::vec3{0.0F});
  CHECK_FALSE(dwe::game::field::squaresOverlap(
      squareAt(field.player().position, rules.player.radius),
      squareAt(field.partner().position, rules.partner.mover.radius)));
  CHECK(partnerDistance(field) < rules.partner.walkDistance * rules.partner.mover.radius);
  CHECK(field.partner().position.x > 0.0F);
  // Snapped, not slid: nothing to draw between two places.
  const auto& transform = world.registry().get<dwe::scene::Transform>(field.partnerEntity());
  CHECK(transform.previousPosition == transform.position);
}

TEST_CASE("the release rule takes a character out of a wall, and the player only out of one") {
  dwe::scene::World world;
  Field field{world, originalRules()};
  MapDocument map = roomMap("room");
  // A pillar of 3 x 3 cells around world (1050, 1050).
  fill(map, {.column = 59, .row = 59}, {.column = 61, .row = 61}, Blocked);
  field.enterMapAt(map, at(-1000.0F, -1000.0F, 0.0F));
  const MapCollision& grid = field.collision();
  const auto blockedAt = [&](glm::vec3 position) {
    return grid.isBlocked(dwe::assets::collisionCellAt(position));
  };
  SUBCASE("the partner") {
    placeAt(world, field.partnerEntity(), 1050.0F, 1050.0F);
    CHECK(field.separateActors("test") == 1);
    CHECK_FALSE(blockedAt(field.partner().position));
    CHECK(dwe::game::field::fitsOnGrid(grid, field.partner().position,
                                       field.rules().partner.mover.radius));
  }
  SUBCASE("the player") {
    placeAt(world, field.playerEntity(), 1050.0F, 1050.0F);
    placeAt(world, field.partnerEntity(), -1000.0F, -1000.0F);
    CHECK(field.separateActors("test") == 1);
    CHECK_FALSE(blockedAt(field.player().position));
    CHECK(groundDistance(field.player().position, {1050.0F, 0.0F, 1050.0F}) < 400.0F);
  }
}

TEST_CASE("a script id names the spawned NPC's record when the map has several with it") {
  dwe::scene::World world;
  Field field{world, originalRules()};
  MapDocument map = roomMap("room");
  // A shop's keepers: two records with script id 5, the second of another species.
  addNpc(map, 300.0F, 0.0F, 5);
  const std::size_t keeper = addNpc(map, 600.0F, 0.0F, 5);
  map.npcs[keeper].typeId = 2;
  field.enterMapAt(map, at(0.0F, 0.0F, 0.0F));
  CHECK(field.npcs().recordOf(5) == nullptr);
  REQUIRE(field.npcs().spawn(
      {.slot = keeper, .species = 2, .autotalk = false, .model = "npc", .radius = 100.0F}));
  REQUIRE(field.npcs().recordOf(5) != nullptr);
  CHECK(field.npcs().recordOf(5)->typeId == 2);
}

TEST_CASE("an NPC put on the player stays where it was put; the player walks off it") {
  dwe::scene::World world;
  Field field{world, originalRules()};
  MapDocument map = roomMap("room");
  const std::size_t slot = addNpc(map, 30.0F, 0.0F, 7);
  field.enterMapAt(map, at(0.0F, 0.0F, 0.0F));
  run(field, 0.2F);
  REQUIRE(spawnNpc(field, slot));
  field.tick(Tick);
  // The original moves nobody: the NPC and the player stand in each other.
  CHECK(*field.npcs().positionOf(7) == glm::vec3{30.0F, 0.0F, 0.0F});
  CHECK(field.player().position == glm::vec3{0.0F});
  CHECK_FALSE(Field::overlaps(squareAt(field.partner().position, 120.0F),
                              squareAt(*field.npcs().positionOf(7), 100.0F)));
  // Away from the NPC the player is free.
  field.setInput({.stick = {-1.0F, 0.0F}, .run = true});
  run(field, 1.0F);
  CHECK(groundDistance(field.player().position, *field.npcs().positionOf(7)) > 300.0F);
  CHECK(*field.npcs().positionOf(7) == glm::vec3{30.0F, 0.0F, 0.0F});
}

TEST_CASE("an NPC off screen is out of the collision, as Etemon up in his tree") {
  // mayo04a: Etemon waits 1000 units up over the tree's door until the story drops him; off screen
  // (entity.isOnScreen 0) the original's collision skips him (utils2.c: isInvisible), so the
  // player walks over his spot to the door.
  dwe::scene::World world;
  Field field{world, originalRules()};
  MapDocument map = roomMap("room");
  const std::size_t slot = addNpc(map, 600.0F, 0.0F, 5);
  field.enterMapAt(map, at(0.0F, 0.0F, 0.0F));
  REQUIRE(spawnNpc(field, slot));
  const auto walkRight = [&] {
    field.setInput({.stick = {1.0F, 0.0F}, .run = true});
    run(field, 1.0F);
    field.setInput({});
    const float reached = field.player().position.x;
    placeAt(world, field.playerEntity(), 0.0F, 0.0F);
    return reached;
  };
  SUBCASE("on screen it stops the player") {
    CHECK(walkRight() < 600.0F - 200.0F + 1.0F);
  }
  SUBCASE("off screen the player walks through its spot") {
    field.npcs().setOnScreen(*field.npcs().entityOf(5), false);
    CHECK(walkRight() > 800.0F);
    // It is still on the field, for the state trace.
    CHECK(field.npcs().bodies().size() == 1);
    // Back on screen it is solid again.
    field.npcs().setOnScreen(*field.npcs().entityOf(5), true);
    CHECK(walkRight() < 600.0F - 200.0F + 1.0F);
  }
}

TEST_CASE("an NPC path past its map's waypoints uses the zeroed slots, as the original") {
  // dgha02's script id 7 has more path parts than waypoints: the original reads the table's slots
  // that loadMap cleared, so it waits 0 frames and walks to the map's origin.
  dwe::scene::World world;
  Field field{world, originalRules()};
  MapDocument map = roomMap("room");
  const std::size_t slot = addNpc(map, 1000.0F, 0.0F, 7);
  map.npcs[slot].aiSections = {0, 0, 1, -1, -1, -1, -1, -1};
  map.npcs[slot].waypoints = {{1, 0, 0}};
  field.enterMapAt(map, at(-1500.0F, 0.0F, 1500.0F));
  REQUIRE(spawnNpc(field, slot));
  run(field, 3.0F);
  CHECK(groundDistance(*field.npcs().positionOf(7), glm::vec3{0.0F}) < 900.0F);
}

TEST_CASE("an NPC on a wall cell stays there when it appears and after a talk") {
  // Kunemon's bed in mayo02 is a wall cell: the original keeps it at (143, -1000) through a talk
  // and 300 frames after (DuckStation, e1-cp28).
  dwe::scene::World world;
  Field field{world, originalRules()};
  MapDocument map = roomMap("room");
  // A pillar of 3 x 3 cells around world (1050, 1050), the NPC in its middle.
  fill(map, {.column = 59, .row = 59}, {.column = 61, .row = 61}, Blocked);
  const std::size_t slot = addNpc(map, 1050.0F, 1050.0F, 6);
  field.enterMapAt(map, at(-1000.0F, -1000.0F, 0.0F));
  REQUIRE(spawnNpc(field, slot));
  const glm::vec3 home{1050.0F, 0.0F, 1050.0F};
  run(field, 0.5F);
  CHECK(*field.npcs().positionOf(6) == home);
  // A talk holds it and lets it go (setMovementEnabled), with the release rule after the script.
  field.holdForScript(true);
  REQUIRE(field.npcs().hold(6, true));
  run(field, 0.5F);
  field.holdForScript(false);
  REQUIRE(field.npcs().hold(6, false));
  run(field, 5.0F);
  CHECK(*field.npcs().positionOf(6) == home);
  CHECK(field.separateActors("test") == 0);
}

TEST_CASE("an NPC playing a script's clip moves by the clip's root motion") {
  // Kunemon's eating clips in mayo02 (30, then 8) carry it 200 and 208 units forward off its bed,
  // through the bed's wall cells (anim.c: tickAnimation, applyRootMomentum; no collision test).
  dwe::scene::World world;
  Field field{world, originalRules()};
  MapDocument map = roomMap("room");
  fill(map, {.column = 59, .row = 59}, {.column = 61, .row = 61}, Blocked);
  const std::size_t slot = addNpc(map, 1050.0F, 1050.0F, 6);
  field.enterMapAt(map, at(-1000.0F, -1000.0F, 0.0F));
  REQUIRE(spawnNpc(field, slot));
  const dwe::scene::Entity npc = *field.npcs().entityOf(6);
  // Five frames of 40 units forward and one of 10 to its side; a walk clip that the script drives.
  constexpr std::int32_t Hop = 30;
  constexpr std::int32_t Walk = 1;
  dwe::assets::ClipMotions clips;
  clips[Hop].steps = {{},
                      {.forward = 40},
                      {.forward = 40},
                      {.forward = 40},
                      {.forward = 40},
                      {.forward = 40},
                      {.side = 10}};
  clips[Walk].steps = {{}, {.forward = 40}};
  clips[Walk].repeatFrom = 0;
  world.registry().emplace<dwe::game::field::CharacterClipMotions>(
      npc, std::make_shared<const dwe::assets::ClipMotions>(std::move(clips)));
  const float yaw = world.registry().get<dwe::scene::Transform>(npc).yaw;
  const glm::vec3 forward = dwe::assets::forwardOfYaw(yaw);
  const glm::vec3 side{forward.z, 0.0F, -forward.x};
  const glm::vec3 home = *field.npcs().positionOf(6);
  field.holdForScript(true);
  field.animateByScript(npc, Hop);
  run(field, 1.0F);
  const glm::vec3 moved = *field.npcs().positionOf(6) - home;
  const glm::vec3 expected = (forward * 200.0F) + (side * 10.0F);
  CHECK(groundDistance(moved, expected) < 0.01F);
  // The clip ended: it stays.
  run(field, 1.0F);
  CHECK(groundDistance(*field.npcs().positionOf(6) - home, expected) < 0.01F);
  // A walk the script drives glides by its velocity alone.
  const glm::vec3 before = *field.npcs().positionOf(6);
  field.driveByScript(npc, glm::vec3{0.0F});
  field.animateByScript(npc, Walk);
  field.driveByScript(npc, forward * 100.0F);
  run(field, 1.0F);
  CHECK(groundDistance(*field.npcs().positionOf(6), before + (forward * 100.0F)) < 0.5F);
  field.holdForScript(false);
}

TEST_CASE("the player and the partner playing a script's clip move by the clip's root motion") {
  // script_interp.c: scriptStartAnimation starts the clip on the player (0xFD) or the partner
  // (0xFC); tamerTickOverworld and partnerTickOverworld run tickAnimation every frame, so the
  // clip's root steps carry them as they carry an NPC (the partner's 40 hops it back 380 units).
  dwe::scene::World world;
  Field field{world, originalRules()};
  field.enterMapAt(roomMap("room"), at(0.0F, 0.0F, 0.0F));
  constexpr std::int32_t Back = 40;
  constexpr std::int32_t Hop = 17;
  dwe::assets::ClipMotions clips;
  clips[Back].steps = {{}, {.forward = -100}, {.forward = -100}, {.forward = -100}, {.side = 20}};
  clips[Hop].steps = {{}, {.forward = -50}, {.forward = -50}};
  dwe::assets::ClipMotions partnerClips = clips;
  field.setPartnerClips(std::move(partnerClips));
  world.registry().emplace<dwe::game::field::CharacterClipMotions>(
      field.playerEntity(), std::make_shared<const dwe::assets::ClipMotions>(clips));
  const auto& registry = world.registry();
  const auto positionOf = [&registry](dwe::scene::Entity entity) {
    return registry.get<dwe::scene::Transform>(entity).position;
  };
  const auto stepOf = [&registry](dwe::scene::Entity entity, float forwardUnits, float sideUnits) {
    const glm::vec3 forward =
        dwe::assets::forwardOfYaw(registry.get<dwe::scene::Transform>(entity).yaw);
    const glm::vec3 side{forward.z, 0.0F, -forward.x};
    return (forward * forwardUnits) + (side * sideUnits);
  };
  field.holdForScript(true);
  const glm::vec3 playerHome = positionOf(field.playerEntity());
  const glm::vec3 partnerHome = positionOf(field.partnerEntity());
  field.animateByScript(field.partnerEntity(), Back);
  field.animateByScript(field.playerEntity(), Hop);
  run(field, 1.0F);
  CHECK(groundDistance(positionOf(field.partnerEntity()) - partnerHome,
                       stepOf(field.partnerEntity(), -300.0F, 20.0F)) < 0.01F);
  CHECK(groundDistance(positionOf(field.playerEntity()) - playerHome,
                       stepOf(field.playerEntity(), -100.0F, 0.0F)) < 0.01F);
  // A clip started again starts from its first frame.
  const glm::vec3 playerBefore = positionOf(field.playerEntity());
  field.animateByScript(field.playerEntity(), Hop);
  run(field, 1.0F);
  CHECK(groundDistance(positionOf(field.playerEntity()) - playerBefore,
                       stepOf(field.playerEntity(), -100.0F, 0.0F)) < 0.01F);
  // A scripted move (tickEntityMoveTo, tickEntityMoveToAxis) adds to the clip's steps, as the
  // original's Etemon falls from its tree in mayo04a, carried 349 units by its clip on the way.
  const glm::vec3 partnerBefore = positionOf(field.partnerEntity());
  field.animateByScript(field.partnerEntity(), Back);
  field.driveByScript(field.partnerEntity(), glm::vec3{0.0F, -50.0F, 0.0F},
                      dwe::game::field::ScriptGlide::AddsToClipMotion);
  run(field, 1.0F);
  const glm::vec3 partnerMoved = positionOf(field.partnerEntity()) - partnerBefore;
  CHECK(groundDistance(partnerMoved, stepOf(field.partnerEntity(), -300.0F, 20.0F)) < 0.01F);
  CHECK(partnerMoved.y == doctest::Approx(-50.0F).epsilon(0.02));
  field.driveByScript(field.partnerEntity(), glm::vec3{0.0F});
  // Let go, the clip no longer carries them.
  field.animateByScript(field.partnerEntity(), Back);
  field.holdForScript(false);
  CHECK_FALSE(registry.all_of<dwe::game::field::ClipRootMotion>(field.partnerEntity()));
  CHECK_FALSE(registry.all_of<dwe::game::field::ClipRootMotion>(field.playerEntity()));
}

TEST_CASE("a clip held in place plays on without carrying its character (animFlag 2)") {
  // anim.c: applyRootMomentum skips the root step while animFlag 2 is set; the gym's courses set
  // it so the partner pushes, punches and runs where it stands. startAnimation resets it.
  dwe::scene::World world;
  Field field{world, originalRules()};
  field.enterMapAt(roomMap("room"), at(0.0F, 0.0F, 0.0F));
  constexpr std::int32_t Push = 27;
  dwe::assets::ClipMotions clips;
  clips[Push].steps = {{}, {.forward = 10}, {.forward = 10}, {.forward = 10}, {.forward = 10}};
  clips[Push].repeatFrom = 1;
  field.setPartnerClips(std::move(clips));
  const auto& registry = world.registry();
  const dwe::scene::Entity partner = field.partnerEntity();
  const auto positionOf = [&registry, partner] {
    return registry.get<dwe::scene::Transform>(partner).position;
  };
  const glm::vec3 forward =
      dwe::assets::forwardOfYaw(registry.get<dwe::scene::Transform>(partner).yaw);
  const float frame = originalRules().transition.frameSeconds;
  field.holdForScript(true);
  field.animateByScript(partner, Push);
  field.holdClipInPlace(partner, true);
  const glm::vec3 home = positionOf();
  run(field, 1.0F);
  CHECK(groundDistance(positionOf(), home) < 0.01F);
  // The clip's frames went on meanwhile.
  CHECK(registry.get<dwe::game::field::ClipRootMotion>(partner).frame > 1);
  // Let go for one frame: one step.
  field.holdClipInPlace(partner, false);
  field.tick(frame);
  field.holdClipInPlace(partner, true);
  run(field, 1.0F);
  CHECK(groundDistance(positionOf(), home + (forward * 10.0F)) < 0.01F);
  // A new clip carries it again.
  field.animateByScript(partner, Push);
  CHECK_FALSE(registry.get<dwe::game::field::ClipRootMotion>(partner).held);
  field.holdForScript(false);
}

TEST_CASE("a care scene's moving clip carries the partner until it stops") {
  // The sleep scene's lie-down (partner.c: partnerTickSleep) moves by the same root steps as a
  // script's clip (Field::moveByClip).
  dwe::scene::World world;
  Field field{world, originalRules()};
  field.enterMapAt(roomMap("room"), at(0.0F, 0.0F, 0.0F));
  constexpr std::int32_t LieDown = 8;
  dwe::assets::ClipMotions clips;
  clips[LieDown].steps = {{}, {.forward = 30}, {.forward = 30}};
  field.setPartnerClips(std::move(clips));
  field.holdForCare(true);
  const auto& registry = world.registry();
  const glm::vec3 home = registry.get<dwe::scene::Transform>(field.partnerEntity()).position;
  const glm::vec3 forward =
      dwe::assets::forwardOfYaw(registry.get<dwe::scene::Transform>(field.partnerEntity()).yaw);
  field.moveByClip(field.partnerEntity(), LieDown);
  run(field, 1.0F);
  const glm::vec3 moved = registry.get<dwe::scene::Transform>(field.partnerEntity()).position;
  CHECK(groundDistance(moved - home, forward * 60.0F) < 0.01F);
  field.moveByClip(field.partnerEntity(), LieDown);
  field.moveByClip(field.partnerEntity(), std::nullopt);
  run(field, 1.0F);
  CHECK(groundDistance(registry.get<dwe::scene::Transform>(field.partnerEntity()).position, moved) <
        0.01F);
  field.holdForCare(false);
}

TEST_CASE("a partner placed by the player keeps off the NPCs, and a map entry checks them too") {
  dwe::scene::World world;
  Field field{world, originalRules()};
  MapDocument map = roomMap("room");
  // Right where the partner appears, spawnDistance behind the player facing +Z.
  const std::size_t slot = addNpc(map, 0.0F, -200.0F, 9);
  field.enterMapAt(map, at(0.0F, 0.0F, 0.0F));
  REQUIRE(spawnNpc(field, slot));
  placeAt(world, field.partnerEntity(), -1500.0F, -1500.0F);
  // Too far: it is placed by the player, not on the NPC.
  run(field, 0.1F);
  CHECK(field.partnerTeleports() >= 1);
  CHECK_FALSE(Field::overlaps(squareAt(field.partner().position, 120.0F),
                              squareAt(*field.npcs().positionOf(9), 100.0F)));
}

TEST_CASE("a care scene or a script let go with the partner on the player sets them apart") {
  dwe::scene::World world;
  Field field{world, originalRules()};
  field.enterMapAt(roomMap("room"), at(0.0F, 0.0F, 0.0F));
  run(field, 0.2F);
  const auto apart = [&] {
    return !Field::overlaps(squareAt(field.player().position, 100.0F),
                            squareAt(field.partner().position, 120.0F));
  };
  SUBCASE("care: at once") {
    field.holdForCare(true);
    placeAt(world, field.partnerEntity(), 5.0F, 5.0F);
    field.tick(Tick);
    field.holdForCare(false);
    field.tick(Tick);
    CHECK(apart());
  }
  SUBCASE("a script: the partner walks off first, and is never snapped if that is enough") {
    field.holdForScript(true);
    placeAt(world, field.partnerEntity(), 60.0F, 0.0F);
    field.tick(Tick);
    field.holdForScript(false);
    run(field, 2.0F);
    CHECK(apart());
    CHECK(field.partner().position.z == doctest::Approx(0.0F));
  }
  SUBCASE("a script, the partner left in an NPC away from the player: it is set apart") {
    MapDocument map = roomMap("room");
    const std::size_t slot = addNpc(map, 620.0F, 0.0F, 4);
    field.enterMapAt(map, at(0.0F, 0.0F, 0.0F));
    REQUIRE(spawnNpc(field, slot));
    field.holdForScript(true);
    placeAt(world, field.partnerEntity(), 600.0F, 0.0F);
    field.tick(Tick);
    field.holdForScript(false);
    field.tick(Tick);
    field.tick(Tick);
    CHECK_FALSE(Field::overlaps(squareAt(field.partner().position, 120.0F),
                                squareAt(*field.npcs().positionOf(4), 100.0F)));
  }
  run(field, 2.0F);
  CHECK(field.overlapEpisodes() == 0);
}

TEST_CASE("the overlap watch logs a player standing on the partner once per episode") {
  dwe::scene::World world;
  Field field{world, originalRules()};
  field.enterMapAt(roomMap("room"), at(0.0F, 0.0F, 0.0F));
  run(field, 0.2F);
  placeAt(world, field.partnerEntity(), 10.0F, 0.0F);
  run(field, 0.5F);
  CHECK(field.overlapEpisodes() == 0);
  run(field, 1.0F);
  CHECK(field.overlapEpisodes() == 1);
  run(field, 1.0F);
  CHECK(field.overlapEpisodes() == 1);
}

TEST_CASE("a script's jump is drawn at once, its small moves over the tick") {
  dwe::scene::World world;
  Field field{world, originalRules()};
  MapDocument map = roomMap("room");
  const std::size_t slot = addNpc(map, 600.0F, 0.0F, 3);
  field.enterMapAt(map, at(0.0F, 0.0F, 0.0F));
  REQUIRE(spawnNpc(field, slot));
  field.holdForScript(true);
  field.tick(Tick);
  const dwe::scene::Entity npc = *field.npcs().entityOf(3);
  const auto& transform = world.registry().get<dwe::scene::Transform>(npc);
  field.placeByScript(npc, {610.0F, 0.0F, 0.0F});
  CHECK(transform.previousPosition.x == doctest::Approx(600.0F));
  field.tick(Tick);
  field.placeByScript(npc, {-600.0F, 0.0F, 0.0F});
  CHECK(transform.previousPosition == transform.position);
  field.turnByScript(npc, 0.01F);
  CHECK(transform.previousYaw != transform.yaw);
  field.tick(Tick);
  field.turnByScript(npc, Pi);
  CHECK(transform.previousYaw == transform.yaw);
  // The player turns to the partner at once.
  field.playerFacePartner();
  const auto& player = world.registry().get<dwe::scene::Transform>(field.playerEntity());
  CHECK(player.previousYaw == player.yaw);
}

TEST_CASE("settling, or a tick the field holds still, draws every character where it stands") {
  dwe::scene::World world;
  Field field{world, originalRules()};
  MapDocument map = roomMap("room");
  const std::size_t slot = addNpc(map, 600.0F, 0.0F, 3);
  field.enterMapAt(map, at(0.0F, 0.0F, 0.0F));
  REQUIRE(spawnNpc(field, slot));
  auto& npc = world.registry().get<dwe::scene::Transform>(*field.npcs().entityOf(3));
  auto& partner = world.registry().get<dwe::scene::Transform>(field.partnerEntity());
  // Something outside the field moved them during an earlier tick.
  npc.position.x += 30.0F;
  partner.position.x += 30.0F;
  field.settle();
  CHECK(npc.previousPosition == npc.position);
  CHECK(partner.previousPosition == partner.position);
  // While a map change waits for its map, the NPCs' ticks begin too.
  field.startScriptedMapChange({.map = "other", .exit = 0, .trigger = 0}, false);
  run(field, 1.0F);
  npc.position.x += 30.0F;
  field.tick(Tick);
  CHECK(field.mapChangePending());
  CHECK(npc.previousPosition == npc.position);
}

TEST_CASE(
    "the partner's size follows its species: follow distances, and a release check if bigger") {
  dwe::scene::World world;
  Field field{world, originalRules()};
  field.enterMapAt(roomMap("room"), at(0.0F, 0.0F, 0.0F));
  // Standing still 260 away: within 2.5 radii of 120 (300).
  placeAt(world, field.partnerEntity(), 260.0F, 0.0F);
  run(field, 0.5F);
  CHECK(field.partner().position.x == doctest::Approx(260.0F));
  // A small species (radius 90) walks up to under 225; zero keeps the size.
  field.setPartnerRadius(0.0F);
  CHECK(field.rules().partner.mover.radius == doctest::Approx(120.0F));
  field.setPartnerRadius(90.0F);
  run(field, 1.0F);
  CHECK(partnerDistance(field) < 225.0F);
  // A big one (radius 300) standing there now stands in the player: it is set apart.
  field.setPartnerRadius(300.0F);
  field.tick(Tick);
  CHECK_FALSE(Field::overlaps(squareAt(field.player().position, 100.0F),
                              squareAt(field.partner().position, 300.0F)));
}

TEST_CASE("a script let go takes its glide and its finished clips off the NPCs") {
  dwe::scene::World world;
  Field field{world, originalRules()};
  MapDocument map = roomMap("room");
  const std::size_t walker = addNpc(map, 600.0F, 0.0F, 3);
  const std::size_t posed = addNpc(map, -600.0F, 0.0F, 5);
  field.enterMapAt(map, at(0.0F, 0.0F, 0.0F));
  REQUIRE(spawnNpc(field, walker));
  REQUIRE(spawnNpc(field, posed));
  const dwe::scene::Entity walking = *field.npcs().entityOf(3);
  const dwe::scene::Entity posing = *field.npcs().entityOf(5);
  field.holdForScript(true);
  field.driveByScript(walking, {100.0F, 0.0F, 0.0F});
  field.animateByScript(walking, 2);
  REQUIRE(field.npcs().setScriptedAnimation(5, 7));
  field.animateByScript(posing, 7);
  field.driveByScript(posing, {0.0F, 0.0F, 50.0F});
  field.tick(Tick);
  field.holdForScript(false);
  auto& registry = world.registry();
  CHECK_FALSE(registry.all_of<dwe::game::field::ScriptedActor>(walking));
  REQUIRE(registry.all_of<dwe::game::field::ScriptedActor>(posing));
  CHECK(registry.get<dwe::game::field::ScriptedActor>(posing).clip == 7U);
  CHECK(registry.get<dwe::game::field::ScriptedActor>(posing).velocity == glm::vec3{0.0F});
  // The next hold does not glide them with an old velocity.
  const glm::vec3 before = *field.npcs().positionOf(5);
  field.holdForScript(true);
  run(field, 0.5F);
  CHECK(*field.npcs().positionOf(5) == before);
}
