#pragma once

#include <dwe/game/partner/poop_piles.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace dwe::app {

// A co-op guest's part in the shared poop piles (multiplayer.md "Shared world interactions"): the
// piles its partner left, for the host's world, and its partner's eat of a pile, which waits for
// the host's word (Asked, then Sent, then Granted or Refused) so two partners never eat one pile.
struct FieldPileShare {
  struct Eat {
    enum class State : std::uint8_t { None, Asked, Sent, Granted, Refused };

    State state = State::None;
    std::string mapId;
    game::partner::Tile tile;
    // The ask's number as sent, to match the host's answer.
    std::uint32_t number = 0;
    // Granted: the pile's size on the host.
    std::int32_t size = 0;
  };

  std::vector<game::partner::PoopPile> left;
  Eat eat;
};

} // namespace dwe::app
