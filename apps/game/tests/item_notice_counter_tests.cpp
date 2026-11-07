#include <doctest/doctest.h>

#include "field_items_screen.hpp"

namespace dwe::app {
namespace {

// tamer.c: tamerTickPickupItem takes a press once TAKE_ITEM_FRAME_COUNTER is past 4, its fifth
// frame waiting; tamerTickTakeChest takes the button held once it is past 5, its sixth.
TEST_CASE(
    "an item notice takes a confirm from the counter's fifth frame, a chest's from its sixth") {
  ItemNoticeCounter counter;
  for (int frame = 1; frame <= 4; ++frame) {
    counter.tick();
    CHECK_FALSE(counter.takes(false));
    CHECK_FALSE(counter.takes(true));
  }
  counter.tick();
  CHECK(counter.takes(false));
  CHECK_FALSE(counter.takes(true));
  counter.tick();
  CHECK(counter.takes(true));
}

TEST_CASE("an item notice's counter stops at 10 and starts over on a confirm") {
  ItemNoticeCounter counter;
  for (int frame = 0; frame < 40; ++frame) {
    counter.tick();
  }
  CHECK(counter.frames == ItemNoticeCounter::Cap);
  counter.reset();
  CHECK(counter.frames == 0);
  CHECK_FALSE(counter.takes(false));
}

} // namespace
} // namespace dwe::app
