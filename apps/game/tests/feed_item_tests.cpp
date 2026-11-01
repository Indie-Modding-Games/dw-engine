#include <doctest/doctest.h>

#include "feed_item.hpp"

using dwe::app::feedItemHeld;
using dwe::app::FeedItemPhase;

TEST_CASE("game_menu.c renderFeedingItem: the item shows in the hand from the offer until eaten") {
  CHECK_FALSE(feedItemHeld(FeedItemPhase::Walking, 0));
  CHECK(feedItemHeld(FeedItemPhase::Offering, 0));
  // partnerTickFeedItem sub state 5: removeObject on frame 11 of the eat clip.
  CHECK(feedItemHeld(FeedItemPhase::Eating, 0));
  CHECK(feedItemHeld(FeedItemPhase::Eating, 10));
  CHECK_FALSE(feedItemHeld(FeedItemPhase::Eating, 11));
  CHECK_FALSE(feedItemHeld(FeedItemPhase::Eating, 15));
  // Sub state 7: held through the refusal, gone when the player shrugs.
  CHECK(feedItemHeld(FeedItemPhase::Refusing, 0));
  CHECK_FALSE(feedItemHeld(FeedItemPhase::Shrugging, 0));
}
