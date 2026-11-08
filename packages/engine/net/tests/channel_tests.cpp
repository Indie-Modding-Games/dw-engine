#include <dwe/net/loopback.hpp>
#include <dwe/net/reliable_channel.hpp>
#include <dwe/net/wire.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

using namespace dwe::net;

namespace {

constexpr Millis Step{10};

Bytes numbered(std::uint32_t number) {
  WireWriter out;
  out.put(number);
  return out.take();
}

std::uint32_t numberOf(const Bytes& message) {
  WireReader in{message};
  return in.get<std::uint32_t>();
}

// Sends `count` numbered messages each way through the conditions and returns, per side, the
// numbers received in the order received.
std::array<std::vector<std::uint32_t>, 2> exchange(LinkConditions conditions, std::uint32_t count,
                                                   std::uint32_t seed = 1) {
  LoopbackNetwork network{conditions, seed};
  ReliableChannel one{network.peer(0)};
  ReliableChannel two{network.peer(1)};
  std::array<std::vector<std::uint32_t>, 2> received;
  for (std::uint32_t number = 0; number < count; ++number) {
    REQUIRE(one.send(numbered(number)));
    REQUIRE(two.send(numbered(number)));
  }
  for (int round = 0; round < 3000 && (received[0].size() < count || received[1].size() < count);
       ++round) {
    one.poll(network.now());
    two.poll(network.now());
    while (auto message = one.receive()) {
      received[0].push_back(numberOf(*message));
    }
    while (auto message = two.receive()) {
      received[1].push_back(numberOf(*message));
    }
    network.advance(Step);
  }
  return received;
}

std::vector<std::uint32_t> upTo(std::uint32_t count) {
  std::vector<std::uint32_t> numbers;
  for (std::uint32_t number = 0; number < count; ++number) {
    numbers.push_back(number);
  }
  return numbers;
}

} // namespace

TEST_CASE("the reliable channel delivers every message once and in order over a clean link") {
  const auto received = exchange({}, 50);
  CHECK(received[0] == upTo(50));
  CHECK(received[1] == upTo(50));
}

TEST_CASE("the reliable channel survives latency, loss, duplicates and reordering") {
  const LinkConditions bad{
      .latency = Millis{60}, .jitter = Millis{80}, .lossPercent = 25, .duplicatePercent = 10};
  for (std::uint32_t seed = 1; seed <= 5; ++seed) {
    CAPTURE(seed);
    const auto received = exchange(bad, 300, seed);
    CHECK(received[0] == upTo(300));
    CHECK(received[1] == upTo(300));
  }
}

TEST_CASE("the reliable channel sends again what was not acknowledged") {
  LoopbackNetwork network{{.lossPercent = 50}, 3};
  ReliableChannel one{network.peer(0)};
  ReliableChannel two{network.peer(1)};
  for (std::uint32_t number = 0; number < 20; ++number) {
    REQUIRE(one.send(numbered(number)));
  }
  for (int round = 0; round < 200; ++round) {
    one.poll(network.now());
    two.poll(network.now());
    network.advance(Step);
  }
  CHECK(one.resent() > 0);
  CHECK(one.unacknowledged() == 0);
}

TEST_CASE("the reliable channel refuses a message over a datagram") {
  LoopbackNetwork network{{}};
  ReliableChannel channel{network.peer(0)};
  CHECK(channel.send(Bytes(ReliableChannel::MaxMessage, 1)));
  CHECK_FALSE(channel.send(Bytes(ReliableChannel::MaxMessage + 1, 1)));
}

TEST_CASE("a quiet channel still tells the peer it is there, and a cut link goes silent") {
  LoopbackNetwork network{{.latency = Millis{20}}};
  ReliableChannel one{network.peer(0)};
  ReliableChannel two{network.peer(1)};
  for (int round = 0; round < 100; ++round) {
    one.poll(network.now());
    two.poll(network.now());
    network.advance(Step);
  }
  REQUIRE(two.lastHeard());
  CHECK(network.now() - *two.lastHeard() < Millis{400});
  network.cut();
  for (int round = 0; round < 200; ++round) {
    one.poll(network.now());
    two.poll(network.now());
    network.advance(Step);
  }
  CHECK(network.now() - *two.lastHeard() >= Millis{1900});
}

TEST_CASE("the channel ignores datagrams that are not its own") {
  LoopbackNetwork network{{}};
  ReliableChannel two{network.peer(1)};
  const Bytes junk{1, 2, 3, 4, 5};
  network.peer(0).send(junk);
  two.poll(network.now());
  CHECK_FALSE(two.heardAny());
  CHECK_FALSE(two.receive());
}

TEST_CASE("the wire format reads back what it wrote, and refuses a short message") {
  WireWriter out;
  out.put(std::uint8_t{7}).put(std::int32_t{-5}).put(std::uint64_t{0x1122334455667788ULL});
  out.put(std::string_view{"name"}).put(true);
  const Bytes bytes = out.take();
  WireReader in{bytes};
  CHECK(in.get<std::uint8_t>() == 7);
  CHECK(in.get<std::int32_t>() == -5);
  CHECK(in.get<std::uint64_t>() == 0x1122334455667788ULL);
  CHECK(in.text() == "name");
  CHECK(in.get<bool>());
  CHECK(in.done());
  WireReader shortOne{std::span{bytes}.first(3)};
  (void)shortOne.get<std::uint8_t>();
  (void)shortOne.get<std::int32_t>();
  CHECK_FALSE(shortOne.ok());
}
