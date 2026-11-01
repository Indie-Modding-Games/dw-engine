#pragma once

#include <cstdint>

namespace dwe::app {

// partner.c: partnerTickFeedItem: the item held out is drawn at the player's model node 9
// (game_menu.c: renderFeedingItem, the item's icon facing the camera, without a shadow).
inline constexpr std::uint32_t FeedItemNode = 9;
// Sub state 5: the item goes once the partner's eat clip (8) reaches this frame.
inline constexpr std::int32_t FeedItemEatenFrame = 11;

// Where the feeding stands for the item in the hand.
enum class FeedItemPhase : std::uint8_t { Walking, Offering, Eating, Refusing, Shrugging };

// Whether the item shows in the player's hand: from the offer (sub state 3, addObject 0xfa7) until
// the partner eats it (frame 11 of its eat clip) or its refusal clip ends (sub state 7,
// removeObject); `eatFrame` is the eat clip's frame while Eating.
[[nodiscard]] constexpr bool feedItemHeld(FeedItemPhase phase, std::int32_t eatFrame) {
  switch (phase) {
  case FeedItemPhase::Offering:
  case FeedItemPhase::Refusing:
    return true;
  case FeedItemPhase::Eating:
    return eatFrame < FeedItemEatenFrame;
  case FeedItemPhase::Walking:
  case FeedItemPhase::Shrugging:
    return false;
  }
  return false;
}

} // namespace dwe::app
