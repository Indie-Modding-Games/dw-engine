# Intérprete de historia

Estado: **diseño aprobado por el coordinador (2026-10-21).** Las decisiones están en
[Decisiones](#decisiones-2026-10-21); las preguntas, con su opción elegida, al final ([Q1–Q14](#preguntas-abiertas)).
Los incrementos S0–S7 se integraron en orden, cada uno como un paquete con revisión. **Hoy está en `main`**
(`formats/script`, `packages/game/story`, `packages/session`): el juego corre la historia del disco con este intérprete. Las ramas
y el gate que cita la página son el flujo de entonces; ahora todo entra en `main` por pull request.

El intérprete de historia tiene dos mitades:

- **La conversión**: `dwx`, al extraer, convierte los scripts de eventos (`script/maphead.yaml` y
  `script/dg/NNN.yaml`) en Lua legible, a partir de **la misma IR** que ya usan los warps de script
  ([warps de script](warps-de-script.md), rama `p4-scripted-warps`).
- **El runtime**: `packages/game/story` ejecuta ese Lua (entrar al mapa, hablar, pisar, pulsar, cargar el mapa) con la
  semántica del original, y es el dueño del estado de la historia (`StoryState`).

Regla de partida (dirección del usuario, vinculante): **primero el comportamiento del original**, con pruebas de
paridad (localizadores del decomp y, después, trazas). Toda mejora es un delta explícito, documentado y opcional.

**El decomp es inspiración, no un port** (regla vinculante, 2026-10-22): «fiel» es el comportamiento que ve el
jugador. El runtime no copia los límites de la máquina original (topes de pila, anchos de operando de 8 o 16 bits,
tamaños fijos): pone guardas del motor holgadas y documentadas. Los rangos de juego (topes de variables, de objetos,
de estadísticas) son reglas del ruleset. Un acceso fuera del estado configurado sigue siendo un error registrado.

La referencia de comportamiento es la ficha [scripts de eventos](../reference/scripts.md) (rama `p4-scripted-warps`).
Este documento no la repite: cita sus apartados y añade lo que el runtime necesita.

## Decisiones (2026-10-21)

El coordinador aprobó el diseño con estas decisiones:

| Q | Decisión |
|---|---|
| Q1 | Lua generado; el intérprete de referencia solo en los tests |
| Q2 | Un módulo por script y parches por sección |
| Q3 | Números ahora; nombres simbólicos después del GDD |
| Q4 | El texto de historia en `es` cae al inglés del disco hasta que el paquete de traducción lo dé. **Nunca** se toma texto de un disco parcheado en español |
| Q5 | 20 Hz, fotogramas enteros |
| Q6 | `ApiRegistry::publishWaiting` |
| Q7 | `via: talk` y la tabla de destinos de la rutina 10 |
| Q8 | Presupuesto propio por paso de historia |
| Q9 | Sin bytecode en el `.pak` |
| Q10 | El autoguardado durante una sección guarda el estado de antes de ella |
| Q11 | **Cambiada dos veces** (2026-10-22, «el decomp es inspiración»): enteros sin anchos de la máquina; los rangos de juego son reglas del ruleset; un acceso fuera del estado configurado es un error de Lua registrado ([Q11](#q11-semántica-entera-y-accesos-fuera-de-rango)) |
| Q12 | Paquete aparte `p4-script-timing-hardening` (paradas por instrucciones, el reloj como techo holgado), antes de que S2 toque `packages/engine/script` |
| Q13 | Solo si S3 lo sigue necesitando; el banco de referencia se consulta más tarde |
| Q14 | Generador de la sesión con la fórmula de recorte del original |

Cada incremento informa del número de mapas que alcanza **medido con una ruta de historia**.

## Punto de partida

- **IR** (`formats/script`, rama `p4-scripted-warps`, sin integrar):
  - `ScriptListing`: instrucciones con `FlowRole`, operandos tipados (`Argument`) y entradas de `IF` (`Condition`).
    Dos constructores que dan lo mismo: `readScriptListing(YamlNode)` (el YAML del disco o el que edita un mod) y
    `listingOf(Disassembly)`.
  - `buildScriptFlow` y `structureScript`: árbol sin saltos (`if`, bucles con `break`, elecciones, colas
    compartidas como llamadas de cola, máquina de estados como último recurso).
  - `walkSection` (recorrido con guardas) y `findMapChanges`: lo usan los warps estáticos.
  - Cifras del disco (test lento de estructuración): 222 scripts, 1553 secciones, 1550 anidadas sin máquina de
    estados, 3 con máquina de estados; 0 `goto`.
- **Sandbox de Lua** (`packages/engine/script`, en `main`): Lua 5.5 con sol2, entornos por paquete, API versionada
  (`ApiRegistry::publish`, valores escalares), hooks con presupuesto por evento y por fotograma, presupuesto por
  llamada de instrucciones (10 M), tiempo (`callDeadline`, 1 s) y memoria (64 MiB total, 16 MiB por paquete).
- **Campo** (`packages/game/field`): paso de jugador y compañero, colisión, warps de tabla con fundido por fotogramas del
  original (`Field::fadeLevel`), `TriggerStep` para casillas que no son warp («para sistemas futuros»). No hay hablar
  con NPC, ni caja de texto de historia, ni cámara con guion, ni movimientos con guion.
- **Sesión** (`packages/session`): el único sitio que junta los `packages/game/*` (regla 4 de
  [dependencias](dependency-rules.md)); tick fijo de 60 Hz en `apps/game` (`TickSeconds`).
- **Combate desde script**: solo el disparador de depuración `battle(...)` (`apps/game/src/battle_trigger.*`).
- **Alcance hoy** (test de alcance de `p4-scripted-warps` (a), contenido `content-f4`): una partida nueva llega a
  **6 mapas** (`mayo00`, `room08`, `topn01`, `trai00`, `twna01`, `twnb01`); con todos los warps de script tomados
  sin condiciones, 236 de 241.

## 1. La tubería: de la IR a Lua

### Por qué Lua y no otra forma

| Opción | A favor | En contra |
|---|---|---|
| **Lua estructurado generado por `dwx` (elegida)** | Decisión ya tomada (R4.1, «el motor solo ejecuta Lua»; «para cambiar un evento, un mod edita el Lua»). Legible, depurable, el mod lo edita sin herramientas. Corrutinas nativas para las esperas. El runtime no depende de `formats` (regla 2) | El emisor tiene que ser exacto; la paridad se prueba aparte |
| Un intérprete de la IR en C++ en el motor | Paridad trivial por construcción | Viola la regla 2 (el runtime no usa `formats`) y la decisión del usuario; los mods no editarían Lua |
| Bytecode propio | Compacto | Ilegible, otro formato que mantener, sin ventaja |

El intérprete de la IR **sí existe, pero solo en los tests**: un intérprete de referencia sobre el **grafo de
bloques** (no sobre el árbol estructurado), con un mundo falso, es el oráculo de paridad del Lua generado
([pruebas](#6-incrementos-y-pruebas)). Es una extensión del recorredor de referencia que ya propone
[warps de script](warps-de-script.md#la-representación-formatsscript).

### Pasos

```
script/dg/NNN.yaml ─┐                       (contrato con dw1fe: no cambia)
script/maphead.yaml ┴→ readScriptListing → buildScriptFlow → structureScript ─┐
                                                                              ├→ lua_emitter → story/dg/NNN.lua
                         walkSection / guardas (solo para los warps estáticos)┘               story/maphead.lua
                                                                                              story/dg/NNN.lines
                                                                                              text/<idioma>/story_dgNNN.yaml
```

- **Entrada:** siempre el YAML (`readScriptListing`), nunca los bytes. Así el YAML que edita `dw1fe` o un mod se
  convierte igual que el del disco (decisión 2026-10-21: «`dwx` convierte también el YAML editado de un mod»). El
  test de disco de la rama (a) ya fija que YAML y desensamblado dan la misma IR.
- **Emisor** (`formats/.../script/lua_emitter`): recorre `StructuredScript` y escribe un módulo por script. Cada
  orden es una llamada a la API `story` con sus operandos con nombre; la forma exacta está en
  [la forma del Lua](#la-forma-del-lua).
- **Mapa de líneas** (`story/dg/NNN.lines`): por cada línea del Lua, el desplazamiento de la orden original. Lo usan
  las trazas de paridad y los mensajes de error («dg/049 0x04c0»), no la ejecución. Formato YAML de dos columnas.
- **Textos:** cada `show_textbox` y `set_selection` se vuelve una clave (`story.dg049.0474`), decisión del usuario
  («los diálogos usan claves de texto»). El texto en inglés sale al catálogo `text/en/story_dg049.yaml` del paquete
  `base`; el español es la [Q4](#q4-textos-de-historia-en-español).

### Determinismo

- Salida byte a byte estable: orden por desplazamiento, sin iterar tablas hash, formato fijo (sangría de dos
  espacios, números en decimal salvo secciones y desplazamientos en hexadecimal de 4 cifras), fin de línea `\n`.
- Test: convertir dos veces el mismo disco da los mismos bytes; convertir el YAML releído da los mismos bytes que
  convertir el desensamblado.
- Sin `goto` (regla del proyecto): un test recorre todo el Lua generado del disco y falla si aparece la palabra.
  Las 3 secciones que el estructurador deja en máquina de estados salen como bucle `while true` que despacha sobre
  una variable local `state`.

### `ExtractorVersion` y huella

- Escribir `story/**` y `text/*/story_*.yaml` cambia lo que extrae `dwx`: **sube `ExtractorVersion`**
  (`apps/dwx/src/disc_cache.hpp`) en el incremento que lo introduce, y el test de huella de la caché lo exige.
- El emisor declara su propia versión (`LuaEmitterVersion`), escrita en la cabecera de cada fichero
  (`-- dwx lua 1, script 49`). Cambiar el emisor sube las dos. Así un Lua de mod generado con un emisor viejo se
  reconoce en el log.

### `dwx` y cocinado

- `dwx extract` escribe el Lua fuente en el paquete `base`, en la máquina del usuario (nada en git).
- `dwx convert-script <yaml>` (nuevo): convierte un YAML suelto, para el mod que edita el YAML con `dw1fe` y quiere
  su Lua. Misma función que la extracción.
- `cook-content` **no** compila a bytecode: el `.pak` lleva el texto Lua. Razón: el sandbox solo carga texto (cargar
  bytecode es una superficie de ataque conocida y no es portable entre versiones de Lua); la ganancia de tiempo de
  carga es despreciable (≈ 1 MB de Lua en total). La tabla de [la tubería de assets](assets-pipeline.md) dice
  «bytecode de Lua» en cocinado: se corrige en el incremento S1 ([Q9](#q9-bytecode-al-cocinar)).
- `dwx validate`: el Lua del paquete compila (solo sintaxis, en una VM sin API) y cada `.lines` cubre su Lua.

### Moddabilidad: cómo un mod cambia una sección

Un mod sustituye un asset por su ID (VFS). La unidad es **un módulo por script** con una tabla de secciones, y una
forma de sustituir **una sola sección** sin copiar el script entero ([Q2](#q2-unidad-de-sustitución-de-un-mod)):

- **Script entero:** el mod trae `story/049.lua` y gana a `base` por ID. Funciona ya con la VFS.
- **Una sección (hecho, `StoryLuaHost`):** el mod trae `story/patches/<nombre>.lua`, que devuelve
  `{ script = 49, sections = { [0x51] = function(original) ... end } }`. La sección del parche gana a la del módulo;
  las demás secciones del script siguen siendo las del módulo. La guía para modders está en
  [API de historia](../modding/historia.md#parches-por-sección).
  - **Envolver:** la función recibe `original`, la sección del módulo (o `nil` si el módulo no la tiene, o no hay
    módulo: un parche puede añadir secciones). `return original()` es una llamada de cola y la sección se comporta
    igual que sin parche; llamarla y seguir después (`local r = original() ... return r`) la mete un nivel más hondo,
    así que un `jump_to_file` dentro de ella vuelve al parche cuando la sección destino acaba, como una subrutina
    (`FileJump::linkDepth`).
  - **Orden:** los parches se cargan una vez, al empezar la primera sección (y otra vez tras un `reload_map` del
    canal de control, `StoryLuaHost::reloadScripts`, que descarta módulos y parches cuando no corre ninguna
    sección), de abajo arriba en la pila de paquetes
    (y por ID dentro de un paquete). Si dos parches traen la misma sección gana el que está más arriba y el log lo
    dice (`story patches: script 49 section 0x0051 runs B; A loses`). El `original` es siempre la sección del módulo,
    no la del parche que perdió: los parches no se encadenan.
  - **Errores:** un parche que no carga, sin `script` válido o con una sección fuera de 0–65535 se deja fuera con un
    error en el log; el juego sigue con la sección del módulo. `StoryLuaHost::patchOf` dice qué parche corre una
    sección (para el control remoto y el log de depuración `[mod:story]`).
  - Un parche no lleva la cabecera de `dwx` (`-- dwx lua N`): lo escribe una persona, no el emisor.

El código de un mod corre en **su entorno** (sandbox, cuota de memoria del paquete); el Lua de `base` corre como
código del motor (`PackOrigin::Engine`, capa `base`). La API `story` es la misma para los dos. Un parche corre en el
entorno de su paquete y paga toda la corrutina, también lo que gasta `original`, que conserva el entorno del módulo
(`ScriptModule::coroutine` con un `ModuleFunction`).

## 2. El runtime

### Dónde vive

| Pieza | Módulo | Qué |
|---|---|---|
| `StoryState` | `packages/game/story` | 800 disparadores, 256 variables, guardado, estado inicial de partida nueva |
| `StoryRunner` | `packages/game/story` | el único intérprete: corrutina en curso, pila de continuaciones, espera pendiente, cola de entradas descartadas (para el log) |
| `DialogueState` | `packages/game/story` | caja de texto y selección del original como estado puro (página, modo de paso, dueño, cancelación, disparador 49) |
| Puertos (`StoryWorld`) | `packages/game/story` (interfaces) | lo que el runtime pide al mundo: campo, compañero, inventario, combate, cámara, sonido, fundidos |
| Puertos implementados | `packages/session` (`story_adapter`) | el pegamento con `packages/game/field`, `packages/game/partner`, `packages/game/inventory`, `packages/game/battle` (regla 4) |
| API `story` | `packages/session` publica en `packages/engine/script` | funciones del espacio `story` (versionado semver) |
| `StoryLuaHost` | `packages/session` (`story_lua`) | el `SectionHost` de la VM: el script N es el módulo `story/NNN` del paquete que lo da, la sección S su `sections[S]`, una corrutina por sección; publica la parte de la API `story` que ya funciona |
| Vista de la caja de texto | `packages/screens` + `packages/engine/ui` | dibuja `DialogueState`; el aspecto original (9b) lo viste |

`packages/game/story` no incluye `packages/game/field` ni ningún otro `packages/game/*`: declara los puertos y `packages/session` los implementa.
`packages/game/story` tampoco incluye `packages/engine/script`: el runner trabaja con una interfaz `SectionHost` (arrancar sección,
reanudar con un valor, cerrar) que implementa `packages/session` con la VM. Así el runner se prueba sin Lua.

### Secciones y disparadores

Las entradas son las de la ficha ([puntos de entrada](../reference/scripts.md#puntos-de-entrada)). El runtime las
expone como `StoryRunner::request(Entry)`:

| Entrada | Quién la pide | Script y sección | Localizador |
|---|---|---|---|
| Cargar mapa (cualquier cambio de mapa, cargar partida) | sesión, al entrar | `MAPHEAD` sección = índice del mapa; su `set_script` apila la carga y luego corre `0xFE` | `map.c: changeMap`, `script_interp.c: runMapHeadScript` |
| Partida nueva | sesión | `MAPHEAD 0xCC` | `main.c: newGameScene` |
| Hablar | campo (choque con NPC + confirmar, o `autotalk`) | script del mapa, sección = id de script del NPC | `tamer.c: checkMapInteraction` |
| Pisar casilla 51–79 | campo (`TriggerStep`) | script del mapa, sección = valor; **se repite cada fotograma** mientras sigue encima | ídem |
| Confirmar en casilla 80–109 | campo | ídem | ídem |
| Muerte, objeto 22, baño, torneo, pesca | sistemas | secciones globales de `MAPHEAD` (`0x4DE`, `0x4DD`, `0x4E2`, `0x4D9`/`0x4DA`, `0x4E4`) | ficha |

Reglas de paridad que el runner aplica tal cual:

- **Un solo script a la vez.** Una petición con otro en marcha **se pierde** (no se encola). Se registra en el log
  de depuración, nunca en el de usuario (`tamer.c: checkMapInteraction`).
- **Arranque diferido:** la primera orden corre en el siguiente fotograma del original (la historia corre antes que
  el campo), salvo la carga de mapa, que da un primer paso en el acto (`runMapHeadScript`), y la sección que pide
  `checkMapInteraction` al acabar un warp con guion, que corre en ese mismo paso (`map.c: scriptTickChangeMap`).
- **Fin:** `end_section`/`end_script` vacían la pila de continuaciones (`returnFromScriptFile`): por eso un
  `end_section` dentro de una subrutina acaba el script entero (en Lua: la subrutina devuelve el centinela `ENDED`
  y quien la llama hace `return ENDED`; nunca `error`).

### La pila de continuaciones

La pila del original mezcla vueltas de subrutina (tipo 1) con continuaciones del motor (tipos 3 y 4). En Lua las
vueltas son llamadas de función normales; el runner guarda solo las continuaciones:

| Tipo original | En el runner | Al acabar la sección actual |
|---|---|---|
| 1 (`jump_and_link`) | llamada Lua | — |
| 3 (`set_script`) | `MapLoaded` | prepara el fondo y corre `0xFE` del script del mapa, si existe |
| 4 (`warp_to`, `teleport`, `section_on_exit`, huida) | `RunSection(s)` | cede un fotograma y corre la sección `s` del script del mapa (`0xFF`: ninguna) |

La pila no se vacía entre scripts (rareza del original); el runner lo replica (las continuaciones pendientes se
desapilan en el siguiente fin) y lo fija un test. La pila del original tiene 8 marcos y no lo comprueba; el runner no
copia ese tope. `jump_to_file` sin subrutinas pendientes es un salto de cola: la sección que salta se cierra y la
sustituye la de destino, así que un bucle de saltos entre ficheros (49:0x52 ↔ 199–202:0x33) corre sin fin, como en el
original. Desde dentro de una subrutina, la sección que salta espera debajo de la de destino hasta su
`jump_return`. Una guarda del motor contra el desbocamiento para el script con un error registrado si las subrutinas
pendientes de todas las secciones en espera pasan de **256**; el disco no pasa de unas pocas.

### Corrutinas, fotogramas y esperas

- **Una corrutina por script en marcha.** El runner la crea al arrancar la sección y la reanuda una vez por
  fotograma del original mientras no espere. Dentro de un fotograma corre órdenes seguidas hasta que una cede: igual
  que `tickScript`.
- **Las órdenes que esperan ceden desde C++**: una función de la API marcada como «de espera» acaba con
  `lua_yield`, sin continuación (Lua 5.5 lo permite a una función C llamada desde Lua dentro de una corrutina). El
  Lua generado queda plano: `story.show_textbox(...)` se lee como una llamada que bloquea. Hace falta una pieza
  nueva en `packages/engine/script`: `ApiRegistry::publishWaiting` ([Q6](#q6-cómo-cede-una-orden-de-espera)). Fuera de una
  sección de historia, una orden de espera es un error de Lua claro.
- **Qué devuelve una espera:** `set_selection` devuelve la opción desde 0, o el número de opciones al cancelar (el
  contrato del emisor); `start_battle` el
  resultado (`-1`, `0`, `1`), aunque el Lua generado no lo use (el original lo deja en la variable 255, y el runtime
  también la escribe).
- **Tipos de espera** (de la ficha, [modelo de ejecución](../reference/scripts.md#modelo-de-ejecución)):

  | Espera | Órdenes | Acaba cuando |
  |---|---|---|
  | `Frame` | `fadeout_hud`, `move_boulder`, rutinas 16, 19, 20, 21, 55, opcodes `FC`/`FD`, marco 4 | el siguiente fotograma |
  | `Textbox` | `show_textbox` | la página se pasa (modo 0/1/2 de `set_textbox_mode`) |
  | `Selection` | `set_selection` | elige o cancela (cancelar imposible con el disparador 49) |
  | `Delay(n)` | `delay` | `max(n, 1)` fotogramas |
  | `Movement(mask)` | `wait_for_entity` | los huecos de movimiento pedidos libres (tabla de operandos de la ficha) |
  | `Battle` | `start_battle` | el combate acaba (y, tras ganar sin disparador 1, la cámara vuelve) |
  | `Routine(id)` | `call_routine` con menú o escena | la máquina de estados de la rutina acaba |
  | `MapChange` | `warp_to`, `teleport` | nunca reanuda: la corrutina se cierra; sigue la continuación |

- **Presupuesto:** cada reanudación es una llamada con su propio presupuesto (`ScriptLimits::instructionsPerResume`,
  200 000 instrucciones). Los scripts del disco son cortos entre esperas; el presupuesto de instrucciones es el
  límite efectivo y es determinista. El límite de tiempo (`resumeDeadline`, 20 ms) queda como red de seguridad y
  nunca debe decidir el resultado de un test ([5](#5-sandbox-y-frontera-de-la-api)).
- **Abortos:** derrota en combate (salta a `MAPHEAD 0x4DE` sin apilar), huida (warp a la pantalla anterior con un
  marco 4 vacío) y cambio de mapa hacen que el runner suelte la corrutina de la sección (`ScriptCoroutine`, que al
  destruirse la cierra) y siguen con la continuación que toque (`btl.c`, `script_common.c: handleItemLoss`, `scriptInstruction64to7E`).

### Tiempo: los fotogramas del original

- El original corre la lógica a **20 fotogramas por segundo** (`main.c: gameLoop`, `VSync(3)`; ficha de
  [campo](../reference/campo.md)). `delay 20` es un segundo.
- El motor hace tick fijo a 60 Hz. El runner cuenta **ticks enteros** y da un fotograma del original cada 3 ticks
  (`OriginalFrameTicks = 3`, constante del ruleset `original`, no del código). Nada de acumular segundos en coma
  flotante: así un fotograma del runner cae siempre en el mismo tick y las trazas se alinean.
- El orden dentro de un fotograma del original, como `tickScript`: (1) avanzar cajas de texto y los 22 huecos de
  movimiento, (2) avanzar el cambio de mapa en curso, (3) mirar la espera, (4) si no espera, reanudar. El sitio del
  paso de historia dentro del tick de la sesión sigue el orden del bucle principal del original (ver
  [localizadores](#localizadores-del-bucle-y-del-cambio-de-mapa)).
- Mientras la sección «congela» (`scriptPauseGame`, primera orden que la llama), el reloj del juego para, el jugador
  y el compañero no responden y la cámara se suelta. Es el mismo contrato que ya tiene `Field` para los cambios de
  mapa (`Field::gameTimeRan`).

### Localizadores del bucle y del cambio de mapa

Comprobado en el decomp (solo lectura) para este diseño:

- **Orden del fotograma** (`main.c: gameLoop`): entrada → `processInput` → `updateTournamentRegistration` →
  `tickScript` (solo si no hay script en pausa) → `tickObjects` (domador, compañero, NPC) → dibujo → `VSync(3)`.
  La historia corre **antes** que el campo. Consecuencia: una sección que pide el campo (hablar, pisar) en el tick de
  objetos empieza en el fotograma siguiente; el runner hace lo mismo poniendo su paso antes del de `Field` en la
  sesión.
- **Sin límite de órdenes por fotograma** y sin saltar fotogramas: si un fotograma se pasa, el juego va más lento
  (`gameLoop`). Nuestro presupuesto por paso es un límite de seguridad, no un comportamiento.
- **`delay N`** guarda la cuenta y cede; cada `tickScript` la baja (`script_textbox.c: tickTextboxHandling`); al
  llegar a 0, la orden siguiente corre **en ese mismo fotograma** (`script_interp.c: tickScript`).
- **`warp_to`/`teleport`** (`scriptInstruction46to58`): apilan el marco 4 con la sección de llegada (`teleport`:
  ninguna) y cortan el script. Desde el fotograma siguiente, `map.c: scriptTickChangeMap`: fundido a negro de 20
  fotogramas, nombre del mapa en el fotograma 10, `changeMap` al llegar a 20 y fundido de entrada de 20.
  `changeMap` llama a `runMapHeadScript`, que corre **en la misma llamada** toda la cadena (`set_script` → marco 3
  → `0xFE`) hasta la primera espera; al desapilar el marco 4, la sección de llegada da su primera orden en ese mismo
  fotograma. **El fundido de entrada se solapa con la sección de llegada.** El control vuelve al acabar el script
  (movimiento, cámara del jugador y reloj).
- **Warp andando** (`tamer.c: tamerTickChangeMap`): sin script; 20 de fundido, `changeMap`, 20 de entrada y luego
  `checkMapInteraction` y el reloj.
- **Cargar partida** (`main.c: main`, `initializeLoadedMap`): `runMapHeadScript(mapa guardado)` sin marco 4 (el
  script acaba con la cadena), luego posiciones, salida y fundido de entrada de 40 fotogramas.
- **Guardar** (`main_menu.c: writeSavegame`): 0x17C bytes de `ScriptState` (`script.h`: cartas, 800 disparadores,
  variables 0–34); la variable 0 se guarda como 0 si valía 3.
- **Hablar** (`tamer.c: checkMapInteraction`): el choque devuelve un NPC (2–9); dispara si su `autotalk` es 1, o es 0
  y se pulsó confirmar en este fotograma, y solo sin script en marcha. La sección es el id de script del NPC, que
  viene de la tabla de especies del mapa (`overworld.c`, la carga de la tabla de especies del mapa); `set_digimon` solo lo coloca. Hay un
  segundo camino desde el NPC con `autotalk` (`overworld.c: NPCEntityTickOverworld`). Una charla congela girando al
  NPC y al domador uno hacia otro (`scriptPauseGame`).

### Interacción con el resto

- **Diálogo:** `DialogueState` replica `script_textbox.c` (composición de la página siguiente mientras se muestra la
  actual, modos de paso 0/1/2, nombre del que habla, `set_textbox_origin`, `set_textbox_size`, cancelar con
  disparador 49, la caja que queda abierta hasta `fadeout_hud`). Los códigos de control solo insertan valores; el
  runner los resuelve al componer (`story.var` para números y nombres). La vista lee el estado; no decide nada.
- **Cámara:** `move_camera_to`, `move_camera_to_entity` y las variantes `_with_camera` ocupan el hueco 10. Puerto
  `CameraPort` implementado sobre la cámara del campo (hoy fija por mapa): necesita un modo «con guion» que el
  campo no tiene.
- **Movimiento de NPC y entidades:** los 22 huecos (`tamer.c`, `tickScriptedMovement`) son estado de la historia,
  pero mueven entidades del campo: el runner guarda qué pide cada hueco y el puerto `FieldPort` lo aplica a las
  entidades con la regla de cada tipo (andar con animación hasta la casilla, deslizar en N pasos, girar 0x200 por
  fotograma). Los deslizamientos comparten estado global en el original (solo uno a la vez): se replica.
- **NPC:** `load_digimon`, `set_digimon` (8 huecos, aparece si coincide con la tabla del mapa), `unload_entity`,
  `set_autotalk`. El campo aún no tiene NPC con lógica: hace falta que los tenga (incremento S4).
- **Fundidos y warps:** `warp_to` usa el fundido del campo que ya existe (20 fotogramas, nombre del mapa según la
  variable 6) y la cadena de carga del runner. Las rutinas 45/46 (fundido a blanco) son un fundido nuevo del mismo
  servicio.
- **Combate:** `start_battle` con rival predefinido (variables 250–253) o el NPC con el que se habló. El disparador
  de depuración actual (`battle_trigger`) ya monta un combate con NPC del mapa; el puerto `BattlePort` lo
  generaliza y devuelve el resultado. Las consecuencias (derrota → `0x4DE`, huida → warp) son del runner.
  Sin rival predefinido, el rival es el NPC cuya sección abrió el último toque o charla (`TALKED_TO_ENTITY`,
  `StoryAdapter::talkedTo`). Tras un combate del que se puede huir (disparador 1 sin poner) el rival sale del mapa
  hasta que este se vuelve a cargar (`battle_main.c`: los NPC en pantalla quedan con `isOnMap` 0); tras uno fijo se
  queda, como el `species_a` de mayo00. Aún no se portan los NPC que se unen ni los que huyen al empezar
  (`rollJoiningEnemies` existe en `packages/game/battle`, pero la app no lo usa).

### Cobertura de órdenes

78 órdenes en el disco. Columnas: orden → semántica (resumen; la ficha manda) → localizador → plan (incremento) →
¿bloquea la historia principal? «Sí» quiere decir que aparece en las secciones de los 6 mapas de inicio
(`twna01` y su ciudad, `mayo00`…: scripts 105, 108, 163, 164, 178, 193, 208–210, 212, 220, 221, más `MAPHEAD`), que
son el primer muro.

| Orden | Semántica | Localizador | Plan | Bloquea |
|---|---|---|---|---|
| `if`, `jump_to`, `switch`, `jump_and_link`, `jump_return`, `jump_to_file` | flujo; `IF` sin cortocircuito, plegado izquierda→derecha | `script_interp.c: scriptIfInstruction`, `scriptInstruction10to27` | S1 (emisor) | sí |
| `end_section`, `end_script` | vacían la pila | `returnFromScriptFile` | S2 | sí |
| `set_script` | carga mapa y apila `MapLoaded` | `scriptInstruction10to27` | S3 | sí |
| `section_on_exit` | apila `RunSection`, restaura variable 0 | ídem | S3 | no |
| `set_trigger`, `unset_trigger`, `set_pstat`, `add_to_pstat`, `copy_pstat`, `store_random` | estado | `scriptInstruction28to3F` | S2 | sí |
| `store_date`, `advance_to_date_at`, `add_minutes_to_date_at`, `store_digimon_type` | reloj y compañero | ídem, `partner_impl.c` | S5 | sí |
| `show_textbox`, `set_dialog_owner`, `set_textbox_size`, `set_textbox_mode`, `set_textbox_origin`, `fadeout_hud` | caja de texto | `script_textbox.c` | S4 | sí |
| `set_selection` | elección del jugador | `scriptShowSelection`, `tickSelectionDialogue` | S4 | sí |
| `delay`, `wait_for_entity` | esperas | `tickScript` | S2 (`delay`), S4 | sí |
| `warp_to`, `teleport` | cambio de mapa y continuación | `scriptInstruction46to58`, `map.c: scriptTickChangeMap` | S3 | sí |
| `load_digimon`, `set_digimon`, `unload_entity`, `unload_digimon`, `set_autotalk` | NPC | `scriptInstruction46to58`, `scriptIdToEntityId` | S4 | sí |
| `play_animation`, `entity_set_rotation`, `entity_look_at_entity`, `entity_walk_to*`, `entity_move_to*` | huecos de movimiento | `tamer.c: tickEntityMoveTo`, `tickScriptedMovement` | S4 | sí |
| `move_camera_to`, `move_camera_to_entity` | hueco 10 | ídem | S4 | sí |
| `call_digimon_routine` | escenas del compañero (0 baño … 10 evolución) | `partner.c`, el despacho de escenas del compañero | S6 | sí (24 usos) |
| `give_item`, `remove_item`, `set_inventory_size`, `add_money`, `reduce_money` | inventario | `item.c`, ficha [inventario](../reference/inventory.md) | S5 | sí |
| `set_stat`, `add_stat`, `reduce_stat`, `remove_condition`, `learn_move`, `deal_damage` | compañero | `script_value.c: getStatsPointer` | S5 | sí |
| `give_card`, `take_card` | cartas | `script_interp.c:1143` (`tickScript`), `getCardAmount`, `setCardAmount` | S5 | sí |
| `start_battle` | combate con guion | `btl.c: handleBattleStart`, `tamer.c: startBattle` | S6 | sí (5 en el inicio) |
| `call_routine` | ver tabla de rutinas | `scriptInstruction64to7E` | S5–S7 | sí (rutinas 7, 8, 32, 35, 39, 47, 48) |
| `set_obj_visibility`, `move_object_to`, `rotate_3d_object`, `spawn_item`, `spawn_chest`, `set_rect_impassable`, `spawn_sprite_at_entity`, rocas | objetos del mapa | `map_object.c`, `overworld.c` | S7 | `set_obj_visibility`, `set_rect_impassable`, `spawn_item` sí |
| `play_sound`, `set_bgm` | sonido | `scriptInstruction*`, variable 245 | S7 (sin audio: registrado) | no (no cambia el estado) |
| `data` | nunca corre | — | S1 (no se emite) | no |

**Rutinas (`call_routine`) del inicio:** 7 (gimnasio, 12 usos), 8 (tienda), 14 (cartas), 32 (introducción y nombres
de la partida nueva), 35/39 (cargas asíncronas: en el motor terminan en el acto, la espera `0xD6` dura un
fotograma), 47 (olvidar técnicas), 48 (máquina de guardar), 25, 30/31, 55. El resto, como propone la ficha en su
columna **4a**.

**Las que no tienen todavía un sistema detrás** se implementan como «ausente con registro»: la orden escribe una línea
de depuración con su desplazamiento y sigue (o, si espera, espera un fotograma). Nunca se finge el efecto. Un test
cuenta las órdenes ausentes por incremento y falla si sube.

### La forma del Lua

Un módulo por script. Ejemplo **sintético** (no es un script del disco):

```lua
-- dwx lua 1: script 900, generated from script/dg/900.yaml
local story <const> = story
local ENDED <const> = story.ENDED

local sections = {}

local function shared_0120()               -- cola compartida por dos secciones
  story.set_trigger(70)
  return story.warp_to({ screen = 12, exit = 2, section = nil })
end

sections[0x0051] = function()              -- casilla de pisar
  if story.trigger(71) then return ENDED end
  story.dialog_owner(story.npc(5))
  local choice = story.select("story.dg900.0040", 2)
  if choice == 1 then
    story.say("story.dg900.0062")
    return shared_0120()
  end
  story.fadeout_hud(0)
  return ENDED
end

return sections
```

- Una función por sección (`sections[0x0051]`), una función local por subrutina y por cola compartida.
- Los `IF` salen del plegado izquierda→derecha con paréntesis explícitos (`(a and b) or c`). Las pruebas son
  lecturas puras (`story.trigger`, `story.var`, `story.stat`, `story.items`, `story.cards`, `story.knows_move`,
  `story.has_condition`, `story.money`): no tener cortocircuito no cambia nada. Los operadores de comparación son los
  de la ficha (sin signo salvo la felicidad).
- `warp_to` y `teleport` salen como `return story.warp_to(...)`: la IR ya los trata como fin de flujo
  (`FlowRole::MapChange`).
- Los nombres: disparadores y variables por número en 4a, con comentario si el motor les da significado
  (tabla de la ficha). Nombres simbólicos: [Q3](#q3-nombres-de-disparadores-y-variables).

## 3. Estado

### `StoryState`

- **Disparadores y variables:** la capacidad la da la configuración; el conjunto base tiene 800 disparadores y 256
  variables. Las variables son enteras y su rango de valores es una regla del ruleset (`variableMax`; el original las
  guarda en un byte, 0–255). **Se guardan todas** las variables de la capacidad configurada. El original solo
  guardaba las 0–34 (`main_menu.c: writeSavegame`); guardar todas es un cambio de contrato aprobado (2026-10-22) que
  no cambia nada que el jugador vea en el disco. Las cartas, el inventario diario y el de reciclaje ya tienen dueño
  (`packages/game/inventory`) y no se duplican.
- Rangos: en el conjunto base, los disparadores válidos son **0–799**. En el original, `getTriggerOffset`
  (`script_interp.c`) indexa `ScriptState.triggers[100]` sin comprobar el rango, y los bytes que siguen en
  `ScriptState` (`script.h`) son las variables, así que un disparador ≥ 800 tocaría sus bits. **El disco no lo hace
  nunca** (medido el 2026-10-22, abajo), así que el runner no reproduce ese solapamiento: un disparador fuera de la
  capacidad configurada (800 en el conjunto base) es un error de Lua registrado como los demás accesos fuera de rango
  ([Q11](#q11-semántica-entera-y-accesos-fuera-de-rango)).
- **Medición de disparadores ≥ 800** (decisión del coordinador: medir antes de decidir). Sobre los 222 guiones del
  disco (`script/dg/*.yaml` y `script/maphead.yaml`), se contó cada operando de disparador estático:
  `set_trigger` 1146, `unset_trigger` 227, `spawn_chest` 71, condiciones `trigger` de `if` 2009, `compare_date` 0;
  en total **3453 operandos, máximo 716, ninguno ≥ 800**. Ninguna orden toma el número de disparador de una variable,
  así que no hay operandos desconocidos. Las listas de disparadores de los bloques `data` (`dget.c`, torneos) son bytes
  que se suman a 200 (`TRIGGER_OGRE_FORTRESS_OPENED`) y no pasan de 453. Un test de disco de S1 fija esta cuenta para
  que un cambio de extracción no la esconda.
- Estado inicial: el de `data/new_game_story` (rama a): todo a cero, más lo que pongan `MAPHEAD 0xCC` y la `0xFE`
  del primer mapa **cuando el runner las ejecute de verdad**. Desde S3, `data/new_game_story` deja de ser la fuente
  del estado inicial y queda solo para el análisis estático de alcance.

- **El día nuevo** (`packages/game/story/new_day`, `session::CalendarAdapter`, sección `calendar` del ruleset): cada día que
  cruza el reloj, y una vez al empezar partida (`initializeScripts`), suma 1 a los contadores de días (variables
  28–31) salvo si valen 255, y borra el bit 0x80 de la variable 2 (`script_common.c: dailyPStatTrigger`). Las órdenes
  de fecha pueden llamar a `CalendarAdapter::startNewDays`. La tienda de cartas que el original repone ese día queda
  para después del Hito 1. El original no tiene estaciones.

### Sección de guardado (encima de la Q8 de warps de script (b))

La rama (b) de warps de script crea la sección `story` con `StoryState`. El intérprete la hereda sin cambiar su
forma:

```yaml
sections:
  story:
    version: 2
    state:
      triggers: "0000…"     # 800 bits en 200 cifras hexadecimales, bit n = disparador n, y luego los extra
      variables: [0, 0, …]  # las 35 que guardaba el original (0–34)
      extra_variables: []   # las que añade el ruleset (capacity), todas
```

La versión 2 (#97) añade la capacidad del ruleset ([capacidad](../modding/historia.md#capacidad)); una partida de la
versión 1 se migra sin cambios.

- **La siguiente versión de la sección** guarda todas las variables (antes, las 0–34), con migración aditiva y su
  test.
- **Una versión posterior** (S3) añadiría lo que el runner necesita para guardar fuera de una sección: nada, porque el original solo
  guarda desde la máquina de guardar (rutina 48, dentro de un script) y al cargar corre la cadena de `MAPHEAD` del
  mapa guardado. El runner hace lo mismo: **no se guarda el estado de la corrutina**. Se guarda el mapa y su
  sitio (ya lo hace la sesión) y al cargar se pide la entrada «cargar mapa».
- **Autoguardado del motor** (`save.md`): si autoguarda con una sección en marcha, guarda el estado de antes de
  entrar en ella (el runner lo congela al empezar una sección). Es un delta del motor (el original no autoguarda) que
  ya existe; queda documentado ([Q10](#q10-autoguardado-con-una-sección-en-marcha)).
- **Migración:** una partida de antes de `story` no tiene la sección: `readMissing` pone el estado de partida nueva y
  los disparadores que el análisis estático da como «ya pasados» para el mapa guardado no se infieren (sería
  inventar historia). La partida queda en el estado inicial y el log lo dice. Las versiones de la sección migran con
  la cadena de `SaveSections`.

## 4. Cómo se abre el mundo original

### El disparador 203

El disparador 203 es «especie 3 reclutada en la ciudad» (rango 200 + especie, ficha de
[estado](../reference/scripts.md#estado)). Lo ponen exactamente tres secciones (análisis del contenido `content-f4`,
alcance por grafo de cada sección):

| Script | Mapa | Sección | Desplazamiento | Camino |
|---|---|---|---|---|
| 105 | `mayo00` | 6 (hablar con el NPC 6) | `0x03f8` | combate con guion contra el NPC 6 (variables 250–253, disparador 1), luego la variable 101 == 3 |
| 48 | `ogre03` | 5 | `0x0062` | — |
| 48 | `ogre03` | 6 | `0x007e` | — |

Lo leen 22 sitios: `MAPHEAD 0x6d` y `0x4dd`, las secciones `0x33`/`0x34` de varios mapas de la ciudad (scripts 108,
160, 163, 165, 167, 169, 171, 178), la `0xfe` del script 178 y la 221. Es decir: reclutar al primero abre partes de
la ciudad. Su camino necesita **hablar** (sección por id de NPC), **combate con guion** y una **variable transitoria**
(101): nada de eso existe hoy.

### La cadena de las primeras horas

El muro de los 6 mapas lo forman, en este orden de necesidad:

1. **Cargar mapa con `MAPHEAD` real** (S3): la sección de cada mapa decide qué script carga y pone variables; sin ella
   los warps de script dependen del estado fijo.
2. **Disparadores puestos por secciones que el jugador corre** (S3): las de pisar y confirmar de la ciudad y su
   alrededor.
3. **Hablar** (S4): las secciones por id de NPC (reclutar, tiendas, el disparador 203, los warps al hablar).
4. **Combate con guion y su resultado** (S6): reclutar casi siempre pasa por un combate.
5. **Condiciones no booleanas** (variables, objetos, estadísticas, elecciones): salen solas al ejecutar el Lua.

### Objetivo de alcance por incremento

Cifras **optimistas** (cota superior) de un análisis monótono sobre `field/script_warps.yaml` de la rama (a) y la
tabla de warps del mapa: un disparador cuenta como puesto en cuanto alguna sección de un mapa alcanzado lo pone, y
una prueba de «quitado» como cumplible si alguna vez lo estuvo. El análisis descarta los warps de tabla cuya casilla
no está en la colisión. La cifra real la da el test de alcance de cada incremento, que **se fija en el test** al
cerrar el incremento:

| Hasta | Qué se ejecuta | Cota (mapas desde `twna01`) |
|---|---|---|
| (a) hoy | estado fijo de partida nueva, solo disparadores | **6** (test real) |
| S3 | `MAPHEAD`, entrar, pisar, confirmar; disparadores que ponen esas secciones | ≤ 108 |
| S4 | además hablar (secciones por id de NPC, warps al hablar) | ≤ 151 |
| S5–S6 | además variables, objetos, estadísticas, elecciones, combate | ≤ 235 |
| S7 | además el transporte aéreo (rutina 10 → `MAPHEAD 0x4E3` → `teleport`) | ≤ 236; `mgen14`–`mgen16` y `miha03` siguen sin camino conocido |

El salto real de S3 será bastante menor que 108: casi toda la progresión pide hablar y combatir. Por eso propongo
fijar en el test de cada incremento la cifra **medida con una ruta de autotest que juega la historia** (no la
cota), y usar la cota solo como techo.

### Q7: warps al hablar y teleports al cargar

- **Warps al hablar** (`chka01`, script 63, sección 5 → `yaka26`, salida 0, sección de llegada 60; guarda: objeto
  122 ≥ 1): en el runtime no son un caso especial, son un `warp_to` dentro de una sección de hablar; salen gratis con
  S4. En el grafo estático (a) aparecen con `via: other`; propongo que `dwx` los marque `via: talk` con el NPC cuando
  la sección es el id de script de un NPC de la tabla del mapa (cambio aditivo del esquema, [Q7](#q7-warps-al-hablar-y-teleports-en-el-grafo-estático)).
- **Teleports con variable** (`MAPHEAD 0x4E5`, desplazamiento `0x6062`: `teleport` con la pantalla en la variable 247
  y la salida en la 248; la sección `0x4E3` muestra un texto y cae en él): en el runtime es leer dos variables, sin
  más. **Pero no es un teleport de carga de mapa ni lo usa `gcan11`:**
  - el único que entra ahí es el transporte aéreo (rutina 10): `script_menu.c`, el tick del transporte, caso 5, escribe
    las variables 247/248 desde su tabla de destinos y apunta el script a `MAPHEAD 0x4E3`. Los destinos son los mapas
    38, 70, 79, 93, 105 y 119 (`script_common.c`, tabla de destinos); el 21 (`miha03`) no está;
  - nada entra en `0x4E5` (ni código ni scripts): es la cola de `0x4E3`;
  - en `dg/049.yaml` (`gcan11`), las variables 247/248 (21 y 12) son solo los argumentos de `call_routine 21`, que
    reinicia la animación de los objetos 2D del mapa (`overworld.c: resetMapObjectAnimation`) y cede un fotograma.
    No cambia de mapa;
  - no hay warp de tabla a `miha03`, ni `warp_to` a la pantalla 21 en ningún script. **El camino `gcan11` → `miha03`
    no está en el decomp**: lo que dice el encargo no se confirma. Queda para una traza del banco de referencia
    ([Q13](#q13-cómo-se-llega-a-miha03)).

  Para el grafo estático, el `teleport` del transporte se resuelve con la tabla de destinos de la rutina 10 (un dato
  del ejecutable que `dwx` puede extraer), no plegando variables: el camino que llega a `0x6062` no las fija.

## 5. Sandbox y frontera de la API

- **Espacio de nombres `story`**, publicado por `packages/session` en `packages/engine/script`, versión semver de toda la API
  (una sola). Funciones escalares (la API ya solo pasa escalares) salvo `warp_to`, que recibe una tabla pequeña:
  si la API sigue siendo solo escalar en 4a, `warp_to(screen, exit, section)` con tres números.
- **Lectura y escritura del estado:** `story.trigger(n)`, `story.set_trigger(n)`, `story.var(n)`,
  `story.set_var(n, v)`… Un mod las usa como el Lua generado. Lo que un mod **no** puede: tocar la corrutina, la pila
  de continuaciones ni pedir secciones fuera de las entradas (un mod que quiera correr una sección la pide con
  `story.request(...)`, que cumple la regla «un script a la vez»).
- **Versión de la API:** los módulos que escribe S1 llaman a `story.*` y a `error`, así que S2, al publicar
  `story`, sube `LuaApiVersion` de 1.1.0 a **1.2.0**, y el paquete base declara `api: 1.2` en su `pack.yaml`. S1 no
  cambia la versión: sus módulos aún no corren.
- **Nombres neutros** («Decidido por el coordinador, 2026-10-22»): las funciones de `story` y las claves que ve un mod
  no nombran franquicia, consola ni empresa. Seis órdenes se renombran al emitir (`load_digimon` →
  `load_species_model`…), con una sola tabla y una lista única de palabras prohibidas que un test comprueba; el
  listado YAML y el código interno conservan los nombres originales. Las claves de texto son
  `story_<id del script en 3 cifras>.text.<offset>` y los módulos `story/<id>` (`000` = `MAPHEAD.SCN`). Tabla y
  reglas: [API de historia](../modding/historia.md).
- **Hooks** (eventos del espacio `story`): `story.section_started`, `story.section_ended`, `story.trigger_changed`
  (escalares). Corren con el presupuesto de hooks; no pueden ceder.
- **Presupuestos:** cada reanudación de una sección tiene un presupuesto propio, más pequeño que el de una llamada
  genérica, en `ScriptLimits` (`instructionsPerResume`, 200 000): el disco no pasa de unas pocas miles de instrucciones de Lua entre
  dos esperas, y un mod que entra en bucle sin ceder debe pararse en un fotograma, no en un segundo
  ([Q8](#q8-presupuesto-de-la-historia)). Una sección parada por presupuesto acaba como un `end_section` (se vacía la
  pila, se descongela todo) y se registra con su mapa de líneas.
- **Determinismo:** lo que decide el resultado de una sección es el presupuesto de instrucciones, nunca el reloj.
  `store_random` usa el generador de la sesión (semilla de la partida), no `math.random` (que el sandbox da por
  paquete). Ver [Q14](#q14-números-aleatorios-de-store_random).
- **El fallo intermitente conocido:** `allocation_budget_tests.cpp` (los cuatro casos de «building big strings…»,
  «pcall does not hide…», «a coroutine pays…») y algunos de `sandbox_escape_tests.cpp` comparan el tiempo de parada
  contra un límite de reloj (`QuickStop`, `DeadlineStop`). Con la máquina cargada fallan en `main` y en las ramas por
  igual (registros en `_work/logs/p4-ids-refs/gate-3a455cdd/script-flake/`, unas 45 de N pasadas). No es del
  intérprete, pero el intérprete **no debe añadir** ningún test con cota de reloj: sus tests miden instrucciones y
  fotogramas. Propuesta para `packages/engine/script` (fuera de este paquete): medir la parada en instrucciones cobradas y
  dejar la cota de reloj solo como un techo holgado ([Q12](#q12-el-fallo-intermitente-de-los-tests-del-sandbox)).

## 6. Incrementos y pruebas

Cada incremento se integra solo, pasa el gate corto (`scripts/gate.sh --tier short`) y deja el juego igual o más
cerca del original. Ninguno cambia el YAML de scripts (contrato con `dw1fe`).

| Inc. | Qué | Aceptación | Tests | Rutas |
|---|---|---|---|---|
| **S0** | Añadir `text` (o la clave del texto) a `ListingInstruction` en la IR ([contradicción 1](#contradicciones-con-la-ir-de-las-partes-a-y-b)) | Los dos constructores siguen dando la misma IR | test de disco de la IR | ninguna nueva |
| **S1** | `lua_emitter` + `dwx` escribe `story/**`, `.lines`, textos `story_*`; `ExtractorVersion` +1; `dwx convert-script`; `dwx validate` compila el Lua | Todo el disco convierte; 0 `goto`; salida estable; Lua compila | unitarios sintéticos por forma (if plegado, bucle, elección, cola, máquina de estados, subrutina con `end_section`); disco: estabilidad, sin `goto`, compila | — |
| **S2** | `packages/game/story`: `StoryState`, `StoryRunner` con corrutinas, `publishWaiting` en `packages/engine/script`, órdenes de estado y flujo, `delay` | **Paridad de efectos** S2: cada sección del disco, contra el intérprete de referencia sobre el grafo con un mundo falso y resultados de esperas aleatorios con semilla, da la misma secuencia de efectos | runner sin Lua (puertos falsos); paridad aleatoria por sección del disco (test lento) | — |
| **S3** | Carga de mapa por `MAPHEAD`, `set_script`, `0xFE`, pisar y confirmar, `warp_to`/`teleport` con continuaciones, guardado `story` | Desde partida nueva, los warps los hace el Lua; `field/script_warps.yaml` deja de ser mecanismo (queda grafo estático); alcance medido ≥ el de (b) | integración en `packages/session` sin ventana; test de alcance con el runner | `route-story-enter` (en/es): partida nueva, salir de casa, cruzar la ciudad |
| **S4** | Hablar con NPC, `DialogueState` y su vista, selección, NPC (`set_digimon`…), huecos de movimiento, cámara con guion | Una conversación con elección y un warp al hablar (Q7) se juegan como en el original | modelo de diálogo puro (modos de paso, cancelar, disparador 49); huecos de movimiento por fotograma | `route-story-talk` (en/es) con capturas de la caja |
| **S5** | Inventario, dinero, estadísticas, cartas, fecha, rutinas sin escena (12/13, 25, 30/31/56, 35/39, 15/16) | Las secciones de la ciudad no registran órdenes ausentes de estos grupos | por orden, contra la ficha (topes y suelos) | ampliar `route-story-talk` con una tienda |
| **S6** | `start_battle` con guion y sus consecuencias, `call_digimon_routine`, rutina 32 (partida nueva), 47, 48 | Reclutar la especie 3 en `mayo00` pone el disparador 203 jugando | combate con guion, derrota → `0x4DE`, huida → warp | `route-story-recruit` (en/es) |
| **S7** | Objetos del mapa, sonido, rutinas con escena restantes, teleport estático (Q7) | Cero órdenes ausentes en el disco | contador de ausentes = 0 | rutas de cada zona nueva |

### Paridad de S2 en el disco

`packages/session/tests/story_lua_disc_tests.cpp` juega cada sección del disco con dos mundos por el `StoryRunner` y
el `StoryLuaHost`, y compara sus efectos (escrituras de estado, esperas, saltos de fichero y llamadas) con un
intérprete de referencia de los listados que guarda la pila de retornos a través de `jump_to_file`. Las órdenes que
la API aún no publica son stubs del test que registran la llamada; `call_routine` y las demás órdenes que corren
código del original en otra parte son una llamada registrada sin ejecución anidada, igual en los dos lados. Dos
corridas coinciden si su estado y sus eventos son idénticos; una corrida cortada debe haberse parado justo en el
límite de eventos, en los dos lados.

Medido en el disco US (`doctest -s`, registro `disc-s-*.log` del corte):

- 3100 corridas, las 3100 coinciden: (i) 463 usan solo órdenes publicadas, (ii) 2637 llaman también a stubs;
  29 se cortaron en los dos lados y 0 en uno solo.
- Pila de retornos: llega a 2, lejos de los 8 marcos del original; en cada `jump_to_file` está vacía, y la
  profundidad de enlace que da el host es 0. Ninguna sección de `MAPHEAD` hace `jump_and_link` (0 llamadas, 0
  saltos afectados), así que la peculiaridad de `jump_return`, que restaura `ACTIVE_MAP_SCRIPT` como base, no se
  alcanza.
- El bucle de entrenamiento (script 49, sección `0x52`, con los scripts 199–202, sección `0x33`) da 20 vueltas,
  40 saltos de cola, con una sola sección abierta a la vez. 20 es el máximo del disco: cada vuelta suma 1 a la
  variable 123 y la sección `0x52` sale del bucle al llegar a 20, así que el test exige exactamente 20.

### Corte 4 de S2: decisiones

El corte 4 se parte en dos. **4a** (nombres y extracción) no cablea nada al juego; **4b** cablea la historia a la
sesión y publica `story` a los módulos de historia del paquete base, que es lo que necesita el juego original. Lo que
toca a los mods es de la fase 5.

**4a**

- **`story.PLAYER`:** la constante del personaje del jugador (entidad `0xFD`) es `story.PLAYER`; `TAMER` nunca se
  publica. El emisor escribe `local PLAYER <const> = story.PLAYER` y la constante del motor es `PlayerEntity`. El
  listado YAML conserva la palabra `tamer`, igual que conserva `set_pstat`: es el contrato con `dw1fe` y no cambia.
- **Variables:** las órdenes de variables se llaman `set_var`, `add_to_var`, `reduce_var` y `copy_var`, y el operando
  `pstat` se llama `var` en los comentarios del Lua ([tabla](../modding/historia.md#nombres)). El listado conserva
  `pstat`.
- `LuaEmitterVersion` pasa de 2 a 3 y `ExtractorVersion` de 5 a 6. Si VAN-01 sube `ExtractorVersion` antes, el corte
  que entra después toma el número siguiente.

**4b**

- **Una VM por partida:** la VM de la historia es de la sesión (la crea `StoryAdapter` en la primera lectura de las
  reglas), separada de la de la consola. La caché de módulos de `StoryLuaHost` vive lo que la partida, y los paquetes
  montados no cambian durante una partida, así que nunca hace falta invalidarla. Qué ve un módulo de historia de un
  mod es de la fase 5.
- **Ritmo:** la sección `story` del ruleset da `ticks_per_frame` (3: el motor hace tick a 60 Hz y la historia del
  original avanza 20 fotogramas por segundo, `main.c: VSync(3)`), `variable_max` (255) y las claves de
  `StoryFlowRules` de S3 (`frozen_variable`/`frozen_value` 0 y 3, `talk_facing_trigger` 44, `show_name_variable` 6
  con `show_name_by_event`/`always`/`never` 0, 1 y 2). Cada tick de la sesión es un
  tick del runner, aunque el reloj de juego esté parado; los números aleatorios de `store_random` salen del generador
  de los sistemas de la sesión (`GameSession::random()`), como el `rand()` único del original.
- **Fallo de `publishApi`:** si publicar la API `story` falla, la sesión no se crea: es un error fatal, no un aviso.
- **Versión de la API:** `LuaApiVersion` sube a 1.2 cuando `story` se publica. Si VAN-01 (`story.play_sound`) sube la
  versión menor antes, este corte toma la siguiente. Que el paquete base declare `api:` queda para la fase 5: un
  paquete sin `api:` cuenta como escrito para este motor, y declararla cambia la salida de `dwx`, lo que obliga a subir
  `ExtractorVersion` (el test de la huella del disco sintético lo exige).
- **Variables guardadas:** el original guarda 35 variables. `writeSavegame` copia `0x17C` bytes desde el principio de
  `ScriptState` (`main_menu.c:5120`; `loadSavegame` los copia de vuelta en `main_menu.c:5058`), y `ScriptState`
  (`script.h:158`) es `smth[212]`, `cards[33]`, `triggers[100]` y `pstats[256]`: `0x17C − 345 = 35`. Se siguen
  guardando 35.
- **Variables 35–255 al cargar:** el original las deja con lo que hubiera en la RAM; el motor las pone a 0, como las
  deja `initializeScripts` al encender (`script_interp.c:2208`). Es una mejora opcional de determinismo: reproduce
  cargar en un original recién encendido. `ResetUnsavedVariablesOnLoad` (`story_adapter.cpp`) en `false` vuelve a lo
  del original.
- **Versión del emisor:** `StoryLuaHost` compara la cabecera `-- dwx lua N` de un módulo con la versión de los
  módulos (`core/story_modules.hpp`, la misma que usa el emisor, porque el runtime no depende de `formats`). Al crear la
  partida mira el módulo 0, que el paquete base siempre trae; acepta de `OldestReadable` (3) a `Version` (4), porque lo
  que cambió desde la 3 solo añade llamadas (`story.cancel_closes_choice`) que un módulo viejo no hace y sin ellas se
  juega como el original; si no está en ese tramo o no la tiene, la partida no arranca, con
  un error que pide volver a extraer («content extracted by an older dwx; run dwx extract again»). Cada módulo que
  carga después pasa la misma comprobación. Sin módulos de historia (los tests sin paquete base) la partida arranca y
  el registro dice que ninguna sección puede correr.
- **Constantes de entidad:** `story.PLAYER` y `story.PARTNER` se publican junto con la primera orden con entidad que
  se publique, no antes.
- **Variable 0 al guardar:** `writeSavegame` guarda la variable 0 como 0 si vale 3 y luego la restaura
  (`main_menu.c:5080`); el motor hace lo mismo con `frozen_variable` y `frozen_value` del ruleset.

### Paridad contra el original (trazas)

El banco de referencia es del coordinador; aquí solo propongo **qué capturar**, por fotograma del original
(20 Hz), desde la RAM del emulador en las direcciones que da el decomp:

- puntero de instrucción del intérprete (script en curso y desplazamiento) y la orden en espera;
- los 800 disparadores y las 256 variables (solo los cambios);
- mapa actual, cambio de mapa en curso (fase del fundido), posición y casilla del domador;
- caja de texto: abierta, página, dueño; selección y opción elegida;
- huecos de movimiento ocupados; resultado del último combate (variable 255).

Nuestro lado escribe la misma traza con `--story-trace <fichero>` (solo con assets de desarrollo): el desplazamiento
sale del mapa de líneas (la línea Lua actual de la corrutina, leída desde C++ con `lua_getinfo`, sin coste fuera de
la traza). La comparación alinea por **fotograma desde el primer control del jugador** y por **evento** (entrar en
sección, cambiar disparador, warp), porque las entradas del jugador no caen en el mismo fotograma: se reproduce la
secuencia de pulsaciones de la traza de referencia como ruta de autotest. Una diferencia se informa con el primer
evento distinto y su localizador.

Nota (2026-11-06): `--story-trace` no llegó a existir. La traza que hay es `--trace <fichero>`, con las posiciones,
los combates y los botones de cada tick ([traza de estado](../guides/traza-de-estado.md)).

## 7. Riesgos

- **Campo sin NPC con lógica ni cámara con guion:** S4 es grande. Mitigación: los puertos se prueban con dobles; la
  vista llega al final del incremento.
- **Combate desde script:** `battle_trigger` es de depuración. S6 depende de que el combate dé resultado y vuelva al
  campo con la cámara del original.
- **Paridad de tiempos:** 20 Hz sobre 60 Hz; si el motor cambia de tick, el runner sigue contando fotogramas del
  original con la constante del ruleset.
- **Rendimiento:** 1 MB de Lua; se carga el módulo del script del mapa al cargarlo (como el original), no todo.
- **Mods:** un mod que sustituye un script entero rompe con cada cambio del emisor; el parche por sección lo reduce.

## Preguntas abiertas

Cada una con opciones; la primera es la recomendada.

### Q1. Forma de ejecución
1. **Lua generado + intérprete de referencia solo en tests** (recomendado): cumple R4.1 y la regla 2.
2. Intérprete de la IR en el motor: paridad trivial, pero viola la regla 2 y la decisión del usuario.

### Q2. Unidad de sustitución de un mod
1. **Módulo por script + parches por sección** (`story/patches/*.lua`) (recomendado): cambiar un evento no copia el
   script entero.
2. Solo el script entero por VFS: cero API nueva; frágil ante cambios del emisor.
3. Un fichero por sección: sustitución por VFS sin API, pero 1553 ficheros y las colas compartidas quedan partidas.

### Q3. Nombres de disparadores y variables
1. **Número en 4a, con comentario donde el motor les da significado; nombres simbólicos más tarde, en una tabla del
   paquete `game`** (recomendado): no inventa nombres de historia antes del GDD.
2. Nombres simbólicos ya: más legible, pero son decisiones de diseño (visibles para modders).

Estado (2026-11-06): la historia sigue con números. `story/names` del paquete `game` (`StoryNames`,
`packages/game/story`) da una etiqueta, un id en snake_case, a los disparadores y variables a los que el motor ya da
un significado (medallas, objetos de la página del jugador, música, datos de rutinas), solo para que las herramientas
los muestren; nada del juego los lee.

### Q4. Textos de historia en español
1. **El español cae al texto inglés del disco** (clave sin traducción → inglés, marcado en el log de desarrollo)
   (recomendado): no hay traducción del original que podamos distribuir.
2. Traducción hecha por el equipo en un paquete aparte: trabajo de contenido, no de 4a.
3. Bloquear las rutas `es` de historia: contradice «rutas en en y es».

### Q5. Ritmo del runner
1. **Fotogramas del original a 20 Hz, uno cada 3 ticks enteros, constante en el ruleset `original`** (recomendado):
   paridad exacta y trazas alineables.
2. Esperas en segundos: más simple, pierde la alineación de las trazas.

### Q6. Cómo cede una orden de espera
1. **`ApiRegistry::publishWaiting` en `packages/engine/script`, que acaba con `lua_yield` desde C** (recomendado): el Lua
   generado queda plano y un mod no puede saltarse la espera.
2. Envoltorios Lua de la base que hacen `coroutine.yield`: no toca `packages/engine/script`, pero un mod sin ese envoltorio
   no puede esperar y las esperas quedan escritas dos veces.
3. El Lua generado cede explícitamente tras cada orden de espera: ruido en cada línea.

### Q7. Warps al hablar y teleports en el grafo estático
1. **`via: talk` con el NPC (cuando la sección es el id de script de un NPC de la tabla del mapa) y el `teleport` del
   transporte resuelto con la tabla de destinos de la rutina 10** (recomendado; aditivo para el esquema de
   `field/script_warps`).
2. Dejarlos como están (`other`, `teleport` sin destino): el runtime los hace igual; el grafo estático (exploración
   libre, minimapa) no los ve.

### Q8. Presupuesto de la historia
1. **Presupuesto propio por reanudación (`instructionsPerResume`), menor que el de una llamada** (recomendado).
2. El de una llamada genérica (10 M instrucciones, 1 s): un mod en bucle congela un segundo cada fotograma.

### Q9. Bytecode al cocinar
1. **Texto Lua también en el `.pak`** (recomendado): una sola forma, sin cargar bytecode; corregir la tabla de la
   tubería de assets.
2. Bytecode solo para paquetes del motor: algo menos de carga, otra superficie y otra ruta de código.

### Q10. Autoguardado con una sección en marcha
1. **Guardar el estado de antes de la sección** (recomendado): al cargar, la cadena de `MAPHEAD` vuelve a correr como
   en el original.
2. No autoguardar mientras corre una sección: retrasa autoguardados; más cerca del original (que no autoguarda).

### Q11. Semántica entera y accesos fuera de rango
**Decidido (cambiado otra vez el 2026-10-22, «el decomp es inspiración»):** los operandos son enteros sin el ancho
de la máquina original (ni 8 ni 16 bits, ni errores de ancho de operando). Lo que el jugador ve del original se
conserva como regla: los topes de juego (`add_to_pstat` hasta 255, `give_item` hasta 99, `reduce_merit` hasta 0, el
rango de las variables) son reglas del ruleset, sumar y restar recortan al rango sin desbordar, y las comparaciones
siguen la ficha ([Rarezas](../reference/scripts.md#rarezas)). Un acceso fuera del estado configurado es un error de
Lua registrado, y cada caso está en esta lista con su localizador:

| Caso | Localizador |
|---|---|
| Más de 256 subrutinas pendientes en las secciones en espera (guarda del motor contra el desbocamiento; el original tiene 8 marcos sin comprobar) | `script_interp.c: pushScriptStack` |
| `jump_return` con la pila vacía o sobre un marco 3/4 | `script_interp.c: scriptInstruction10to27` |
| Disparador o variable fuera de la capacidad configurada (en el original un disparador ≥ 800 pisaría las variables; el disco no lo hace, ver [`StoryState`](#storystate)) | `script_interp.c: getTriggerOffset`; `script.h: ScriptState` |
| Deslizar (`entity_move_to*`) con 0 pasos (división entre cero) | `tamer.c: tickEntityMoveTo` |
| `switch` con 0 casos | `script_interp.c: scriptInstruction10to27` |
| `jump_to_file` a una sección que no existe | `script_interp.c: scriptInstruction10to27` |

El disco no alcanza ninguno. La lista crece solo con nuevos casos de este tipo, nunca con reglas.

### Q12. El fallo intermitente de los tests del sandbox
1. **Paquete aparte en `packages/engine/script`: medir la parada en instrucciones y dejar el reloj como techo holgado**
   (recomendado).
2. Subir las cotas de reloj: menos fallos, el mismo problema con más carga.
3. Marcarlos como lentos y no correrlos en `check-commit`: los esconde.

### Q13. Cómo se llega a `miha03`
El decomp no da ningún camino a `miha03` (ni tabla, ni `warp_to`, ni el transporte), y tampoco a `mgen14`–`mgen16`.
1. **Pedir al banco de referencia una traza con el cambio de mapa a `miha03`** (recomendado): dice qué código lo hace
   (un `changeMap` desde código, como la muerte o el objeto de volver a casa).
2. Aceptarlos como excepciones documentadas de alcance (la tabla que prevé la parte (b) en `campo.md`).

### Q14. Números aleatorios de `store_random`
El original usa el `rand()` de la librería C (semilla por defecto, sin `srand`), compartido con combate, torneos y
campo (`math.c: randomLimit`, `script_interp.c`).
1. **El generador de la sesión, con la semilla de la partida, y la misma fórmula de recorte `(límite × r) >> 15`**
   (recomendado): determinista y reproducible por semilla; la secuencia exacta del original no se puede igualar
   porque depende de todo lo demás que consume `rand()`.
2. Imitar el `rand()` del original con un flujo compartido por todos los sistemas: solo daría trazas iguales si todos
   los sistemas consumieran en el mismo orden; es un cambio que no es de este paquete.

## Contradicciones con la IR de las partes (a) y (b)

1. **La IR no lleva el texto.** `ScriptListing` deja fuera el texto de `show_textbox` y `set_selection` («Text, DATA
   bytes and pad bytes stay in the YAML»), y el emisor de Lua lo necesita para las claves y el catálogo. Propuesta
   S0: `ListingInstruction` gana `text` (opcional), leído por los dos constructores.
2. **Las guardas no son `guard.hpp` con `all`/`any`/`not`.** El documento de warps de script lo dice así, pero el
   código tiene `PathGuard` (lista de decisiones `BranchTerm`/`ChoiceTerm`) y el YAML usa `guard.terms` y
   `script_guard`. Al emisor le da igual (escribe los `if` desde el árbol estructurado y el plegado de
   `scriptIfInstruction`), pero el documento de (a) debería decir lo que hay.
3. **`via: talk` no existe.** El documento de (a) lo describe (`via: talk`, `cell` = id del NPC), pero el código y el
   esquema solo tienen `step`, `confirm`, `enter` y `other`. Es la Q7.
4. **`teleport` con variable no tiene destino** en el grafo estático, y el documento de (a) lo lleva a `global`; en el
   código queda como un warp sin destino. Es la otra mitad de la Q7.
5. **`data/new_game_story` como estado inicial** sirve para (a)/(b); con el runner, el estado inicial lo da ejecutar
   `MAPHEAD 0xCC` y la `0xFE` del primer mapa. No es contradicción, pero (b) no debería escribir más estado inicial
   desde el análisis estático.
