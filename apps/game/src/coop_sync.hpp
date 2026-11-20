#pragma once

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

// The console command of the shared world's sync diagnostics (#359): coop_sync("on"), ("off") or
// ("report"). The console is a developer tool (F1, behind userSettings.devTools as F2's menu), so
// release builds never run it.
inline constexpr std::string_view CoopSyncCommand = "coop_sync";

// Every player's place in the host's field: the session's authority over where they are.
[[nodiscard]] std::vector<game::coop::SyncHeld> coopSyncAuthority(const game::field::Field& field);
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

  // The console's word: "on", "off", else a report now (and on).
  void ask(std::string_view value);
  [[nodiscard]] bool watching() const { return on_; }

  // The host's tick: the watch to each guest, their reports into the monitor, its field as the
  // authority. Returns the lines to log (events, and the whole picture when asked or due).
  [[nodiscard]] std::vector<std::string> hostTick(const game::field::Field& field,
                                                  std::uint32_t worldTick,
                                                  std::span<const CoopSyncPeer> guests,
                                                  net::Millis now);
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
  bool on_ = false;
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
