#pragma once

#include <dwe/game/versus/protocol.hpp>
#include <dwe/game/versus/series.hpp>
#include <dwe/net/lockstep.hpp>
#include <dwe/net/peer_link.hpp>

#include <deque>
#include <optional>
#include <vector>

namespace dwe::game::versus {

// What the two machines are doing together.
enum class LinkPhase : std::uint8_t {
  // The host waits for a guest; the guest waits for the host's welcome.
  Connecting,
  // Both know the series and pick their partners.
  Lineups,
  // Both lineups are in: the battles are played one by one.
  Playing,
  // The series is over and both said so.
  Finished,
  // It ended early: failure() says why.
  Failed,
};

enum class LinkFailure : std::uint8_t {
  None,
  // The host did not answer in time.
  NoAnswer,
  // Nothing came from the other machine for too long.
  Silence,
  // The other machine left (Leave::Quit) or this one did.
  Left,
  ProtocolMismatch,
  VersionMismatch,
  ContentMismatch,
  BadLineup,
  // The battles stopped playing alike (a checksum or a result differed).
  Desync,
  // A message could not be read.
  Damaged,
  // The connection (net::PeerFailure): no signaling server set up, the server out of reach, no
  // room with that code, a room that already has two players, no route in time, no direct route
  // between the two machines, or a connection that closed.
  NoServer,
  ServerUnreachable,
  WrongCode,
  RoomFull,
  TimedOut,
  NoRoute,
  Disconnected,
};

struct LinkTimeouts {
  // How long a guest waits for the host's answer once the connection is open.
  net::Millis answer{10000};
  // How long either waits with nothing from the other once they talk.
  net::Millis silence{8000};
};

// The versus protocol between two machines over a peer link (net::PeerLink): the guest says hello,
// the host checks the protocol, the game version and the content hash and welcomes it with the
// series, both send their lineups, and then each battle runs in lockstep (net::Lockstep, its
// inputs on the unreliable lane, sent again every InputsEvery until acknowledged) and ends with
// both sides telling each other its result, which must agree. Everything else goes on the reliable
// lane. A connection that fails, leaving, silence and any disagreement end it with a reason the
// screens can tell the player. It never touches a save.
class VersusLink {
public:
  // The host offers `setup`; the guest learns it from the welcome.
  // How often the inputs message goes again while a battle runs.
  static constexpr net::Millis InputsEvery{50};

  // The host offers `setup`; the guest learns it from the welcome. The link must outlive this.
  static VersusLink host(net::PeerLink& link, Identity identity, SeriesSetup setup,
                         LinkTimeouts timeouts = {});
  static VersusLink guest(net::PeerLink& link, Identity identity, LinkTimeouts timeouts = {});

  // Moves the link on, reads what arrived, sends what is due and checks the timeouts.
  void poll(net::Millis now);
  [[nodiscard]] const net::PeerLink& peerLink() const { return *link_; }

  [[nodiscard]] Side side() const { return side_; }
  [[nodiscard]] LinkPhase phase() const { return phase_; }
  [[nodiscard]] LinkFailure failure() const { return failure_; }
  // Once both know it.
  [[nodiscard]] const std::optional<SeriesSetup>& setup() const { return setup_; }
  [[nodiscard]] const std::optional<Identity>& peer() const { return peer_; }

  // This side's partners for the series (as many as its battles), in order.
  core::Result<void> sendLineup(std::vector<VersusFighter> fighters);
  [[nodiscard]] const std::optional<std::vector<VersusFighter>>& lineup(Side side) const {
    return lineups_.at(static_cast<std::size_t>(side));
  }

  // Starts the lockstep of the series' next battle; its messages from the other side wait until
  // then. Only while playing and no battle runs.
  core::Result<void> startRound();
  // The battle that runs, if any.
  [[nodiscard]] net::Lockstep* lockstep() { return lockstep_ ? &*lockstep_ : nullptr; }
  [[nodiscard]] const net::Lockstep* lockstep() const { return lockstep_ ? &*lockstep_ : nullptr; }
  // Sends this side's input of the next tick when the lockstep wants it.
  void submitInput(std::uint32_t input);
  // Sends that the tick to play was played and left the battle with `checksum`.
  void playedTick(std::uint64_t checksum);
  // The battle ended here with `result`; the series moves on once the other side's agrees.
  void finishRound(RoundResult result, std::uint64_t checksum);
  // The battles both sides finished and agreed on.
  [[nodiscard]] const Series& series() const { return series_; }
  // Whether this side finished the battle and waits for the other's result.
  [[nodiscard]] bool awaitingResult() const { return localResult_.has_value(); }

  // Leaves: the other side hears why, and this one fails with Left.
  void leave(LeaveReason reason = LeaveReason::Quit);

private:
  VersusLink(net::PeerLink& link, Side side, Identity identity, LinkTimeouts timeouts);
  void send(const Message& message);
  void sendInputs();
  void fail(LinkFailure failure, std::optional<LeaveReason> tell = std::nullopt);
  void take(const net::Bytes& message);
  void takeInputs(const net::Bytes& message);
  void handle(const Message& message);
  void checkIdentity(const Identity& other);
  void settleRound();
  void beginPlayingIfReady();

  net::PeerLink* link_;
  Side side_;
  Identity identity_;
  LinkTimeouts timeouts_;
  LinkPhase phase_ = LinkPhase::Connecting;
  LinkFailure failure_ = LinkFailure::None;
  std::optional<SeriesSetup> setup_;
  std::optional<Identity> peer_;
  std::array<std::optional<std::vector<VersusFighter>>, 2> lineups_;
  Series series_{1};
  std::optional<net::Lockstep> lockstep_;
  // The other side's checksums that came after it finished the battle that runs here.
  std::deque<net::Bytes> nextRound_;
  bool remoteRoundOver_ = false;
  std::optional<RoundOver> localResult_;
  std::optional<RoundOver> remoteResult_;
  // When the connection opened, for the answer timeout.
  std::optional<net::Millis> opened_;
  net::Millis lastInputs_{0};
  net::Millis now_{0};
};

} // namespace dwe::game::versus
