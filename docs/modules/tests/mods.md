# Example mods for the tests

**Synthetic** packs used by the tests and the autotest routes to check that a mod adds and replaces content by ID
without code of its own ([tables by entries](../../architecture/vfs.md#tables-by-entries)). They contain nothing
from the original game: all the values, the image and the model are made up.

## `example_mod`

A mod of the `mods` layer that depends on `base` and on the `game` pack and declares `api: "1.4"`, which is required in a mod. It shows the three things a mod can do:

| What | Files |
|---|---|
| **Adds a Digimon** `example_mod:pebblemon` | Its row in `data/digimon.yaml` (without `index`: the engine sets it) and in `data/raise.yaml`, its model in `digimon/example_mod/pebblemon/model.glb` and the model contract in `contract.yaml`, required for a new Digimon |
| **Adds a map** `example_mod:meadow` | Its row in `data/maps.yaml` and its folder `map/example_mod/meadow/` with `data.yaml` and the background `background_day.png`. Exit 0 leads to File City (`twna01`, exit 1) |
| **Replaces an asset** | `field/warps.yaml` replaces the one in the `game` pack entirely: the field starts on the new map. It is a copy of the one in `content/packs/game` with a different `start`, and is only valid for the tests |
| **Names what it adds** | `text/en/example_mod.yaml` and `text/es/example_mod.yaml`, with the keys `digimon_names.pebblemon` and `map_names.meadow` |

The new IDs carry the pack id as a namespace (`example_mod:`), and the folders of their assets carry it as one more
folder (`map/example_mod/meadow/`), so the paths remain `[a-z0-9_/]`.

### The binaries

The two binary files are written by [`make_example_mod.py`](https://github.com/Indie-Modding-Games/dw-engine/blob/main/tests/mods/make_example_mod.py) (Python 3, no dependencies):
`python3 tests/mods/make_example_mod.py tests/mods/example_mod`. It always writes the same bytes.

- `background_day.png`: a 256x256 indexed PNG with five colors: checkered grass, a horizontal path and water
  at the top.
- `model.glb`: a 4-vertex tetrahedron on a single bone (`bone_00`), in the units and axes of the `dwx` models
  (Y down), with an 8x8 PNG texture embedded. It has the animations `anim_00` to `anim_04` that the
  field asks of a partner, each a 2-tick sway at 20 ticks per second that ends in a loop (the
  `extras` that `packages/engine/assets/src/model_data.cpp` reads). It meets `contract.yaml` (a root bone and those animations).

To change them, any other PNG or glTF that meets the same requirements will do; `dwx validate --with <base> --with content/packs/game
tests/mods/example_mod` says whether the pack is still valid.

### Where it is used

- `packages/session/tests/example_mod_tests.cpp`: the tables compose, the field enters the new map through a warp of an
  original map and leaves through its own, a new game starts with the new Digimon as partner and the texts name
  the new content in both languages. With `DWE_CONTENT`, the same on the extracted base with all the game systems.
- `apps/dwx/tests/validate_tests.cpp`: with `DWE_CONTENT`, `dwx validate` of the mod on the base and the `game` pack.
- `tests/autotest/route-mod-map.yaml`: the game with `--mods tests/mods/example_mod` starts on the new map and leaves to
  File City ([vertical slice](../../guides/vertical-slice.md)).

## `ui_skin/test_contrast`

A synthetic UI skin in a mod of the `mods` layer that depends on the `game` pack: `skin.yaml` extends the
`modern` skin and its `theme.rcss` changes the colors on every screen, with the skin's name in both
languages (`localization/<language>/test_contrast.yaml`). It proves that a mod adds a skin without touching the engine. It is used by
`packages/engine/ui/tests/skin_switch_tests.cpp`, the tests of the skin settings in `apps/game` and the route
`tests/autotest/route-skins.yaml`.

## `game_mode/practice_mode`

A synthetic mod that only adds one game mode, `practice` ("Practice"), with the rules of the original
(`modes/practice/mode.yaml`) and its texts in Spanish and English. With it mounted, a new game shows the list of
modes ([modes](../game/modes.md)); it is used by `route-game-modes.yaml` (`--mods tests/mods/game_mode`).
