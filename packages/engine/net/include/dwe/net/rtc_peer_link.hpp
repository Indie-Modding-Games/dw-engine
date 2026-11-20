#pragma once

#include <dwe/net/peer_link.hpp>
#include <dwe/net/signal.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace dwe::net {

// How a WebRTC link finds the other peer.
struct RtcSettings {
  // The signaling server (wss://...). Empty: none is set up, and the link fails with NoServer.
  std::string signalServer;
  // The STUN (and TURN) servers, as URLs ("stun:host:port"). The relay servers the signaling server
  // hands out with the room (signal.hpp: IceServer) are added to them.
  std::vector<std::string> iceServers;
  // Relay only: the route goes through a relay (TURN) server and never directly, so the other
  // player never sees this machine's address. Without a relay server it fails with NoRelay.
  bool relayOnly = false;
  // Once both peers are in the room, how long they may take to open a route.
  Millis connectTimeout{30000};
  // How many players a hosted room seats, host included (2 to MaxRoomSeats). Past the first guest
  // each one comes in through the host's next seat (hostSeat).
  int seats = 2;
};

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
  // A listed room goes in the signaling server's public list, with what its host tells about it;
  // any other is reached by its code.
  static std::unique_ptr<RtcPeerLink> host(RtcSettings settings, const SocketOpener& open,
                                           const std::optional<PublicHost>& listing = std::nullopt);
  static std::unique_ptr<RtcPeerLink> join(RtcSettings settings, std::string_view code,
                                           const SocketOpener& open);
  // The host's next seat in its room `code` of more than two seats (the room's token, from the
  // first link's roomToken()): a link of its own that waits for the room's next guest. `taken`:
  // the guests the host holds now.
  static std::unique_ptr<RtcPeerLink> hostSeat(RtcSettings settings, std::string_view code,
                                               std::string_view token, int taken,
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
  [[nodiscard]] std::string roomToken() const override;
  void announceSeats(const SeatsNotice& notice) override;
  void keepAliveWhileBusy(Millis limit) override;

private:
  struct Impl;
  RtcPeerLink(RtcSettings settings, bool hosting);
  std::unique_ptr<Impl> impl_;
};

} // namespace dwe::net
