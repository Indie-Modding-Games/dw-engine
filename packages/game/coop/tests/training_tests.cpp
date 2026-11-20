#include <dwe/game/coop/link.hpp>
#include <dwe/game/coop/protocol.hpp>
#include <dwe/net/loopback.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <string>

// docs/architecture/coop-training.md: the world's clock and who trains, over the wire.
using namespace dwe::game::coop;

TEST_CASE("coop training: the world clock and the training status round trip, damaged ones "
          "are refused") {
  const auto roundTrip = [](const Message& message) {
    const auto decoded = decode(encode(message));
    REQUIRE(decoded);
    REQUIRE(decoded->has_value());
    CHECK(**decoded == message);
  };
  roundTrip(WorldClock{.elapsedFrames = 9'600, .running = true, .speedThousandths = 1000});
  roundTrip(
      WorldClock{.elapsedFrames = 86'400'000LL * 30, .running = false, .speedThousandths = 0});
  roundTrip(TrainingStatus{.player = 2, .course = "trn_hp"});
  roundTrip(TrainingStatus{.player = HostPlayerId, .course = ""});

  CHECK_FALSE(decode(encode(WorldClock{.elapsedFrames = -1})));
  CHECK_FALSE(decode(encode(TrainingStatus{.player = MaxPlayers, .course = "trn_hp"})));
  CHECK_FALSE(
      decode(encode(TrainingStatus{.player = 1, .course = std::string(MaxCourseText + 1, 'a')})));
  // A clock cut short is damaged.
  auto bytes = encode(WorldClock{.elapsedFrames = 9'600, .running = true});
  bytes.pop_back();
  CHECK_FALSE(decode(bytes));
}

TEST_CASE("coop training: the clock goes only from the host, a training status both ways, over a "
          "real link") {
  // link.cpp (directionAllowed): a kind with no rule is refused by a live game.
  dwe::net::LoopbackNetwork network({});
  const Identity identity{.protocol = ProtocolVersion, .gameVersion = "1", .contentHash = "h"};
  CoopLink host = CoopLink::host(network.link(0), identity);
  CoopLink guest = CoopLink::guest(network.link(1), identity, "Guest");
  const auto pump = [&] {
    for (int i = 0; i < 8; ++i) {
      network.advance(dwe::net::Millis{1});
      host.poll(network.now());
      guest.poll(network.now());
    }
  };
  pump();
  REQUIRE(guest.phase() == LinkPhase::Ready);
  while (host.receive() || guest.receive()) {
  }
  const Message clock{WorldClock{.elapsedFrames = 12'000, .running = true}};
  CHECK_FALSE(guest.send(clock));
  CHECK(host.send(clock));
  const std::array<Message, 2> statuses{Message{TrainingStatus{.player = 0, .course = "trn_mp"}},
                                        Message{TrainingStatus{.player = 2, .course = ""}}};
  for (const Message& status : statuses) {
    CHECK(host.send(status));
  }
  const Message own{TrainingStatus{.player = GuestPlayerId, .course = "trn_speed"}};
  CHECK(guest.send(own));
  pump();
  std::size_t atGuest = 0;
  while (auto message = guest.receive()) {
    if (atGuest == 0) {
      CHECK(*message == clock);
    } else {
      CHECK(*message == statuses.at(atGuest - 1));
    }
    ++atGuest;
  }
  CHECK(atGuest == 3);
  const auto atHost = host.receive();
  REQUIRE(atHost);
  CHECK(*atHost == own);
}
