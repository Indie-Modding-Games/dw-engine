# Historia S3: nota de port

Estado: **hecho, en `main`** (entró después de S2 4b). Diseño padre: [intérprete de historia](story-interpreter.md) (§2 «El
runtime», §3 «Estado», §6 «Corte 4 de S2: decisiones»).

Alcance de S3 (BACKLOG HIST-S3): cargar el mapa por `MAPHEAD`, `set_script` y `0xFE`, las casillas de pisar y de
confirmar, `warp_to` y `teleport` con sus continuaciones, y la sección de guardado `story`. Es un port: cada
comportamiento sale de la función del decomp (`~/code/dw_decomp/src/main`) que cita la tabla, traducida a C++26 dentro
de la arquitectura de dwe.

## Función → módulo

| Comportamiento | Original (`file:line`) | Destino en dwe |
|---|---|---|
| Arrancar una sección: puntero, dueño del diálogo `0xFD`, guardar la variable 0, vaciar los 22 huecos de movimiento, marca «sin congelar» | `script_interp.c:2244 callScriptSection` | `StoryRunner::startSection`: el valor de la variable 0 y el estado «congelado» pasan a ser estado del runner |
| Cargar mapa: la sección de `MAPHEAD` con el número del mapa corre **en el acto** | `script_interp.c:2235 runMapHeadScript`, `map.c:5331 changeMap` (5397) | `StoryAdapter::mapEntered(map, arrivalSection)` → `StoryRunner::request({0, map}, Now)`, llamado desde `apps/game` (`story_field_link.cpp`) en todo cambio de mapa (andando, con guion, al cargar) |
| `set_script` (0xFB): fija el script del mapa, apila la continuación «mapa cargado» y sigue la sección | `script_interp.c:2833 scriptInstructionFBtoFF` (0xFB) | orden `StoryFlow::setScript` en `packages/game/story`; el runner guarda el script del mapa actual (`CURRENT_SCRIPT_ID`). El mapa ya lo cargó la app: si el mapa de la orden no es el que se acaba de entrar, error registrado (en el disco, 255 de 255 coinciden) |
| Fin de sección: desapilar continuaciones; tipo 3 → corre `0xFE` del script del mapa si existe; tipo 4 → corre la sección guardada al fotograma siguiente, o con `0xFF` el final de `MAPHEAD`, que sigue desapilando; pila vacía → fin del script | `script_interp.c:2684 returnFromScriptFile`, `2181 setMapHeadActive` | `StoryRunner::endScript` pasa a ser «desapilar»: `std::vector<Continuation>` con `MapLoaded` y `RunSection{optional<section>}`. La pila sigue sin vaciarse entre scripts (rareza del original) |
| Fallo de una sección (error de Lua, presupuesto) | — (no existe en el original) | acaba como `end_section`: desapila y corre las continuaciones (§5 del diseño padre) |
| Al acabar el script: cerrar cajas, restaurar la variable 0 y, si se congeló, devolver control, cámara y reloj | `script_interp.c:390 tickScript` (634–642) | `StoryRunner` → puerto `FieldControlPort::release()`;  |
| Congelar: control, cámara y reloj fuera; variable 0 = 3; **quita siempre el disparador 44** | `script_interp.c:2726 scriptPauseGame` | `StoryRunner::freeze()` → `FieldControlPort::freeze()`;  |
| `warp_to` (0x4B): congela, decide si sale el nombre, apila `RunSection(llegada)` y corta la sección | `script_interp.c:1402` (en `1297 scriptInstruction46to58`), `860 consumeMapChangeShowName` | orden de espera que cierra la corrutina: nueva espera `MapChangeWait{screen, exit, arrival}` en `section_host.hpp`. Regla del nombre (variable 6: 0 = «sí salvo en sección de evento», 1 sí, 2 no; luego se pone a 0) en `packages/game/story` |
| `teleport` (0x58): pantalla y salida de las variables `n` y `n+1`, sin sección de llegada | `script_interp.c:1534` | igual que `warp_to`, `arrival = none` |
| `section_on_exit` (0x7B): apila `RunSection(s)` y restaura la variable 0 | `script_interp.c:2151` (en `scriptInstruction64to7E`) | orden `StoryFlow::sectionOnExit` |
| Cambio de mapa con guion: fundido de 20, nombre en el fotograma 10, `changeMap` en el 20, fundido de entrada de 20 que se **solapa** con la sección de llegada; si ya no hay script, control de vuelta; luego `checkMapInteraction` | `script_interp.c:405–412`, `625–630`; `map.c:5277 scriptTickChangeMap` | `Field::startScriptedMapChange(MapChange, showName)` reutiliza el fundido del warp andando sin el paso de salida; la app entra en el mapa como en cualquier otro cambio y llama a `mapEntered`; el runner espera en estado «cambiando de mapa» hasta esa llamada |
| Casillas 51–79: la sección del mismo número **en cada fotograma** que el jugador está encima y no hay script; 80–109: al pulsar confirmar; 120 con la condición 8 del compañero: `MAPHEAD 0x4E2` | `tamer.c:660 checkMapInteraction` (684–712), llamado en `tamer.c:588` | `Field::cellSections()` informa de las secciones que pide la casilla del jugador (nivel, no flanco) solo con el jugador moviéndose libre; `StoryAdapter` (`FrameCells`) las pide una vez por fotograma del original con `NextFrame`; ocupado = se pierde (`RequestOutcome::Dropped`) |
| Warp andando: 20 + `changeMap` + 20, luego `checkMapInteraction` | `tamer.c:1367 tamerTickChangeMap` | ya portado en `Field`; S3 solo añadió la llamada a `mapEntered` |
| Partida nueva: `MAPHEAD 0xCC` | `main.c:3455` | `story_scenes.new_game.chain` (`rules/<id>/story_scenes.yaml`): `original` corre `MAPHEAD 0xCC` y la apertura; `seeded` empieza con `data/new_game_story` y el inicio de `field/warps` |
| Cargar partida: `runMapHeadScript(mapa guardado)` sin continuación y luego posiciones | `main.c:3465`, `3483`, `3791 initializeLoadedMap` | la sesión entra en el sitio guardado y llama a `mapEntered`; la corrutina no se guarda |
| Guardar: 0x17C bytes de `ScriptState` (35 variables), variable 0 guardada como 0 si vale 3 | `main_menu.c:5071 writeSavegame` (5080, 5120), `5016 loadSavegame` (5058) | lo hace S2 4b. S3 añadió la Q10 del diseño padre: con una sección en marcha, `StoryAdapter` guarda el estado de **antes** de esa sección (copia al arrancar) |
| Pantalla y salida anteriores (`PREVIOUS_SCREEN`, sus ajustes por mapa, guardadas en la partida) | `map.c:5331 changeMap` (5336–5394), `main_menu.c:5048–5051`, `5110–5113` | `screen_history` (`packages/game/story`) y la tabla `previous_screen` del ruleset |
| Entradas globales (`0x4DD` objeto 22, `0x4DE` muerte, `0x4D9`/`0x4DA` torneo, `0x4E4` pesca); hablar (`checkMapInteraction` 667–681, `overworld.c:1318`, `1333`) | `item.c:2263`, `partner_impl.c:1912`, `1943`, `tournament.c:97`, `116`, `script_common.c:3059` | cada una con el sistema que la pide (ver el [intérprete de historia](story-interpreter.md), §6) |

