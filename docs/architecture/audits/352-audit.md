# Audit of #352: sessions of up to four players

Checked on 2026-11-27 against `feat/battle-modes` (wave w144). "Before" is the state the wave found; "After" is the
state on its branch. Design: [coop-browser-4p](../coop-browser-4p.md), [multiplayer](../multiplayer.md) §5.9 and §5.10.

## Acceptance criteria

| # | Criterion | Before | After | Evidence |
|---|---|---|---|---|
| 1 | Rooms with a capacity of four can be made | Done | Done | `MaxPlayers = 4` (`protocol.hpp`); `CoopConnection::seats` clamps 2..4 (`coop_connection.cpp`); test "a host seats three guests in one room, each with its own player id (#352)" (`coop_connection_tests.cpp`) |
| 2 | Joining a room with one, two or three seats taken | Done | Done | Same test (guests join one after another, ids 1..3); "a game under way takes a late joiner through its waiting seat (#356)"; "a room of four links each guest under its own id..." (`packages/game/coop/tests/room_tests.cpp`) |
| 3 | A full room turns new connections away | Done | Done | The host opens no seat once three guests are in (`seatsAsked` stays at 2 in the four-seat test); `RejectReason::RoomFull` from `CoopConnection` admission and `CoopLink` (`protocol_tests.cpp`, admission through real links); Worker seat tests in `apps/versus-signal/test` |
| 4 | The UI shows the four player spaces | Done | Done | `online_room/logic.lua`: `MemberRows = 4`, rows hidden past `capacity`, free/host/ready/local/arriving classes; test "a hosted sample room is public or private as asked..." (capacity 4, three members) (`online_rooms_tests.cpp`) |
| 5 | Connection states update | Done | Done | `online.member.<n>.ready` / `.arriving`, `Lobby` seats with `SeatState`; tests "the cooperative room's phase follows the link, the lobby and leaving", "a guest that drops out of the game comes back to its own seat and id (#356)" |
| 6 | The four players get into the same game | Done | Done | `CoopScreen::sendHostStart` sends the host's world to every guest; the host waits for every guest's ready (`CoopScreen::guestReady`); a gameplay bridge per guest in `game.cpp` |
| 7 | Each player keeps its identity and its own control | Done | Done | One `CoopLink`, bridge and `NetworkFieldController` per guest; `PlayerRegistry` by id (test "the registry holds the host and three guests by id..."); `Field` holds guests by id (test "a field holds up to three guests, each by its id...") |
| 8 | The four players share one map | Done | Done | `Field::addPlayerNearHost`; tests "players who join or come back stand around the host, each on a free spot", "three guests are each placed, driven and listed by their own id" (`coop_world_tests.cpp`) |
| 9 | Sync works between the four | Done | Done | Per-guest snapshots and acks; tests "four players converge over lossy links (#359)" and "four players keep their links through a 30 s load..." (`coop_sync_tests.cpp`) |
| 10 | Any player leaving does not break the others' session while the host stays | Partial | Done | Before: a guest's link drop was per link (test "...the second guest leaves the lobby: the others stay"), but in a battle another guest removed the leaver's bodies at once and its partner's figure then drove this guest's own partner. After: "one guest of a room of four dropping leaves the others' links up" (`room_tests.cpp`) and "a guest keeps another guest's bodies that left while its partner still fights (#352)" (`coop_world_tests.cpp`); `presentCoopPlayers` keeps them until the battle ends |
| 11 | No system depends on exactly two players | Partial | Done | Before: `BattleBegin` decode took only the host or player 1 as its starter, so `CoopLink::send` refused a battle that player 2 or 3 started. After: any valid player; test "a battle that any guest of a room of four starts goes out to every guest (#352)". Trades stay two-sided by design (one asker, one asked; any pair of the four) |
| 12 | Solo and two-player sessions keep working | Done | Done | Rooms of two keep closing their seat at start (test "a seat held past the grace is free again, and a room of two still closes its seats"); the whole `dwe_coop_tests` suite and the co-op cases of `dwe_game_tests` pass; solo play does not reach the changed code |
| 13 | Existing multiplayer features work with four | Partial | Done | Shared battles (criterion 11), every guest's battle messages on its own link ("with four players every guest's battle messages reach the host on its own link"), training, events and NPC presence by `MaxPlayers` arrays (`coop_training.hpp`, `coop_events.hpp`), HUD tags per other player |

Before: 10 Done, 3 Partial, 0 Missing. After: 13 Done.

## Technical points of the issue

| Point | State | Evidence |
|---|---|---|
| Reuse the existing connection, sync and spawning | Done | One `CoopLink` per guest over the same transport and protocol; no code path for players 3 and 4 only |
| Dynamic participants, unique ids | Done | Ids 0..3, lowest free id per seat, seat keys for rejoining |
| Participants managed in the session system | Done | `PlayerRegistry`, `WorldAuthority`, `CoopConnection::guests()` |
| Sync limits and traffic | Done | `MaxSnapshotEntities = 48` fits four players and partners plus NPCs in one datagram; snapshots go per guest at the same rate |

## Still open

- Several items thrown at once in a battle draw one sprite (`BattleScreen::thrown_`, `FieldScene::flyingItem_`); it is
  visual only. Left to the battle UI work.
- A helper's item leaves its bag before the host takes the throw; with several helpers on one partner's throw slot a
  refused throw can lose it. Handed to the shared-battles work (#351).
- A guest without a profile name is called "Guest {number}" by its row in the lobby but by its player id on the field
  tag; profile names are normally sent, so the two only differ with an older peer.
- Four real machines over the Internet were not tried in this wave; the tests run four links in one process.
