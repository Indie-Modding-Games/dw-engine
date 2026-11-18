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
inline constexpr std::size_t MaxPlayerCount = 2;

enum class PlayerLink : std::uint8_t { Local, Joining, Connected, Dropped };

struct PlayerSlot {
  PlayerId id = HostPlayerId;
  std::string name;
  bool local = false;
  PlayerLink link = PlayerLink::Dropped;
  std::unique_ptr<PlayerController> controller;
};

// Runtime owner of the fixed host/guest controller slots. It does not own Field entities or
// connection transport; apps/game composes those around the roster.
class PlayerRegistry {
public:
  explicit PlayerRegistry(std::string hostName = "Host");

  // One guest slot is admitted. A rejected attempt leaves both slots unchanged.
  [[nodiscard]] bool addGuest(std::string name, std::unique_ptr<PlayerController> controller,
                              PlayerLink link = PlayerLink::Joining);
  void setGuestLink(PlayerLink link);

  [[nodiscard]] LocalController& localController();
  [[nodiscard]] std::optional<PlayerCommands> next(PlayerId id, std::uint64_t tick);
  [[nodiscard]] const PlayerSlot* find(PlayerId id) const;
  [[nodiscard]] std::size_t size() const;

private:
  std::array<std::optional<PlayerSlot>, MaxPlayerCount> slots_;
};

} // namespace dwe::app
