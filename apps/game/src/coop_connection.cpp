#include "coop_connection.hpp"

#include <dwe/net/signal.hpp>

#include <algorithm>
#include <utility>

namespace dwe::app {
namespace {
bool codeCharacter(char value) {
  return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'z') ||
         (value >= 'A' && value <= 'Z');
}

std::string_view linkFailureName(game::coop::LinkFailure failure) {
  using Failure = game::coop::LinkFailure;
  switch (failure) {
  case Failure::None:
    return {};
  case Failure::NoAnswer:
    return "no_answer";
  case Failure::Silence:
    return "silence";
  case Failure::ProtocolMismatch:
    return "protocol_mismatch";
  case Failure::VersionMismatch:
    return "version_mismatch";
  case Failure::ContentMismatch:
    return "content_mismatch";
  case Failure::RoomFull:
    return "room_full";
  case Failure::Busy:
    return "busy";
  case Failure::Damaged:
    return "damaged";
  case Failure::Disconnected:
    return "disconnected";
  }
  return "unknown";
}

std::string_view peerFailureName(net::PeerFailure failure) {
  switch (failure) {
  case net::PeerFailure::None:
    return {};
  case net::PeerFailure::NoServer:
    return "no_server";
  case net::PeerFailure::ServerUnreachable:
    return "server_unreachable";
  case net::PeerFailure::WrongCode:
    return "wrong_code";
  case net::PeerFailure::RoomFull:
    return "room_full";
  case net::PeerFailure::TimedOut:
    return "signal_timeout";
  case net::PeerFailure::NoRoute:
    return "no_direct_route";
  case net::PeerFailure::Closed:
    return "connection_closed";
  case net::PeerFailure::Refused:
    return "server_refused";
  case net::PeerFailure::NoRelay:
    return "no_relay";
  }
  return "unknown";
}
} // namespace

void CoopConnection::open() {
  close();
  view_ = View::Menu;
}

void CoopConnection::beginRoom(net::RoomSessionDesc desc) {
  if (room_ && room_->noticesPending()) {
    closing_.push_back(std::move(*room_));
  }
  room_.emplace(std::move(desc));
}

void CoopConnection::closeRoom(std::string reason) {
  if (room_) {
    room_->close(std::move(reason));
  }
}

void CoopConnection::markRoomReady(bool ready) {
  if (room_) {
    room_->markReady(ready);
  }
}

void CoopConnection::markRoomPlaying() {
  if (room_) {
    room_->markPlaying();
  }
}

void CoopConnection::pollRoom() {
  if (room_) {
    room_->poll();
  }
  for (net::RoomSession& closing : closing_) {
    closing.poll();
  }
  std::erase_if(closing_, [](const net::RoomSession& room) { return !room.noticesPending(); });
}

void CoopConnection::close() {
  closeRoom({});
  if (link_ && view_ == View::Connected)
    link_->leave();
  link_.reset();
  ++generation_;
  peer_.reset();
  typed_.clear();
  error_.clear();
  view_ = View::Closed;
}

bool CoopConnection::host(bool listed) {
  if (!desc_.configurationError.empty()) {
    view_ = View::Error;
    error_ = desc_.configurationError;
    return false;
  }
  if (!desc_.host) {
    view_ = View::Error;
    error_ = "no_host_transport";
    return false;
  }
  link_.reset();
  ++generation_;
  peer_.reset();
  error_.clear();
  std::optional<net::PublicHost> listing;
  if (listed) {
    listing = net::PublicHost{.name = net::directoryName(playerName_),
                              .icon = playerIcon_,
                              .compat = desc_.compat,
                              .mode = "campaign"};
  }
  beginRoom({.hosting = true, .code = {}, .server = desc_.signalServer, .open = desc_.open});
  peer_ = desc_.host(listing);
  if (!peer_) {
    view_ = View::Error;
    error_ = "transport_unavailable";
    closeRoom(error_);
    return false;
  }
  if (peer_->state() == net::PeerState::Failed) {
    view_ = View::Error;
    error_ = std::string{peerFailureName(peer_->failure())};
    room_->observe(*peer_);
    return false;
  }
  link_ = std::make_unique<game::coop::CoopLink>(
      game::coop::CoopLink::host(*peer_, desc_.identity, playerName_));
  ++generation_;
  view_ = View::Hosting;
  return true;
}

