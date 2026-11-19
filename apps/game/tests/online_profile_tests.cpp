#include <dwe/net/signal.hpp>

#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "online_profile.hpp"
#include "online_signal_directory.hpp"
#include "user_settings.hpp"

using namespace dwe::app;

TEST_CASE("a typed online name is kept tidy and cut to the longest a name may be") {
  CHECK(OnlineProfile::cleanName("  Ana   la buena  ") == "Ana la buena");
  CHECK(OnlineProfile::cleanName("A\tna") == "Ana");
  CHECK(OnlineProfile::cleanName("\x01\x02") == "");
  CHECK(OnlineProfile::cleanName("   ") == "");
  // Letters, not bytes: twelve accented letters fit, the thirteenth does not.
  CHECK(OnlineProfile::cleanName("ñññññññññññññ") == "ññññññññññññ");
  CHECK(OnlineProfile::cleanName("Supercalifragilistico") == "Supercalifra");
}

TEST_CASE("the online profile lives in the game's settings, with defaults where they keep none") {
  UserSettings settings;
  OnlineProfile fresh = OnlineProfile::fromSettings(settings, "es");
  CHECK(fresh.name() == "Jugador");
  CHECK_FALSE(fresh.named());
  CHECK(fresh.icon() == "egg");
  CHECK(OnlineProfile::fromSettings(settings, "en").name() == "Player");

  CHECK_FALSE(fresh.setName(" "));
  CHECK(fresh.name() == "Jugador");
  CHECK(fresh.setName(" Ana "));
  CHECK_FALSE(fresh.setIcon("no-such-icon"));
  CHECK(fresh.setIcon("crown"));
  fresh.recordVersus(true);
  fresh.recordVersus(false);
  fresh.recordVersus(true);
  fresh.store(settings);

  const OnlineProfile read = OnlineProfile::fromSettings(settings, "en");
  CHECK(read.name() == "Ana");
  CHECK(read.named());
  CHECK(read.icon() == "crown");
  CHECK(read.wins() == 2);
  CHECK(read.losses() == 1);

  // A settings file edited by hand with an unknown icon falls back to the first one.
  settings.onlineIcon = "dragon";
  CHECK(OnlineProfile::fromSettings(settings, "en").icon() == "egg");
}

TEST_CASE("the signaling server's public rooms become the rooms screens' list") {
  const std::vector<dwe::net::PublicRoom> rooms{{.roomId = "r1",
                                                 .joinCode = "AB23",
                                                 .name = "Cooperative room",
                                                 .message = "Waiting for players",
                                                 .players = 1,
                                                 .capacity = 2,
                                                 .createdAt = 10.0,
                                                 .state = "waiting"},
                                                {.roomId = "r2",
                                                 .joinCode = "CD45",
                                                 .name = "Sala de Ana",
                                                 .message = {},
                                                 .players = 2,
                                                 .capacity = 2,
                                                 .createdAt = 11.0,
                                                 .state = "waiting"}};
  const std::vector<RoomListing> listed = SignalRoomDirectory::listings(rooms);
  REQUIRE(listed.size() == 2);
  // The server's generic name gives way to the screens' own words.
  CHECK(listed[0].name.empty());
  CHECK(listed[0].nameKey == "online.untitled_room");
  CHECK(listed[0].mode == "campaign");
  CHECK(listed[0].pingMs == -1);
  CHECK(listed[0].state == ListingState::Waiting);
  CHECK(listed[1].name == "Sala de Ana");
  CHECK(listed[1].nameKey.empty());
  // A room with every seat taken is full, whatever state the server gave it.
  CHECK(listed[1].state == ListingState::Full);
}

TEST_CASE("without a signaling server the public list says so") {
  SignalRoomDirectory directory{{}, {}};
  directory.refresh(0.0);
  CHECK(directory.state() == DirectoryState::Unavailable);
  CHECK(directory.error() == "no_server");
}
