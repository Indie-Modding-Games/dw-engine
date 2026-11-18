# Repository layout

The repository is a Turborepo-style monorepo: **`apps/`** holds what runs (the game, the tools, the launcher),
**`packages/`** holds the libraries grouped by layer (engine, game and the libraries that the apps share) and
**`tooling/`** holds the common build configuration. Each package is self-contained: its `CMakeLists.txt`, its code,
its tests and a README that says what it is, what it depends on and how to build or test it on its own
(`scripts/dev.sh build --filter <package>`, see [getting started](../getting-started/setup.md#build-and-test-one-package)).

```
dw-engine/
├─ apps/                    what runs (modules/apps/)
│  ├─ game/                 dwe-game, the game executable
│  ├─ dwx/                  extract · validate · cook · pack · export-images · evolution-guide · upscale
│  ├─ sim/                  dwe-sim: balance simulations without graphics
│  ├─ launcher/             the desktop launcher (Tauri)
│  ├─ studio/               DWE Studio, development workbench (Electron)
│  ├─ studio-vscode/        mockup of DWE Studio as an editor extension (not built)
│  ├─ beta-gateway/         the beta gateway between the launcher and GitHub (Cloudflare Worker)
│  └─ versus-signal/        the online versus signaling server (Cloudflare Worker)
├─ packages/                libraries, grouped by layer
│  ├─ engine/               engine modules (modules/engine/)
│  │  ├─ core/              Result, log, YAML, JSON Schema, math
│  │  ├─ platform/          window, events and gamepads (SDL3)
│  │  ├─ net/               WebRTC, reliable channel, lockstep and signaling client
│  │  ├─ rhi/               the only one that talks to Vulkan; shaders/ has the shared Slang modules
│  │  ├─ render/
│  │  ├─ audio/
│  │  ├─ input/             actions, never keys
│  │  ├─ assets/            packages, VFS, ids and tables
│  │  ├─ animation/         model clips: poses, loops, root motion
│  │  ├─ scene/             entities and components (EnTT)
│  │  ├─ script/            Lua and the mods' API
│  │  ├─ ui/                RmlUi with its logic in Lua
│  │  ├─ localization/      texts in the player's language
│  │  ├─ signs/             translated map signs
│  │  ├─ video/             movies
│  │  ├─ debug/             development console and panels (Dear ImGui)
│  │  └─ ruleset/           ruleset format, strategies by name and game clock
│  ├─ game/                 game rules (modules/game/)
│  │  ├─ partner/           care, life, training
│  │  ├─ evolution/
│  │  ├─ battle/
│  │  ├─ field/             movement, collision, warps, partner following
│  │  ├─ inventory/         bag, money, item use and shops
│  │  ├─ fishing/ · weather/ · gym/ · tournament/ · curling/
│  │  ├─ versus/            the online versus between two machines
│  │  ├─ story/             story state and event scripts
│  │  └─ save/              saved games: slots, autosave and migrations
│  ├─ session/              the game session: joins the packages/game/ systems, without a window
│  ├─ screens/              view models of the screens
│  ├─ content/              how the game and dwe-sim mount the packages
│  ├─ formats/              readers for the original disc's formats: images, models, maps, audio, video,
│  │                        scripts (only dwx uses them)
│  └─ vendor/               build wrappers for external libraries that come from vcpkg (cgltf), not our own code
├─ tooling/                 common build configuration
│  ├─ cmake/                our own functions: dwe_add_module, dwe_add_shaders, warnings, sanitizers
│  │  └─ toolchains/        compiler and standard library for each platform
│  ├─ triplets/             our own vcpkg triplets (dependencies with Clang and libc++)
│  └─ vcpkg-ports/          our own vcpkg ports
├─ content/
│  ├─ schemas/              JSON Schema for each document type
│  ├─ packs/                the team's text packages: game (data, rulesets, UI and Lua) and es-translation
│  └─ enhance/              image upscaling recipes (dwx extract and dwx upscale)
├─ tests/
│  ├─ autotest/             routes that actually play the game (and training/, the training check)
│  ├─ mods/                 synthetic packages for the mod tests
│  └─ support/              what the tests of all modules share
├─ docs/                    this documentation: the VitePress site (dev.sh docs) and each module's page
│                           (modules/)
├─ scripts/                 dev.sh: the entry point for all development (image, build, test, run, docs, packages…);
│                           packages.py (the packages and what a change affects), gate.sh, training-check.sh,
│                           launcher.sh, packaging (package_*.sh, package/) and publishing (ci/,
│                           release_local.sh)
├─ .devcontainer/           development Docker images: Clang, libc++, CMake, vcpkg, Node and VitePress (ADR 0015);
│                           the launcher's and the Wine one
├─ .github/
│  ├─ workflows/            ci.yml (pull request checks), release.yml (test version on every
│  │                        merge), promote.yml (stable version), platform-check.yml, docs-pages.yml
│  ├─ actions/              steps shared by the workflows
│  ├─ CONTRIBUTING.md       link to docs/contributing.md
│  └─ pull_request_template.md
├─ CMakeLists.txt           add_subdirectory(packages) and add_subdirectory(apps), and the repository's tests
├─ CMakePresets.json        linux-clang-*, windows-clang-*, windows-cross*, macos-*, ci and windows-ci
├─ vcpkg.json               dependencies with pinned versions
├─ .clang-format · .clang-tidy · .clangd · .editorconfig · .gitattributes · .gitignore · .luacheckrc · stylua.toml
└─ README.md · LICENSE
```

`content/base/` does not appear because it is **never in the repository**: it is generated from each user's disc and
is in `.gitignore` (see [legal policy](../legal/assets.md)).

The packages in `packages/` cannot depend on anything in `apps/`, the engine only uses the engine, and
`packages/game/` only has modules of the game layer. CMake checks this when configuring
([dependency rules](dependency-rules.md#folder-rules)). `packages/vendor/` is outside those rules: it has no
modules, only build wrappers for external libraries that vcpkg installs without a CMake package.

## Shape of a package

All packages with C++ code, whether engine, game or apps, have the same shape:

```
packages/engine/render/
├─ CMakeLists.txt           dwe_add_module(render LAYER systems DEPS core rhi)
├─ README.md                what it is, what it depends on and the commands to build and test it on its own
├─ include/dwe/render/      public API: the only thing the other modules see
├─ src/                     implementation and private headers
└─ tests/                   the module's unit tests
```

- The package's name is that of its folder (`render`). CMake gives it a `pkg_render` target that builds its libraries,
  programs and tests, and a ctest label `render` with its tests.
- Its long documentation is a page of the site, `docs/modules/<group>/<module>.md` (here
  `docs/modules/engine/render.md`); the folder's README is short and links to it.
- Another module can only include `<dwe/render/...>`. What is in `src/` is private.
- Namespaces follow the module: `dwe::render`, `dwe::game::battle`.
- A new module is declared with `dwe_add_module`, which applies the warnings, the sanitizers and the dependency
  check; a program, with `dwe_add_app`.
