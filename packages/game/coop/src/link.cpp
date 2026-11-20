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
  case RejectReason::Closed:
    return LinkFailure::RoomClosed;
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
      std::holds_alternative<ViewState>(message) || std::holds_alternative<BattleFrame>(message) ||
      std::holds_alternative<PlayerState>(message) || std::holds_alternative<SyncReport>(message))
    return net::Lane::Unreliable;
  return net::Lane::Reliable;
}

bool directionAllowed(Side receiver, const Message& message) {
  if (std::holds_alternative<Leave>(message))
    return true;
  if (receiver == Side::Host) {
    // The guest's input and its own player's transform; whether that player is the guest's own is
    // the authority's to judge (WorldAuthority::accept).
    if (std::holds_alternative<Commands>(message) || std::holds_alternative<ViewState>(message) ||
        std::holds_alternative<PlayerState>(message))
      return true;
    if (std::holds_alternative<Ready>(message) || std::holds_alternative<SyncReport>(message))
      return true;
    // What the guest's game changed of the shared world, and its takes, asked of the host (#357).
    if (std::holds_alternative<WorldAsk>(message) || std::holds_alternative<ItemClaim>(message))
      return true;
    // A battle the guest's partner fights, its part in any battle and whether its partner can join
    // the host's.
    if (std::holds_alternative<BattleAsk>(message) ||
        std::holds_alternative<BattleAction>(message) ||
        std::holds_alternative<BattleOffer>(message))
      return true;
    // A trade's asks, answers, offers and replies; the host referees them (trade.hpp).
    if (std::holds_alternative<TradeAsk>(message) || std::holds_alternative<TradeAnswer>(message) ||
        std::holds_alternative<TradeOffer>(message) ||
        std::holds_alternative<TradeConfirm>(message) ||
        std::holds_alternative<TradeCancel>(message) || std::holds_alternative<TradeReply>(message))
      return true;
    if (const auto* chunk = std::get_if<ProfileChunk>(&message))
      return chunk->kind == ProfilePayloadKind::GuestSave;
    return false;
  }
  // The shared world's state comes from the host only (#353): a guest that sends any of these
  // loses its link.
  if (std::holds_alternative<JoinState>(message) || std::holds_alternative<Snapshot>(message) ||
      std::holds_alternative<PlayerCorrection>(message) ||
      std::holds_alternative<Roster>(message) || std::holds_alternative<Lobby>(message) ||
      std::holds_alternative<SyncWatch>(message))
    return true;
  if (std::holds_alternative<WorldChange>(message) || std::holds_alternative<WorldSync>(message) ||
      std::holds_alternative<GroundItems>(message) || std::holds_alternative<ClaimAnswer>(message))
    return true;
  if (std::holds_alternative<BattleBegin>(message) ||
      std::holds_alternative<BattleRefused>(message) ||
      std::holds_alternative<BattleFrame>(message) || std::holds_alternative<BattleEnd>(message))
    return true;
  if (std::holds_alternative<TradeState>(message) ||
      std::holds_alternative<TradePrepare>(message) ||
      std::holds_alternative<TradeCommit>(message) ||
      std::holds_alternative<TradeRevert>(message) || std::holds_alternative<TradeClosed>(message))
    return true;
  if (const auto* chunk = std::get_if<ProfileChunk>(&message))
    return chunk->kind == ProfilePayloadKind::HostSave;
  return false;
}
} // namespace

CoopLink::CoopLink(net::PeerLink& link, Side side, Identity identity, LinkTimeouts timeouts)
    : link_{&link}, side_{side}, identity_{std::move(identity)}, timeouts_{timeouts} {
  link.keepAliveWhileBusy(timeouts_.busy);
}

CoopLink CoopLink::host(net::PeerLink& link, Identity identity, std::string playerName,
                        LinkTimeouts timeouts, PlayerId guestId) {
  CoopLink result{link, Side::Host, std::move(identity), timeouts};
  result.playerName_ = std::move(playerName);
  result.guestId_ = validGuest(guestId) ? guestId : GuestPlayerId;
  return result;
}

void CoopLink::assignGuest(PlayerId id) {
  if (side_ == Side::Host && phase_ == LinkPhase::Connecting && validGuest(id)) {
    guestId_ = id;
  }
}

CoopLink CoopLink::guest(net::PeerLink& link, Identity identity, std::string playerName,
                         LinkTimeouts timeouts, std::uint64_t rejoinKey) {
  CoopLink result{link, Side::Guest, std::move(identity), timeouts};
  link.send(net::Lane::Reliable, encode(Message{Hello{.identity = result.identity_,
                                                      .playerName = std::move(playerName),
                                                      .rejoinKey = rejoinKey}}));
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
    RejectReason reason = identityMismatch(identity_, hello->identity);
    if (reason == RejectReason::None && admission_) {
      // The room decides the seat (#356): a returning guest's own, a new one's, or a refusal.
      const Admission admission = admission_(*hello);
      reason = admission.refused;
      if (reason == RejectReason::None && validGuest(admission.id)) {
        guestId_ = admission.id;
        seatKey_ = admission.seatKey;
      } else if (reason == RejectReason::None) {
        reason = RejectReason::RoomFull;
      }
    }
    const bool accepted = reason == RejectReason::None;
    const std::uint64_t sessionId = accepted ? nextSessionId() : 0;
    link_->send(net::Lane::Reliable,
                encode(Message{Welcome{.identity = identity_,
                                       .accepted = accepted,
                                       .assignedPlayer = guestId_,
                                       .reason = reason,
                                       .sessionId = sessionId,
                                       .hostName = accepted ? playerName_ : std::string{},
                                       .seatKey = accepted ? seatKey_ : 0}}));
    if (accepted) {
      remotePlayer_ = guestId_;
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
        !validGuest(welcome->assignedPlayer) || welcome->sessionId == 0) {
      fail(LinkFailure::Damaged);
      return;
    }
    remotePlayer_ = HostPlayerId;
    localPlayer_ = welcome->assignedPlayer;
    remoteName_ = welcome->hostName;
    seatKey_ = welcome->seatKey;
    phase_ = LinkPhase::Ready;
    return;
  }
  if (const auto* leaveMessage = std::get_if<Leave>(&message)) {
    leaveReason_ = leaveMessage->reason;
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
