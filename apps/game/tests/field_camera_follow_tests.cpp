#include <doctest/doctest.h>
#include <glm/vec2.hpp>

#include <optional>

#include "field_camera_follow.hpp"

using dwe::app::FieldCameraFollow;

TEST_CASE("the field's camera starts on the player and follows its movement") {
  FieldCameraFollow follow;
  CHECK(follow.update({200.0F, 300.0F}, true, std::nullopt) == glm::vec2{200.0F, 300.0F});
  CHECK(follow.update({210.0F, 295.0F}, true, std::nullopt) == glm::vec2{210.0F, 295.0F});
}

TEST_CASE("after a battle the camera follows from where it stayed, not on the player") {
  // map.c: tickCameraFollowPlayer adds the player's projected movement to cameraX/cameraY; a
  // battle (btl.c: startBattle, unsetCameraFollowPlayer) leaves the camera put while the player
  // runs back, and nothing centres it again.
  FieldCameraFollow follow;
  CHECK(follow.update({400.0F, 400.0F}, true, std::nullopt) == glm::vec2{400.0F, 400.0F});
  // The battle: the player runs 60 pixels down and 30 left; the camera stays.
  CHECK(follow.update({370.0F, 460.0F}, false, std::nullopt) == glm::vec2{400.0F, 400.0F});
  CHECK(follow.update({370.0F, 460.0F}, false, std::nullopt) == glm::vec2{400.0F, 400.0F});
  // Over: the first frame keeps the camera, then it moves with the player, the offset kept.
  CHECK(follow.update({370.0F, 460.0F}, true, std::nullopt) == glm::vec2{400.0F, 400.0F});
  CHECK(follow.update({380.0F, 470.0F}, true, std::nullopt) == glm::vec2{410.0F, 410.0F});
  CHECK(follow.update({380.0F, 470.0F}, true, std::nullopt) == glm::vec2{410.0F, 410.0F});
}

TEST_CASE("a script's camera wins and the field follows on from where it left it") {
  FieldCameraFollow follow;
  static_cast<void>(follow.update({100.0F, 100.0F}, true, std::nullopt));
  CHECK(follow.update({100.0F, 100.0F}, false, glm::vec2{250.0F, 90.0F}) ==
        glm::vec2{250.0F, 90.0F});
  CHECK(follow.update({120.0F, 100.0F}, true, std::nullopt) == glm::vec2{270.0F, 90.0F});
}

TEST_CASE("a new map centres the camera on the player again") {
  FieldCameraFollow follow;
  static_cast<void>(follow.update({100.0F, 100.0F}, true, std::nullopt));
  static_cast<void>(follow.update({160.0F, 100.0F}, false, std::nullopt));
  follow.reset();
  CHECK_FALSE(follow.focus().has_value());
  CHECK(follow.update({500.0F, 20.0F}, true, std::nullopt) == glm::vec2{500.0F, 20.0F});
}
