#include <dwe/core/log.hpp>
#include <dwe/net/room_session.hpp>

#include <utility>

namespace dwe::net {

std::string_view roomPhaseName(RoomPhase phase) {
  switch (phase) {
  case RoomPhase::Connecting:
    return "connecting";
  case RoomPhase::Open:
    return "open";
  case RoomPhase::Connected:
    return "connected";
  case RoomPhase::Ready:
    return "ready";
  case RoomPhase::Playing:
    return "playing";
  case RoomPhase::Closed:
    return "closed";
  }
  return "closed";
}

namespace {

// Which phases follow which: the room only moves forwards, but for taking «ready» back.
bool allowed(RoomPhase from, RoomPhase to) {
  if (to == RoomPhase::Closed) {
    return from != RoomPhase::Closed;
  }
  switch (from) {
  case RoomPhase::Connecting:
    return to == RoomPhase::Open || to == RoomPhase::Connected;
  case RoomPhase::Open:
    return to == RoomPhase::Connected;
  case RoomPhase::Connected:
    return to == RoomPhase::Ready || to == RoomPhase::Playing;
  case RoomPhase::Ready:
    return to == RoomPhase::Connected || to == RoomPhase::Playing;
  case RoomPhase::Playing:
  case RoomPhase::Closed:
    return false;
  }
  return false;
}

} // namespace

RoomSession::RoomSession(RoomSessionDesc desc) : desc_{std::move(desc)}, code_{desc_.code} {}

bool RoomSession::enter(RoomPhase next) {
  if (next == phase_) {
    return true;
  }
  if (!allowed(phase_, next)) {
    core::logDebug("rooms: {} cannot go to {}", roomPhaseName(phase_), roomPhaseName(next));
    return false;
  }
  core::logInfo("rooms: room {} {} -> {}", code_.empty() ? std::string{"-"} : code_,
                roomPhaseName(phase_), roomPhaseName(next));
  phase_ = next;
  if (next == RoomPhase::Connected) {
    connectedOnce_ = true;
  } else if (next == RoomPhase::Ready) {
    notify("ready");
  } else if (next == RoomPhase::Playing) {
    notify("playing");
  } else if (next == RoomPhase::Closed && connectedOnce_) {
    // An open room leaves the list with its room; one whose peers met stays until told.
    notify("closed");
  }
  return true;
}

void RoomSession::notify(std::string state) {
  if (!desc_.hosting || token_.empty() || desc_.server.empty() || !desc_.open) {
    return;
  }
  notices_.push_back(std::move(state));
  poll();
}

void RoomSession::observe(const PeerLink& link) {
  if (phase_ == RoomPhase::Closed) {
    return;
  }
  if (code_.empty() && !link.roomCode().empty()) {
    code_ = link.roomCode();
    token_ = link.roomToken();
    if (desc_.hosting) {
      enter(RoomPhase::Open);
    }
  }
  if (link.state() == PeerState::Failed) {
    failure_ = link.failure();
    enter(RoomPhase::Closed);
    return;
  }
  if (link.peerFound() && (phase_ == RoomPhase::Connecting || phase_ == RoomPhase::Open)) {
    enter(RoomPhase::Connected);
  }
}

void RoomSession::markReady(bool ready) {
  enter(ready ? RoomPhase::Ready : RoomPhase::Connected);
}

void RoomSession::markPlaying() {
  enter(RoomPhase::Playing);
}

void RoomSession::close(std::string reason) {
  if (phase_ == RoomPhase::Closed) {
    return;
  }
  reason_ = std::move(reason);
  enter(RoomPhase::Closed);
}

void RoomSession::poll() {
  if (notice_ == nullptr) {
    if (notices_.empty()) {
      return;
    }
    noticeState_ = std::move(notices_.front());
    notices_.pop_front();
    notice_ = std::make_unique<SignalRequest>(
        desc_.open(stateUrl(desc_.server, code_, token_, noticeState_)));
  }
  notice_->poll();
  if (!notice_->done()) {
    return;
  }
  std::string error = notice_->failed() ? std::string{"unreachable"} : notice_->errorReason();
  // A room that already left the list (it expired, or its room removed it) is closed anyway.
  if (noticeState_ == "closed" && error == "no-room") {
    error.clear();
  }
  if (error.empty()) {
    sent_.push_back(noticeState_);
  } else {
    core::logWarning("rooms: the {} notice of room {} failed: {}", noticeState_, code_, error);
    noticeError_ = std::move(error);
  }
  notice_.reset();
  poll();
}

} // namespace dwe::net
