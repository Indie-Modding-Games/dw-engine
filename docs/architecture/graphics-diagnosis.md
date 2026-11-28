# Graphics diagnosis

When the game cannot run on the player's graphics card, or runs worse than it could, the player gets the exact reason,
with the real names (card, driver version, the program in the way), what to do in numbered steps, a button to the
right driver page, and what the game does meanwhile. The same diagnosis feeds every place that talks about it: the
launcher's first check, the dialog after a failed start (exit 114), the dialog after an early device loss (exit 113),
and the game's own notice in software or compatible mode. Nothing is looked up online: the tables below are part of the
game.

## Who does what

| Part | Where | What it does |
|---|---|---|
| Facts | `packages/engine/rhi/src/system_facts.cpp` | Reads the computer: every Vulkan device of a fresh instance (properties, driver id, features, largest device-local heap), the system's display adapters with or without a Vulkan driver, implicit layers, remote session, virtual machine, laptop |
| Reasons | `packages/engine/rhi/src/gpu_diagnosis.cpp` | Pure function `diagnose(facts)`: reason codes, most actionable first, with their values. Unit tests per case in `rhi/tests/gpu_diagnosis_test.cpp` |
| JSON | `diagnosisJson` | `dwe-game --check-system` prints it as `diagnosis=<json>`; a failed start writes `dwe-diagnosis 1 <json>` to the game's log |
| Advice | `apps/launcher/src-tauri/src/diagnosis.rs` | Turns each code into step keys and buttons (driver pages, Windows graphics settings), builds the report "Copy details" copies; cargo tests |
| Text | `apps/launcher/ui/js/app.js` (`adviceBlock`), `ui/js/i18n/*.js` (`why.*`) | What happened, why, the numbered steps, the buttons, what the game does meanwhile, the other reasons, the collapsed details, in 9 languages |
| In game | `content/packs/game/ui/software_notice`, `localization/*/software_notice.yaml` | The notice names the reason the launcher passed in `DWE_GRAPHICS_REASON` (`code\|key=value\|…`), or, in compatible mode without the launcher, the game's own first reason |

When the launcher narrows the drivers to its software one (`VK_DRIVER_FILES`), the game cannot see the real card any
more: the reason then comes from the launcher's own check, made before narrowing.

## Reasons

Blockers come first, then warnings, then information. Within each, the one the player can act on comes first; a
"Vulkan too old" or "missing features" reason whose fix is not a driver moves down. The launcher leaves out a reason an
earlier one already covers: an older Vulkan or the compatible path under "driver too old" for the same card, "no
graphics card" under a named adapter, "integrated card" under "dedicated card unused" (`covers` in `diagnosis.rs`).

| Code | When | Fix | Steps |
|---|---|---|---|
| `no-loader` | `libvulkan` / `vulkan-1.dll` missing (the probe cannot start) | driver | Linux: install the loader package; else install the driver |
| `window-failed` | The window or its surface did not open | settings | Close full-screen programs; on Linux without `DISPLAY`/`WAYLAND_DISPLAY`, start from the desktop. The launcher does not step down to a slower mode for it |
| `remote-session` | Remote desktop (`SM_REMOTESESSION`, RDP/remote adapters) | settings | Play at the computer; game streaming tools |
| `implicit-layers` | Vulkan failed and a known overlay layer is installed | settings | Close the named programs (OBS, Overwolf, RivaTuner, Bandicam, ReShade, MangoHud, Epic, Steam, AMD switchable graphics, GamePP, WeGame), update them |
| `basic-display-adapter` | Windows' basic display adapter or no vendor driver | driver | Install the maker's driver, restart |
| `dedicated-gpu-unused` | A laptop's dedicated card has no Vulkan device or is not the one picked | driver | Windows: Settings › System › Display › Graphics (`ms-settings:display-advancedgraphics`), NVIDIA/AMD panel, driver. Linux: driver, `prime-run` / `DRI_PRIME=1` |
| `driver-too-old` | Driver older than the first with Vulkan 1.3 (table below), on a family that can run it | driver | Install the named version or newer |
| `no-vendor-driver` | An adapter with no Vulkan driver (Linux: `nouveau`, no driver) | driver | Install the driver |
| `out-of-memory` | `VK_ERROR_OUT_OF_*_MEMORY` at start | settings | Close programs, Performance preset |
| `device-lost` | `VK_ERROR_DEVICE_LOST` at start or in the first seconds | driver | Update, clean install, no overclock |
| `vulkan-too-old` | The card reports Vulkan 1.0 or 1.1 | driver or hardware by family | Update the driver, or the card cannot |
| `missing-features` | Vulkan 1.2 without the compatible path's features | driver or hardware | Same |
| `instance-failed`, `device-failed` | `VK_ERROR_INCOMPATIBLE_DRIVER`, `VK_ERROR_INITIALIZATION_FAILED` and others | driver | Clean driver install, close overlays |
| `virtual-machine` | DMI vendor/product or a virtual adapter | settings | 3D acceleration, play on the host |
| `compatible-path` | Plays on the Vulkan 1.2 compatible path | driver or none | Update the driver when it would raise it |
| `hardware-no-vulkan` | An adapter whose family never had Vulkan | hardware | A newer card |
| `no-gpu` | Nothing else explains it | unknown | Install the driver |
| `low-vram` | A discrete card with less than 1 GiB | hardware | Performance preset |
| `integrated-gpu` | The chosen card is integrated | information | Charger, Performance preset |

## Driver decoding

`VkPhysicalDeviceProperties::driverVersion` is written each maker's way: NVIDIA packs 10.8.8.6 bits (566.36),
Intel on Windows packs `raw >> 14` and `raw & 0x3fff` (101.5186), Mesa and AMD use `VK_MAKE_VERSION`. AMD's own
driver reports its Vulkan driver version (2.0.x), not the Adrenalin one, so it has no row in the table.

## Tables (checked 2026-11-27)

First drivers with Vulkan 1.3:

| Driver | Version | Source |
|---|---|---|
| NVIDIA, Windows | 511.65 | NVIDIA release notes, 2022-02-01 |
| NVIDIA, Linux | 510.47.03 | NVIDIA production branch, 2022-02-01 |
| Intel, Windows | 101.1660 | geeks3d, 2022-04-01 |
| Mesa RADV and ANV | 22.0.0 | docs.mesa3d.org/relnotes/22.0.0.html, 2022-03-09 |

What a family reaches with its newest driver: NVIDIA Fermi and older and AMD TeraScale never had Vulkan; NVIDIA
Kepler stops at Vulkan 1.2 (474.89 on Windows); Intel has no Windows Vulkan driver before the 6th generation; on Linux
Mesa's hasvk gives Ivy Bridge and Haswell Vulkan 1.2 and Broadwell 1.3; AMD GCN 1 to 3 on Windows end with the 22.6.1
legacy driver, whose Vulkan version is not published (unknown: the advice says "a newer driver may fix it").

Overlay layers come from their manifests and from other engines' workarounds; drivers' own helper layers (NVIDIA
Optimus, Mesa device select) are not reported.

## Trying it

`dwe-game --check-system` prints the diagnosis. `VK_DRIVER_FILES=/nonexistent.json` hides every driver;
`VK_DRIVER_FILES=<lvp_icd.json>` leaves only lavapipe. The launcher's page can be looked at in a browser with a mock
of `window.__TAURI__` that answers `check_system` with an `advice` list.
