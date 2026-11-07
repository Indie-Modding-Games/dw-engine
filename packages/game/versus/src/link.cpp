#include <dwe/game/versus/link.hpp>

#include <format>
#include <utility>

namespace dwe::game::versus {

namespace {

std::size_t indexOf(Side side) {
  return static_cast<std::size_t>(side);
}

Side otherOf(Side side) {
  return side == Side::Host ? Side::Guest : Side::Host;
}

bool isLockstep(const net::Bytes& message) {
  return !message.empty() && (message.front() == net::LockstepInputMessage ||
                              message.front() == net::LockstepChecksumMessage);
}

LinkFailure failureOf(LeaveReason reason) {
  switch (reason) {
  case LeaveReason::Quit:
    return LinkFailure::Left;
  case LeaveReason::ProtocolMismatch:
    return LinkFailure::ProtocolMismatch;
  case LeaveReason::VersionMismatch:
    return LinkFailure::VersionMismatch;
  case LeaveReason::ContentMismatch:
    return LinkFailure::ContentMismatch;
  case LeaveReason::BadLineup:
    return LinkFailure::BadLineup;
  case LeaveReason::Desync:
    return LinkFailure::Desync;
  }
  return LinkFailure::Left;
}

} // namespace

VersusLink::VersusLink(net::DatagramLink& link, Side side, Identity identity, LinkTimeouts timeouts)
    : channel_{std::make_unique<net::ReliableChannel>(link)}, side_{side},
      identity_{std::move(identity)}, timeouts_{timeouts} {}

VersusLink VersusLink::host(net::DatagramLink& link, Identity identity, SeriesSetup setup,
                            LinkTimeouts timeouts) {
  VersusLink versus{link, Side::Host, std::move(identity), timeouts};
  versus.setup_ = setup;
  versus.series_ = Series{setup.battles};
  return versus;
}

VersusLink VersusLink::guest(net::DatagramLink& link, Identity identity, LinkTimeouts timeouts) {
  VersusLink versus{link, Side::Guest, std::move(identity), timeouts};
  versus.send(Hello{.identity = versus.identity_});
  return versus;
}

void VersusLink::poll(net::Millis now) {
  now_ = now;
  if (!started_) {
    started_ = now;
  }
  channel_->poll(now);
  while (std::optional<net::Bytes> message = channel_->receive()) {
    if (phase_ == LinkPhase::Failed) {
      continue;
    }
    take(*message);
  }
  if (phase_ == LinkPhase::Failed || phase_ == LinkPhase::Finished) {
    return;
  }
  const std::optional<net::Millis> heard = channel_->lastHeard();
  if (!heard) {
    if (side_ == Side::Guest && now - *started_ >= timeouts_.answer) {
      fail(LinkFailure::NoAnswer);
    }
    return;
  }
  if (now - *heard >= timeouts_.silence) {
    fail(LinkFailure::Silence);
  }
}

core::Result<void> VersusLink::sendLineup(std::vector<VersusFighter> fighters) {
  if (phase_ != LinkPhase::Lineups || lineups_.at(indexOf(side_))) {
    return core::fail("the versus lineup goes once, after the welcome");
  }
  if (static_cast<std::int32_t>(fighters.size()) != setup_->battles) {
    return core::fail(std::format("the series has {} battles, not {} partners", setup_->battles,
                                  fighters.size()));
  }
  send(Lineup{.fighters = fighters});
  lineups_.at(indexOf(side_)) = std::move(fighters);
  beginPlayingIfReady();
  return {};
}

core::Result<void> VersusLink::startRound() {
  if (phase_ != LinkPhase::Playing || lockstep_ || localResult_ || series_.finished()) {
    return core::fail("no versus battle can start now");
  }
  lockstep_.emplace(indexOf(side_), setup_->inputDelay);
  std::deque<net::Bytes> waiting = std::exchange(nextRound_, {});
  for (const net::Bytes& message : waiting) {
    take(message);
  }
  return {};
}

void VersusLink::submitInput(std::uint32_t input) {
  if (phase_ == LinkPhase::Playing && lockstep_ && !localResult_ && lockstep_->wantsInput()) {
    if (!channel_->send(lockstep_->submit(input))) {
      fail(LinkFailure::Damaged);
    }
  }
}

void VersusLink::playedTick(std::uint64_t checksum) {
  if (phase_ != LinkPhase::Playing || !lockstep_) {
    return;
  }
  if (!channel_->send(lockstep_->played(checksum))) {
    fail(LinkFailure::Damaged);
    return;
  }
  if (lockstep_->desyncAt()) {
    fail(LinkFailure::Desync, LeaveReason::Desync);
  }
}

void VersusLink::finishRound(RoundResult result, std::uint64_t checksum) {
  if (phase_ != LinkPhase::Playing || !lockstep_ || localResult_) {
    return;
  }
  localResult_ = RoundOver{
      .round = series_.round(), .result = static_cast<std::int32_t>(result), .checksum = checksum};
  send(*localResult_);
  settleRound();
}

void VersusLink::leave(LeaveReason reason) {
  if (phase_ != LinkPhase::Failed) {
    fail(LinkFailure::Left, reason);
  }
}

void VersusLink::send(const Message& message) {
  if (!channel_->send(encode(message))) {
    fail(LinkFailure::Damaged);
  }
}

void VersusLink::fail(LinkFailure failure, std::optional<LeaveReason> tell) {
  if (phase_ == LinkPhase::Failed) {
    return;
  }
  if (tell) {
    send(Leave{.reason = *tell});
    // It goes now: the screens may stop polling soon after.
    channel_->poll(now_);
  }
  phase_ = LinkPhase::Failed;
  failure_ = failure;
  lockstep_.reset();
}

void VersusLink::take(const net::Bytes& message) {
  if (isLockstep(message)) {
    if (!lockstep_ || remoteRoundOver_) {
      nextRound_.push_back(message);
      return;
    }
    if (!lockstep_->receive(message)) {
      fail(LinkFailure::Damaged);
      return;
    }
    if (lockstep_->desyncAt()) {
      fail(LinkFailure::Desync, LeaveReason::Desync);
    }
    return;
  }
  auto decoded = decode(message);
  if (!decoded || !*decoded) {
    fail(LinkFailure::Damaged);
    return;
  }
  handle(**decoded);
}

void VersusLink::handle(const Message& message) {
  if (const auto* hello = std::get_if<Hello>(&message)) {
    if (side_ != Side::Host || phase_ != LinkPhase::Connecting) {
      return;
    }
    peer_ = hello->identity;
    checkIdentity(hello->identity);
    if (phase_ != LinkPhase::Failed) {
      send(Welcome{.identity = identity_, .setup = *setup_});
      phase_ = LinkPhase::Lineups;
    }
  } else if (const auto* welcome = std::get_if<Welcome>(&message)) {
    if (side_ != Side::Guest || phase_ != LinkPhase::Connecting) {
      return;
    }
    peer_ = welcome->identity;
    checkIdentity(welcome->identity);
    if (phase_ != LinkPhase::Failed) {
      setup_ = welcome->setup;
      series_ = Series{welcome->setup.battles};
      phase_ = LinkPhase::Lineups;
    }
  } else if (const auto* lineup = std::get_if<Lineup>(&message)) {
    auto& theirs = lineups_.at(indexOf(otherOf(side_)));
    if (phase_ != LinkPhase::Lineups || theirs) {
      return;
    }
    if (static_cast<std::int32_t>(lineup->fighters.size()) != setup_->battles) {
      fail(LinkFailure::BadLineup, LeaveReason::BadLineup);
      return;
    }
    theirs = lineup->fighters;
    beginPlayingIfReady();
  } else if (const auto* over = std::get_if<RoundOver>(&message)) {
    remoteResult_ = *over;
    remoteRoundOver_ = true;
    settleRound();
  } else if (const auto* left = std::get_if<Leave>(&message)) {
    fail(failureOf(left->reason));
  }
}

void VersusLink::checkIdentity(const Identity& other) {
  if (other.protocol != identity_.protocol) {
    fail(LinkFailure::ProtocolMismatch, LeaveReason::ProtocolMismatch);
  } else if (other.gameVersion != identity_.gameVersion) {
    fail(LinkFailure::VersionMismatch, LeaveReason::VersionMismatch);
  } else if (other.contentHash != identity_.contentHash) {
    fail(LinkFailure::ContentMismatch, LeaveReason::ContentMismatch);
  }
}

void VersusLink::settleRound() {
  if (!localResult_ || !remoteResult_) {
    return;
  }
  const RoundOver local = *std::exchange(localResult_, std::nullopt);
  const RoundOver remote = *std::exchange(remoteResult_, std::nullopt);
  lockstep_.reset();
  remoteRoundOver_ = false;
  if (local.round != remote.round || local.result != remote.result ||
      local.checksum != remote.checksum) {
    fail(LinkFailure::Desync, LeaveReason::Desync);
    return;
  }
  series_.record(static_cast<RoundResult>(local.result));
  if (series_.finished()) {
    phase_ = LinkPhase::Finished;
  }
}

void VersusLink::beginPlayingIfReady() {
  if (lineups_[0] && lineups_[1]) {
    phase_ = LinkPhase::Playing;
  }
}

} // namespace dwe::game::versus
