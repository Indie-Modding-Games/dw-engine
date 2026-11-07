#include <dwe/net/lockstep.hpp>
#include <dwe/net/wire.hpp>

#include <format>

namespace dwe::net {

namespace {

// A peer may not send inputs further ahead than this (a broken or hostile peer would fill the
// memory otherwise).
constexpr std::uint32_t MaxAhead = 600;

} // namespace

Lockstep::Lockstep(std::size_t localSide, std::uint32_t inputDelay)
    : local_{localSide & 1U}, delay_{inputDelay}, scheduled_{inputDelay} {}

Bytes Lockstep::submit(std::uint32_t input) {
  const std::uint32_t tick = scheduled_++;
  inputs_.at(local_)[tick] = input;
  WireWriter out;
  out.put(LockstepInputMessage).put(tick).put(input);
  return out.take();
}

core::Result<bool> Lockstep::receive(std::span<const std::uint8_t> message) {
  if (message.empty() ||
      (message.front() != LockstepInputMessage && message.front() != LockstepChecksumMessage)) {
    return false;
  }
  WireReader in{message};
  const auto kind = in.get<std::uint8_t>();
  const auto tick = in.get<std::uint32_t>();
  if (kind == LockstepInputMessage) {
    const auto input = in.get<std::uint32_t>();
    if (!in.done() || tick < delay_ || tick > tick_ + delay_ + MaxAhead) {
      return core::fail(std::format("a damaged lockstep input for tick {}", tick));
    }
    inputs_.at(1 - local_)[tick] = input;
    return true;
  }
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
  inputs_.at(0).erase(tick);
  inputs_.at(1).erase(tick);
  localChecks_[tick] = checksum;
  compare(tick);
  WireWriter out;
  out.put(LockstepChecksumMessage).put(tick).put(checksum);
  return out.take();
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
