#pragma once

#include <dwe/net/peer_link.hpp>
#include <dwe/net/signal.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace dwe::net {

// How a WebRTC link finds the other peer.
struct RtcSettings {
  // The signaling server (wss://...). Empty: none is set up, and the link fails with NoServer.
  std::string signalServer;
  // The STUN (and later TURN) servers, as URLs ("stun:host:port").
  std::vector<std::string> iceServers;
  // Once both peers are in the room, how long they may take to open a route.
  Millis connectTimeout{30000};
};

// Opens a text WebSocket to a URL; the tests give one to an in-process server.
using SocketOpener = std::function<std::unique_ptr<SignalSocket>(const std::string& url)>;

// A WebSocket to the signaling server (libdatachannel's client). A wss:// server's certificate
// must be valid for the URL's host and chain up to one of the trusted roots (PEM certificates;
// trustedSignalRoots() unless given), or the socket fails: nobody in between can pose as the
// server.
std::unique_ptr<SignalSocket> openWebSocket(const std::string& url);
std::unique_ptr<SignalSocket> openWebSocketTrusting(const std::string& url,
                                                    const std::string& trustedRoots);

// A peer link over WebRTC data channels (libdatachannel): the host makes a room on the signaling
// server and offers, the guest joins it with the room's code and answers, both trade their ICE
// candidates through the server, and once the two data channels are open the server's room is
// closed and everything goes peer to peer. The reliable lane is an ordered reliable channel
// ("control"), the unreliable one an unordered channel without retransmissions ("inputs"). The
// library's threads only fill a mailbox; everything else happens in poll(), on the caller's
// thread.
class RtcPeerLink final : public PeerLink {
public:
  static std::unique_ptr<RtcPeerLink> host(RtcSettings settings, const SocketOpener& open);
  static std::unique_ptr<RtcPeerLink> join(RtcSettings settings, std::string_view code,
                                           const SocketOpener& open);
  ~RtcPeerLink() override;
  RtcPeerLink(const RtcPeerLink&) = delete;
  RtcPeerLink& operator=(const RtcPeerLink&) = delete;
  RtcPeerLink(RtcPeerLink&&) = delete;
  RtcPeerLink& operator=(RtcPeerLink&&) = delete;

  void send(Lane lane, std::span<const std::uint8_t> message) override;
  std::optional<Bytes> receive(Lane lane) override;
  void poll(Millis now) override;
  [[nodiscard]] PeerState state() const override;
  [[nodiscard]] PeerFailure failure() const override;
  [[nodiscard]] std::optional<Millis> lastHeard() const override;
  [[nodiscard]] std::string roomCode() const override;
  [[nodiscard]] bool peerFound() const override;

private:
  struct Impl;
  RtcPeerLink(RtcSettings settings, bool hosting);
  std::unique_ptr<Impl> impl_;
};

} // namespace dwe::net
