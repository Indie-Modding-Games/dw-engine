#include <dwe/net/reliable_channel.hpp>
#include <dwe/net/wire.hpp>

#include <format>
#include <utility>

namespace dwe::net {

namespace {

// "DW" and the channel's format: a datagram without them is not ours.
constexpr std::uint16_t Magic = 0x5744;
constexpr std::uint8_t Format = 1;
constexpr std::uint8_t DataKind = 1;
constexpr std::uint8_t AckKind = 2;
// The mask acknowledges the 32 messages after the last one in order.
constexpr std::uint32_t MaskWidth = 32;
// A message this far ahead of the next expected one is not kept (a peer gone wrong).
constexpr std::uint32_t Window = 4096;
// At most this many messages go again on one poll, so a long silence does not flood the link.
constexpr std::size_t ResendBurst = 64;

} // namespace

ReliableChannel::ReliableChannel(DatagramLink& link, ChannelOptions options)
    : link_{&link}, options_{options} {}

core::Result<void> ReliableChannel::send(std::span<const std::uint8_t> message) {
  if (message.size() > MaxMessage) {
    return core::fail(std::format("a network message of {} bytes is over the {} a datagram holds",
                                  message.size(), MaxMessage));
  }
  outgoing_.emplace(nextSequence_++,
                    Outgoing{.message = Bytes{message.begin(), message.end()}, .sentAt = {}});
  return {};
}

void ReliableChannel::poll(Millis now) {
  while (std::optional<Bytes> datagram = link_->receive()) {
    take(*datagram, now);
  }
  std::size_t burst = 0;
  bool sentData = false;
  for (auto& [sequence, outgoing] : outgoing_) {
    const bool fresh = !outgoing.sentAt;
    if (!fresh && now - *outgoing.sentAt < options_.resendAfter) {
      continue;
    }
    if (burst++ >= ResendBurst) {
      break;
    }
    if (!fresh) {
      ++resent_;
    }
    outgoing.sentAt = now;
    sendDatagram(sequence, outgoing.message, now);
    sentData = true;
  }
  if (!sentData && (ackOwed_ || now - lastSent_ >= options_.keepAliveEvery)) {
    sendDatagram(std::nullopt, {}, now);
  }
}

std::optional<Bytes> ReliableChannel::receive() {
  if (inbox_.empty()) {
    return std::nullopt;
  }
  Bytes message = std::move(inbox_.front());
  inbox_.pop_front();
  return message;
}

void ReliableChannel::sendDatagram(std::optional<std::uint32_t> sequence,
                                   std::span<const std::uint8_t> payload, Millis now) {
  std::uint32_t mask = 0;
  for (std::uint32_t bit = 0; bit < MaskWidth; ++bit) {
    if (early_.contains(nextExpected_ + 1 + bit)) {
      mask |= 1U << bit;
    }
  }
  WireWriter out;
  out.put(Magic).put(Format).put(sequence ? DataKind : AckKind).put(nextExpected_ - 1).put(mask);
  if (sequence) {
    out.put(*sequence).put(payload);
  } else {
    out.put(std::uint32_t{0});
  }
  link_->send(out.bytes());
  lastSent_ = now;
  ackOwed_ = false;
}

void ReliableChannel::take(std::span<const std::uint8_t> datagram, Millis now) {
  WireReader in{datagram};
  const auto magic = in.get<std::uint16_t>();
  const auto format = in.get<std::uint8_t>();
  const auto kind = in.get<std::uint8_t>();
  const auto ack = in.get<std::uint32_t>();
  const auto mask = in.get<std::uint32_t>();
  const auto sequence = in.get<std::uint32_t>();
  if (!in.ok() || magic != Magic || format != Format || (kind != DataKind && kind != AckKind)) {
    return;
  }
  lastHeard_ = now;
  acknowledged(ack, mask);
  if (kind != DataKind) {
    return;
  }
  ackOwed_ = true;
  if (sequence < nextExpected_ || sequence - nextExpected_ >= Window) {
    return;
  }
  const std::span<const std::uint8_t> payload = in.rest();
  early_.try_emplace(sequence, Bytes{payload.begin(), payload.end()});
  for (auto next = early_.find(nextExpected_); next != early_.end();
       next = early_.find(nextExpected_)) {
    inbox_.push_back(std::move(next->second));
    early_.erase(next);
    ++nextExpected_;
  }
}

void ReliableChannel::acknowledged(std::uint32_t upTo, std::uint32_t mask) {
  outgoing_.erase(outgoing_.begin(), outgoing_.upper_bound(upTo));
  for (std::uint32_t bit = 0; bit < MaskWidth; ++bit) {
    if ((mask & (1U << bit)) != 0) {
      outgoing_.erase(upTo + 2 + bit);
    }
  }
}

} // namespace dwe::net
