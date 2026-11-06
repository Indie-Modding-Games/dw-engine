# Historia S3: nota de port

Estado: **aprobado por el coordinador** (2026-10-22: P1, P2 y P3 con la opción 1). S3 está en `main`
(entró después de S2 4b). Diseño padre: [intérprete de historia](interprete-historia.md) (§2 «El
runtime», §3 «Estado», §6 «Corte 4 de S2: decisiones»).

Alcance de S3 (BACKLOG HIST-S3): cargar el mapa por `MAPHEAD`, `set_script` y `0xFE`, las casillas de pisar y de
confirmar, `warp_to` y `teleport` con sus continuaciones, y la sección de guardado `story`. Es un port: cada
comportamiento sale de la función del decomp (`~/code/dw_decomp/src/main`) que cita la tabla, traducida a C++26 dentro
de la arquitectura de dwe. Regla vigente (usuario, 2026-10-22): **en la fase 4 no hay tests ni rutas nuevas**. Las
pruebas y las rutas quedan para la fase 5, más abajo.

## Función → módulo

| Comportamiento | Original (`file:line`) | Destino en dwe |
|---|---|---|
| Arrancar una sección: puntero, dueño del diálogo `0xFD`, guardar la variable 0, vaciar los 22 huecos de movimiento, marca «sin congelar» | `script_interp.c:2244 callScriptSection` | `StoryRunner::startSection`: el valor de la variable 0 y el estado «congelado» pasan a ser estado del runner; los huecos de movimiento son S4 |
| Cargar mapa: la sección de `MAPHEAD` con el número del mapa corre **en el acto** | `script_interp.c:2235 runMapHeadScript`, `map.c:5331 changeMap` (5397) | `StoryAdapter::mapEntered(index, cause)` → `StoryRunner::request({0, index}, Now)`, llamado desde `apps/game` `FieldScene::enterMap` en todo cambio de mapa (andando, con guion, al cargar) |
| `set_script` (0xFB): fija el script del mapa, apila la continuación «mapa cargado» y sigue la sección | `script_interp.c:2833 scriptInstructionFBtoFF` (0xFB) | orden `StoryFlow::setScript` en `game/story`; el runner guarda el script del mapa actual (`CURRENT_SCRIPT_ID`). El mapa ya lo cargó la app: si el mapa de la orden no es el que se acaba de entrar, error registrado (en el disco, 255 de 255 coinciden) |
| Fin de sección: desapilar continuaciones; tipo 3 → corre `0xFE` del script del mapa si existe; tipo 4 → corre la sección guardada al fotograma siguiente, o con `0xFF` el final de `MAPHEAD`, que sigue desapilando; pila vacía → fin del script | `script_interp.c:2684 returnFromScriptFile`, `2181 setMapHeadActive` | `StoryRunner::endScript` pasa a ser «desapilar»: `std::vector<Continuation>` con `MapLoaded` y `RunSection{optional<section>}`. La pila sigue sin vaciarse entre scripts (rareza del original) |
| Fallo de una sección (error de Lua, presupuesto) | — (no existe en el original) | acaba como `end_section`: desapila y corre las continuaciones (§5 del diseño padre) |
| Al acabar el script: cerrar cajas, restaurar la variable 0 y, si se congeló, devolver control, cámara y reloj | `script_interp.c:390 tickScript` (634–642) | `StoryRunner` → puerto `FieldControlPort::release()`; cerrar cajas es S4 |
| Congelar: control, cámara y reloj fuera; variable 0 = 3; **quita siempre el disparador 44** | `script_interp.c:2726 scriptPauseGame` | `StoryRunner::freeze()` → `FieldControlPort::freeze()`; girar a los que hablan (hueco 0 y el del NPC) es S4 |
| `warp_to` (0x4B): congela, decide si sale el nombre, apila `RunSection(llegada)` y corta la sección | `script_interp.c:1402` (en `1297 scriptInstruction46to58`), `860 consumeMapChangeShowName` | orden de espera que cierra la corrutina: nueva espera `MapChangeWait{screen, exit, arrival}` en `section_host.hpp`. Regla del nombre (variable 6: 0 = «sí salvo en sección de evento», 1 sí, 2 no; luego se pone a 0) en `game/story` |
| `teleport` (0x58): pantalla y salida de las variables `n` y `n+1`, sin sección de llegada | `script_interp.c:1534` | igual que `warp_to`, `arrival = none` |
| `section_on_exit` (0x7B): apila `RunSection(s)` y restaura la variable 0 | `script_interp.c:2151` (en `scriptInstruction64to7E`) | orden `StoryFlow::sectionOnExit` |
| Cambio de mapa con guion: fundido de 20, nombre en el fotograma 10, `changeMap` en el 20, fundido de entrada de 20 que se **solapa** con la sección de llegada; si ya no hay script, control de vuelta; luego `checkMapInteraction` | `script_interp.c:405–412`, `625–630`; `map.c:5277 scriptTickChangeMap` | `Field::startScriptedMapChange(MapChange, showName)` reutiliza el fundido del warp andando sin el paso de salida; la app entra en el mapa como hoy y llama a `mapEntered(index, ScriptedWarp)`; el runner espera en estado «cambiando de mapa» hasta esa llamada |
| Casillas 51–79: la sección del mismo número **en cada fotograma** que el jugador está encima y no hay script; 80–109: al pulsar confirmar; 120 con la condición 8 del compañero: `MAPHEAD 0x4E2` | `tamer.c:660 checkMapInteraction` (684–712), llamado en `tamer.c:588` | `Field` informa una `TileInteraction{value, kind}` por fotograma del original (nivel, no flanco: hoy `triggerSteps` es flanco) solo con el jugador en estado normal; `StoryAdapter` pide la sección con `NextFrame`; ocupado = se pierde (`RequestOutcome::Dropped`, ya existe) |
| Warp andando: 20 + `changeMap` + 20, luego `checkMapInteraction` | `tamer.c:1367 tamerTickChangeMap` | ya portado en `Field`; S3 solo añade la llamada a `mapEntered(index, Walk)` |
| Partida nueva: `MAPHEAD 0xCC` | `main.c:3455` | ver [P3](#p3-partida-nueva) |
| Cargar partida: `runMapHeadScript(mapa guardado)` sin continuación y luego posiciones | `main.c:3465`, `3483`, `3791 initializeLoadedMap` | la sesión entra en el sitio guardado y llama a `mapEntered(index, Load)`; la corrutina no se guarda. El ajuste de salida cuando el jugador cae sobre ciertos NPC (`initializeLoadedMap`) es S4 |
| Guardar: 0x17C bytes de `ScriptState` (35 variables), variable 0 guardada como 0 si vale 3 | `main_menu.c:5071 writeSavegame` (5080, 5120), `5016 loadSavegame` (5058) | lo hace S2 4b. S3 añade la Q10 del diseño padre: con una sección en marcha, `StoryAdapter` guarda el estado de **antes** de esa sección (copia al arrancar) |
| Pantalla y salida anteriores (`PREVIOUS_SCREEN`, sus ajustes por mapa, guardadas en la partida) | `map.c:5331 changeMap` (5336–5394), `main_menu.c:5048–5051`, `5110–5113` | **S6** (solo las usa la huida); números a la tabla del ruleset entonces |
| Entradas globales (`0x4DD` objeto 22, `0x4DE` muerte, `0x4D9`/`0x4DA` torneo, `0x4E4` pesca); hablar (`checkMapInteraction` 667–681, `overworld.c:1318`, `1333`) | `item.c:2263`, `partner_impl.c:1912`, `1943`, `tournament.c:97`, `116`, `script_common.c:3059` | S4–S7, cada una con el sistema que la pide |

Puertos nuevos en `game/story` (los implementa `apps/session`; `game/story` sigue sin incluir `game/*` ni
`engine/script`): `FieldControlPort` (congelar y soltar control, cámara y reloj) y `MapChangePort` (empezar un cambio
de mapa con guion). Las constantes del cambio de mapa (20, 10, 20) ya son reglas del campo (`FieldRules`); el runner no
las duplica.

## Nombres publicados en `story`

Se publican en S3, cada uno en el tramo en que funciona (regla MOD-VISIBLE): `set_script`, `section_on_exit`,
`warp_to`, `teleport`. Son neutros y el emisor ya los escribe así (`warp_to`/`teleport` como `return story.…`,
`set_script` como llamada que sigue). `LuaApiVersion` sube una versión menor (1.2 → 1.3, o la siguiente libre si
VAN-01 sube antes). No se publica `story.request` (es de mods: fase 5).

## Tramos (≤ 800 líneas cambiadas sin contar tests ni docs)

| Tramo | Qué | Estimación |
|---|---|---|
| **S3a** `game/story` | pila de continuaciones y «desapilar», fallo = `end_section`, `MapChangeWait`, congelar y soltar (variable 0, disparador 44), regla del nombre del mapa, órdenes `setScript`/`sectionOnExit`, puertos `FieldControlPort` y `MapChangePort` | ~450 |
| **S3b** sesión, campo y app | `StoryLuaHost` publica las cuatro órdenes (`LuaApiVersion` +menor); `StoryAdapter` implementa los puertos, `mapEntered` (andar, guion, cargar), copia de antes de la sección para guardar; `Field::startScriptedMapChange`, `TileInteraction` por fotograma, control con guion; `FieldScene` llama a `mapEntered`; convivencia con `field/script_warps` ([P1](#p1-warps-de-casilla-antes-de-s4)) | ~650 |

Si S3b pasa de 800 al escribirlo, se corta en «entrar y warps» y «casillas, carga y guardado».

## CAMBIOS DE CONTRATO

1. **API Lua `story`:** cuatro nombres nuevos, `LuaApiVersion` +1 menor (aditivo).
2. **`field/script_warps.yaml`:** deja de ser el mecanismo de los warps y queda como grafo estático (exploración
   libre, minimapa, análisis de alcance). El esquema y el YAML no cambian (contrato con `dw1fe` intacto). Cuándo
   pasa: [P1](#p1-warps-de-casilla-antes-de-s4).
3. **Guardado:** la forma de la sección `story` no cambia (no se guarda la corrutina). Cambia su significado con
   una sección en marcha (estado de antes de la sección, Q10). Al cargar corre la cadena de `MAPHEAD`.
4. **Interfaz interna** `SectionHost::Wait` gana `MapChangeWait`: no es un contrato con mods ni con datos.

## Pruebas y rutas

En la fase 4 no se añaden (regla del usuario 2026-10-22); los tests que ya existen deben seguir pasando. Para la
fase 5 quedan:

- paridad con el intérprete de referencia de S2 (`formats/tests/story_lua_harness.hpp`,
  `apps/session/tests/story_lua_disc_tests.cpp`): añadir los marcos 3 y 4 a la referencia y correr, por cada mapa,
  la cadena `MAPHEAD` → `set_script` → `0xFE` y cada `warp_to` con su sección de llegada;
- la ruta `route-story-enter` (en/es): partida nueva, salir de casa, cruzar la ciudad;
- el test de alcance con el runner contra la cifra de (b).

## Qué depende de S2 4b

La VM de historia por partida y su dueño (`StoryAdapter`), el `StoryLuaHost` montado en la sesión, el tick del
runner en la sesión antes que el campo, `ticksPerFrame` del ruleset, `LuaApiVersion` 1.2 y `api:` del paquete base,
las 35 variables guardadas con la variable 0 y la puesta a 0 de 35–255 al cargar, y la comprobación de cabecera
`-- dwx lua N`. S3a solo toca `game/story` y puede escribirse antes; S3b necesita 4b en `main`.

## Preguntas

### P1. Warps de casilla antes de S4

**Decidido por el coordinador (2026-10-22): opción 1.**

Medido en `content-f8`: de las 218 secciones de pisar, confirmar o `0xFE` con un `warp_to`, solo 23 llegan al warp
con las órdenes que funcionan tras S3. Las demás pasan antes por órdenes de S4 (`entity_walk_to` en 125, giros,
diálogo). Si el Lua fuese ya el único mecanismo, esos warps dejarían de funcionar hasta S4.

1. **En S3, una casilla con un warp en `field/script_warps` sigue con el camino de (b) y no pide la sección; las
   demás piden su sección al Lua. El último tramo de S4 quita (b) como mecanismo** (recomendado).
2. Quitar (b) en S3 y aceptar la regresión hasta S4.
3. Adelantar a S3 las órdenes de movimiento de S4 (+1–2 tramos).

### P2. Órdenes de `MAPHEAD` sin publicar

**Decidido por el coordinador (2026-10-22): opción 1.**

202 de las 207 secciones de mapa de `MAPHEAD` llaman a `set_bgm` justo después de `set_script`; 179 cargan NPC y 95
usan `store_date`. Sin esas órdenes, la sección falla ahí. Con el fallo como `end_section`, la continuación igual
corre `0xFE`; se pierde lo que venga detrás en la sección.

1. **Aceptarlo en S3, con el fallo como `end_section` y registrado. `set_bgm` llega con VAN-01 (música), los NPC con
   S4 y `store_date` con S5** (recomendado).
2. Adelantar a S3 `store_date` y `store_partner_species` (sus sistemas existen; ~60 líneas).

### P3. Partida nueva

**Decidido por el coordinador (2026-10-22): opción 1.**

La cadena real (`MAPHEAD 0xCC` → `twna01` → la apertura que lleva a `topn01`, sección 54) necesita diálogo (S4) y
las rutinas de partida nueva (S6).

1. **S3 sigue sembrando con `data/new_game_story` y el inicio de `field/warps`, y corre la cadena de `MAPHEAD` del
   mapa de inicio al entrar. La cadena real de `0xCC` se enciende en S6** (recomendado).
2. Correr ya `0xCC` y aceptar que la apertura se corta.

## S4–S7 con este flujo

| Inc. | Tramos | Por qué |
|---|---|---|
| S4 | 4 | `DialogueState` + vista; 22 huecos de movimiento + cámara con guion; NPC del campo + hablar; quitar (b) como mecanismo |
| S5 | 2 | inventario, dinero y cartas; estadísticas, fecha y rutinas sin escena |
| S6 | 3 | combate con guion y consecuencias + pantalla anterior; rutinas del compañero; rutinas 32/47/48 + cadena real de `0xCC` |
| S7 | 3 | objetos del mapa; sonido y rutinas con escena restantes; transporte aéreo y teleport estático |
