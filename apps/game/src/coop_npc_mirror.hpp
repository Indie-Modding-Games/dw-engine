#pragma once

#include <dwe/game/field/npcs.hpp>
#include <dwe/net/datagram.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>

#include "coop_snapshot_interpolation.hpp"

namespace dwe::app {

// Guest NPCs follow the host's snapshots (docs/architecture/multiplayer.md «The map's NPCs»).
// One the host no longer lists goes after GoneAfter while this map stands still; without a
// snapshot of this map they walk on their own, and a battle of this game's own keeps them.
class CoopNpcMirror {
public:
  static constexpr net::Millis GoneAfter{1000};

  struct View {
    // The host's world as drawn now, when it stands on this game's map.
    const PresentedCoopWorld* world = nullptr;
    std::uint32_t mapSerial = 0;
    // No script of this game runs and its map does not change.
    bool ready = false;
    // A battle of this game's own runs: its fighters stand where it puts them, the rest stay put.
    bool battling = false;
  };
  // Returns how many NPCs it took off.
  std::size_t show(game::field::FieldNpcs& npcs, const View& view, net::Millis now);

private:
  std::optional<std::uint32_t> serial_;
  std::map<std::size_t, net::Millis> missingSince_;
};

} // namespace dwe::app
