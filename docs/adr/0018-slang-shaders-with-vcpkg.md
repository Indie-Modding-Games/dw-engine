# 0018. Slang shaders compiled in the build with vcpkg's `slangc`

- **Status:** Accepted
- **Date:** 2026-10-19

## Context

[ADR 0003](0003-vulkan-and-sdl3.md) chooses Slang for shaders, but does not say how they are compiled or how the
SPIR-V reaches the game. The conditions: everything is built inside the container (ADR 0015) and on Windows with
`x64-windows-static`; nothing is installed on the host; the game must not look for shader files at startup; and the
other modules (map, models, UI) must be able to add their own shaders without touching the renderer.

## Decision

- **`slangc` is a vcpkg host tool**: `{"name": "shader-slang", "host": true}` in `vcpkg.json`. The port ships the
  official Slang binaries and only supports dynamic linking, so it is never a dependency of the executable: it only
  runs during the build. Linux uses the host triplet `x64-linux-libcxx` (dynamic CRT); the Windows presets set
  `VCPKG_HOST_TRIPLET=x64-windows` so the static target does not drag the port along. CMake looks for `slangc` only in
  the tools of that triplet (`NO_DEFAULT_PATH`), so a Vulkan SDK on the `PATH` does not sneak in another version, and
  writes the version when configuring.
- **`dwe_add_shaders(<target> FILES a.slang ...)`** (`tooling/cmake/DweShaders.cmake`) compiles each file, with all its
  entry points marked with `[shader("...")]`, to **a single SPIR-V module** (`-fvk-use-entrypoint-name`,
  `-matrix-layout-column-major` like GLM, warnings as errors, `import` dependencies via depfile and, in case a
  generator ignores that, on all the shared modules in `shaders/`).
- **The SPIR-V is embedded** in a generated header `<dwe/shaders/<name>.hpp>` with `dwe::shaders::<Name>`, a
  `std::array<std::uint32_t, N>`, private to the target. There are no shader files at runtime.
- The **shared Slang modules** live in `shaders/` (for example `packages/engine/rhi/shaders/dwe/bindless.slang`, which
  is imported with `import dwe.bindless;`). Each module's shaders live in `<module>/shaders/` and are compiled by its
  own `CMakeLists.txt`.

## Consequences

- Shaders are compiled and validated in every build, CI included; a Slang error breaks the build.
- Each track adds shaders with one line of CMake, without touching `render`.
- The executable carries its shaders inside: there are no paths to resolve or files that could be missing.
- Changing a shader recompiles only the `.cpp` that includes its header.
- Shader hot reload is left for later (`debug` would do it, compiling with the Slang API).
- The Slang version is pinned by the vcpkg baseline (2026.19 today).

## Alternatives rejected

- **glslang/GLSL:** it works, but ADR 0003 already chose Slang (modules, generics, pointers) and the vcpkg port works
  in the container.
- **Install the SPIR-V next to the executable:** it forces looking for files at startup and packaging them separately.
- **Compile at runtime with the Slang library:** it adds a big DLL to the game and does not let errors show up in the
  build.
