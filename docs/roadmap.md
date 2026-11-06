# Plan por fases

Cada fase tiene un criterio de cierre que se puede comprobar. Una fase no se da por terminada hasta que se cumple.

Este documento es el resumen con el estado del día. Lo que hace cada fase, sus requisitos y cómo se comprueban
están en las [especificaciones por fase](plan/index.md).

| Fase | Nombre | Tamaño | Estado |
|---|---|---|---|
| 0 | Base | S | Cerrada (2026-10-20): Linux y Windows pasan en la CI de GitHub; la limpieza de los repositorios antiguos espera una decisión del equipo |
| 1 | Tubería de assets | M | Cerrada (2026-10-20): el disco US se extrae entero (0 pendientes, 0 parciales) y el motor carga el resultado, extraído y cocinado; el formato cocinado de los vídeos pasa a la fase 5 |
| 2 | Prueba vertical | M | Cerrada (2026-10-20) |
| 3 | Núcleo jugable | L | Cerrada (2026-10-21) |
| 4 | El juego original entero | XL | En curso: la historia entera se juega, del título a los créditos, y la beta cerrada llega por el lanzador |
| 5 | Mods, modos y mejoras | Continuo | Pendiente |

## Dónde estamos (2026-11-06)

- **La fase 4 es solo el juego original.** Desde el 2026-10-22 la fase 4 es el juego original (vanilla) entero y
  jugable. Lo que no es del original pasa a la fase 5: modos de juego, el modo versus, la API de mods completa, la
  historia reescrita y el formato cocinado de los vídeos.
- **La historia, por etapas.** El port se entrega etapa a etapa detrás del candado de historia
  (`content/packs/game/rules/original/story_lock.yaml`, opción `--story-stage`). Las nueve etapas están abiertas
  (`active_stage: stage_9`) y las rutas `tests/autotest/route-full-01` a `route-full-09` juegan la historia entera
  encadenadas ([prueba vertical](guides/prueba-vertical.md)).
- **Cobertura del port:** 1339 de las 1438 funciones del original que hay que portar están portadas y 8 a medias.
  Las 91 que faltan son el modo versus y la demo del título, de la fase 5 ([cobertura](reference/port-coverage.md)).
- **Beta cerrada por el lanzador.** Cada fusión en `main` que cambia código publica una versión de prueba
  (`<versión>-beta.<commits en main>`, canal «Pruebas» del lanzador, con la clave de la beta a través de la
  [pasarela](modulos/services/beta-gateway.md)). Todavía no se ha promovido ninguna estable. Paquetes para Linux,
  Windows y macOS ([lanzador](modulos/apps/launcher.md), [beta para Linux](guides/beta-linux.md),
  [beta para Windows y macOS](guides/beta-windows-macos.md)). La primera ronda de testers empezó el 2026-11-06.
