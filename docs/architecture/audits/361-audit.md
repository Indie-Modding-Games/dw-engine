# Audit of #361: co-op difficulty

Audited on 2026-11-27 on `wave/w135-integrate` with the proposal of #426 (flat +20 % per player, §12). Design:
[co-op difficulty](../coop-difficulty.md).

Status: **Done** (the doc or the code meets it, with a section or a test), **Partial**, **Missing**. The issue is
research; its out-of-scope list (no final scaling build, no stat redesign, no AI rework, no solo rebalance) holds, and
no balance code changed.

## Deliverables

| Deliverable | Before | After | Evidence |
|---|---|---|---|
| Analysis of the original difficulty | Done | Done | §1: enemies and encounters, battle pressure, rewards, tournaments, from the decomp and the battle data. |
| Systems the multiplayer affects | Done | Done | §2. |
| Comparison of scaling strategies | Done | Done | §3 (stats, count, AI, rewards, by boss) and §12.5 (the built model against the flat +20 %). |
| Difficulty parameters for 1 to 4 players | Partial | Done | §5.1 to §5.4 had the proposal; §5.5 adds what ships per level (standard and hard, normal and fixed) and §12.8 the examples of the alternative. Code: `coop_scaling` in `battle.yaml`, `game::battle::coopScalingFactors`. Tests: `packages/game/battle/tests/core/coop_battle_tests.cpp`. |
| Encounters and bosses that need special handling | Partial | Partial | §8 lists them; a note on what the game does today (trigger 1 gets the fixed table, no per-fight override) was added. Myotismon, Devimon, Mamemon and Piximon are still to verify: Devimon is placed on map yaka11a, the others are not in map data and are probably spawned by scripts. |
| Technical implementation proposal | Done | Done | §9 (setting, data, hooks) and §12.4; the setting's name was corrected to `UserSettings::coopDifficulty` and the lobby display marked as built. |
| Progression and economy risks | Done | Done | §6 and §7. |
| Test scenarios | Done | Done | §10 (unit, `dwe-sim` balance, two to four PCs) and §12.6. |

## Acceptance criteria

| Criterion | Before | After | Evidence |
|---|---|---|---|
| A grounded analysis of the original difficulty | Done | Done | §1. |
| The main imbalances of co-op are identified | Done | Done | §2, §7. |
| A scaling model for up to four players | Done | Done | §5 (built) and §12 (alternative), with §5.5 and §12.8 tables. |
| Normal encounters, bosses and special events are told apart | Done | Done | §5.1, §5.2, §8; tournaments and versus never scale (§1.4, §8). |
| Impact on individual and shared progression | Done | Done | §6. |
| Parameters that can change without altering the original | Done | Done | §9.2 (`coop_scaling` data, one player stays at 100 %). |
| A technical recommendation to build and test it | Partial | Done | §11 had two models and no choice; it now recommends keeping the built model as default, adding `linear_20` as a third level, and the order: join roll bonus, per-fight overrides, then `linear_20`, each measured with §10.2. |

## Summary

Deliverables: 6 Done, 2 Partial before; 7 Done, 1 Partial after. Criteria: 6 Done, 1 Partial before; 7 Done after.
The lobby already shows the host's level (`online.current.coop_difficulty`, `ui/online_room/logic.lua`), so no
game-side value was missing.

Remaining: verify the four bosses of §8 in the original before tuning them, and build the per-fight overrides (§9).
