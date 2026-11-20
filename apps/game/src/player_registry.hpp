#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "player_commands.hpp"

namespace dwe::app {

using PlayerId = std::uint8_t;
inline constexpr PlayerId HostPlayerId = 0;
inline constexpr PlayerId GuestPlayerId = 1;
// The host and up to three guests (game::coop::MaxPlayers).
inline constexpr std::size_t MaxPlayerCount = 4;

enum class PlayerLink : std::uint8_t { Local, Joining, Connected, Dropped };

struct PlayerSlot {
  PlayerId id = HostPlayerId;
  std::string name;
  bool local = false;
  PlayerLink link = PlayerLink::Dropped;
  std::unique_ptr<PlayerController> controller;
};

// Runtime owner of the players' controller slots, by id: the local host and up to three guests.
// It does not own Field entities or connection transport; apps/game composes those around the
// roster (the session's own roster is the host's game::coop::WorldAuthority).
class PlayerRegistry {
public:
  explicit PlayerRegistry(std::string hostName = "Host");

  // Admits guest `id` (any id but the host's, below MaxPlayerCount, not taken). A rejected attempt
  // leaves every slot unchanged.
  [[nodiscard]] bool addGuest(PlayerId id, std::string name,
                              std::unique_ptr<PlayerController> controller,
                              PlayerLink link = PlayerLink::Joining);
  // Frees guest `id`'s slot and its controller (the player left); false when it was free.
  bool removeGuest(PlayerId id);
  void setLink(PlayerId id, PlayerLink link);

  [[nodiscard]] LocalController& localController();
  [[nodiscard]] std::optional<PlayerCommands> next(PlayerId id, std::uint64_t tick);
  [[nodiscard]] const PlayerSlot* find(PlayerId id) const;
  [[nodiscard]] std::size_t size() const;

private:
  std::array<std::optional<PlayerSlot>, MaxPlayerCount> slots_;
};

} // namespace dwe::app