- **Lo siguiente:** arreglar lo que encuentren los testers (reportes F12), cerrar los huecos que quedan
  ([huecos que el jugador notará](reference/port-coverage.md#huecos-que-el-jugador-notará)), promover la primera
  versión estable y cerrar la fase 4. Después, la fase 5.

La fase 3 empieza con las reglas del original como primer ruleset; el diseño nuevo llega después como rulesets que
aprueba el responsable de diseño ([ADR 0010](adr/0010-reglas-propias-con-rulesets.md),
[fase 3](plan/fase-3-nucleo-jugable.md)).

## Fase 0 — Base

- Esta documentación: arquitectura, ADR, plan y convenciones.
- Repositorio con CMake, presets, vcpkg, clang-format, clang-tidy y CI en Linux y Windows.
- `engine/` mínimo:
  - ventana y dispositivo Vulkan;
  - entrada por acciones con teclado, ratón y mandos, con conexión en caliente ([ADR 0014](adr/0014-mandos-y-entrada.md));
  - bucle con lógica a paso fijo y render desacoplado;
  - VM de Lua con recarga en caliente;
  - log y consola ImGui.
- Comprobación del disco por SHA-1.
- Limpieza de los repositorios actuales según la [política legal](legal/assets.md).

**Hecho cuando:** se abre una ventana, un mando o el teclado mueven algo en pantalla, se ejecuta un script de Lua y la
CI pasa.

**Estado (2026-10-19):**

| Punto | Estado |
|---|---|
| Documentación, ADR, plan y convenciones | Hecho |
| CMake, presets, vcpkg, clang-format, contenedor y CI de Linux | Hecho. Compila sin warnings en `linux-clang-debug` y en `ci` (warnings como errores), tests, formato y clang-tidy en verde en local. La CI corre en GitHub desde el primer push |
| CI de Windows | Hecho: el job `windows` pasa en GitHub (clang-cl, STL de MSVC, CRT estático) y el preset `windows-cross` compila lo mismo desde el contenedor |
| clang-tidy limpio | Hecho: cero avisos; la CI lo pasa con los avisos como errores |
| Ventana y dispositivo Vulkan | Hecho: swapchain que se rehace al cambiar de tamaño, sin errores de validación |
| Entrada por acciones, conexión en caliente | Hecho con teclado; mandos sin probar en una máquina real |
| Lógica a paso fijo y render desacoplado | Hecho |
| VM de Lua aislada con recarga en caliente | Hecho |
| Log y consola ImGui | Hecho |
| Comprobación del disco por SHA-1 | Hecho a medias: `dwx verify` reconoce los ejecutables de cada versión; faltan los hashes de las imágenes completas (Redump) |
| Limpieza de los repositorios actuales | Pendiente de una decisión del equipo |

## Fase 1 — Tubería de assets

- Librería `formats/`: sistema de ficheros del CD, TIM, TMD/MMD, mapas, ALLTIM, textos, bytecode de eventos, VAB/SEQ,
  STR y las tablas del ejecutable.
- `dwx extract`, `validate`, `cook` y `pack`.
- Sistema de ficheros virtual con capas y manifiestos de paquete.
- Esquemas de cada tipo en `content/schemas/` y documentación de los formatos en `docs/formats/`.
- Visor ImGui de modelos y mapas.
- Primeras fichas de referencia ([`docs/reference/`](reference/index.md)): cuidado, evolución y batalla.

**Hecho cuando:** cada fichero del disco US se extrae y el motor carga el resultado. El informe de cobertura está a
cero pendientes.

**Estado (2026-10-20): cerrada.** El criterio se cumple: `dwx extract` deja el informe a 0 pendientes y 0 parciales, abre cada salida con los lectores del motor, `dwx validate` no encuentra problemas y el juego monta el paquete extraído y el cocinado (carpeta y `.pak`). Requisitos uno a uno en la [especificación](plan/fase-1-tuberia-de-assets.md#estado).

| Punto | Estado |
|---|---|
| Disco: CUE, sectores, ISO9660 y tabla de ficheros del ejecutable | Hecho. `dwx extract --list` lista los 818 ficheros del disco US con su SHA-1; la tabla del ejecutable coincide con el volumen |
| Sistema de ficheros virtual con capas y manifiestos | Hecho: módulo `assets` (IDs por nombre, `pack.yaml`, capas, override por ID) y YAML en `core` ([ADR 0016](adr/0016-yaml-con-libyaml.md)) |
| TIM y packs → PNG | Hecho: TIM sueltos, apilados, ALLTIM y CARD.ALL (315 PNG con su paleta en YAML) |
| `dwx extract` a `content/base/` con informe de cobertura | Hecho: paquete base que monta el VFS, salida determinista y `coverage.yaml`. Disco US: 818 de 818 ficheros extraídos, 0 parciales, 0 pendientes, 100 % de los bytes; 4812 salidas (1,5 GB, 1,1 GB de ellos vídeo). Unos 15 s en release y 2,5–3,5 min en la compilación `ci` con sanitizers. Al terminar abre cada salida con los lectores del motor. Los 590 bytes de `EFEDAT.EFE` a los que nada llega van marcados como `unused`, con su motivo ([Efectos](formats/efectos.md#datos-sin-uso)) |
| TMD/MMD y animaciones → glTF | Hecho: 337 modelos `.glb` con texturas y animaciones, con variantes por escena |
| Mapas y tablas del ejecutable → PNG + YAML, esquemas | Hecho, conectado a `dwx extract` |
| Textos, VHB/SEQ, STR, scripts SCN | Hecho: textos y fuente, scripts de eventos desensamblados, música (MIDI + SF2), sonidos (WAV) y vídeos (Y4M + WAV + `movie.yaml`) |
| `dwx validate`, `cook` y `pack` | Hecho. `validate`: JSON Schema de cada documento, referencias, contratos de los modelos y apertura de cada asset; 0 problemas en el paquete base y en el cocinado. `cook`: texturas KTX2 con Basis UASTC (BC7 en la GPU, RGBA8 si no hay BC7), audio Opus y caché por contenido; las imágenes cuya paleta se elige al jugar siguen en PNG. `pack`: `.pak` con zstd ([ADR 0017](adr/0017-cocinado-y-archivos-pak.md), aceptada). Sin vídeos, el base pasa de 351,7 MB a 421,6 MB cocinado (`.pak`: de 240,9 a 313,3 MB). Con vídeos, de 1523,5 a 1593,5 MB (`.pak`: de 611,1 a 683,5 MB). El formato cocinado de los vídeos está sin decidir: se copian Y4M + WAV |
| Visor ImGui | Texturas hechas: el [visor de assets](guides/visor-de-assets.md) del overlay lista los IDs de los paquetes montados, carpetas y `.pak`, y enseña los PNG y los KTX2 cocinados (en BC7 o RGBA8, según la GPU). Los modelos y los mapas pasan a la fase 2, con el render 3D |
| Fichas de referencia: cuidado, evolución y batalla | Hecho, con los puntos dudosos marcados |

## Fase 2 — Prueba vertical

- Render: fondos prerenderizados con capas de profundidad, modelos con skinning y la cámara de cada mapa.
- Campo: movimiento, colisión, cambio de mapa por warps y el partner siguiendo al jugador.
- RmlUi integrado, con un primer menú nuevo que se maneja con ratón y con mando.
- **Minimapa.**

**Hecho cuando:** se puede caminar de File City a la casa del anciano y volver, con minimapa, en cualquier resolución y
con cualquier dispositivo de entrada.

**Estado (2026-10-20): cerrada.** Las cuatro líneas están en el juego y el recorrido pasa con teclado, mando y ratón,
con 0 errores de validación (detalle en la
[especificación](plan/fase-2-prueba-vertical.md#estado-2026-10-20)).

| Línea | Qué incluye | Estado |
|---|---|---|
| Render del mapa | Fondo prerenderizado con capas de profundidad y la cámara de cada mapa | Hecha |
| Modelos | Skinning y animaciones del jugador y del partner a partir de los `.glb` extraídos | Hecha |
| Campo | Movimiento, colisión, warps y el partner siguiendo al jugador | Hecha |
| Interfaz | RmlUi, un primer menú nuevo con ratón y mando, y el minimapa | Hecha |
| Integración | El juego en `apps/game`, entrada con los tres dispositivos y autotests de las tres rutas | Hecha |

## Fase 3 — Núcleo jugable

Cada sistema sigue el [ciclo de diseño](conventions/design-cycle.md). El primer ruleset reproduce el original (ficha
de referencia, decomp y tablas del disco); el diseño nuevo llega después como otros rulesets aprobados por el
responsable de diseño.

- Rulesets: el formato, con estrategias para las reglas que son lógica, y el ruleset `original`.
- Partner: stats, cuidado, vida y evolución.
- Batalla.
- Inventario y guardado (ranuras y autoguardado).
- Panel de ajuste en vivo y simulaciones de balance.
- Interfaz de la batalla y del partner con ratón, teclado y mando.
- El cambio de mapa como el original: fundido sustractivo a gris y nombre del mapa.

**Hecho cuando:** el bucle de criar, entrenar, combatir y evolucionar se juega con el ruleset del original, y un
autotest lo recorre con el reloj acelerado.

**Estado (2026-10-21): cerrada.** El criterio pasa con la prueba sin ventana y con la ruta del juego
([cómo se comprueba](plan/fase-3-nucleo-jugable.md#cómo-se-comprueba)).

## Fase 4 — El juego original entero

- Historia: los scripts de eventos del disco convertidos a Lua al extraer, y el intérprete de historia que los
  ejecuta (`game/story`).
- Todos los sistemas del original que pide la historia: tiendas, torneos, pesca, minijuegos, muerte y renacer,
  películas y final.
- Entrega por etapas detrás del candado de historia, cada una con sus rutas de autotest.
- Beta cerrada: lanzador, paquetes por sistema, versiones de prueba y estables.

**Hecho cuando:** el juego original se juega entero, del título a los créditos, y hay una versión estable publicada.

**Estado (2026-11-06): en curso.** Las nueve etapas están abiertas y la beta tiene versiones de prueba; falta lo de
[Dónde estamos](#dónde-estamos-2026-11-06).

Al principio la fase 4 era «Contenido y modos» ([especificación](plan/fase-4-contenido-y-modos.md)). De su fase 4a
ya están en `main` la conversión de los scripts a Lua, los paquetes de mods por ID, la API de Lua versionada, los
aspectos de la interfaz y los tests de ida y vuelta del editor de mapas externo. El resto pasó a la fase 5.

## Fase 5 — Mods, modos y mejoras

- Historia reescrita sobre los scripts convertidos.
- Profundidad de personajes: árboles de diálogo, afinidad y misiones.
- Framework de modos de juego, cada uno con su ruleset, y el modo versus.
- API de mods completa: paquetes que añaden o sobrescriben contenido por ID, y hooks de Lua.
- Más especies y mapas, fondos HD.
- El formato cocinado de los vídeos.
- El editor de mapas es la herramienta externa de la organización (`dw1fe`), con tests de ida y vuelta de nuestro
  formato; el motor no tiene editor propio.

**Hecho cuando:** se publica la primera versión del mod. A partir de ahí la fase sigue abierta.

## Riesgos

| Riesgo | Cómo se mitiga |
|---|---|
| Alcance enorme | Prueba vertical pronto, criterios de cierre por fase y mejoras visibles desde la fase 2 |
| Perder la identidad del juego sin querer | Pilares de diseño escritos y fichas de referencia antes de cada sistema |
| Un diseño distinto en la cabeza de cada persona | El GDD es la especificación, y una persona responsable de diseño tiene la última palabra |
| El balance | Rulesets editables, ajuste en vivo, simulaciones y pruebas de juego frecuentes |
| Formato de scripts de eventos poco documentado | Investigarlo en la fase 1 con el decomp como referencia |
| Digimon de otros juegos | Importador desde el disco del usuario, o modelos propios del equipo |
| Riesgo legal | [Política legal de assets](legal/assets.md) |
