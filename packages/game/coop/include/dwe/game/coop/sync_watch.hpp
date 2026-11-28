#pragma once

#include <dwe/game/coop/protocol.hpp>
#include <dwe/net/peer_link.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace dwe::game::coop {

// The shared world's sync diagnostics (#359), a developer tool: what reached one side of a stream
// numbered in order (the host's snapshots by world tick, a guest's states by sequence). An update
// past a gap counts the gap as lost; one older than the newest came late (and was not lost after
// all); the same number again is a repeat.
class SequenceWatch {
public:
  enum class Arrival : std::uint8_t { First, InOrder, AfterGap, Late, Repeat };

  Arrival observe(std::uint32_t number);

  [[nodiscard]] std::optional<std::uint32_t> newest() const { return newest_; }
  [[nodiscard]] std::uint32_t received() const { return received_; }
  [[nodiscard]] std::uint32_t lost() const { return lost_; }
  [[nodiscard]] std::uint32_t late() const { return late_; }
  [[nodiscard]] std::uint32_t repeats() const { return repeats_; }

private:
  std::optional<std::uint32_t> newest_;
  std::uint32_t received_ = 0;
  std::uint32_t lost_ = 0;
  std::uint32_t late_ = 0;
  std::uint32_t repeats_ = 0;
};

struct SyncRules {
  // How far (world units) a guest may hold a player from where the host has it: the snapshots play
  // a little behind, so a running player is a few hundred units off without anything wrong.
  float tolerance = 600.0F;
  // A divergence that lasts this long is persistent: something is out of sync, not just late.
  net::Millis persistAfter{2000};
};

// One player a guest holds away from the host's authority: on another map (distance < 0), or
// farther than the tolerance.
struct SyncDivergence {
  PlayerId guest = GuestPlayerId;
  PlayerId player = HostPlayerId;
  std::string heldMap;
  std::string authorityMap;
  float distance = -1.0F;
  net::Millis since{0};
  bool persistent = false;

  friend bool operator==(const SyncDivergence&, const SyncDivergence&) = default;
};

// The host's side of the diagnostics: each player's authoritative place (the host's field) against
// what each guest says it holds (its SyncReport), the snapshots each guest missed or got late, the
// states the host missed or got late from each guest, and the map changes. It only watches: it
// never corrects anyone (the authority does that).
class SyncMonitor {
public:
  explicit SyncMonitor(SyncRules rules = {}) : rules_{rules} {}

  // The host's view now: every player's authoritative place and the host's newest world tick.
  void authority(std::vector<SyncHeld> players, std::uint32_t worldTick, net::Millis now);
  // A guest's report, as it arrived.
  void report(PlayerId guest, const SyncReport& report, net::Millis now);
  // What the host saw of a guest's own states (lost and late on their way to the host).
  void states(PlayerId guest, const SequenceWatch& watch);
  // A guest left: its reports go.
  void forget(PlayerId guest);
  // Traced, every report also logs each player the guest holds next to where the host has it.
  void trace(bool on) { trace_ = on; }
  [[nodiscard]] bool tracing() const { return trace_; }

  // What happened since the last call, one line each and stamped with the host's time and world
  // tick: map changes, updates newly lost or late, divergences that turned persistent or ended,
  // and while traced every report.
  [[nodiscard]] std::vector<std::string> takeEvents();
  [[nodiscard]] std::vector<SyncDivergence> divergences() const;
  // Every guest reported since `since` and holds every player where the host has it.
  [[nodiscard]] bool converged(net::Millis since) const;
  // The whole picture as text lines: per player its authoritative place, per guest what it holds,
  // its lag in world ticks, the updates lost and late both ways and the open divergences.
  [[nodiscard]] std::vector<std::string> lines(net::Millis now) const;

private:
  struct Guest {
    std::optional<SyncReport> newest;
    net::Millis reportedAt{0};
    SequenceWatch reports;
    std::uint32_t statesLost = 0;
    std::uint32_t statesLate = 0;
    std::uint32_t snapshotsLost = 0;
    std::uint32_t snapshotsLate = 0;
    std::map<PlayerId, std::string> heldMaps;
  };

  void compare(PlayerId guest, net::Millis now);
  void note(std::string text);

  SyncRules rules_;
  bool trace_ = false;
  net::Millis now_{0};
  std::vector<SyncHeld> authority_;
  std::uint32_t worldTick_ = 0;
  std::map<PlayerId, std::string> maps_;
  std::map<PlayerId, Guest> guests_;
  std::map<std::pair<PlayerId, PlayerId>, SyncDivergence> divergences_;
  std::vector<std::string> events_;
};

} // namespace dwe::game::coop
