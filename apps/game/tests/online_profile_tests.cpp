#include <dwe/net/signal.hpp>

#include <doctest/doctest.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
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
  const auto room = [](std::string code, std::string name, int players, std::string phase) {
    return dwe::net::PublicRoom{.roomId = "r-" + code,
                                .joinCode = code,
                                .name = std::move(name),
                                .message = {},
                                .players = players,
                                .capacity = 2,
                                .createdAt = 10.0,
                                .state = players < 2 ? "waiting" : "full",
                                .phase = std::move(phase),
                                .hostName = {},
                                .icon = {},
                                .compat = {},
                                .mode = {}};
  };
  std::vector<dwe::net::PublicRoom> rooms{
      room("AB23", "Cooperative room", 1, "open"), room("CD45", "Sala de Ana", 2, {}),
      room("EF67", {}, 2, "connected"), room("GH89", {}, 2, "playing")};
  rooms[0].hostName = "Ana";
  rooms[0].mode = "versus";
  // A first-protocol server's room: its state says it all.
  rooms[1].state = "waiting";
  const std::vector<RoomListing> listed = SignalRoomDirectory::listings(rooms);
  // A room already playing is not listed.
  REQUIRE(listed.size() == 3);
  // The server's generic name gives way to the screens' own words.
  CHECK(listed[0].name.empty());
  CHECK(listed[0].nameKey == "online.untitled_room");
  CHECK(listed[0].hostName == "Ana");
  CHECK(listed[0].mode == "versus");
  CHECK(listed[0].pingMs == -1);
  CHECK(listed[0].state == ListingState::Waiting);
  CHECK(listed[1].name == "Sala de Ana");
  CHECK(listed[1].nameKey.empty());
  CHECK(listed[1].mode == "campaign");
  // A room with every seat taken is full, whatever state the server gave it.
  CHECK(listed[1].state == ListingState::Full);
  // Both players in: full.
  CHECK(listed[2].state == ListingState::Full);
}

namespace {

// A server that answers each URL with one message (or never, for a URL it does not know).
class AnsweringSocket final : public dwe::net::SignalSocket {
public:
  explicit AnsweringSocket(std::optional<std::string> answer) : answer_{std::move(answer)} {}
  void send(std::string /*text*/) override {}
  std::optional<std::string> receive() override { return std::exchange(answer_, std::nullopt); }
  [[nodiscard]] dwe::net::SocketState state() const override {
    return answer_ ? dwe::net::SocketState::Open : dwe::net::SocketState::Closed;
  }
  void close() override { answer_.reset(); }

private:
  std::optional<std::string> answer_;
};

struct AnsweringServer {
  std::map<std::string, std::string> answers;
  std::vector<std::string> asked;

  dwe::net::SocketOpener opener() {
    return [this](const std::string& url) -> std::unique_ptr<dwe::net::SignalSocket> {
      asked.push_back(url);
      const auto found = answers.find(url);
      return std::make_unique<AnsweringSocket>(
          found != answers.end() ? std::optional<std::string>{found->second} : std::nullopt);
    };
  }
};

void settle(SignalRoomDirectory& directory) {
  for (int tick = 0; tick < 4; ++tick) {
    directory.tick(0.0);
  }
}

} // namespace

TEST_CASE("the public list asks what the server can do first, then its own game's rooms") {
  AnsweringServer server;
  server.answers["wss://s/v1/info"] = R"({"protocol":"v1","features":["public-rooms"]})";
  server.answers["wss://s/v1/rooms?compat=coop-1-ab"] =
      R"({"rooms":[{"roomId":"r","joinCode":"AB23","players":1,"capacity":2,"phase":"open",)"
      R"("hostName":"Ana","state":"waiting"}]})";
  SignalRoomDirectory directory{"wss://s", server.opener(), "coop-1-ab"};
  directory.refresh(0.0);
  CHECK(directory.state() == DirectoryState::Loading);
  settle(directory);
  REQUIRE(directory.state() == DirectoryState::Ready);
  REQUIRE(directory.rooms().size() == 1);
  CHECK(directory.rooms()[0].hostName == "Ana");
  // The server's features are asked once.
  directory.refresh(1.0);
  settle(directory);
  CHECK(server.asked == std::vector<std::string>{"wss://s/v1/info",
                                                 "wss://s/v1/rooms?compat=coop-1-ab",
                                                 "wss://s/v1/rooms?compat=coop-1-ab"});
}

TEST_CASE("the public list says when the server has none, or was asked too often") {
  AnsweringServer server;
  server.answers["wss://s/v1/info"] = R"({"protocol":"v1","features":[]})";
  SignalRoomDirectory none{"wss://s", server.opener()};
  none.refresh(0.0);
  settle(none);
  CHECK(none.state() == DirectoryState::Unavailable);
  CHECK(none.error() == "directory_unavailable");

  server.answers["wss://s/v1/info"] = R"({"protocol":"v1","features":["public-rooms"]})";
  server.answers["wss://s/v1/rooms"] = R"({"type":"error","reason":"limit"})";
  SignalRoomDirectory busy{"wss://s", server.opener()};
  busy.refresh(0.0);
  settle(busy);
  CHECK(busy.state() == DirectoryState::Unavailable);
  CHECK(busy.error() == "list_limit");

  // A server that does not answer /v1/info is asked for its list all the same.
  AnsweringServer older;
  older.answers["wss://s/v1/rooms"] = R"({"rooms":[]})";
  SignalRoomDirectory first{"wss://s", older.opener()};
  first.refresh(0.0);
  settle(first);
  CHECK(first.state() == DirectoryState::Ready);
  CHECK(first.rooms().empty());
}

TEST_CASE("without a signaling server the public list says so") {
  SignalRoomDirectory directory{{}, {}};
  directory.refresh(0.0);
  CHECK(directory.state() == DirectoryState::Unavailable);
  CHECK(directory.error() == "no_server");
}

TEST_CASE("relay only is part of the online profile and its settings") {
  UserSettings settings;
  OnlineProfile profile = OnlineProfile::fromSettings(settings, "en");
  CHECK_FALSE(profile.relayOnly());
  // Never chosen: the settings keep not naming it.
  profile.store(settings);
  CHECK_FALSE(settings.onlineRelayOnly.has_value());
  profile.setRelayOnly(true);
  profile.store(settings);
  CHECK(settings.onlineRelayOnly == true);
  const auto written = formatUserSettings(settings);
  REQUIRE(written);
  CHECK(written->find("online_relay_only: true") != std::string::npos);
  const auto read = parseUserSettings(*written);
  REQUIRE(read);
  CHECK(OnlineProfile::fromSettings(*read, "en").relayOnly());
  profile.setRelayOnly(false);
  profile.store(settings);
  CHECK(settings.onlineRelayOnly == false);
}
