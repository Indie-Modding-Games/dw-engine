# Modules

Each module of the repository has its page here: what it does, its public API and how it is used. The page lives at
`docs/modules/<group>/<module>.md` and is updated in the same PR that changes the module; the README in the module's
folder is short and links to it. The pages keep their address even though the code moved to `packages/` and `apps/`
(those for session, screens and content stay in `modules/apps/` and those for beta-gateway and versus-signal in `modules/services/`). The
layers and what each one may use are in the [dependency rules](../architecture/dependency-rules.md); the shape of a package, in the
[repository layout](../architecture/repository-layout.md#shape-of-a-package).

## Engine (`packages/engine/`)

| Module | What it does |
|---|---|
| [core](engine/core.md) | `Result` and errors without exceptions, log, messages, YAML, JSON Schema and math |
| [platform](engine/platform.md) | Window, events and gamepads on SDL3, and running other programs |
| [rhi](engine/rhi.md) | The only module that talks to Vulkan 1.3 |
| [render](engine/render.md) | The game's rendering on top of `rhi` |
| [audio](engine/audio.md) | Sound output, the mixer, the voices and the volumes |
| [input](engine/input.md) | Input by actions, never by keys or buttons |
| [assets](engine/assets.md) | Content by packs, stable ids and composed tables |
| [animation](engine/animation.md) | Sampling and blending of the models' animations |
| [scene](engine/scene.md) | Entities and components with EnTT |
| [script](engine/script.md) | Lua 5.5 with sol2 and the versioned, isolated API for mods |
| [ui](engine/ui.md) | The UI with RmlUi, with its logic in Lua |
| [localization](engine/localization.md) | The texts the player sees in their language |
| [signs](engine/signs.md) | The signs painted on the maps, rewritten in the player's language |
| [video](engine/video.md) | The movies: reading the video frame by frame and giving the image for each moment |
| [ruleset](engine/ruleset.md) | The ruleset format, the named strategies and the game clock |
| [debug](engine/debug.md) | Console and development panels with Dear ImGui |
| [net](engine/net.md) | Networking between two machines: WebRTC with a reliable lane and an unreliable one, lockstep and the signaling client |

## Game (`packages/game/`)

| Module | What it does |
|---|---|
| [partner](game/partner.md) | Partner care: needs, life, death and rebirth, training |
| [evolution](game/evolution.md) | When and into which form the partner evolves |
| [battle](game/battle.md) | The battle rules as a deterministic simulation |
| [field](game/field.md) | The field: movement, collision, triggers, warps and following |
| [inventory](game/inventory.md) | The bag, money, item use and the shops |
| [fishing](game/fishing.md) | Fishing: the rod, the float, the line and the fish in the water |
| [weather](game/weather.md) | The field weather and the seasons (improvements, "Weather" and "Seasons" options): rules per zone, own seed, fades |
| [gym](game/gym.md) | The town gym: the choreography of the courses and the tally of the gains |
| [tournament](game/tournament.md) | The arena tournaments: opponents, bracket, generated opponents and wins |
| [curling](game/curling.md) | The stones-on-ice game: sliding, collisions, points and the opponent |
| [shop_job](game/shop_job.md) | The improved canyon shop job (option): the customer line, the haggling and the pay |
| [versus](game/versus.md) | Online versus: identity, series, lineups and results between two machines in lockstep |
| [story](game/story.md) | The story state and the event script runtime |
| [save](game/save.md) | Versioned saved games, slots and autosave |
| [modes](game/modes.md) | The game modes: the `mode.yaml` manifest and the packs' catalog |

## Shared libraries (`packages/`)

| Module | What it does |
|---|---|
| [session](apps/session.md) | The game session: the only place that brings together the systems of `packages/game/` |
| [screens](apps/screens.md) | The screens' view models, without rules |
| [content](apps/content.md) | How the applications mount the packs |
| [formats](formats.md) | Readers for the original's formats; only `dwx` uses them |

## Applications (`apps/`)

| Application | What it does |
|---|---|
| [game](apps/game.md) | `dwe-game`, the game executable |
| [dwx](apps/dwx.md) | The asset tool: extract, validate, cook and pack |
| [sim](apps/sim.md) | `dwe-sim`: balance simulations without graphics |
| [launcher](apps/launcher.md) | The desktop launcher: reads the disc, updates the game and starts it |
| [studio](apps/studio.md) | DWE Studio, a development workbench: editor, build, run and compare with the original |
| [mapper](apps/mapper.md) | `dwe-mapper`, the map editor: collisions, exits, characters and story events over the background, and new maps |
| [beta-gateway](services/beta-gateway.md) | The beta gateway between the launcher and the GitHub releases, with the beta key |
| [versus-signal](services/versus-signal.md) | The signaling server for online versus: rooms with a code so two games connect over WebRTC |

## Other

| Module | What it does |
|---|---|
| [shaders](shaders.md) | Slang modules shared by the engine's shaders (`packages/engine/rhi/shaders/`) |
| [tests/support](tests/support.md) | What the tests of all the modules share |
| [tests/mods](tests/mods.md) | Synthetic packs the tests use to check the mods |
| [tests/autotest](tests/autotest.md) | The routes that play the game for real with keyboard, mouse or gamepad |
