#include <doctest/doctest.h>

#include <limits>

#include "coop_snapshot.hpp"

using namespace dwe::app;

TEST_CASE("field entities quantize to stable cooperative snapshot values") {
  const auto encoded = toCoopEntityState(
      7, dwe::game::coop::GuestPlayerId, dwe::game::coop::EntityRole::Player,
      {.position = {12.4F, -1.6F, 98.7F}, .yaw = -0.25F, .motion = dwe::scene::Motion::Run});
  REQUIRE(encoded);
  CHECK(encoded->networkId == 7);
  CHECK(encoded->owner == dwe::game::coop::GuestPlayerId);
  CHECK(encoded->role == dwe::game::coop::EntityRole::Player);
  CHECK(encoded->x == 12);
  CHECK(encoded->y == -2);
  CHECK(encoded->z == 99);
  CHECK(encoded->yaw > 0);
  CHECK(encoded->motion == dwe::game::coop::Motion::Run);
}

TEST_CASE("field snapshot quantizer rejects invalid transforms") {
  CHECK_FALSE(
      toCoopEntityState(0, dwe::game::coop::HostPlayerId, dwe::game::coop::EntityRole::Player, {}));
  CHECK_FALSE(toCoopEntityState(1, 9, dwe::game::coop::EntityRole::Player, {}));
  CHECK_FALSE(
      toCoopEntityState(1, dwe::game::coop::HostPlayerId, dwe::game::coop::EntityRole::Player,
                        {.position = {std::numeric_limits<float>::infinity(), 0.0F, 0.0F}}));
}
