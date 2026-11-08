#include <dwe/net/channel_peer_link.hpp>

#include <algorithm>
#include <utility>

namespace dwe::net {

namespace {

constexpr std::uint8_t ReliableTag = 0;
constexpr std::uint8_t UnreliableTag = 1;

Bytes tagged(std::uint8_t tag, std::span<const std::uint8_t> payload) {
  Bytes datagram;
  datagram.reserve(payload.size() + 1);
  datagram.push_back(tag);
  datagram.insert(datagram.end(), payload.begin(), payload.end());
  return datagram;
}

} // namespace

void ChannelPeerLink::ReliableLane::send(std::span<const std::uint8_t> datagram) {
  owner_->link_->send(tagged(ReliableTag, datagram));
}

std::optional<Bytes> ChannelPeerLink::ReliableLane::receive() {
  if (owner_->reliableIn_.empty()) {
    return std::nullopt;
  }
  Bytes datagram = std::move(owner_->reliableIn_.front());
  owner_->reliableIn_.pop_front();
  return datagram;
}

ChannelPeerLink::ChannelPeerLink(DatagramLink& link, std::string roomCode)
    : link_{&link}, roomCode_{std::move(roomCode)} {}

void ChannelPeerLink::send(Lane lane, std::span<const std::uint8_t> message) {
  if (lane == Lane::Unreliable) {
    link_->send(tagged(UnreliableTag, message));
    return;
  }
  // Only a message over the channel's limit fails, and the protocols above never send one.
  (void)channel_.send(message);
}

std::optional<Bytes> ChannelPeerLink::receive(Lane lane) {
  if (lane == Lane::Reliable) {
    return channel_.receive();
  }
  if (unreliableIn_.empty()) {
    return std::nullopt;
  }
  Bytes message = std::move(unreliableIn_.front());
  unreliableIn_.pop_front();
  return message;
}

void ChannelPeerLink::poll(Millis now) {
  while (std::optional<Bytes> datagram = link_->receive()) {
    if (datagram->empty()) {
      continue;
    }
    const std::uint8_t tag = datagram->front();
    Bytes payload{datagram->begin() + 1, datagram->end()};
    if (tag == ReliableTag) {
      reliableIn_.push_back(std::move(payload));
    } else if (tag == UnreliableTag) {
      lastUnreliable_ = now;
      unreliableIn_.push_back(std::move(payload));
    }
  }
  channel_.poll(now);
}

std::optional<Millis> ChannelPeerLink::lastHeard() const {
  const std::optional<Millis> reliable = channel_.lastHeard();
  if (!reliable || !lastUnreliable_) {
    return reliable ? reliable : lastUnreliable_;
  }
  return std::max(*reliable, *lastUnreliable_);
}

} // namespace dwe::net
