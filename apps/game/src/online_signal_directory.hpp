#pragma once

#include <dwe/net/rtc_peer_link.hpp>
#include <dwe/net/signal.hpp>

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "online_rooms.hpp"

namespace dwe::app {

// The public rooms as the signaling server lists them (GET /v1/rooms, read over a WebSocket: one
// snapshot per refresh). Rooms the server lists have no mode or ping yet: they are co-op
// campaign rooms, with no ping bars.
class SignalRoomDirectory final : public RoomDirectory {
public:
  // `server`: the signaling server (wss://...); empty when none is configured.
  SignalRoomDirectory(std::string server, net::SocketOpener open);

  void refresh(double nowSeconds) override;
  void tick(double nowSeconds) override;
  [[nodiscard]] DirectoryState state() const override { return state_; }
  [[nodiscard]] std::span<const RoomListing> rooms() const override { return rooms_; }
  [[nodiscard]] std::string_view error() const override { return error_; }

  // A snapshot of the list as the screens show it (also for the tests).
  [[nodiscard]] static std::vector<RoomListing> listings(std::span<const net::PublicRoom> rooms);

private:
  std::string server_;
  net::SocketOpener open_;
  std::unique_ptr<net::RoomListRequest> request_;
  double askedAt_ = 0.0;
  DirectoryState state_ = DirectoryState::Loading;
  std::vector<RoomListing> rooms_;
  std::string error_;
};

} // namespace dwe::app
