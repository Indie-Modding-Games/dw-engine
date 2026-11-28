# Diagnóstico gráfico

Cuando el juego no puede usar la tarjeta gráfica del jugador, o la usa peor de lo que podría, el jugador ve la razón
exacta con los nombres reales (tarjeta, versión del controlador, el programa que estorba), qué hacer en pasos
numerados, un botón a la página de controladores correcta y qué hace el juego mientras tanto. El mismo diagnóstico
alimenta todos los sitios que hablan de ello: la primera comprobación del launcher, el diálogo tras un arranque fallido
(salida 114), el diálogo tras perder la tarjeta al empezar (salida 113) y el aviso del propio juego en modo software o
compatible. No se consulta nada en internet: las tablas de abajo van dentro del juego.

## Quién hace qué

| Parte | Dónde | Qué hace |
|---|---|---|
| Hechos | `packages/engine/rhi/src/system_facts.cpp` | Lee el equipo: cada dispositivo Vulkan de una instancia nueva (propiedades, id del controlador, funciones, mayor memoria local), los adaptadores de pantalla del sistema con o sin controlador Vulkan, capas implícitas, escritorio remoto, máquina virtual, portátil |
| Razones | `packages/engine/rhi/src/gpu_diagnosis.cpp` | Función pura `diagnose(facts)`: códigos de razón, la más accionable primero, con sus valores. Pruebas por caso en `rhi/tests/gpu_diagnosis_test.cpp` |
| JSON | `diagnosisJson` | `dwe-game --check-system` lo imprime como `diagnosis=<json>`; un arranque fallido escribe `dwe-diagnosis 1 <json>` en el log del juego |
| Consejo | `apps/launcher/src-tauri/src/diagnosis.rs` | Convierte cada código en pasos y botones (páginas de controladores, configuración de gráficos de Windows) y arma el informe de «Copiar detalles»; pruebas cargo |
| Texto | `apps/launcher/ui/js/app.js` (`adviceBlock`), `ui/js/i18n/*.js` (`why.*`) | Qué pasó, por qué, los pasos, los botones, qué hace el juego mientras tanto, las otras razones y los detalles plegados, en 9 idiomas |
| En el juego | `content/packs/game/ui/software_notice`, `localization/*/software_notice.yaml` | El aviso nombra la razón que el launcher pasa en `DWE_GRAPHICS_REASON` (`código\|clave=valor\|…`) o, en modo compatible sin launcher, la primera razón del propio juego |

Cuando el launcher limita los controladores al suyo de software (`VK_DRIVER_FILES`), el juego ya no ve la tarjeta
real: la razón sale entonces de la comprobación del launcher, hecha antes de limitar.

## Razones

Primero los bloqueos, luego los avisos y al final la información. Dentro de cada grupo va primero lo que el jugador
puede arreglar; «Vulkan demasiado antiguo» o «faltan funciones» bajan cuando no se arreglan con un controlador. El
lanzador deja fuera una razón que otra anterior ya cubre: un Vulkan más antiguo o el modo compatible bajo «controlador
demasiado antiguo» de la misma tarjeta, «sin tarjeta gráfica» bajo un adaptador con nombre, «tarjeta integrada» bajo
«tarjeta dedicada sin usar» (`covers` en `diagnosis.rs`).

