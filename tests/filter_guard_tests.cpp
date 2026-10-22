#include <doctest/doctest.h>

// The test case that the filter guard's ctest entries in CMakeLists.txt select, by its full name or
// by a part of it.
TEST_CASE("the filter guard's test case runs") {
  CHECK(true);
}
