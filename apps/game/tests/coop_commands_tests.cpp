#include <doctest/doctest.h>

#include <limits>

#include "coop_commands.hpp"

using namespace dwe::app;

TEST_CASE("application field commands map to and from the quantized coop protocol") {
  PlayerCommands local;
  local.sequence = 42;
  local.field.stick = {-0.5F, 1.0F};
  local.field.run = true;
  local.field.cancelTarget = true;
  local.field.confirm = true;
  local.field.moveTarget = glm::vec3{123.4F, 5.0F, -87.6F};

  const auto encoded = toCoopCommand(local);
  REQUIRE(encoded);
  CHECK(encoded->sequence == 42);
  CHECK(encoded->field.stickX == -16384);
  CHECK(encoded->field.stickY == 32767);
  CHECK(encoded->field.run);
  CHECK(encoded->field.cancelTarget);
  CHECK(encoded->field.confirm);
  REQUIRE(encoded->field.moveTarget);
  CHECK(encoded->field.moveTarget->first == 123);
  CHECK(encoded->field.moveTarget->second == -88);

  const PlayerCommands restored = fromCoopCommand(*encoded);
  CHECK(restored.sequence == local.sequence);
  CHECK(restored.field.stick.x == doctest::Approx(-0.5F).epsilon(0.0001));
  CHECK(restored.field.stick.y == 1.0F);
  CHECK(restored.field.run == local.field.run);
  CHECK(restored.field.cancelTarget == local.field.cancelTarget);
  CHECK(restored.field.confirm == local.field.confirm);
  REQUIRE(restored.field.moveTarget);
  CHECK(restored.field.moveTarget->x == 123.0F);
  CHECK(restored.field.moveTarget->y == 0.0F);
  CHECK(restored.field.moveTarget->z == -88.0F);
}

TEST_CASE("application refuses invalid network field values") {
  PlayerCommands commands;
  commands.field.stick.x = std::numeric_limits<float>::infinity();
  CHECK_FALSE(toCoopCommand(commands));

  commands.field.stick = {};
  commands.field.moveTarget = glm::vec3{2'000'000.0F, 0.0F, 0.0F};
  CHECK_FALSE(toCoopCommand(commands));
}

TEST_CASE("network field controller uses only fresh commands") {
  NetworkFieldController controller;
  const dwe::game::coop::Commands received{
      .sequence = 7, .field = {.stickX = 32767, .run = true, .moveTarget = std::nullopt}};
  CHECK(controller.receive(received, dwe::net::Millis{100}));
  CHECK_FALSE(controller.receive(received, dwe::net::Millis{110}));

  controller.setNow(dwe::net::Millis{200});
  const PlayerCommands fresh = controller.next(0);
  CHECK(fresh.sequence == 7);
  CHECK(fresh.field.stick.x == 1.0F);
  CHECK(fresh.field.run);

  controller.setNow(dwe::net::Millis{351});
  const PlayerCommands expired = controller.next(1);
  CHECK(expired.field.stick == glm::vec2{});
  CHECK_FALSE(expired.field.run);
  CHECK_FALSE(expired.field.cancelTarget);
  CHECK_FALSE(expired.field.confirm);
  CHECK_FALSE(expired.field.moveTarget.has_value());
}