| Código | Cuándo | Arreglo | Pasos |
|---|---|---|---|
| `no-loader` | Falta `libvulkan` / `vulkan-1.dll` (la sonda no arranca) | controlador | Linux: instalar el paquete del cargador; si no, el controlador |
| `window-failed` | La ventana o su superficie no se abrió | ajustes | Cerrar programas a pantalla completa; en Linux sin `DISPLAY`/`WAYLAND_DISPLAY`, abrir desde el escritorio. El launcher no baja a un modo más lento por esto |
| `remote-session` | Escritorio remoto | ajustes | Jugar frente al equipo; herramientas de streaming de juegos |
| `implicit-layers` | Vulkan falló y hay una capa de overlay conocida | ajustes | Cerrar los programas nombrados (OBS, Overwolf, RivaTuner, Bandicam, ReShade, MangoHud, Epic, Steam, gráficos conmutables de AMD, GamePP, WeGame) y actualizarlos |
| `basic-display-adapter` | Adaptador de pantalla básico de Windows o sin controlador del fabricante | controlador | Instalar el controlador, reiniciar |
| `dedicated-gpu-unused` | La tarjeta dedicada de un portátil no tiene Vulkan o no es la elegida | controlador | Windows: Configuración › Sistema › Pantalla › Gráficos (`ms-settings:display-advancedgraphics`), panel NVIDIA/AMD, controlador. Linux: controlador, `prime-run` / `DRI_PRIME=1` |
| `driver-too-old` | Controlador anterior al primero con Vulkan 1.3 (tabla), en una familia que puede | controlador | Instalar esa versión o más nueva |
| `no-vendor-driver` | Adaptador sin controlador Vulkan (Linux: `nouveau`, ninguno) | controlador | Instalar el controlador |
| `out-of-memory` | `VK_ERROR_OUT_OF_*_MEMORY` al arrancar | ajustes | Cerrar programas, ajuste Rendimiento |
| `device-lost` | `VK_ERROR_DEVICE_LOST` al arrancar o en los primeros segundos | controlador | Actualizar, instalación limpia, sin overclock |
| `vulkan-too-old` | La tarjeta ofrece Vulkan 1.0 o 1.1 | controlador o hardware según familia | Actualizar, o la tarjeta no puede |
| `missing-features` | Vulkan 1.2 sin las funciones del modo compatible | controlador o hardware | Igual |
| `instance-failed`, `device-failed` | `VK_ERROR_INCOMPATIBLE_DRIVER`, `VK_ERROR_INITIALIZATION_FAILED` y otros | controlador | Instalación limpia, cerrar overlays |
| `virtual-machine` | Fabricante/producto DMI o adaptador virtual | ajustes | Aceleración 3D, jugar en el equipo real |
| `compatible-path` | Juega por el modo compatible de Vulkan 1.2 | controlador o nada | Actualizar cuando eso lo suba |
| `hardware-no-vulkan` | Adaptador de una familia que nunca tuvo Vulkan | hardware | Una tarjeta más nueva |
| `no-gpu` | Nada más lo explica | desconocido | Instalar el controlador |
| `low-vram` | Tarjeta dedicada con menos de 1 GiB | hardware | Ajuste Rendimiento |
| `integrated-gpu` | La tarjeta elegida es integrada | información | Cargador, ajuste Rendimiento |

## Versión del controlador

`VkPhysicalDeviceProperties::driverVersion` va a la manera de cada fabricante: NVIDIA usa bits 10.8.8.6 (566.36),
Intel en Windows `raw >> 14` y `raw & 0x3fff` (101.5186), Mesa y AMD `VK_MAKE_VERSION`. El controlador propio de AMD
informa su versión Vulkan (2.0.x), no la de Adrenalin, así que no tiene fila en la tabla.

## Tablas (comprobadas el 2026-11-27)

Primeros controladores con Vulkan 1.3:

| Controlador | Versión | Fuente |
|---|---|---|
| NVIDIA, Windows | 511.65 | Notas de NVIDIA, 2022-02-01 |
| NVIDIA, Linux | 510.47.03 | Rama de producción de NVIDIA, 2022-02-01 |
| Intel, Windows | 101.1660 | geeks3d, 2022-04-01 |
| Mesa RADV y ANV | 22.0.0 | docs.mesa3d.org/relnotes/22.0.0.html, 2022-03-09 |

Hasta dónde llega cada familia con su controlador más nuevo: NVIDIA Fermi y anteriores y AMD TeraScale nunca tuvieron
Vulkan; NVIDIA Kepler se queda en Vulkan 1.2 (474.89 en Windows); Intel no tiene Vulkan en Windows antes de la 6.ª
generación; en Linux, hasvk de Mesa da Vulkan 1.2 a Ivy Bridge y Haswell y 1.3 a Broadwell; AMD GCN 1 a 3 en Windows
termina con el controlador heredado 22.6.1, cuya versión Vulkan no está publicada (desconocido: el consejo dice «un
controlador más nuevo puede arreglarlo»).

Las capas de overlay salen de sus manifiestos y de los apaños de otros motores; no se informan las capas auxiliares de
los propios controladores (NVIDIA Optimus, selección de dispositivo de Mesa).

## Probarlo

`dwe-game --check-system` imprime el diagnóstico. `VK_DRIVER_FILES=/nonexistent.json` oculta todos los controladores;
`VK_DRIVER_FILES=<lvp_icd.json>` deja solo lavapipe. La página del launcher se puede ver en un navegador con un
simulacro de `window.__TAURI__` que responda a `check_system` con una lista `advice`.
