# Audit of #357: shared state for world interactions and objects

Checked on the `feat/battle-modes` branch (PR #410) after the co-op bug wave (#414, #417, #421, #422, #423). Design:
[multiplayer](../multiplayer.md) §5.8.

| Criterion | Before | After | Evidence |
|---|---|---|---|
| The authority records the relevant interactions | Done | Done | The host's `WorldLedger` numbers every shared change (`packages/game/coop/include/dwe/game/coop/world_state.hpp:44`); `CoopHostWorld::share`; tests "the host numbers its own shared changes and keeps a player's own facts out", "the host takes a guest's ask once, only the shared part, and only on its map" (`world_state_tests.cpp`) |
| Changes reach the players they concern | Done | Done | `WorldChange` to every guest, map edits applied only on that map, `WorldSync` and `GroundItems` on join and on every host map (`coop_world_state.cpp`); tests "the host's own interactions reach every guest; a player's own facts stay home", "a guest that joins late or changes map gets the world as it is now", "a door a guest's script moves moves on the host's map and for the others there" (`coop_world_state_tests.cpp`) |
| The same interaction is never processed twice | Done | Done | Numbered asks and claims, `WorldReplica` revisions (`world_state.hpp:104`), absolute values; NPC events held per player (#417, `EventStatus`); tests "a guest applies each host change once and in order, and a sync covers the rest", "a chest taken by two players within one round trip gives its item to one of them", "an item on the ground goes to the one player who picks it up, once", "coop events: a guest's event holds its NPC in every other game until it ends..." |
| Objects stay coherent between clients | Done | Done | Chests follow their trigger (`followChestTriggers`), the guest's ground is the host's on its map, NPCs a fact moves come and go (`CoopNpcPresence`), room resets overtake old asks (#422); tests in `coop_world_state_tests.cpp`, `coop_npc_presence_tests.cpp`, "a room reset overtakes what a guest asked before it saw the reset" |
| Local and shared interactions are told apart | Partial | Done | The table in §5.8 and `StoryAdapter::playerOwnTrigger`/`playerOwnVariable`. Gap found: variable 247, the partner of 248 when scripts hand a map object, a mood bubble or the bird's destination to an engine routine, was still shared, so every routine call in one game asked the host to overwrite it in everyone's game. Now local (`story.yaml`); test "co-op: the variables scripts hand to the engine's routines stay in each game" (`story_adapter_tests.cpp`) |
| Single-player behaviour is kept | Done | Done | Every co-op path runs only with a link; `player_own_*` only matter to co-op ("Single player reads them as any other", `story.yaml`); single player never claims |

Totals: before 5 Done, 1 Partial, 0 Missing; after 6 Done.

## The per-player variables of #421 and #423

Checked over every story script (222 scripts, a read-only grep of an extraction from 2026-10-30) for the variables and
the trigger the shop job made per player:

- **Trigger 101** is only used by script 49, the shop job: cleared when a job starts, set at its halfway line. Safe.
- **Variable 117** is only written and read by the shop job's customers (scripts 199 to 202). Safe.
- **Variable 248** is never read by a script. Scripts set it right before an engine routine reads it (routine 21's
  animation, routine 28's mood face, the bird's entrance for `teleport(247)`). Safe, and 247, set the same way, is now
  per player too (above).
- **Variable 110** is a scratch roll: about 600 `store_random(110, …)` and 1200 reads, each read in the section that
  rolled it or a routine it calls, except the Toy Mansion (scripts 142 to 144: omoc05 to omoc07), whose map entry rolls
  the right door and whose door tiles read it later. Per player, each game judges its own walk with its own roll; when
  it was shared, a guest's entry roll was asked of the host and could change the host's right door during its walk. A
  guest's door warp is brought back to the group's map as any guest warp is.

No use breaks with these per player.
