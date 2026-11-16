# Dependency rules

The layers in the [overview](overview.md) are only used downward. These rules are what keep the engine modifiable in
the long term: if they are broken, changing one piece forces you to touch many others.

## Rules

1. **Dependencies only go down.** A layer uses the ones below it, never the ones above it nor any in a cycle.
2. **The runtime never depends on `formats`.** Only `apps/dwx` uses it. This way no PS1 limitation (fixed IDs, table
   sizes, palettes) reaches the game.
3. **`packages/game/*` knows nothing about Vulkan or SDL.** It asks `render`, `audio`, `input`, `ui` or `scene` for
   things through their public APIs.
4. **The modules in `packages/game/` do not use each other directly.** They communicate through `scene` events or
   through interfaces declared in the module that needs them. This way `battle` does not break when `partner`
   changes. What all of them share (the ruleset, its strategies and the game clock) is in the `ruleset` service
   ([rulesets](rulesets.md)). The only place that joins them is the app library `packages/session`
   ([module page](../modules/apps/session.md)): the game session, which the game and `dwe-sim` use the same way.
5. **Lua only sees what `script` exposes.** The Lua API is a contract versioned with semver: incompatible changes
   bump the major version.
6. **Only public headers are included**: `<dwe/<module>/...>`. Never another module's `src/`.

## Folder rules

A module's folder also limits what it can use ([repository layout](repository-layout.md)):

- **`packages/engine/` only uses the engine.** Its modules are in the `core`, base, systems or services layers and
  only depend on other modules in `packages/engine/`. The engine knows nothing about the game or the apps' libraries.
- **`packages/game/` is the game layer.** All its modules are declared with `LAYER game`.
- **No package depends on `apps/`.** What is in `packages/` cannot use a module declared in `apps/`; the apps do use
  the packages. If two apps need the same code, that code is a package.
- **`packages/vendor/` is not a layer.** It only has build wrappers for external libraries (the `dwe_cgltf` library);
  any module can link them the way it links a vcpkg library, without these rules.

## What each layer can use

| Layer | Can depend on |
|---|---|
| `core` | The standard library and basic third-party dependencies |
| `platform`, `rhi`, `net` | `core` |
| `render`, `audio`, `input`, `assets`, `localization`, `animation`, `scene`, `signs`, `video` | Base layers, `core` and other systems, without cycles (`animation`, `audio`, `render`, `localization`, `signs` and `video` use `assets`) |
| `script`, `ui`, `debug`, `ruleset` | Systems, base and `core` |
| `packages/game/*` | Services, systems, `net` and `core`; **not** `rhi` or `platform` |
| `packages/session` | `packages/game/*`, services, systems, `net` and `core`; **not** `rhi`, `platform` or `render` (it runs without a window in `dwe-sim` and in tests) |
| `packages/screens` | `packages/session`, `packages/game/*`, `ui`, `ruleset`, `localization`, `assets` and `core`; **not** `rhi`, `platform` or `render` (the screens' view models, tested without a window) |
| `apps/game` | Everything except `formats` |
| `packages/content` | `assets`, `platform` and `core` (`platform` for the user's data folder and for running `dwx`) |
| `apps/dwx` | `formats`, `assets` and `core` |
| `formats` | `core` |

Since `packages/content` uses `platform`, `dwe-sim` links `platform` (and with it SDL3) through `packages/content`,
although it still does not open windows. The `apps/dwx` tests also link `platform`, only to run `dwx` the way the game
does (`platform::runProcess`); the `dwx` program does not link it.

## How it is enforced

- `dwe_add_module(<name> DEPS ...)` declares the allowed dependencies. CMake does not let you include the headers of
  an undeclared module.
- When configuring, `tooling/cmake/DweModule.cmake` checks each dependency against the layers and fails if a
  forbidden arrow shows up: upward, between two modules in `packages/game/`, or toward `formats` from a module other
  than `dwx`. It also checks the [folder rules](#folder-rules): it knows which group each module belongs to
  (engine, game, package or app) from the folder where it is declared. CI configures on every pull request, so it
  fails there too.
- `scripts/packages.py` reads the same dependencies from the `CMakeLists.txt` files so that `scripts/dev.sh` knows
  what to build and test with `--filter` and `--affected`.
- An exception to these rules needs an ADR.
