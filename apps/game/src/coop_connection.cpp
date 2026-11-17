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
  case Failure::None: return {};
  case Failure::NoAnswer: return "no_answer";
  case Failure::Silence: return "silence";
  case Failure::ProtocolMismatch: return "protocol_mismatch";
  case Failure::VersionMismatch: return "version_mismatch";
  case Failure::ContentMismatch: return "content_mismatch";
  case Failure::RoomFull: return "room_full";
  case Failure::Busy: return "busy";
  case Failure::Damaged: return "damaged";
  case Failure::Disconnected: return "disconnected";
  }
  return "unknown";
}
} // namespace

void CoopConnection::open() {
  close();
  view_ = View::Menu;
}

void CoopConnection::close() {
  if (link_ && view_ == View::Connected) link_->leave();
  link_.reset();
  peer_.reset();
  typed_.clear();
  error_.clear();
  view_ = View::Closed;
}

bool CoopConnection::host() {
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
  peer_.reset();
  error_.clear();
  peer_ = desc_.host();
  if (!peer_) {
    view_ = View::Error;
    error_ = "transport_unavailable";
    return false;
  }
  link_ = std::make_unique<game::coop::CoopLink>(
      game::coop::CoopLink::host(*peer_, desc_.identity));
  view_ = View::Hosting;
  return true;
}

void CoopConnection::beginJoin() {
  peer_.reset();
  link_.reset();
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
  peer_ = desc_.join(*normalized);
  if (!peer_) {
    view_ = View::Error;
    error_ = "transport_unavailable";
    return false;
  }
  link_ = std::make_unique<game::coop::CoopLink>(
      game::coop::CoopLink::guest(*peer_, desc_.identity, "Guest"));
  view_ = View::Joining;
  return true;
}

void CoopConnection::type(std::string_view text, bool enter) {
  if (view_ != View::JoinEntry) return;
  for (const char value : text) {
    if (codeCharacter(value) && typed_.size() < net::RoomCodeLength) {
      typed_.push_back(value);
      error_.clear();
    }
  }
  if (enter) (void)join(typed_);
}

void CoopConnection::tick(net::Millis now) {
  if (link_ == nullptr || view_ == View::Error) return;
  link_->poll(now);
  if (link_->phase() == game::coop::LinkPhase::Ready) {
    view_ = View::Connected;
  } else if (link_->phase() == game::coop::LinkPhase::Rejected ||
             link_->phase() == game::coop::LinkPhase::Failed ||
             link_->phase() == game::coop::LinkPhase::Left) {
    error_ = std::string{linkFailureName(link_->failure())};
    if (error_.empty() && link_->phase() == game::coop::LinkPhase::Rejected) {
      error_ = "rejected";
    }
    view_ = View::Error;
  }
}

std::string CoopConnection::code() const {
  return peer_ != nullptr ? peer_->roomCode() : std::string{};
}

std::string_view coopConnectionViewName(CoopConnection::View view) {
  switch (view) {
  case CoopConnection::View::Closed: return "closed";
  case CoopConnection::View::Menu: return "menu";
  case CoopConnection::View::JoinEntry: return "join_entry";
  case CoopConnection::View::Hosting: return "hosting";
  case CoopConnection::View::Joining: return "joining";
  case CoopConnection::View::Connected: return "connected";
  case CoopConnection::View::Error: return "error";
  }
  return "error";
}

} // namespace dwe::app
