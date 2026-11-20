#include "online_signal_directory.hpp"

#include <algorithm>
#include <utility>

namespace dwe::app {
namespace {

// How long a read of the list may take before the screens say the server did not answer.
constexpr double AnswerSeconds = 8.0;

// The room's phase as the list shows it (a first-protocol server sends its state only); none
// for a room already playing, which the list leaves out.
std::optional<ListingState> listingState(const net::PublicRoom& room, int players, int capacity) {
  if (room.phase == "playing") {
    return std::nullopt;
  }
  if (room.phase == "connected" || room.phase == "ready") {
    return ListingState::Full;
  }
  if (room.state == "starting") {
    return ListingState::Starting;
  }
  if (room.state == "full" || (capacity > 0 && players >= capacity)) {
    return ListingState::Full;
  }
  return ListingState::Waiting;
}

} // namespace

SignalRoomDirectory::SignalRoomDirectory(std::string server, net::SocketOpener open,
                                         std::string compat)
    : server_{std::move(server)}, open_{std::move(open)}, compat_{std::move(compat)} {}

std::vector<RoomListing> SignalRoomDirectory::listings(std::span<const net::PublicRoom> rooms) {
  std::vector<RoomListing> out;
  out.reserve(rooms.size());
  for (const net::PublicRoom& room : rooms) {
    const int capacity = std::clamp(room.capacity, 1, MaxRoomPlayers);
    const int players = std::clamp(room.players, 0, capacity);
    const std::optional<ListingState> state = listingState(room, players, capacity);
    if (!state) {
      continue;
    }
    // The server names every room alike for now («Cooperative room»): the screens name a room
    // without a name of its own after its host (ui/api/online.lua).
    const bool genericName = room.name.empty() || room.name == "Cooperative room";
    out.push_back(RoomListing{.roomId = room.roomId,
                              .joinCode = room.joinCode,
                              .name = genericName ? std::string{} : room.name,
                              .message = room.message,
                              .hostName = room.hostName,
                              .players = players,
                              .capacity = capacity,
                              .createdAt = room.createdAt,
                              .state = *state,
                              .nameKey = {},
                              .mode = room.mode.empty() ? std::string{"campaign"} : room.mode,
                              .pingMs = -1});
  }
  return out;
}

void SignalRoomDirectory::refresh(double nowSeconds) {
  askedAt_ = nowSeconds;
  error_.clear();
  request_.reset();
  infoRequest_.reset();
  if (server_.empty() || !open_) {
    state_ = DirectoryState::Unavailable;
    error_ = "no_server";
    return;
  }
  state_ = DirectoryState::Loading;
  if (!info_) {
    infoRequest_ = std::make_unique<net::ServerInfoRequest>(open_(net::infoUrl(server_)));
    return;
  }
  askList();
}

void SignalRoomDirectory::askList() {
  if (info_ && !info_->has("public-rooms")) {
    state_ = DirectoryState::Unavailable;
    error_ = "directory_unavailable";
    return;
  }
  request_ = std::make_unique<net::RoomListRequest>(open_(net::roomsUrl(server_, compat_)));
}

void SignalRoomDirectory::tick(double nowSeconds) {
  if (infoRequest_ != nullptr) {
    infoRequest_->poll();
    if (infoRequest_->done()) {
      // A server that did not answer is asked for its list all the same: the list says whether it
      // is out of reach.
      info_ = infoRequest_->info();
      infoRequest_.reset();
      askList();
    } else if (nowSeconds - askedAt_ > AnswerSeconds) {
      infoRequest_.reset();
      state_ = DirectoryState::Unavailable;
      error_ = "signal_timeout";
    }
    return;
  }
  if (request_ == nullptr) {
    return;
  }
  request_->poll();
  if (request_->done()) {
    if (request_->failed()) {
      state_ = DirectoryState::Unavailable;
      error_ = request_->refusal() == "limit" ? "list_limit" : "server_unreachable";
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
