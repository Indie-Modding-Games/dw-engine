#pragma once

#include <cstddef>
#include <cstdint>

namespace dwe::app {

// A co-op guest's take of an item another player could take too (#357, multiplayer.md "Shared
// world interactions"): the item notice asks (Asked), the guest's co-op world sends the claim to
// the host (Sent) and writes its answer here (Granted, Refused). The item is the guest's only once
// granted, so two players never both get it.
struct FieldItemClaim {
  enum class State : std::uint8_t { None, Asked, Sent, Granted, Refused };

  State state = State::None;
  // A chest (`index` its place in FieldItems::chests) or an item on the ground (its slot).
  bool chest = false;
  std::size_t index = 0;
  // The claim's number as sent, to match the host's answer.
  std::uint32_t number = 0;
};

} // namespace dwe::app