Puertos nuevos en `packages/game/story` (los implementa `packages/session`; `packages/game/story` sigue sin incluir `packages/game/*` ni
`packages/engine/script`): `FieldControlPort` (congelar y soltar control, cámara y reloj) y `MapChangePort` (empezar un cambio
de mapa con guion). Las constantes del cambio de mapa (20, 10, 20) ya son reglas del campo (`FieldRules`); el runner no
las duplica.

## Nombres publicados en `story`

Se publicaron en S3, cada uno en el tramo en que funciona (regla MOD-VISIBLE): `set_script`, `section_on_exit`,
`warp_to`, `teleport`. Son neutros y el emisor los escribe así (`warp_to`/`teleport` como `return story.…`,
`set_script` como llamada que sigue). `LuaApiVersion` subió una versión menor. No se publica `story.request` (es de
mods: fase 5).

## CAMBIOS DE CONTRATO

1. **API Lua `story`:** cuatro nombres nuevos, `LuaApiVersion` +1 menor (aditivo).
2. **`field/script_warps`:** cuando la historia lleva el script del mapa deja de ser el mecanismo de los warps y
   queda como grafo estático (exploración libre, minimapa, análisis de alcance; `Field::staticWarps`). El esquema y el
   YAML no cambian (contrato con `dw1fe` intacto).
3. **Guardado:** la forma de la sección `story` no cambia (no se guarda la corrutina). Cambia su significado con
   una sección en marcha (estado de antes de la sección, Q10). Al cargar corre la cadena de `MAPHEAD`.
4. **Interfaz interna** `SectionHost::Wait` gana `MapChangeWait`: no es un contrato con mods ni con datos.
