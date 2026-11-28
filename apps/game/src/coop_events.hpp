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

struct CoopEvent {
  std::string mapId;
  std::uint16_t npc = 0;

  friend bool operator==(const CoopEvent&, const CoopEvent&) = default;
};

struct CoopEventPeer {
  game::coop::PlayerId id = game::coop::GuestPlayerId;
  CoopGameplayBridge* bridge = nullptr;
};

// An event starts once in a shared world (docs/architecture/multiplayer.md «Events»).
// While one game plays an NPC's event, the other games leave that NPC be.
class CoopEvents {
public:
  // `own` is the host's own event.
  void hostTick(const std::optional<CoopEvent>& own, std::span<const CoopEventPeer> guests);
  void guestTick(const std::optional<CoopEvent>& own, game::coop::PlayerId self,
                 CoopGameplayBridge& host);

  // By script id.
  [[nodiscard]] std::vector<std::int32_t> heldByOthers(game::coop::PlayerId self,
                                                       std::string_view mapId) const;

  // Lets its NPC go; it hears everything again if it comes back.
  void playerLeft(game::coop::PlayerId id);
  void reset();

private:
  using Events = std::array<std::optional<CoopEvent>, game::coop::MaxPlayers>;

  Events events_{};
  std::array<Events, game::coop::MaxPlayers> told_{};
  std::optional<CoopEvent> sent_;
};

} // namespace dwe::app
