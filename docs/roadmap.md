# Phase plan

Each phase has a closing criterion that can be checked. A phase is not considered finished until it is met.

This document is the summary with the status of the day. The designs that are agreed before programming are in the
[design proposals](plan/index.md).

| Phase | Name | Size | Status |
|---|---|---|---|
| 0 | Base | S | Closed (2026-10-20): Linux and Windows pass on GitHub CI; cleaning up the old repositories awaits a team decision |
| 1 | Asset pipeline | M | Closed (2026-10-20): the US disc is extracted whole (0 pending, 0 partial) and the engine loads the result, extracted and cooked; the cooked format of the videos moves to phase 5 |
| 2 | Vertical slice | M | Closed (2026-10-20) |
| 3 | Playable core | L | Closed (2026-10-21) |
| 4 | The whole original game | XL | Closed (2026-11-17): the whole story plays from the title to the credits and `v0.1.0-beta.1082` is the first stable release |
| 5 | Mods, modes and improvements | Ongoing | Started: online versus and in the Arena, the game modes and the mod hooks are already in `main` |

## Where we are (2026-11-17)

- **Phase 4 is only the original game.** Since 2026-10-22, phase 4 is the whole original (vanilla) game, playable.
  What is not from the original moves to phase 5: game modes, the versus mode, the complete mod API, the rewritten
  story and the cooked format of the videos.
- **The story, by stages.** The port is delivered stage by stage behind the story lock
  (`content/packs/game/rules/original/story_lock.yaml`, option `--story-stage`). All nine stages are open
  (`active_stage: stage_9`) and the routes `tests/autotest/route-full-01` to `route-full-09` play the whole story
  chained together ([vertical slice](guides/vertical-slice.md)).
- **Port coverage:** 1391 of the 1456 original functions that have to be ported are ported and 5 are half done.
  The 60 that are missing belong to versus and the title demo, of phase 5 ([coverage](reference/port-coverage.md)).
- **Phase 5 started.** Online versus and in the Arena, the game modes and the mod hooks are already in `main`.
- **Closed beta through the launcher.** Every merge into `main` that changes code publishes a test version
  (`<version>-beta.<commits on main>`, "Testing" channel of the launcher, with the beta key going through the
  [gateway](modules/services/beta-gateway.md)). The first stable release, `v0.1.0-beta.1082`, was promoted on 2026-11-17 ("Stable" channel). Packages for Linux,
  Windows and macOS ([launcher](modules/apps/launcher.md), [beta for Linux](guides/beta-linux.md),
  [beta for Windows and macOS](guides/beta-windows-macos.md)). The first round of testers started on 2026-11-06.
- **Next:** phase 5. Fixes to the original game keep going out as test versions and are promoted to stable
  when they are ready ([gaps the player will notice](reference/port-coverage.md#gaps-the-player-will-notice)).

## Phase 0 — Base

Closed (2026-10-20). Repository with CMake, presets, vcpkg and the CI on Linux and Windows, and a minimal engine:
window and Vulkan device, action-based input, fixed-step logic, Lua with hot reload, log and console, and the disc
check by SHA-1 (`dwx verify`). How to build it: [setting up the environment](getting-started/setup.md).

## Phase 1 — Asset pipeline

Closed (2026-10-20). The `packages/formats/` library, `dwx extract`, `validate`, `cook` and `pack`, the virtual file
system with layers, the schemas and the first reference sheets. The US disc is extracted whole (0 pending, 0 partial)
and the engine loads the result, extracted and cooked. Where it is documented: [asset
pipeline](architecture/assets-pipeline.md), [formats](formats/index.md) and [reference sheets](reference/index.md).

## Phase 2 — Vertical slice

Closed (2026-10-20). Prerendered backgrounds with depth layers, skinned models, the field (movement, collision, warps,
the partner following the player), RmlUi with a first menu and the minimap. How to play it and run its routes:
[vertical slice](guides/vertical-slice.md).

## Phase 3 — Playable core

Closed (2026-10-21). Rulesets (the format, strategies for the rules that are logic, and the `original` ruleset, which
reproduces the original; the new design comes later as rulesets approved by the design lead, [ADR
0010](adr/0010-own-rules-with-rulesets.md)), the partner, battle, inventory and saving, the live tuning panel,
balance simulations and the map change like the original. Each system follows the [design cycle](conventions/design-cycle.md).
The loop of raising, training, fighting and evolving is checked by a windowless test and by the `route-loop-keyboard`
route ([vertical slice](guides/vertical-slice.md)).

## Phase 4 — The whole original game

- Story: the disc's event scripts converted to Lua on extraction, and the story interpreter that
  runs them (`packages/game/story`).
- All the original's systems that the story requires: shops, tournaments, fishing, minigames, death and rebirth,
  movies and ending.
- Delivery by stages behind the story lock, each with its autotest routes.
- Closed beta: launcher, packages per system, test and stable versions.

**Done when:** the original game is played whole, from the title to the credits, and a stable version is published.

**Closed (2026-11-17).** All nine stages are open and the chain `route-full-01` to `route-full-09` played the whole
story on `main` at `fd4f2901` with no retries (61 minutes). That commit's test version, `v0.1.0-beta.1082`, was
promoted to the first stable release.

At first phase 4 was "Content and modes". Of its phase 4a,
the conversion of scripts to Lua, the mod packs by ID, the versioned Lua API, the interface
skins and the round-trip tests of the external map editor are already in `main`. The rest moved to phase 5.

## Phase 5 — Mods, modes and improvements

- Rewritten story on top of the converted scripts.
- Character depth: dialogue trees, affinity and quests.
- Game modes framework, each with its own ruleset and fixed options, and the versus mode: the
  [game variants](architecture/game-variants.md) Classic Enhanced and Folder Mode are in `main`.
- Complete mod API: packs that add or override content by ID, and Lua hooks.
- More species and maps, HD backgrounds.
- The cooked format of the videos.
- The map editor is the organization's external tool (`dw1fe`), with round-trip tests of our
  format; the engine has no editor of its own.

**Done when:** the first version of the mod is published. From then on the phase stays open.

## Risks

| Risk | How it is mitigated |
|---|---|
| Enormous scope | Vertical slice early, closing criteria per phase and visible improvements from phase 2 |
| Accidentally losing the game's identity | Written design pillars and reference sheets before each system |
| A different design in each person's head | The GDD is the specification, and one person responsible for design has the last word |
| Balance | Editable rulesets, live tuning, simulations and frequent playtests |
| Digimon from other games | Importer from the user's disc, or the team's own models |
| Legal risk | [Legal asset policy](legal/assets.md) |
