#include <dwe/net/endpoint.hpp>
#include <dwe/net/reliable_channel.hpp>
#include <dwe/net/udp.hpp>

#include <doctest/doctest.h>

#include <cctype>
#include <chrono>
#include <string>
#include <thread>

using namespace dwe::net;

TEST_CASE("an endpoint reads and prints as a.b.c.d:port") {
  const auto endpoint = parseEndpoint("192.168.1.20:7250");
  REQUIRE(endpoint);
  CHECK(endpoint->address == std::array<std::uint8_t, 4>{192, 168, 1, 20});
  CHECK(endpoint->port == 7250);
  CHECK(formatEndpoint(*endpoint) == "192.168.1.20:7250");
  CHECK_FALSE(parseEndpoint("192.168.1:7250"));
  CHECK_FALSE(parseEndpoint("192.168.1.300:7250"));
  CHECK_FALSE(parseEndpoint("192.168.1.2:0"));
  CHECK_FALSE(parseEndpoint("192.168.1.2:70000"));
  CHECK_FALSE(parseEndpoint("192.168.1.2"));
}

TEST_CASE("a join code holds the endpoint in ten letters and reads back") {
  const Endpoint endpoint{.address = {203, 0, 113, 7}, .port = 7250};
  const std::string code = joinCodeOf(endpoint);
  CHECK(code.size() == 11);
  CHECK(code[5] == '-');
  const auto back = parseJoinCode(code);
  REQUIRE(back);
  CHECK(*back == endpoint);
  // Lower case, no dash, and the letters people mix up.
  std::string loose;
  for (const char letter : code) {
    if (letter != '-') {
      loose.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(letter))));
    }
  }
  CHECK(parseJoinCode(loose).value_or(Endpoint{}) == endpoint);
  CHECK(parseJoinTarget(" " + code + " ").value_or(Endpoint{}) == endpoint);
  CHECK(parseJoinTarget("203.0.113.7:7250").value_or(Endpoint{}) == endpoint);
  CHECK_FALSE(parseJoinCode("ABCDE"));
  CHECK_FALSE(parseJoinCode("ZZZZZ-ZZZZZ"));
  CHECK_FALSE(parseJoinCode("ABCDE-1234U"));
}

TEST_CASE("two UDP links on this machine talk through the reliable channel") {
  auto host = UdpLink::host(0);
  REQUIRE_MESSAGE(host, host.error().message);
  const Endpoint where{.address = {127, 0, 0, 1}, .port = (*host)->localPort()};
  auto guest = UdpLink::join(where);
  REQUIRE_MESSAGE(guest, guest.error().message);
  ReliableChannel hostChannel{**host};
  ReliableChannel guestChannel{**guest};
  REQUIRE(guestChannel.send(Bytes{1, 2, 3}));
  REQUIRE(hostChannel.send(Bytes{4, 5}));
  std::optional<Bytes> atHost;
  std::optional<Bytes> atGuest;
  const auto start = std::chrono::steady_clock::now();
  for (int round = 0; round < 400 && (!atHost || !atGuest); ++round) {
    const auto now = std::chrono::duration_cast<Millis>(std::chrono::steady_clock::now() - start);
    guestChannel.poll(now);
    hostChannel.poll(now);
    if (!atHost) {
      atHost = hostChannel.receive();
    }
    if (!atGuest) {
      atGuest = guestChannel.receive();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  REQUIRE(atHost);
  CHECK(*atHost == Bytes{1, 2, 3});
  REQUIRE(atGuest);
  CHECK(*atGuest == Bytes{4, 5});
  REQUIRE((*host)->peer());
  CHECK((*host)->peer()->port == (*guest)->localPort());
}
