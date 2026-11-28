# Audit of #359: shared world diagnostics and consistency tests

Checked on 2026-11-27 against `wave/w135-integrate` (wave w146). "Before" is the state the wave found; "After" is the
state on its branch. Design: [multiplayer](../multiplayer.md) §5.9, «Sync diagnostics».

The base came with #375: `SequenceWatch` and `SyncMonitor` (`packages/game/coop/src/sync_watch.cpp`), the console
command `coop_sync` and `CoopSync` (`apps/game/src/coop_sync.cpp`), the `SyncWatch` and `SyncReport` messages and the
headless harness of `apps/game/tests/coop_sync_tests.cpp`.

## Acceptance criteria

| # | Criterion | Before | After | Evidence |
|---|---|---|---|---|
| 1 | Each player's authoritative position can be looked up | Partial | Done | `coop_sync("report")` lists every player's place. Before: a guest's place was its body in the host's field, which is drawn 100 ms behind (#358), not what the authority holds. After: the guest's newest valid state as `WorldAuthority` took it (`coopSyncAuthority` with the host's authority) |
| 2 | The state each client received can be compared | Done | Done | Each guest's `SyncReport` holds every player where it draws it; `SyncMonitor::compare` per guest and player; test "the monitor finds a guest holding a player elsewhere and calls it persistent in time" (`sync_watch_tests.cpp`) |
| 3 | Lost or out-of-order updates are found | Done | Done | `SequenceWatch` on the host's snapshots (guest side) and the guests' commands (host side), reported both ways; tests "a sequence watch counts the gaps as lost and the stragglers as late, not lost", "the monitor logs map changes and the updates lost or late both ways" |
| 4 | Map changes are logged | Done | Done | "map: player N went from A to B" on the host and "map: player N now holds player M on B" per guest; same test as criterion 3 |
| 5 | Tests can run with two, three and four players | Done | Done | Before: "two/three/four players converge over lossy links (#359)" (`coop_sync_tests.cpp`). Added: "two/three/four players: ... cut off from the snapshots ..." (one guest diverges alone and comes back in sync) and "two/three/four players over real links ..." (`sync_watch_tests.cpp`, the reports over `CoopLink`s of a `CoopRoom`) |
| 6 | Lasting differences between states are found | Done | Done | A divergence held for 2 s turns persistent and is logged, and logged again when it ends; now also checked with 2, 3 and 4 players, where only the guest cut off diverges (criterion 5 tests) |
| 7 | The diagnostics can be turned off in production builds | Partial | Done | Before: the console only opens with the developer tools on (off by default in a package), but a player can turn them on in the Interface tab, and a packaged guest always answered a host's `SyncWatch`. After: the CMake option `DWE_COOP_SYNC` (off by default when `DWE_PACKAGE` is on) builds them out: the host never asks, guests never report (`CoopSyncBuilt`). Test "a build without the sync diagnostics never watches, and its guests never report" |

Before: 5 Done, 2 Partial, 0 Missing. After: 7 Done.

## Description

| Point | State before | State after | Evidence |
|---|---|---|---|
| Observe the differences between the authoritative state and each client's | Done | Done | Criteria 1, 2, 6 |
| Log the players' updates with positions, ids, maps and timestamps | Partial | Done | Before: events and the periodic summary, without timestamps and without the single updates. After: every event line starts with the host's time and world tick, and `coop_sync("trace")` logs every report, player by player, next to where the host has that player. Test "traced, the diagnostics log every report next to where the host has each player" |

## Still open

- The guests' `PlayerState` stream (one every 3 ticks, skipped while the guest travels) is not watched for losses:
  its numbers skip on purpose. The guests' commands, sent every tick on the same link, are watched instead.
- Only tried headless (tests); two real machines over the Internet were not tried in this wave.
