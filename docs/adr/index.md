# Architecture decision records (ADR)

An ADR records an important decision in writing: what the problem was, what was decided, what consequences it has and
which alternatives were rejected. It lets whoever comes later understand **why** the project is the way it is, without
having to ask.

We use a simplified version of the [MADR](https://adr.github.io/madr/) format.

## When an ADR is needed

- Choosing or changing a technology (language, library, format).
- Changing the architecture or the [dependency rules](../architecture/dependency-rules.md).
- Any exception to a project rule.

## How to propose one

1. Copy [`docs/adr/template.md`](https://github.com/Indie-Modding-Games/dw-engine/blob/main/docs/adr/template.md) as `NNNN-short-title.md`, with the next free number.
2. Fill it in with status **Proposed** and open it in a PR.
3. It is discussed in the PR. When there is agreement, the status changes to **Accepted** and it is merged.

## Statuses

| Status | Meaning |
|---|---|
| Proposed | Under discussion |
| Accepted | In force |
| Rejected | It was discussed and ruled out; it is kept so the discussion is not repeated |
| Superseded by NNNN | No longer in force; ADR NNNN replaces it |

An accepted ADR **is not rewritten**. If the decision changes, a new one is written and the old one is marked as
superseded.

## Index

| # | Decision | Status |
|---|---|---|
| [0001](0001-native-reimplementation.md) | Native reimplementation without PS1 limitations | Accepted |
| [0002](0002-cpp26.md) | C++26 with GCC and Clang | Accepted |
| [0003](0003-vulkan-and-sdl3.md) | Vulkan 1.3 for rendering and SDL3 for the platform | Accepted |
| [0004](0004-lua-and-sol2.md) | Lua 5.5 + sol2, sandboxed | Accepted |
| [0005](0005-asset-pipeline.md) | Assets in three states and layers, without binary fidelity | Accepted |
| [0006](0006-three-repositories.md) | Three repositories: code, design and art | Accepted |
| [0007](0007-no-exceptions.md) | No exceptions, with `std::expected` | Accepted |
| [0008](0008-headers-before-modules.md) | Headers now, C++ modules later | Accepted |
| [0009](0009-rmlui.md) | RmlUi for the game UI | Accepted |
| [0010](0010-own-rules-with-rulesets.md) | Own rules, organized in rulesets | Accepted |
| [0011](0011-asset-legal-policy.md) | "Bring your own data" and the legal policy for assets | Accepted |
| [0012](0012-ecs-with-entt.md) | Entities with EnTT | Accepted |
| [0013](0013-native-pc-game.md) | A native PC game | Accepted |
| [0014](0014-gamepads-and-input.md) | Gamepads and action-based input | Accepted |
| [0015](0015-container-development.md) | Build inside a Docker container | Accepted |
| [0016](0016-yaml-with-libyaml.md) | YAML with libyaml, wrapped in `core` | Accepted |
| [0017](0017-cooking-and-pak-files.md) | Cooking to KTX2 UASTC and Opus, and our own `.pak` archives with zstd | Accepted |
| [0018](0018-slang-shaders-with-vcpkg.md) | Slang shaders compiled in the build with vcpkg's `slangc` | Accepted |
| [0019](0019-bindless-descriptors.md) | A single bindless descriptor set and per-frame data by buffer address | Accepted |
| [0020](0020-map-camera-and-depth-layers.md) | Off-center map camera and depth layers as planes | Accepted |
| [0021](0021-cpu-animation-and-gpu-skinning.md) | Animation sampled on the CPU, skinning on the GPU, in an `animation` module | Accepted |
| [0022](0022-first-ruleset-faithful-to-the-original.md) | The first ruleset reproduces the original; the new design arrives as rulesets | Accepted |
| [0023](0023-remaster-pipeline.md) | Remaster by recipes, with interchangeable providers, content cache and local by default | Superseded by 0025 |
| [0024](0024-co-op-with-authoritative-host.md) | Online co-op with an authoritative host and players driven by controllers | Proposed |
| [0025](0025-high-definition-in-the-engine.md) | High definition in the engine, with only deterministic steps offline | Accepted |
| [0026](0026-game-variants-as-modes.md) | Game variants as game modes with fixed options | Accepted |
