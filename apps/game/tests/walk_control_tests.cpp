// What walks the player: the keyboard and the pad, or the mouse, never both
// (docs/modulos/apps/game.md, «Caminar con teclado o ratón»).

#include <dwe/game/field/field.hpp>
#include <dwe/scene/world.hpp>

#include <doctest/doctest.h>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <optional>
#include <string>

#include "packages/game/field/tests/fixture.hpp"
#include "walk_control.hpp"

namespace dwe::app {
namespace {

const WalkDevices Everything{.keys = {0.0F, 1.0F},
                             .clicked = glm::vec3{300.0F, 0.0F, 0.0F},
                             .steered = glm::vec2{1.0F, 0.0F}};

// A field with the player standing in the middle of an empty room.
struct Room {
  scene::World world;
  game::field::Field field{world, game::field::testing::originalRules()};

  Room() {
    field.enterMapAt(game::field::testing::roomMap("room"),
                     {.position = {0.0F, 0.0F, 0.0F}, .yaw = 0.0F});
  }
};

} // namespace

TEST_CASE("with the keys only the keyboard and the pad walk the player, and a click never does") {
  const WalkOrder walk = walkOrderOf(WalkControl::Keys, Everything);
  CHECK(walk.stick == glm::vec2{0.0F, 1.0F});
  CHECK_FALSE(walk.moveTarget.has_value());
  // A held button does not steer either.
  CHECK(walkOrderOf(WalkControl::Keys, {.steered = glm::vec2{1.0F, 0.0F}}).stick ==
        glm::vec2{0.0F});
}

TEST_CASE("with the mouse only the clicks and the held button walk the player, not the keys") {
  const WalkOrder walk = walkOrderOf(WalkControl::Mouse, Everything);
  CHECK(walk.stick == glm::vec2{1.0F, 0.0F});
  REQUIRE(walk.moveTarget.has_value());
  CHECK(*walk.moveTarget == glm::vec3{300.0F, 0.0F, 0.0F});
  CHECK(walkOrderOf(WalkControl::Mouse, {.keys = {0.0F, 1.0F}}).stick == glm::vec2{0.0F});
}

TEST_CASE("the walk control starts on the keys unless the mouse was saved") {
  CHECK(startWalkControl(std::nullopt) == WalkControl::Keys);
  CHECK(startWalkControl(std::string{"mouse"}) == WalkControl::Mouse);
  CHECK(startWalkControl(std::string{"keys"}) == WalkControl::Keys);
  CHECK(startWalkControl(std::string{"both"}) == WalkControl::Keys);
  CHECK(walkControlName(WalkControl::Mouse) == "mouse");
  CHECK(walkControlName(WalkControl::Keys) == "keys");
}

TEST_CASE("switching the walk control drops the walk target the player had, both ways") {
  UserSettings settings;
  WalkControlChanger changer{settings, std::nullopt};
  REQUIRE(changer.control() == WalkControl::Keys);
  Room room;
  const glm::vec3 target{300.0F, 0.0F, 0.0F};

  room.field.setInput({.moveTarget = target});
  REQUIRE(room.field.hasMoveTarget());
  CHECK(changer.change(ui::UiValue{std::string{"mouse"}}, &room.field) == WalkControl::Mouse);
  CHECK_FALSE(room.field.hasMoveTarget());
  CHECK(settings.walkControl == std::optional<std::string>{"mouse"});

  room.field.setInput({.moveTarget = target});
  REQUIRE(room.field.hasMoveTarget());
  CHECK(changer.change(ui::UiValue{std::string{"keys"}}, &room.field) == WalkControl::Keys);
  CHECK_FALSE(room.field.hasMoveTarget());
  CHECK(settings.walkControl == std::optional<std::string>{"keys"});

  // A value that is not a walk control changes nothing, and with no game running there is no
  // field to drop a target from.
  CHECK(changer.change(ui::UiValue{true}, nullptr) == WalkControl::Keys);
  CHECK(changer.change(ui::UiValue{std::string{"both"}}, nullptr) == WalkControl::Keys);
  CHECK(changer.change(ui::UiValue{std::string{"mouse"}}, nullptr) == WalkControl::Mouse);
}

} // namespace dwe::app
