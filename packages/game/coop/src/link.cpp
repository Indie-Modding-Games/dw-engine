#include <dwe/game/coop/link.hpp>

#include <atomic>

namespace dwe::game::coop {

namespace {
constexpr net::Millis CommandTimeout{250};

LinkFailure failureFor(RejectReason reason) {
  switch (reason) {
  case RejectReason::None:
    return LinkFailure::None;
  case RejectReason::ProtocolMismatch:
    return LinkFailure::ProtocolMismatch;
  case RejectReason::VersionMismatch:
    return LinkFailure::VersionMismatch;
  case RejectReason::ContentMismatch:
    return LinkFailure::ContentMismatch;
  case RejectReason::RoomFull:
    return LinkFailure::RoomFull;
  case RejectReason::Busy:
    return LinkFailure::Busy;
  }
  return LinkFailure::Damaged;
}

std::uint64_t nextSessionId() {
  static std::atomic<std::uint64_t> next{1};
  return next.fetch_add(1, std::memory_order_relaxed);
}

RejectReason identityMismatch(const Identity& expected, const Identity& actual) {
  if (expected.protocol != actual.protocol)
    return RejectReason::ProtocolMismatch;
  if (expected.gameVersion != actual.gameVersion)
    return RejectReason::VersionMismatch;
  if (expected.contentHash != actual.contentHash)
    return RejectReason::ContentMismatch;
  return RejectReason::None;
}

net::Lane laneFor(const Message& message) {
  if (std::holds_alternative<Commands>(message) || std::holds_alternative<Snapshot>(message) ||
      std::holds_alternative<ViewState>(message))
    return net::Lane::Unreliable;
  return net::Lane::Reliable;
}

bool directionAllowed(Side receiver, const Message& message) {
  if (std::holds_alternative<Leave>(message))
    return true;
  if (receiver == Side::Host) {
    if (std::holds_alternative<Commands>(message) || std::holds_alternative<ViewState>(message))
      return true;
    if (std::holds_alternative<Ready>(message))
      return true;
    if (const auto* chunk = std::get_if<ProfileChunk>(&message))
      return chunk->kind == ProfilePayloadKind::GuestSave;
    return false;
  }
  if (std::holds_alternative<JoinState>(message) || std::holds_alternative<Snapshot>(message))
    return true;
  if (const auto* chunk = std::get_if<ProfileChunk>(&message))
    return chunk->kind == ProfilePayloadKind::HostSave;
  return false;
}
} // namespace

CoopLink::CoopLink(net::PeerLink& link, Side side, Identity identity, LinkTimeouts timeouts)
    : link_{&link}, side_{side}, identity_{std::move(identity)}, timeouts_{timeouts} {}

CoopLink CoopLink::host(net::PeerLink& link, Identity identity, LinkTimeouts timeouts) {
  return CoopLink{link, Side::Host, std::move(identity), timeouts};
}

CoopLink CoopLink::guest(net::PeerLink& link, Identity identity, std::string playerName,
                         LinkTimeouts timeouts) {
  CoopLink result{link, Side::Guest, std::move(identity), timeouts};
  link.send(net::Lane::Reliable, encode(Message{Hello{.identity = result.identity_,
                                                      .playerName = std::move(playerName)}}));
  return result;
}

void CoopLink::poll(net::Millis now) {
  now_ = now;
  link_->poll(now);
  for (const auto lane : {net::Lane::Reliable, net::Lane::Unreliable}) {
    while (auto bytes = link_->receive(lane)) {
      auto decoded = decode(*bytes);
      if (!decoded || !*decoded) {
        fail(LinkFailure::Damaged);
        continue;
      }
      handle(**decoded);
    }
  }
  if (link_->state() == net::PeerState::Failed) {
    fail(LinkFailure::Disconnected);
    return;
  }
  if (link_->state() == net::PeerState::Open && !opened_)
    opened_ = now;
  if (!opened_ || phase_ == LinkPhase::Rejected || phase_ == LinkPhase::Failed ||
      phase_ == LinkPhase::Left)
    return;
  if (phase_ == LinkPhase::Connecting && now - *opened_ >= timeouts_.answer) {
    fail(LinkFailure::NoAnswer);
    return;
  }
  if (phase_ == LinkPhase::Ready) {
    const auto heard = link_->lastHeard();
    if (heard && now - *heard >= timeouts_.silence)
      fail(LinkFailure::Silence);
  }
}

void CoopLink::handle(const Message& message) {
  if (const auto* hello = std::get_if<Hello>(&message)) {
    if (side_ != Side::Host || phase_ != LinkPhase::Connecting) {
      fail(LinkFailure::Damaged, true);
      return;
    }
    const RejectReason reason = identityMismatch(identity_, hello->identity);
    const bool accepted = reason == RejectReason::None;
    const std::uint64_t sessionId = accepted ? nextSessionId() : 0;
    link_->send(net::Lane::Reliable, encode(Message{Welcome{.identity = identity_,
                                                            .accepted = accepted,
                                                            .assignedPlayer = GuestPlayerId,
                                                            .reason = reason,
                                                            .sessionId = sessionId}}));
    if (accepted) {
      remotePlayer_ = GuestPlayerId;
      remoteName_ = hello->playerName;
      phase_ = LinkPhase::Ready;
    } else {
      failure_ = failureFor(reason);
      phase_ = LinkPhase::Rejected;
    }
    return;
  }
  if (const auto* welcome = std::get_if<Welcome>(&message)) {
    if (side_ != Side::Guest || phase_ != LinkPhase::Connecting) {
      fail(LinkFailure::Damaged);
      return;
    }
    if (!welcome->accepted) {
      failure_ = failureFor(welcome->reason);
      phase_ = LinkPhase::Rejected;
      return;
    }
    if (identityMismatch(identity_, welcome->identity) != RejectReason::None ||
        welcome->assignedPlayer != GuestPlayerId || welcome->sessionId == 0) {
      fail(LinkFailure::Damaged);
      return;
    }
    remotePlayer_ = HostPlayerId;
    phase_ = LinkPhase::Ready;
    return;
  }
  if (const auto* leaveMessage = std::get_if<Leave>(&message)) {
    (void)leaveMessage;
    phase_ = LinkPhase::Left;
    return;
  }
  if (phase_ == LinkPhase::Ready && directionAllowed(side_, message))
    incoming_.push_back(message);
  else
    fail(LinkFailure::Damaged, true);
}

std::optional<Message> CoopLink::receive() {
  if (incoming_.empty())
    return std::nullopt;
  Message message = std::move(incoming_.front());
  incoming_.pop_front();
  return message;
}

core::Result<void> CoopLink::send(const Message& message) {
  if (phase_ != LinkPhase::Ready)
    return core::fail("cooperative link is not ready");
  if (!directionAllowed(side_ == Side::Host ? Side::Guest : Side::Host, message))
    return core::fail("cooperative message is invalid for this side");
  const net::Lane lane = laneFor(message);
  const net::Bytes bytes = encode(message);
  const auto validated = decode(bytes);
  if (!validated || !validated->has_value())
    return core::fail("cooperative message does not satisfy the wire protocol");
  if (lane == net::Lane::Unreliable && bytes.size() + 1 > net::MaxDatagram)
    return core::fail("cooperative message exceeds the unreliable datagram limit");
  link_->send(lane, bytes);
  return {};
}

void CoopLink::leave(LeaveReason reason) {
  if (phase_ == LinkPhase::Ready) {
    link_->send(net::Lane::Reliable, encode(Message{Leave{reason}}));
    link_->poll(now_);
  }
  phase_ = LinkPhase::Left;
}

void CoopLink::fail(LinkFailure failure, bool sendLeave) {
  if (sendLeave && phase_ == LinkPhase::Ready) {
    link_->send(net::Lane::Reliable, encode(Message{Leave{LeaveReason::SessionEnded}}));
    link_->poll(now_);
  }
  failure_ = failure;
  phase_ = LinkPhase::Failed;
}

bool RemoteCommandBuffer::push(const Commands& command, net::Millis receivedAt) {
  if (sequence_) {
    const std::uint32_t delta = command.sequence - *sequence_;
    if (delta == 0 || delta >= 0x80000000u)
      return false;
  }
  sequence_ = command.sequence;
  command_ = command.field;
  receivedAt_ = receivedAt;
  return true;
}

FieldCommand RemoteCommandBuffer::current(net::Millis now) const {
  if (!sequence_ || now - receivedAt_ > CommandTimeout)
    return {};
  return command_;
}

void RemoteCommandBuffer::reset() {
  sequence_.reset();
  command_ = {};
  receivedAt_ = net::Millis{0};
}

} // namespace dwe::game::coop
