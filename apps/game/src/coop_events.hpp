#pragma once

#include <dwe/game/coop/protocol.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "coop_gameplay_bridge.hpp"

namespace dwe::app {

// The NPC whose event a player's game plays: the map and the NPC's script id.
struct CoopEvent {
  std::string mapId;
  std::uint16_t npc = 0;

  friend bool operator==(const CoopEvent&, const CoopEvent&) = default;
};

// One guest the host tells the events to.
struct CoopEventPeer {
  game::coop::PlayerId id = game::coop::GuestPlayerId;
  CoopGameplayBridge* bridge = nullptr;
};

// An event starts once in a shared world (#417, docs/architecture/multiplayer.md «Events»). Each
// game runs its own story, so two players who talk to the same NPC would both play its event and
// both start its battle. While one player's game plays an NPC's event, the other games leave that
// NPC be: a guest tells the host about its own event, the host tells every guest about everyone's.
class CoopEvents {
public:
  // The host's tick: the guests' events in, everyone's out to every guest. `own` is the host's.
  void hostTick(const std::optional<CoopEvent>& own, std::span<const CoopEventPeer> guests);
  // A guest's tick: the others' events in, its own out.
  void guestTick(const std::optional<CoopEvent>& own, game::coop::PlayerId self,
                 CoopGameplayBridge& host);

  // The NPCs that the other players' events hold on map `mapId`, by script id.
  [[nodiscard]] std::vector<std::int32_t> heldByOthers(game::coop::PlayerId self,
                                                       std::string_view mapId) const;

  // A guest left: its event lets its NPC go, and it hears everything again if it comes back.
  void playerLeft(game::coop::PlayerId id);
  void reset();

private:
  using Events = std::array<std::optional<CoopEvent>, game::coop::MaxPlayers>;

  Events events_{};
  // The host's side: what it told each guest of each player's event.
  std::array<Events, game::coop::MaxPlayers> told_{};
  // A guest's side: its own event as it last told the host.
  std::optional<CoopEvent> sent_;
};

} // namespace dwe::app
