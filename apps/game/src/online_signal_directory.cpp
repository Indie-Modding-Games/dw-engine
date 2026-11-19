#include "online_signal_directory.hpp"

#include <algorithm>
#include <utility>

namespace dwe::app {
namespace {

// How long a read of the list may take before the screens say the server did not answer.
constexpr double AnswerSeconds = 8.0;

ListingState listingState(std::string_view state, int players, int capacity) {
  if (state == "starting") {
    return ListingState::Starting;
  }
  if (state == "full" || (capacity > 0 && players >= capacity)) {
    return ListingState::Full;
  }
  return ListingState::Waiting;
}

} // namespace

SignalRoomDirectory::SignalRoomDirectory(std::string server, net::SocketOpener open)
    : server_{std::move(server)}, open_{std::move(open)} {}

std::vector<RoomListing> SignalRoomDirectory::listings(std::span<const net::PublicRoom> rooms) {
  std::vector<RoomListing> out;
  out.reserve(rooms.size());
  for (const net::PublicRoom& room : rooms) {
    const int capacity = std::clamp(room.capacity, 1, MaxRoomPlayers);
    const int players = std::clamp(room.players, 0, capacity);
    // The server names every room alike for now («Cooperative room»): the screens show their
    // own words for a room without a name of its own.
    const bool genericName = room.name.empty() || room.name == "Cooperative room";
    out.push_back(RoomListing{.roomId = room.roomId,
                              .joinCode = room.joinCode,
                              .name = genericName ? std::string{} : room.name,
                              .message = room.message,
                              .hostName = {},
                              .players = players,
                              .capacity = capacity,
                              .createdAt = room.createdAt,
                              .state = listingState(room.state, players, capacity),
                              .nameKey = genericName ? "online.untitled_room" : std::string{},
                              .mode = "campaign",
                              .pingMs = -1});
  }
  return out;
}

void SignalRoomDirectory::refresh(double nowSeconds) {
  askedAt_ = nowSeconds;
  error_.clear();
  if (server_.empty() || !open_) {
    request_.reset();
    state_ = DirectoryState::Unavailable;
    error_ = "no_server";
    return;
  }
  state_ = DirectoryState::Loading;
  request_ = std::make_unique<net::RoomListRequest>(open_(net::roomsUrl(server_)));
}

void SignalRoomDirectory::tick(double nowSeconds) {
  if (request_ == nullptr) {
    return;
  }
  request_->poll();
  if (request_->done()) {
    if (request_->failed()) {
      state_ = DirectoryState::Unavailable;
      error_ = "server_unreachable";
    } else {
      rooms_ = listings(*request_->rooms());
      state_ = DirectoryState::Ready;
    }
    request_.reset();
  } else if (nowSeconds - askedAt_ > AnswerSeconds) {
    request_.reset();
    state_ = DirectoryState::Unavailable;
    error_ = "signal_timeout";
  }
}

} // namespace dwe::app
