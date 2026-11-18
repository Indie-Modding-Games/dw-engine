# 0003. Vulkan 1.3 for rendering and SDL3 for the platform

- **Status:** Accepted
- **Date:** 2026-10-18

## Context

The target platform is **PC: Windows and Linux**, including Steam Deck. Rendering must not have the limitations of the
PS1 GPU, and we want full control over it.

## Decision

- **Vulkan 1.3** as the base, with dynamic rendering and synchronization2.
- Support libraries: **vk-bootstrap** (initialization) and, from phase 2, **VMA** (memory). The Vulkan functions are
  linked with the system loader. volk is not used, because the Dear ImGui Vulkan backend that vcpkg ships links the
  loader directly and mixing the two breaks linking.
- Shaders in **Slang**, compiled to SPIR-V.
- **SDL3** for window, input (keyboard, mouse and gamepads), audio and files. SDL_GPU is not used.
- All Vulkan access stays inside the `rhi` module.

## Consequences

- Full control of rendering and access to the modern features of PC GPUs.
- More code of our own than with a ready-made abstraction layer.
- Vulkan 1.3 is supported by all PC GPUs from recent years, on Windows and on Linux.
- macOS, consoles and mobile are not target platforms. If they ever were, `rhi` is the only place that would change.

## Alternatives rejected

- **SDL_GPU:** simpler, but less control.
- **Direct3D 12:** it only works on Windows.
- **OpenGL:** an old API, with worse performance control.
