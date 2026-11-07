#pragma once

#include <dwe/core/error.hpp>
#include <dwe/net/datagram.hpp>

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>

namespace dwe::net {

// The first byte of the lockstep's messages; the other protocols on the same channel use others.
inline constexpr std::uint8_t LockstepInputMessage = 0x40;
inline constexpr std::uint8_t LockstepChecksumMessage = 0x41;

// A deterministic simulation run on two machines in step: each side sends only its own inputs,
// each scheduled `inputDelay` ticks ahead so it has time to arrive, and a tick is played once both
// inputs of it are known. After each tick both sides send a checksum of their state; the first
// tick where they differ is a desync, which ends the run. The ticks before the delay have no input
// (0) on either side. It sends nothing itself: its messages go over a reliable ordered channel.
class Lockstep {
public:
  Lockstep(std::size_t localSide, std::uint32_t inputDelay);

  // Whether the local input of one more tick may be given now: no more than the delay ahead of the
  // tick to play.
  [[nodiscard]] bool wantsInput() const { return scheduled_ <= tick_ + delay_; }
  // The local input of the next tick it is due for; the message carries it to the peer.
  Bytes submit(std::uint32_t input);
  // A message of the peer: false when it is not a lockstep message (another protocol's), an error
  // when it is one and is damaged.
  core::Result<bool> receive(std::span<const std::uint8_t> message);
  // The two inputs of the tick to play, by side, once both are known.
  [[nodiscard]] std::optional<std::array<std::uint32_t, 2>> ready() const;
  // The tick to play was played and left the state with `checksum`; the message tells the peer.
  Bytes played(std::uint64_t checksum);

  [[nodiscard]] std::uint32_t tick() const { return tick_; }
  [[nodiscard]] std::uint32_t inputDelay() const { return delay_; }
  [[nodiscard]] std::size_t localSide() const { return local_; }
  // The first tick whose checksums differed.
  [[nodiscard]] const std::optional<std::uint32_t>& desyncAt() const { return desync_; }
  // Ticks played whose peer checksum has not arrived yet.
  [[nodiscard]] std::size_t uncheckedTicks() const { return localChecks_.size(); }

private:
  [[nodiscard]] std::optional<std::uint32_t> inputOf(std::size_t side, std::uint32_t tick) const;
  void compare(std::uint32_t tick);

  std::size_t local_;
  std::uint32_t delay_;
  std::uint32_t tick_ = 0;
  std::uint32_t scheduled_;
  std::array<std::map<std::uint32_t, std::uint32_t>, 2> inputs_;
  std::map<std::uint32_t, std::uint64_t> localChecks_;
  std::map<std::uint32_t, std::uint64_t> remoteChecks_;
  std::optional<std::uint32_t> desync_;
};

} // namespace dwe::net
