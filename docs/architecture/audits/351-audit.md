# Audit of #351: shared battles by presence

Audited on 2026-11-27 on `feat/battle-modes` (protocol 14), after the bug wave (#411, #413, #418, #419, #420). Design:
[co-op combat](../coop-combat.md), [multiplayer](../multiplayer.md) §6.1.

Status: **Done** (the code meets the criterion, with a test or a clear code path), **Partial**, **Missing**.

## Scenarios

| Scenario | Status | Evidence |
|---|---|---|
| A. Solo play: the original battle | Done | Without a link the story's battles are never handed (`CoopBattles::tick`, `apps/game/src/coop_battles.cpp:286`, `handBattlesToHost(false)`); one partner and no helpers keep the original scaling and layout. Tests: `a battle without allies is the original one`, `co-op scaling follows the ruleset's tables and leaves single player alone` (`packages/game/battle/tests/core/coop_battle_tests.cpp`). |
| B. Both players there: one shared battle | Done | The guest's story battle goes to the host while the host stands on its map (`coop_battles.cpp:286`); the host runs it once (`answer`, `coop_battles.cpp:465`), every free partner on the map joins (`freeAllies`, `:423`), every guest gets the frames and its own end. Tests: `a co-op battle runs over the link from the guest's ask to its end`, `every guest there joins as an ally and gets only its own spoils` (`packages/game/coop/tests/battle_tests.cpp`). |
| C. The other player is away, on another map or gone | Done | Another map: the battle is not handed and runs solo; the host's own battle begins with no ally and the away guest does not watch it (`startShowing`, `coop_battles.cpp:708`). The host refusing `NotHere`/`Failed`, or leaving with the ask waiting, runs the battle on the guest's own game (`coop_battles.cpp:660`, `:958`). An ask never waits longer than `BattleAskWaitTicks` (10 s). Tests: `shared battles see a player here only on this map and not between maps (#351)` (`apps/game/tests/coop_world_tests.cpp:362`), `an ask that waits too long is refused as busy, and a guest leaving drops it (#413)`, `the host leaving drops the guest's ask and battle`. |

## Acceptance criteria

| Criterion | Before | After | Evidence |
|---|---|---|---|
| Detects whether another player can take part | Done | Done | Presence by map (`coopPlayerHere`), the guest's offer only while free and on the host's map (`sendOffer`, `coop_battles.cpp:686`; `BattleMirror::partnerFree`). Tests: `the guest's partner joins the host's battle when it offers it, and commands it`, `a guest keeps its partner offered while its own ask waits on the host (#411)`. |
| Solo encounters keep the original behaviour | Done | Done | Scenario A. |
| Two players share one battle instance | Done | Done | One battle at a time on the host (`BattleReferee`, `packages/game/coop/src/battle.cpp`). Test: `the referee runs one battle at a time and pulls the second player in as helper`. |
| No duplicate battle for one shared encounter | Done | Done | A second ask while one runs is refused `Busy` and the guest's story gets `NotStarted` (`battle.cpp:54`, `coop_battles.cpp:636`, `:656`); an ask the host cannot take yet waits instead of starting a second battle (#413). Tests: `an ask the host cannot take yet waits for it instead of being refused (#413)`. Limit: see "Remaining". |
| Both players get the battle's updates | Done | Done | `BattleFrame` every 3 ticks to every guest, each with its own strip (`frameOf`); the mirror keeps the newest. Tests: `the mirror follows the host's battle and keeps only the newest frame`, `a battle frame carries the items thrown, and the guest gets them over the link`. |
| Both players' partners fight | Done | Done | Allies after the enemies (`BattleSetup::allies`). Tests: `both partners fight: the begin names the allies and their figures travel`, `an ally fights after the enemies, on the partner's side`. |
| The result reaches both players | Done | Done | `BattleReferee::end` gives every guest its end (`battle.cpp:142`); the starter's story gets the outcome (`stopShowing`, `coop_battles.cpp:913`). Test: `the guest's own battle answers its ask and brings its spoils`. |
| XP, rewards and progress are not duplicated | Done | Done | Each guest's spoils only for its own partner that fought; bits and drops stay in the host's world (`endBattle`, `coop_battles.cpp:391`; `BattleMirror::receive(BattleEnd)` drops a helper's spoils). Tests: `every guest there joins as an ally and gets only its own spoils`, `a guest takes a battle's spoils only when a battle could leave them`, `two partners win together: the bits stay in the battle's result`. |
| A disconnect never leaves the other player stuck | Done | Done | A guest drop: its partner fights on its own call (`guestLeft`, `coop_battles.cpp:458`), the host closes its own end box. The host drop: the guest's battle and ask go, its story goes on (`hostLeft`, `coop_battles.cpp:943`). Tests: `a guest that drops mid-battle leaves its partner fighting on its own`, `the host leaving drops the guest's ask and battle`, `at the end box the allies and helpers wait on the starter, who alone closes it`. |
| Scene changes during an encounter | Done | Done | An ask during the host's map change waits; from another map it is refused `NotHere` and runs solo (`BattleReferee::judge`); a guest between maps neither joins nor watches. Test: `shared battles see a player here only on this map and not between maps (#351)`. |

## Found and fixed in this audit

| Item | Before | After | Fix and test |
|---|---|---|---|
| A battle placed a guest's partner where the host's own follow put it, not where the guest saw it, so the partner jumped when the battle began (#411 §6) | Partial | Done | The guest's states carry its partner on every report and the host draws it from them, smoothed (`CoopGuestWorld::report`, `CoopHostWorld::partnerAt`). Test: `co-op partners: the host draws a guest's partner where the guest's game has it (#411)` (`apps/game/tests/coop_replication_tests.cpp`). |
| A guest's own (solo) battle on the host's map had its enemies moved to the host's NPC places every frame by the NPC mirror, and could lose one the host no longer had | Missing | Done | The mirror leaves the guest's NPCs to its own battle (`CoopNpcMirror::View::battling`). Test: `co-op NPCs: a battle of the guest's own keeps its NPCs where it put them` (`apps/game/tests/coop_npc_mirror_tests.cpp`). |

| A guest's thrown item left its bag before the host took the throw, so a throw the host refused (another in flight) lost the item | Partial | Done | The item is paid once the host's frame shows the throw (`CoopThrowPurse`, `CoopBattles::followPurse`). Test: `a guest pays for its thrown item only once the host's frames show the throw` (`apps/game/tests/coop_battle_view_tests.cpp`). |

## Remaining

- A guest's solo battle that began away from the host, on a map the host then enters, is the guest's own: the host's
  copy of that map has its own NPCs and could fight the same wild one. One shared battle per map, kept in the host's
  world, is task 12 of [co-op combat](../coop-combat.md) and depends on separate maps (#356/#357).
- Map NPCs react only to the host's player (multiplayer §5.8, #419): a wild one never chases a guest.
- End to end with two real machines: not run here; the links and the battle flow are covered by in-process tests.

## Summary

Before: 10 Done, 0 Partial, 0 Missing (10 criteria), plus three gaps found (two Partial, one Missing). After: 13 Done.
