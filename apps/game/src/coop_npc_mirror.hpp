#pragma once

#include <dwe/game/field/npcs.hpp>
#include <dwe/net/datagram.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>

#include "coop_snapshot_interpolation.hpp"

namespace dwe::app {

// A guest's map NPCs as the host's snapshots show them (#419, docs/architecture/multiplayer.md
// «The map's NPCs»): each one the host lists stands where the host's does, so a wild one walks,
// chases and leaves the same way on every screen. One the host no longer lists is taken off once
// it has been missing for GoneAfter while this game's map stands still, so nothing stays behind.
// Without a snapshot of this map (another map, a map change, no link) they walk on their own.
class CoopNpcMirror {
public:
  static constexpr net::Millis GoneAfter{1000};

  struct View {
    // The host's world as drawn now, when it stands on this game's map.
    const PresentedCoopWorld* world = nullptr;
    std::uint32_t mapSerial = 0;
    // No script of this game runs and its map does not change.
    bool ready = false;
  };
  // Returns how many NPCs it took off.
  std::size_t show(game::field::FieldNpcs& npcs, const View& view, net::Millis now);

private:
  std::optional<std::uint32_t> serial_;
  std::map<std::size_t, net::Millis> missingSince_;
};

} // namespace dwe::app
