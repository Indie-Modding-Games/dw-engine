#include <dwe/net/lockstep.hpp>
#include <dwe/net/wire.hpp>

#include <algorithm>
#include <format>
#include <vector>

namespace dwe::net {

namespace {

// A peer may not send inputs further ahead than this (a broken or hostile peer would fill the
// memory otherwise).
constexpr std::uint32_t MaxAhead = 600;

} // namespace

Lockstep::Lockstep(std::size_t localSide, std::uint32_t inputDelay, std::uint32_t run)
    : local_{localSide & 1U}, delay_{inputDelay}, run_{run}, scheduled_{inputDelay},
      peerHas_{inputDelay}, remoteNext_{inputDelay} {}

void Lockstep::submit(std::uint32_t input) {
  inputs_.at(local_)[scheduled_++] = input;
}

Bytes Lockstep::inputs() const {
  const std::uint32_t count = std::min<std::uint32_t>(scheduled_ - peerHas_, MaxInputsPerMessage);
  WireWriter out;
  out.put(LockstepInputMessage)
      .put(run_)
      .put(remoteNext_)
      .put(peerHas_)
      .put(static_cast<std::uint8_t>(count));
  const auto& local = inputs_.at(local_);
  for (std::uint32_t tick = peerHas_; tick < peerHas_ + count; ++tick) {
    out.put(local.at(tick));
  }
  return out.take();
}

core::Result<bool> Lockstep::receive(std::span<const std::uint8_t> message) {
  if (message.empty() ||
      (message.front() != LockstepInputMessage && message.front() != LockstepChecksumMessage)) {
    return false;
  }
  WireReader in{message};
  const auto kind = in.get<std::uint8_t>();
  if (kind == LockstepInputMessage) {
    const auto run = in.get<std::uint32_t>();
    const auto acknowledged = in.get<std::uint32_t>();
    const auto first = in.get<std::uint32_t>();
    const auto count = in.get<std::uint8_t>();
    std::vector<std::uint32_t> values;
    for (std::uint8_t index = 0; index < count && in.ok(); ++index) {
      values.push_back(in.get<std::uint32_t>());
    }
    if (!in.done() || count > MaxInputsPerMessage) {
      return core::fail("a damaged lockstep inputs message");
    }
    if (run != run_) {
      return true;
    }
    if (acknowledged < delay_ || acknowledged > scheduled_ || first < delay_ ||
        first + count > tick_ + delay_ + MaxAhead) {
      return core::fail(std::format("a lockstep inputs message out of range (ticks {} to {})",
                                    first, first + count));
    }
    peerHas_ = std::max(peerHas_, acknowledged);
    auto& remote = inputs_.at(1 - local_);
    for (std::uint32_t index = 0; index < count; ++index) {
      const std::uint32_t tick = first + index;
      if (tick >= remoteNext_ || remote.contains(tick)) {
        remote[tick] = values.at(index);
      }
    }
    while (remote.contains(remoteNext_)) {
      ++remoteNext_;
    }
    forgetLocal();
    return true;
  }
  const auto tick = in.get<std::uint32_t>();
  const auto checksum = in.get<std::uint64_t>();
  if (!in.done()) {
    return core::fail(std::format("a damaged lockstep checksum for tick {}", tick));
  }
  remoteChecks_[tick] = checksum;
  compare(tick);
  return true;
}

std::optional<std::uint32_t> Lockstep::inputOf(std::size_t side, std::uint32_t tick) const {
  if (tick < delay_) {
    return 0;
  }
  const auto& inputs = inputs_.at(side);
  const auto found = inputs.find(tick);
  if (found == inputs.end()) {
    return std::nullopt;
  }
  return found->second;
}

std::optional<std::array<std::uint32_t, 2>> Lockstep::ready() const {
  if (desync_) {
    return std::nullopt;
  }
  const std::optional<std::uint32_t> first = inputOf(0, tick_);
  const std::optional<std::uint32_t> second = inputOf(1, tick_);
  if (!first || !second) {
    return std::nullopt;
  }
  return std::array{*first, *second};
}

Bytes Lockstep::played(std::uint64_t checksum) {
  const std::uint32_t tick = tick_++;
  inputs_.at(1 - local_).erase(tick);
  forgetLocal();
  localChecks_[tick] = checksum;
  compare(tick);
  WireWriter out;
  out.put(LockstepChecksumMessage).put(tick).put(checksum);
  return out.take();
}

void Lockstep::forgetLocal() {
  // A local input goes once it was played here and the peer holds it.
  auto& local = inputs_.at(local_);
  const std::uint32_t done = std::min(peerHas_, tick_);
  local.erase(local.begin(), local.lower_bound(done));
}

void Lockstep::compare(std::uint32_t tick) {
  const auto local = localChecks_.find(tick);
  const auto remote = remoteChecks_.find(tick);
  if (local == localChecks_.end() || remote == remoteChecks_.end()) {
    return;
  }
  if (local->second != remote->second && !desync_) {
    desync_ = tick;
  }
  localChecks_.erase(local);
  remoteChecks_.erase(remote);
}

} // namespace dwe::net
