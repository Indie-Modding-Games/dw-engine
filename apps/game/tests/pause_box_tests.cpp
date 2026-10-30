#include <doctest/doctest.h>

#include "pause_box.hpp"

using dwe::app::PauseBox;

TEST_CASE("pause box: Start pauses and the next Start goes on, as handlePause") {
  PauseBox pause;
  CHECK_FALSE(pause.tick(false, true).paused);

  const auto opened = pause.tick(true, true);
  CHECK(opened.paused);
  CHECK(opened.opened);
  // Held or not pressed again, the game stays paused.
  const auto waiting = pause.tick(false, true);
  CHECK(waiting.paused);
  CHECK_FALSE(waiting.opened);

  const auto closed = pause.tick(true, true);
  CHECK_FALSE(closed.paused);
  CHECK(closed.closed);
  CHECK_FALSE(pause.paused());
}

TEST_CASE("pause box: no pause where the original's Start does something else") {
  PauseBox pause;
  // The title, a movie, a naming box: Start is not a pause there.
  const auto step = pause.tick(true, false);
  CHECK_FALSE(step.paused);
  CHECK_FALSE(step.opened);
  CHECK_FALSE(pause.paused());
}
