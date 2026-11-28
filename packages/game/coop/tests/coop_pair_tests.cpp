#include <doctest/doctest.h>

#include "support/coop_pair.hpp"

using namespace dwe::game::coop;
using dwe::game::coop::test::CoopPair;

TEST_CASE("the test pair connects a host and a guest, carries messages and can be cut") {
  CoopPair pair{{.hostName = "Mika", .guestName = "Ana"}};
  REQUIRE(pair.connect());
  CHECK(pair.host().remoteName() == "Ana");
  CHECK(pair.guest().remoteName() == "Mika");
  pair.deliver(pair.host(), Message{SyncWatch{.on = true}});
  const auto heard = CoopPair::drain(pair.guest());
  REQUIRE(heard.size() == 1);
  CHECK(std::holds_alternative<SyncWatch>(heard.front()));

  pair.cut();
  pair.pump(100, dwe::net::Millis{100});
  CHECK(pair.guest().phase() == LinkPhase::Failed);
  CHECK(pair.guest().failure() == LinkFailure::Silence);
}
