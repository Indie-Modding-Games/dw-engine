#include <dwe/core/error.hpp>

#include <doctest/doctest.h>

using dwe::core::fail;
using dwe::core::Result;

namespace {

Result<int> half(int value) {
  if (value % 2 != 0) {
    return fail("odd value");
  }
  return value / 2;
}

} // namespace

TEST_CASE("a result carries either the value or the error") {
  CHECK(half(4).value() == 2);
  REQUIRE_FALSE(half(3).has_value());
  CHECK(half(3).error().message == "odd value");
}
