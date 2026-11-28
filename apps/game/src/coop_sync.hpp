#pragma once

#include <dwe/game/coop/authority.hpp>
#include <dwe/game/coop/sync_watch.hpp>
#include <dwe/game/field/field.hpp>

#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "coop_gameplay_bridge.hpp"

namespace dwe::app {

// coop_sync("on"), ("trace"), ("off") or ("report"); a build with DWE_COOP_SYNC off (a package's
// default) leaves the diagnostics out.
inline constexpr std::string_view CoopSyncCommand = "coop_sync";
#ifdef DWE_NO_COOP_SYNC
inline constexpr bool CoopSyncBuilt = false;
#else
inline constexpr bool CoopSyncBuilt = true;
#endif

// The host's own place from its field; each guest's newest valid state as `authority` took it.
[[nodiscard]] std::vector<game::coop::SyncHeld>
coopSyncAuthority(const game::field::Field& field,
                  const game::coop::WorldAuthority* authority = nullptr);
// What a guest holds of every player: its own where it stands, the others where the host's
// snapshots draw them (`presented`, none before the first one).
[[nodiscard]] game::coop::SyncReport coopSyncReport(const game::field::Field& field,
                                                    const PresentedCoopWorld* presented,
                                                    game::coop::PlayerId self,
                                                    const game::coop::SequenceWatch& snapshots,
                                                    std::uint32_t sequence);

// One guest the host's diagnostics hear from.
struct CoopSyncPeer {
  game::coop::PlayerId id = game::coop::GuestPlayerId;
  CoopGameplayBridge* bridge = nullptr;
};

// The sync diagnostics in a co-op game. On the host, once switched on, it tells every guest to
// report, compares their reports with its field (game::coop::SyncMonitor) and logs what happens:
// map changes, snapshots and commands lost or late, divergences that last. A guest reports what it
// holds a couple of times a second while the host watches. Off, it sends nothing at all.
class CoopSync {
public:
  // A guest reports every this many ticks while watched; the host logs a summary every
  // SummaryTicks.
  static constexpr std::uint64_t ReportTicks = 30;
  static constexpr std::uint64_t SummaryTicks = 600;

  explicit CoopSync(bool built = CoopSyncBuilt) : built_{built} {}

  // "on", "trace" (on and logs every report), "off", else a report now (and on).
  void ask(std::string_view value);
  [[nodiscard]] bool watching() const { return on_; }

  // The host's tick: the watch to each guest, their reports into the monitor, its field as the
  // authority. Returns the lines to log (events, and the whole picture when asked or due).
  [[nodiscard]] std::vector<std::string>
  hostTick(const game::field::Field& field, std::uint32_t worldTick,
           std::span<const CoopSyncPeer> guests, net::Millis now,
           const game::coop::WorldAuthority* authority = nullptr);
  // A guest's tick: takes the host's watch, and reports to it while watched.
  void guestTick(const game::field::Field& field, const PresentedCoopWorld* presented,
                 game::coop::PlayerId self, CoopGameplayBridge& host, std::uint64_t tick);
  // The game ended: everything goes (the console's choice stays).
  void reset();

  [[nodiscard]] const game::coop::SyncMonitor* monitor() const {
    return monitor_ ? &*monitor_ : nullptr;
  }
  [[nodiscard]] bool watched() const { return watched_; }

private:
  game::coop::SyncMonitor& watch();

  bool built_ = true;
  bool on_ = false;
  bool trace_ = false;
  bool reportAsked_ = false;
  std::uint64_t hostTicks_ = 0;
  // The guests told the current watch, by player id.
  std::set<game::coop::PlayerId> told_;
  std::optional<game::coop::SyncMonitor> monitor_;
  // A guest's side: whether the host watches, and its report count.
  bool watched_ = false;
  std::uint32_t reports_ = 0;
};

} // namespace dwe::app
