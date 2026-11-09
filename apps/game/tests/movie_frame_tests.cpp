// Where a movie's picture goes in the window (movie_rules.hpp: movieFrame): on the original's 4:3
// screen (mov.c: MOV_initEnvironment, 176 lines from line 32 of 240), or, with the widescreen
// pictures option, fitted to the window alone with its shape kept.

#include <doctest/doctest.h>

#include "movie_rules.hpp"

namespace dwe::app {

TEST_CASE("a movie sits on the original's 4:3 screen, or fills a wide window with the option") {
  // 1920 x 1080: the original's screen is 1440 x 1080 in the middle, the picture 1440 x 792 from
  // line 32 (144 pixels down).
  const MovieFrame original = movieFrame(1920.0F, 1080.0F, 320.0F, 176.0F, false);
  CHECK(original.left == doctest::Approx(240.0));
  CHECK(original.top == doctest::Approx(144.0));
  CHECK(original.width == doctest::Approx(1440.0));
  CHECK(original.height == doctest::Approx(792.0));

  // Filling: as wide as the window, its shape kept, in the middle.
  const MovieFrame filled = movieFrame(1920.0F, 1080.0F, 320.0F, 176.0F, true);
  CHECK(filled.left == doctest::Approx(0.0));
  CHECK(filled.width == doctest::Approx(1920.0));
  CHECK(filled.height == doctest::Approx(1056.0));
  CHECK(filled.top == doctest::Approx(12.0));

  // An ultrawide window: as tall as the window, bars at the sides only.
  const MovieFrame ultrawide = movieFrame(2560.0F, 1080.0F, 320.0F, 176.0F, true);
  CHECK(ultrawide.height == doctest::Approx(1080.0));
  CHECK(ultrawide.width / ultrawide.height == doctest::Approx(320.0 / 176.0));
  CHECK(ultrawide.left == doctest::Approx((2560.0 - ultrawide.width) / 2.0));

  // A 4:3 window: both are the same.
  const MovieFrame narrowOriginal = movieFrame(1024.0F, 768.0F, 320.0F, 176.0F, false);
  const MovieFrame narrowFilled = movieFrame(1024.0F, 768.0F, 320.0F, 176.0F, true);
  CHECK(narrowFilled.left == doctest::Approx(narrowOriginal.left));
  CHECK(narrowFilled.top == doctest::Approx(narrowOriginal.top));
  CHECK(narrowFilled.width == doctest::Approx(narrowOriginal.width));
  CHECK(narrowFilled.height == doctest::Approx(narrowOriginal.height));
}

} // namespace dwe::app
