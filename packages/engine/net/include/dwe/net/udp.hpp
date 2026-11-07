#pragma once

#include <dwe/core/error.hpp>
#include <dwe/net/datagram.hpp>
#include <dwe/net/endpoint.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace dwe::net {

// A datagram link over a UDP socket of the system (BSD sockets, or Winsock on Windows), IPv4 only.
// The host binds a port and takes as its peer whoever sends to it first; the guest sends to the
// host's endpoint. No relay and no NAT traversal: across the internet the host must be reachable
// on its port (an open or forwarded port).
class UdpLink final : public DatagramLink {
public:
  // Listens on every interface at `port` (0: one the system picks, see localPort()).
  static core::Result<std::unique_ptr<UdpLink>> host(std::uint16_t port);
  // Talks to the host at `endpoint` from a port the system picks.
  static core::Result<std::unique_ptr<UdpLink>> join(const Endpoint& endpoint);

  ~UdpLink() override;
  UdpLink(const UdpLink&) = delete;
  UdpLink& operator=(const UdpLink&) = delete;
  UdpLink(UdpLink&&) = delete;
  UdpLink& operator=(UdpLink&&) = delete;

  void send(std::span<const std::uint8_t> datagram) override;
  std::optional<Bytes> receive() override;

  // The port the socket is bound to.
  [[nodiscard]] std::uint16_t localPort() const { return localPort_; }
  // The peer, once known (the host learns it from the first datagram).
  [[nodiscard]] const std::optional<Endpoint>& peer() const { return peer_; }

  // The system's socket, closed with the link.
  struct Socket;

private:
  UdpLink(std::unique_ptr<Socket> socket, std::uint16_t localPort, std::optional<Endpoint> peer);

  std::unique_ptr<Socket> socket_;
  std::uint16_t localPort_ = 0;
  std::optional<Endpoint> peer_;
  // The host keeps the first sender as its peer and ignores the others.
  bool lockedPeer_ = false;
};

// The machine's own IPv4 addresses on its networks (not the loopback), to show the host's
// endpoint and join code; empty when the system tells none.
[[nodiscard]] std::vector<std::array<std::uint8_t, 4>> localAddresses();

} // namespace dwe::net
