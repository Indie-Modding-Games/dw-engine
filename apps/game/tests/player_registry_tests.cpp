#include <doctest/doctest.h>

#include "player_registry.hpp"

using namespace dwe::app;

TEST_CASE("the player registry starts with one local host controller") {
  PlayerRegistry players{"A"};

  REQUIRE(players.size() == 1);
  const PlayerSlot* host = players.find(HostPlayerId);
  REQUIRE(host != nullptr);
  CHECK(host->name == "A");
  CHECK(host->local);
  CHECK(host->link == PlayerLink::Local);
  CHECK(players.find(GuestPlayerId) == nullptr);
  CHECK(players.next(HostPlayerId, 0)->sequence == 0);
  CHECK_FALSE(players.next(GuestPlayerId, 0).has_value());
}

TEST_CASE("the guest slot accepts one connected command source") {
  PlayerRegistry players;
  PlayerCommands scripted;
  scripted.field.stick = {0.0F, 1.0F};
  auto controller = std::make_unique<ScriptedController>(std::vector{scripted});

  CHECK(players.addGuest("B", std::move(controller), PlayerLink::Connected));
  CHECK(players.size() == 2);
  const PlayerSlot* guest = players.find(GuestPlayerId);
  REQUIRE(guest != nullptr);
  CHECK(guest->name == "B");
  CHECK_FALSE(guest->local);
  CHECK(guest->link == PlayerLink::Connected);
  const std::optional<PlayerCommands> command = players.next(GuestPlayerId, 1);
  REQUIRE(command.has_value());
  CHECK(command->sequence == 0);
  CHECK(command->field.stick == glm::vec2{0.0F, 1.0F});

  CHECK_FALSE(players.addGuest(
      "duplicate", std::make_unique<ScriptedController>(std::vector<PlayerCommands>{})));
  CHECK_FALSE(
      players.addGuest("", std::make_unique<ScriptedController>(std::vector<PlayerCommands>{})));
  CHECK_FALSE(players.addGuest("local",
                               std::make_unique<ScriptedController>(std::vector<PlayerCommands>{}),
                               PlayerLink::Local));
  CHECK(players.size() == 2);
}

TEST_CASE("joining and dropped guest slots do not advance their controller") {
  PlayerRegistry players;
  CHECK(players.addGuest("B", std::make_unique<ScriptedController>(std::vector<PlayerCommands>(1)),
                         PlayerLink::Joining));
  CHECK_FALSE(players.next(GuestPlayerId, 0).has_value());

  players.setGuestLink(PlayerLink::Connected);
  REQUIRE(players.next(GuestPlayerId, 1).has_value());
  players.setGuestLink(PlayerLink::Dropped);
  CHECK_FALSE(players.next(GuestPlayerId, 2).has_value());
}
