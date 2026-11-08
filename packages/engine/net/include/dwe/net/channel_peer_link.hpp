#pragma once

#include <dwe/net/datagram.hpp>
#include <dwe/net/peer_link.hpp>
#include <dwe/net/reliable_channel.hpp>

#include <deque>
#include <optional>
#include <string>

namespace dwe::net {

// A peer link over one datagram link: the reliable lane is a ReliableChannel and the unreliable
// one goes as it is, each datagram tagged with its lane. It is open from the start (the datagram
// link is already there); the tests use it over the loopback network.
class ChannelPeerLink final : public PeerLink {
public:
  explicit ChannelPeerLink(DatagramLink& link, std::string roomCode = {});

  void send(Lane lane, std::span<const std::uint8_t> message) override;
  std::optional<Bytes> receive(Lane lane) override;
  void poll(Millis now) override;
  [[nodiscard]] PeerState state() const override { return PeerState::Open; }
  [[nodiscard]] PeerFailure failure() const override { return PeerFailure::None; }
  [[nodiscard]] std::optional<Millis> lastHeard() const override;
  [[nodiscard]] std::string roomCode() const override { return roomCode_; }

  [[nodiscard]] const ReliableChannel& channel() const { return channel_; }

private:
  // The reliable lane's side of the datagram link: what the channel sends gets the lane's tag,
  // and it reads what poll() sorted out for it.
  class ReliableLane final : public DatagramLink {
  public:
    explicit ReliableLane(ChannelPeerLink& owner) : owner_{&owner} {}
    void send(std::span<const std::uint8_t> datagram) override;
    std::optional<Bytes> receive() override;

  private:
    ChannelPeerLink* owner_;
  };

  DatagramLink* link_;
  std::string roomCode_;
  ReliableLane lane_{*this};
  ReliableChannel channel_{lane_};
  std::deque<Bytes> reliableIn_;
  std::deque<Bytes> unreliableIn_;
  std::optional<Millis> lastUnreliable_;
};

} // namespace dwe::net
