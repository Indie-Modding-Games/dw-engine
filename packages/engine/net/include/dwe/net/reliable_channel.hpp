#pragma once

#include <dwe/core/error.hpp>
#include <dwe/net/datagram.hpp>

#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <span>

namespace dwe::net {

struct ChannelOptions {
  // A message not acknowledged after this long is sent again.
  Millis resendAfter{120};
  // With nothing to send for this long, a bare acknowledgement goes, so the peer knows this side is
  // still there.
  Millis keepAliveEvery{250};
};

// Messages over an unreliable datagram link, each delivered once and in the order sent: every
// message carries a sequence number, the peer acknowledges what it holds (the last one in order
// and a mask of the 32 after it) and what is not acknowledged in time goes again. One message per
// datagram, so a message is at most MaxMessage bytes.
class ReliableChannel {
public:
  static constexpr std::size_t HeaderSize = 16;
  static constexpr std::size_t MaxMessage = MaxDatagram - HeaderSize;

  explicit ReliableChannel(DatagramLink& link, ChannelOptions options = {});

  // Queues a message; it leaves on the next poll(). Fails for one over MaxMessage.
  core::Result<void> send(std::span<const std::uint8_t> message);
  // Reads what arrived, acknowledges it, and sends what is new or due again.
  void poll(Millis now);
  // The next message in order, if it arrived.
  std::optional<Bytes> receive();

  // Whether anything valid came from the peer yet, and when it last did.
  [[nodiscard]] bool heardAny() const { return lastHeard_.has_value(); }
  [[nodiscard]] std::optional<Millis> lastHeard() const { return lastHeard_; }
  // Messages sent and not acknowledged yet.
  [[nodiscard]] std::size_t unacknowledged() const { return outgoing_.size(); }
  // Datagrams that went again.
  [[nodiscard]] std::size_t resent() const { return resent_; }

private:
  struct Outgoing {
    Bytes message;
    std::optional<Millis> sentAt;
  };
  void sendDatagram(std::optional<std::uint32_t> sequence, std::span<const std::uint8_t> payload,
                    Millis now);
  void take(std::span<const std::uint8_t> datagram, Millis now);
  void acknowledged(std::uint32_t upTo, std::uint32_t mask);

  DatagramLink* link_;
  ChannelOptions options_;
  // Sequence numbers start at 1; 0 acknowledges nothing.
  std::uint32_t nextSequence_ = 1;
  std::map<std::uint32_t, Outgoing> outgoing_;
  std::uint32_t nextExpected_ = 1;
  std::map<std::uint32_t, Bytes> early_;
  std::deque<Bytes> inbox_;
  bool ackOwed_ = false;
  std::optional<Millis> lastHeard_;
  Millis lastSent_{0};
  std::size_t resent_ = 0;
};

} // namespace dwe::net