void CoopConnection::beginJoin() {
  closeRoom({});
  peer_.reset();
  link_.reset();
  ++generation_;
  error_.clear();
  typed_.clear();
  view_ = View::JoinEntry;
}

bool CoopConnection::join(std::string_view code) {
  const auto normalized = net::normalizedRoomCode(code);
  if (!normalized) {
    error_ = "bad_code";
    return false;
  }
  if (!desc_.join) {
    view_ = View::Error;
    error_ = "no_join_transport";
    return false;
  }
  if (!desc_.configurationError.empty()) {
    view_ = View::Error;
    error_ = desc_.configurationError;
    return false;
  }
  beginRoom({.hosting = false, .code = *normalized, .server = {}, .open = {}});
  peer_ = desc_.join(*normalized);
  if (!peer_) {
    view_ = View::Error;
    error_ = "transport_unavailable";
    closeRoom(error_);
    return false;
  }
  if (peer_->state() == net::PeerState::Failed) {
    view_ = View::Error;
    error_ = std::string{peerFailureName(peer_->failure())};
    room_->observe(*peer_);
    return false;
  }
  link_ = std::make_unique<game::coop::CoopLink>(
      game::coop::CoopLink::guest(*peer_, desc_.identity, playerName_));
  ++generation_;
  view_ = View::Joining;
  return true;
}

bool CoopConnection::submitJoin() {
  return join(typed_);
}

void CoopConnection::type(std::string_view text, bool enter) {
  if (view_ != View::JoinEntry)
    return;
  for (const char value : text) {
    if (codeCharacter(value) && typed_.size() < net::RoomCodeLength) {
      typed_.push_back(value);
      error_.clear();
    }
  }
  if (enter)
    (void)join(typed_);
}

void CoopConnection::tick(net::Millis now) {
  pollRoom();
  if (link_ == nullptr || view_ == View::Error)
    return;
  link_->poll(now);
  if (room_ && peer_ != nullptr) {
    room_->observe(*peer_);
  }
  if (link_->phase() == game::coop::LinkPhase::Ready) {
    view_ = View::Connected;
  } else if (link_->phase() == game::coop::LinkPhase::Rejected ||
             link_->phase() == game::coop::LinkPhase::Failed ||
             link_->phase() == game::coop::LinkPhase::Left) {
    error_ = peer_ != nullptr ? std::string{peerFailureName(peer_->failure())} : std::string{};
    if (error_.empty())
      error_ = std::string{linkFailureName(link_->failure())};
    if (error_.empty() && link_->phase() == game::coop::LinkPhase::Rejected) {
      error_ = "rejected";
    }
    view_ = View::Error;
    closeRoom(error_);
  }
}

std::string CoopConnection::code() const {
  if (peer_ != nullptr && !peer_->roomCode().empty()) {
    return peer_->roomCode();
  }
  // A guest's link has no code of its own: the room's is the one it joined.
  return room_ && peer_ != nullptr ? room_->code() : std::string{};
}

std::string_view coopConnectionViewName(CoopConnection::View view) {
  switch (view) {
  case CoopConnection::View::Closed:
    return "closed";
  case CoopConnection::View::Menu:
    return "menu";
  case CoopConnection::View::JoinEntry:
    return "join_entry";
  case CoopConnection::View::Hosting:
    return "hosting";
  case CoopConnection::View::Joining:
    return "joining";
  case CoopConnection::View::Connected:
    return "connected";
  case CoopConnection::View::Error:
    return "error";
  }
  return "error";
}

} // namespace dwe::app
