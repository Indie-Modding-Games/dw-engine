// The movie that follows another (movie_rules.hpp: movieAfter, main.c: main): after the ending's
// movie the original goes round its loop and plays the boot movie before the "Press Start" screen.

#include <doctest/doctest.h>

#include "movie_rules.hpp"

namespace dwe::app {

TEST_CASE("the boot movie follows the ending's, and no other movie has one after it") {
  CHECK(movieAfter(MovieMoment::Ending) == MovieMoment::Boot);
  CHECK_FALSE(movieAfter(MovieMoment::Boot).has_value());
  CHECK_FALSE(movieAfter(MovieMoment::NewGame).has_value());
  CHECK_FALSE(movieAfter(MovieMoment::ClearedLoad).has_value());
}

} // namespace dwe::app
