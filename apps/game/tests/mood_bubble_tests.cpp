#include <doctest/doctest.h>

#include "mood_bubble.hpp"

using dwe::app::MoodBubble;

TEST_CASE("the mood bubble grows and rises over 30 frames as measured in the original") {
  MoodBubble bubble;
  bubble.show(6, 2);
  REQUIRE(bubble.shown());
  CHECK(bubble.character() == 6);
  CHECK(bubble.firstU() == 64);
  // The first frame draws the timer's 30: one pixel up, a sixteenth of the size.
  CHECK(bubble.offset() == 1);
  CHECK(bubble.scale() == doctest::Approx(1.0F / 16.0F));
  // After two frames the sprite reads scale 0x200 (two pixels).
  bubble.frame(true);
  CHECK(bubble.offset() == 2);
  for (int frame = 0; frame < 40; ++frame) {
    bubble.frame(true);
  }
  CHECK(bubble.offset() == 16);
  CHECK(bubble.scale() == doctest::Approx(1.0F));
}

TEST_CASE("without its character on the map the bubble waits") {
  MoodBubble bubble;
  bubble.show(6, 0);
  bubble.frame(false);
  bubble.frame(false);
  CHECK(bubble.offset() == 1);
}

TEST_CASE("two bubbles in a row grow twice as fast and one removal leaves one") {
  MoodBubble bubble;
  bubble.show(6, 1);
  bubble.show(6, 4);
  CHECK(bubble.objects() == 2);
  CHECK(bubble.firstU() == 128);
  bubble.frame(true);
  // Two render objects: the timer goes 30 -> 28.
  CHECK(bubble.offset() == 2);
  bubble.remove();
  CHECK(bubble.shown());
  bubble.remove();
  CHECK_FALSE(bubble.shown());
  bubble.remove();
  CHECK(bubble.objects() == 0);
}
