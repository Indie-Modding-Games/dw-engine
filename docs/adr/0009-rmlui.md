# 0009. RmlUi for the game UI

- **Status:** Accepted
- **Date:** 2026-10-18 (accepted on 2026-10-19, with the phase 2 details)

## Context

We want to improve the menus, add a HUD and minimaps, and let design and mods change the UI without recompiling.
The UI must look good at any resolution and be fully usable with mouse, keyboard and gamepad
([ADR 0013](0013-native-pc-game.md), [ADR 0014](0014-gamepads-and-input.md)).

## Decision

The game UI is built with **RmlUi**: HTML-like documents (RML), CSS-like styles (RCSS) and logic in Lua. It is drawn
with our `render` module. Dear ImGui is left only for the debug tools and the editor.

Details, fixed in phase 2:

| Topic | Decision |
|---|---|
| Version | RmlUi 6.3, from the vcpkg `rmlui` port, without its default features and with `freetype`. No `lua`, `svg` or `lottie` |
| Module | `packages/engine/ui` (services layer). RmlUi is private to the module: its public API does not expose RmlUi types |
| Files | Documents, styles, scripts, fonts and images are read **by asset id** through the VFS: `ui/pause_menu/logic.lua` is the asset `ui/pause_menu/logic`. That is why each document goes in its own folder (`document.rml`, `logic.lua`; its style, in the skin's folder): two files with the same name and a different extension would give the same id |
| Rendering | A `RenderInterface` on top of the public `render` API (no Vulkan in `ui`). `Ui::update()` calls RmlUi's `Render()` outside the frame: it keeps the geometry on the CPU, creates the textures in the bindless table and records the calls; the `UiRenderer` (a `render::Drawer` of the UI pass, with its Slang shader) replays them from GPU buffers that are created once when the geometry is compiled, with clipping (*scissor*) and transforms. Everything in premultiplied alpha, as RmlUi expects: file images are premultiplied when loaded. No clip masks for now |
| Resolution | The context measures the window in physical pixels. A *dp* is one pixel at 720 lines: the ratio is `min(height/720, width/960)` times the UI scale the player chooses, so the UI is equally readable at 800×600 and at 4K |
| Font | Noto Sans (regular and bold), licensed under SIL OFL 1.1, in `content/packs/game/ui/fonts/` with its license next to it. All the `.ttf`/`.otf` fonts in `ui/fonts/` are loaded, so a pack can add others |
| Input | Navigation comes from **actions** (`move_*`, `confirm`, `cancel`, `menu`), not from keys: keyboard, d-pad and stick navigate the same way and can be remapped. Directions use RmlUi's `nav` properties; `confirm` clicks the focused element. The mouse goes straight to RmlUi (movement, buttons, wheel), and while a menu is open the focus follows the pointer. Button prompts come from the action map and the last device used |
| Capture | While a modal document (the menu) is shown, the UI keeps the input: the game sees neither its actions nor the clicks. Without a menu, the HUD captures nothing (`pointer-events: none`) |
| Lua | RmlUi's Lua plugin does not work for us: it is made for Lua 5.1–5.4 and its API is not ours. The UI uses **its own `ScriptVm`** (the same sandbox as `script`, without `io`, `os` or code loading, with memory and instruction limits) and its own API: the `ui` table (`send`, `setting`, `prompt`, `device`, `show`, `hide`…), the `Element` and `Event` types, and the handlers `on_show`, `on_action`, `on_setting`, `on_device`, `on_tick`. Each document has its own Lua environment, where its `<script>` tags and event attributes (`onclick="..."`) run |
| App control | Menus change nothing by themselves: they send **commands** (`ui.send("fullscreen", true)`) that the app applies, and the app publishes the state with `setSetting`, which is what they show. That way the UI does not depend on the game |
| Hot reload | In development, the UI rereads the files of the open documents every half second and reloads the ones that change: the new document replaces the old one only if it loads correctly, and keeps whether it was visible and its place among the modals |

## Consequences

- Menus are edited as text, with hot reload (guide: [Edit menus](../guides/edit-menus.md)).
- The RmlUi render backend on top of `render` has to be maintained.
- The UI's Lua API is part of the contract with mods, like the one in `script`.
- RmlUi brings FreeType and its dependencies (zlib, libpng, bzip2, brotli) into the executable.
- RmlUi shares libc++ container instances with the engine; in builds with AddressSanitizer some copies annotate the
  containers and others do not. The executables that use `ui` link the `rhi` device, which already turns off
  `detect_container_overflow` for the same reason with vk-bootstrap, so RmlUi is built the same way in all builds and
  a debug build without sanitizers links.

## Alternatives rejected

- **A UI of our own:** full control, but a lot of work before having the first menu.
- **Dear ImGui for the game:** it is meant for tools, not for menus with their own style.
- **RmlUi's Lua plugin:** it does not support Lua 5.5 and would expose its own API, without our sandbox.
- **Sharing the game's `ScriptVm`:** a broken menu or a UI mod could overwrite the game state.
