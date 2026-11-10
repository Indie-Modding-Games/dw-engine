#include <doctest/doctest.h>

#include "evolving_bubble.hpp"

using dwe::app::evolvingBubbleFrame;
using dwe::app::EvolvingBubbleLastFrame;
using dwe::app::partnerBubblePlaced;

TEST_CASE("the evolving bubble shows on frames 1 to 51 of the ready wait, bursting on the last") {
  // bubble.c: tickConditionBubble: made with frameCount 0, one step a frame, the burst at 51, gone
  // once frameCount passes 50.
  CHECK(EvolvingBubbleLastFrame == 51);
  CHECK(evolvingBubbleFrame(0) == 0);
  CHECK(evolvingBubbleFrame(1) == 1);
  CHECK(evolvingBubbleFrame(15) == 15);
  CHECK(evolvingBubbleFrame(51) == 51);
  CHECK(evolvingBubbleFrame(52) == 0);
  // The ready wait lasts 56 frames (rules/original/evolution.yaml: wait_frames): the bubble is gone
  // before the scenery dims.
  CHECK(evolvingBubbleFrame(56) == 0);
  CHECK(evolvingBubbleFrame(-1) == 0);
}

TEST_CASE("the evolving bubble keeps its place while the walk up holds the partner") {
  // partnerTickEvolving holds the player and walks the partner up before the scene: the field is
  // held for care, as in the care scenes that show no bubble, but the evolving one still shows.
  CHECK(partnerBubblePlaced(false, false));
  CHECK_FALSE(partnerBubblePlaced(true, false));
  CHECK(partnerBubblePlaced(true, true));
}
