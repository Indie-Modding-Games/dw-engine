# Audit of #406: Folder battle mode

Checked on the `feat/battle-modes` branch (PR #410), wave w135. Design: [combat design](../combat-design.md),
[MP cost](../costo-mp.md), [co-op combat](../coop-combat.md); the full audit by mechanic, the decisions and the plan are
in [Folder battle](../folder-battle.md). Tests are in `packages/game/battle/tests` (`dwe_battle_tests`, 237 cases pass)
and the session's battle tests (`dwe_screens_tests -tc='*battle*'`, 9 pass).

## Acceptance criteria

| Criterion | Before | After | Evidence |
|---|---|---|---|
| All the work belongs to Folder | Done | Done | Folder code in `src/folder/`; shared code reaches it only through `BattleSystem` (`src/core/battle_system.hpp`) |
| Vanilla keeps its behaviour | Done | Done | `ClassicBattle` runs the original functions; the new hooks' defaults are the original code (`src/core/battle_systems.cpp:30-50`); test "the classic battle keeps the partners' area moves off each other" |
| No Folder dependency in Vanilla | Done | Done | `classic/` never includes `folder/`; Classic never reads `battle.folder` |
| Shared components keep the original mechanics | Done | Done | folder-battle.md, "Shared code touched": every row says its Classic effect, none |
| Dependencies between the systems are documented | Partial | Done | The doc said only the factory knew both systems; `Battle`'s Folder entry points call `folder/` directly. Corrected in "Architecture" |
| Both versions verified after the changes | Done | Done | Battle and session battle tests pass; no in-game or oracle run in this wave |
| The current system audited against the design | Done | Done | folder-battle.md, "Audit" |
| Contradictions found and corrected | Partial | Done | The brain MP discount gave INT a second job (§6.5): dropped in Folder (`src/folder/techniques/techniques.cpp:35`). The doc's co-op claim (guest sends `Order`/`Stance`) corrected |
| Orders run as the player meant | Done | Done | Phases 2, 3, 14; `tests/folder/` |
| The partner keeps movement and execution autonomy | Done | Done | Phases 3, 6 |
| The execution cycle works | Done | Done | `src/folder/orders/order_cycle.cpp`; phase 3 |
| Techniques respect parameters, costs, reach and conditions | Partial | Done | Technique catalog: functions, area, friendly fire, reach, costo-mp cost (`src/folder/formulas/technique_catalog.cpp:94,115`); charged and checked with the Folder cost (`folder_battle.cpp:70`, `order_checks.cpp`). Tests "a folder technique costs what costo-mp.md says", "the folder battle charges and shows the technique's folder cost" |
| Stats have no duplicated effects | Partial | Done | Brain discount dropped (INT only processes). Speed reserve still drained but never gates Folder (documented) |
| Damage, defense and affinities are consistent | Partial | Done | §7.4 block bonus by distance (`src/folder/defense/defense.cpp:134`); test "the folder cover chance gains points the closer the attacker" |
| Positioning and geometry affect the battle | Partial | Done | §7.5 areas in pixels: everyone inside an area is reached (`src/folder/techniques/techniques.cpp:43`); test "a folder area technique hits whoever stands in its area, allies too" |
| Movement stances work as designed | Partial | Partial | Guard follows the nearest ally but never steps between ally and foe (folder-battle.md, "Remaining gaps") |
| The AI never replaces the player's orders | Done | Done | Phase 7 |
| The planned co-op interactions are supported | Partial | Done | Friendly fire between co-op partners; the checksum now covers the whole Folder state (`src/core/checksum.cpp:115`), test "the folder checksum covers each order's target"; co-op scaling applies to Folder (test "co-op difficulty scales a folder battle's enemies as it scales a classic one's") |
| Items and resources respect their limits | Partial | Done | MP costs follow costo-mp.md; items are the shared ones (phase 9) |
| The UI shows the state of orders and battle | Partial | Partial | Plate and strip show queue, phase, bar, break, reasons and the Folder MP cost (`Battle::partnerMoveCost`). A technique's area, functions and friendly fire are in the API (`Battle::folderTechnique`) but the skins do not draw them yet |
| Tests added for the changed mechanics | Partial | Done | `tests/folder/techniques_tests.cpp` (10 cases) |
| No regressions in Folder | Done | Done | 237 battle cases pass |
| No regressions in Vanilla | Done | Done | Classic cases and session battle tests pass |
| Unresolved discrepancies documented | Partial | Done | folder-battle.md, "Decisions", "Pending balance", "Remaining gaps" |
| Changes isolated and organised | Done | Done | `src/folder/techniques/`, `src/folder/formulas/technique_catalog.cpp`, three `BattleSystem` hooks |

Totals: before 13 Done, 12 Partial, 0 Missing; after 23 Done, 2 Partial.

## Deliverables

| Deliverable | Before | After | Evidence |
|---|---|---|---|
| Short audit of the current system | Done | Done | folder-battle.md, "Audit"; this file |
| Discrepancies with the design | Done | Done | folder-battle.md, "Other findings", "Remaining gaps" |
| The corrections implemented | Partial | Done | Catalog, costs, areas, friendly fire, cover by distance, checksum |
| Tests for the changed mechanics | Partial | Done | `techniques_tests.cpp`; the balance probe runs again (`balance_probe_tests.cpp` looked for a stale value) |
| Further technical decisions | Done | Done | folder-battle.md, "Decisions" (friendly fire, areas, MP costs, reach classes, effect and boost costs, brain discount, block bonus) |
| Requirements pending balance | Partial | Done | folder-battle.md, "Pending balance" |
| Multiplayer risks | Partial | Done | folder-battle.md, "Other findings" (checksum), "Co-op difficulty in Folder" (friendly fire), "Remaining gaps" (guest `Order`/`Stance` unused) |
| Vanilla compatibility report | Partial | Done | folder-battle.md, "Shared code touched" with the Classic effect of each hook |

Totals: before 3 Done, 5 Partial; after 8 Done.

## Gaps named for this wave

| Gap | Before | After | Evidence |
|---|---|---|---|
| Technique catalog for Folder (area and shape, function, friendly fire) wired into the resolution | Missing | Done | `folder.catalog` in `battle.yaml`, `techniqueOf`; `reached`/`touches` hooks (`src/folder/folder_battle.cpp`); folder-battle.md, "Technique catalog" (56 techniques) |
| Block bonus by distance | Missing | Done | `cover_near_bonus_percent`, `cover_far_bonus_percent`, `cover_far_pixels` (provisional 10 → 0 over 800 px) |
| MP costs per costo-mp.md, applied and readable by the UI | Missing | Done | `folder.mp_costs`; `Battle::partnerMoveCost` and `Battle::folderTechnique(slot, side)` |
| Co-op difficulty in Folder | Missing | Done | folder-battle.md, "Co-op difficulty in Folder"; coop-difficulty.md §9.4; the flat +20 % alternative is documented for Folder, not built |

All together: before 16 Done, 17 Partial, 4 Missing; after 35 Done, 2 Partial, 0 Missing.

## Remaining

- Guard does not intercept; the skins do not draw a technique's area or friendly fire.
- All new values are provisional (costs, reach and area thresholds, block bonus); which area techniques should spare
  allies is a balance call.
- Not run in the game or against the oracle; co-op friendly fire not tried on two machines.
