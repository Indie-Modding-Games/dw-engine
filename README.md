# dwe

**dwe is a native C++26 reimplementation of a classic late-1990s creature-raising RPG, built for PC and modding.** It aims to preserve the original game's visual identity and behavior while providing a new engine, editable content, and Lua scripting. `dwe` is a working name.

The project is under active development. Individual systems and menu demos are available, but full-game fidelity and end-to-end completion are still in progress.

## How it works

The game runs on a new C++ engine. Lua supplies scripting and UI behavior, while content packs provide data, layouts, translations, and assets. Original behavior is studied through reference gameplay and a separate, read-only decompilation; that reference repository is not a build dependency.

Each user supplies their own supported disc image. The `dwx` tool extracts and converts its content locally into formats the engine can load and developers can edit. Original game assets, code, and extracted data are not distributed in this repository. See the [asset policy](docs/legal/assets.md).

This is an independent project, unaffiliated with and not endorsed by the original rights holders. All trademarks belong to their respective owners.

## Technology stack

| Area | Technologies | Role in dwe |
|---|---|---|
| Native engine | **C++26** | Engine services, simulation, gameplay systems, content tools, and application integration. |
| Platform and input | **SDL3** | Window creation, events, keyboard and gamepad input, and audio output. |
| Graphics | **Vulkan 1.3**, vk-bootstrap, Vulkan Memory Allocator | Rendering, GPU resources, pipelines, and presentation. SDL3 creates the window; the renderer uses Vulkan directly. |
| Shaders and mathematics | **Slang**, GLM | GPU shaders, transforms, camera mathematics, and skinning. |
| Scripting | **Lua**, sol2 | Script bindings and controlled scripting environments. Menu behavior also uses Lua. |
| Game interface | **RmlUi**, RML, RCSS, FreeType | Data-driven menus and HUDs, layout, styling, navigation, and text. A custom bitmap-font path supports the original visual style using locally extracted content. |
| Debug interface | **Dear ImGui** | Engine inspection and development overlays, separate from the game's menus. |
| Scene organization | **EnTT** | Entity/component storage for the scene and field systems. |
| Models and images | **glTF/GLB**, cgltf, PNG, lodepng | Editable source assets and validated model/image loading. Animation is sampled on the CPU; skeletal skinning runs on the GPU. |
| Packaged content | **KTX2**, Zstandard, `.pak` archives | Cooked textures, compressed content, and runtime packaging. |
| Audio | **Opus**, TinySoundFont, SDL3 | Encoded audio clips, SoundFont-based music synthesis, mixing, and device output. |
| Data and localization | **YAML**, libyaml, JSON Schema | Content tables, rulesets, pack definitions, translations, and validation schemas. |
| Development tools | **DWE Studio**, Electron, electron-vite, Monaco Editor | A desktop workbench for editing, running, and observing the project. Studio is a companion tool; the native game does not run inside Electron. |
| Build and dependencies | **CMake**, Ninja, vcpkg, Docker, Clang, ccache | Reproducible development environments, dependency management, compilation, and build caching. |
| Quality tools | **doctest**, CTest, sanitizers, clang-tidy, clang-format, StyLua, luacheck | Automated checks, diagnostics, formatting, and static analysis. |
| Documentation | **VitePress**, Mermaid | The project's documentation site and architecture diagrams. |

SDL3 and Vulkan have complementary responsibilities here. Dwe currently uses its own Vulkan rendering layer rather than SDL's GPU abstraction. C++ owns the native systems; Lua and editable packs expose behavior and presentation without requiring every change to rebuild the engine.

## Repository layout

- [`engine/`](engine/): reusable services, rendering, audio, scripting, UI, assets, and scene infrastructure.
- [`game/`](game/): gameplay systems such as field movement, partner care, evolution, inventory, and combat.
- [`apps/`](apps/): the native game, session integration, `dwx` content tools, and DWE Studio.
- [`formats/`](formats/): readers and converters for the user's local source content.
- [`content/`](content/): distributable game-pack definitions, scripts, UI documents, translations, and schemas.
- [`docs/`](docs/index.md): architecture, formats, development guides, conventions, and project decisions.

The content pipeline separates editable source files from cooked runtime files. Development workflows can load source content directly; packaging converts it into optimized runtime formats. See the [asset pipeline](docs/architecture/assets-pipeline.md).

## Build and run

Follow the [step-by-step launch guide](docs/getting-started/jugar.md) (Spanish) for disc extraction, content paths, controls, and troubleshooting. The [file extension guide](docs/formats/extensiones.md) explains character models, maps, and other content files.

The documented development workflow targets Linux. You need Git, Docker, and a GPU driver supporting Vulkan 1.3. The development image provides the compiler and build dependencies. See [environment setup](docs/getting-started/setup.md) for details.

```bash
scripts/dev.sh image
scripts/dev.sh configure
scripts/dev.sh build
```

After preparing your own extracted content, launch the native game with its content directory:

```bash
scripts/dev.sh run --content /path/to/extracted-content
```

Building the repository does not download the original game or populate its assets. Follow the [local extraction workflow](docs/architecture/assets-de-desarrollo.md) to prepare your content.

## Documentation

The full documentation is maintained in Spanish as a VitePress site. Start with the [documentation index](docs/index.md), then explore [rendering](docs/architecture/render.md), [UI](docs/architecture/ui.md), [models and animation](docs/architecture/models.md), and [audio](docs/architecture/audio.md).

```bash
scripts/dev.sh docs-dev   # Local site at http://localhost:5173
scripts/dev.sh docs       # Build the site into build/docs-site
```

## Related repositories

- [dw-engine-ai](https://github.com/Indie-Modding-Games/dw-engine-ai): private project instructions and work continuity.
- [dw-engine-assets](https://github.com/Indie-Modding-Games/dw-engine-assets): private processed-content backup and restoration tooling.

These repositories require separate access. They are not submodules and are not downloaded as part of the engine build. Engine source and technical documentation remain in this repository.

## License

The project's license is not yet finalized. See [ADR 0011](docs/adr/0011-politica-legal-de-assets.md). Third-party dependencies retain their respective licenses; original game content is governed separately by the [asset policy](docs/legal/assets.md).
