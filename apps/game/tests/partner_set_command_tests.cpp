#include <doctest/doctest.h>

#include "partner_set_command.hpp"

namespace dwe::app {

TEST_CASE("partner_set reads a value's name and an amount") {
  const auto offense = partnerWriteFromText("offense 500");
  REQUIRE(offense.has_value());
  CHECK(offense->kind == PartnerWrite::Kind::Offense);
  CHECK(offense->value == 500);
  const auto care = partnerWriteFromText(" care  0 ");
  REQUIRE(care.has_value());
  CHECK(care->kind == PartnerWrite::Kind::CareMistakes);
  CHECK(partnerWriteFromText("hours 120")->kind == PartnerWrite::Kind::Hours);
  CHECK_FALSE(partnerWriteFromText("offense").has_value());
  CHECK_FALSE(partnerWriteFromText("luck 3").has_value());
  CHECK_FALSE(partnerWriteFromText("hp -1").has_value());
  CHECK_FALSE(partnerWriteFromText("hp 1 2").has_value());
}

} // namespace dwe::app
