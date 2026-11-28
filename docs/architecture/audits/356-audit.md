# Audit of #356: scenes and positions per participant

Checked on the `feat/battle-modes` branch (PR #410) after the co-op bug wave. Design: [multiplayer](../multiplayer.md)
§5.7 (decision 2: the group goes where the host goes).

| Criterion | Before | After | Evidence |
|---|---|---|---|
| Each participant has a current map | Done | Done | `PlayerPose::mapId` per record (`packages/game/coop/include/dwe/game/coop/authority.hpp:18`), `WorldAuthority::playersOn` (`:114`) on the host, `CoopGameplayBridge::playersOn` on a guest; test "presence: the players on a map are the ones whose newest valid pose is there" (`authority_tests.cpp`) |
| Map changes are told to everyone | Done | Done | The host's map change relocates every guest with the exit it came in by (`apps/game/src/coop_world.cpp`, `CoopHostWorld::tick`); test "a map change of the host takes every guest along, with the host's arrival" (`coop_world_tests.cpp`) |
| Spawn points are in sync | Done | Done | The correction carries the host's spawn spot and the guest arrives there exactly (`FieldScene::moveCoopSelf`, `coopArrival_`); `Field::placeOtherPlayersNearHost` on the host; same test as above and "a guest follows the host's corrections and reports with their epoch" |
| No position from an earlier map is applied | Done | Done | States carry the correction epoch and map; `StateVerdict::OldEpoch` and `WrongMap` (`authority.hpp:56`, `:58`); the guest reports nothing while a correction is pending; a guest draws no one from another map (`coop_world_scene.cpp:27`); tests "the authority keeps the newest valid state of each player and drops stale ones", "player states over a jittery, lossy, duplicating link keep the newest one" |
| Players can be on different maps without breaking the session | Done | Done | Per-player maps in the authority; states from another map are refused, not applied; NPC mirroring, battles and ground items only act on the same map (§5.7, §5.8, `coopPlayerHere`); test "shared battles see a player here only on this map and not between maps (#351)". By design (decision 2) a guest's own exits are held and its own story warp is brought back to the group's map |
| A player's arrival updates what the others draw | Done | Done | Remote players are hidden while on another map or between maps and shown again on arrival (`presentCoopPlayers`); a room reset reloads the guest's map (#422); test "a room the host's warp reset is loaded again by the guest, not kept as it was" |
| UX point 21: the guest sees "the host changed zone" before the fade | Missing | Done | `CoopGuestWorld::ZoneLeadTicks` and `takeZoneChange` (`apps/game/src/coop_world.hpp`), notice `online.coop.host_changed_zone` in the 9 languages, shown by the default `coop_hud` document; tests "a guest follows the host's corrections and reports with their epoch" (the lead) and "only the host's lead to another map tells the guest the zone changed (#356)". The skin's own look for it is left to the skin work |

Totals: before 6 Done, 0 Partial, 1 Missing (the UX point); after 7 Done.

Still unverified: the notice's timing against the real fade on two machines over a real link.
