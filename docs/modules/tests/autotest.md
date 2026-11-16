# Autotest routes

`tests/autotest/` holds the routes: game playthroughs written in YAML that `dwe-game --autotest <route.yaml>` plays
with the same events as a player (keyboard, mouse or a virtual gamepad), so they go through the action map, the UI
and the real field. It is not a CMake module: the runner lives in `apps/game` (`autotest.hpp`, `autotest.cpp`) and the
routes need content extracted from a disc, so they do not run in CI.

- The format of a route (header, steps and `wait` conditions): [route
  format](../../guides/vertical-slice.md#route-format).
- What each vertical slice route checks: [autotests](../../guides/vertical-slice.md#autotests).
- Running many at once, in `en` and `es`: [the gate](../../guides/gate.md).

## What is there

| Routes | What they cover |
|---|---|
| `route-keyboard`, `route-gamepad`, `route-mouse` and the `mouse-*`, `walk-*`, `battle-*` ones | Input with each device: walking, menus, battle and window size |
| `route-stage1` … `route-stage9` and their variants (`route-stage2-*`…) | The story stages and each one's story lock ([story](../game/story.md)) |
| `route-full-*`, `route-canonical-stages` | The chained campaign and the canonical stages |
| `route-save-*`, `route-sleep-save` | Saving and loading, also when sleeping ([save and load](../../architecture/save-and-load.md#6-routes-and-tests)) |
| `route-bank*`, `route-bulk-*`, `route-feed-*`, `route-partner-*`, `route-status-extras` | Individual screens and systems: shops and the guardian, bulk changes, partner care, the status sheet |
| `route-settings-*`, `route-options*`, `route-skin*`, `route-language`, `route-text-*`, `route-signs*`, `route-minimap*` | Options, UI skins, language, text that fits and signs |
| `route-evolve`, `route-evolution-requirements`, `route-field-hud`, `route-free-exploration*`, `route-fade` | Evolution and its requirements, the field HUD, free exploration and fades |
| `route-menu-stress` | The game menu opened and closed hundreds of times: on a pass-through tile, with key presses during the frames of its box, with the partner moving, on the player page and at the smallest and largest UI scale |
| `route-mod-*`, `route-hidden-*`, `route-dev-assets` | Mod packs, hidden content and development assets |
| `training/route-training-<map>-<course>.yaml` | A whole session of each course of each gym, with `expect_range` on the `gym.session.*` settings ([checking training](../../guides/check-training.md)) |

A new route enters the gate's `daily` level on its own (every `route-*.yaml` in the folder, except those in
`training/` and the manual previews in `preview/`).

## Conventions

- The name is `route-<topic>.yaml` and `name:` is the prefix of its screenshots.
- A comment at the top says what it checks, which original function it follows and which options it starts with
  (`--start <map> --new-game --seed 1`...): the route does not carry them inside.
- A route checks with `expect_*` steps (`expect_setting`, `expect_range`, `expect_change`, `expect_clear`,
  `expect_text_fits`), not only with screenshots: a screenshot does not fail on its own.
- Anything the game decides by chance goes in `when` or `script_if` steps, so the route passes with any roll.
- A route that comes from an F12 report carries its save (`load_save:`, relative to the route).
- The exit code is 0 if everything passes, 2 if it passes but `walk_to` or `wait` steps were skipped, and 1 if it fails.

## Running one

```sh
scripts/dev.sh run --content <extracted folder> --lang es --autotest tests/autotest/route-keyboard.yaml \
    --shots <screenshots folder>
scripts/dev.sh training-check --content <extracted folder>   # the ones in training/, no window, with their summary
```
