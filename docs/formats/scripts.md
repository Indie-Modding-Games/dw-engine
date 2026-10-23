# Scripts de eventos

Cómo lee `formats/script` los scripts de eventos (`SCN/MAPHEAD.SCN` y `SCN/DG.SCN`) y cómo los escribe `dwx extract`.
El desensamblador sigue el juego de opcodes de `include/dw/script.h` y las lecturas de operandos de
`script_interp.c: tickScript` y sus `scriptInstruction*`. Lo que el intérprete no ejecutaría (un opcode sin caso,
una prueba de `IF` sin manejador, operandos que se salen) es un `unknown`, nunca una suposición.

## Ficheros

- **`MAPHEAD.SCN`** es el script 0: `initializeScripts` lo lee entero y lo deja residente.
- **`DG.SCN`** guarda los demás. `initializeScripts` lee sus primeros 0x2000 bytes como tabla de offsets `u32` por id
  de script, y `getScript(id)` lee `[tabla[id], tabla[id + 1])`. `readScriptTable` recorre la tabla hasta la entrada
  que vale el tamaño del fichero y falla si un offset retrocede o se sale. Los bytes de la tabla tras esa entrada son
  restos que nadie lee (la lectura de 0x2000 bytes pisa además el principio de los primeros scripts).
- Cada script ocupa sectores enteros: tras su código viene **relleno de sector**, restos que a menudo parecen código y
  que nadie lee. El final real lo da el barrido (abajo), nunca el tamaño del hueco. `dwx` lo cuenta en una nota.
- El script 0 de `DG.SCN` es una copia byte a byte de `MAPHEAD.SCN` (más relleno) que nunca se carga: `getScript(0)`
  devuelve el residente. `dwx` lo comprueba y lo extrae solo desde `MAPHEAD.SCN`; si no fuera una copia, los dos
  serían `story/000`, así que la extracción de `DG.SCN` se detiene con un mensaje en lugar de elegir uno.
  Solo se comparan los bytes de `MAPHEAD.SCN` con el principio de la ranura: el resto es relleno de sector y no se mira.

## Cabecera

```
+0  u16 codeStart                     = 2 + 4 * secciones + 2: el offset de la primera instrucción
+2  {u16 id, u16 offset}[secciones]   offsets desde el principio del script
    u16 0xFFFF
```

`readScriptHeader` comprueba que `codeStart` es el final de la lista y que cada sección empieza dentro del código.
Una sección es un punto de entrada que elige el motor (`getScriptSection`, `callScriptSection`): en `MAPHEAD`, el id
de mapa (y unas secciones especiales que llama el código del motor); en los demás, `0xFE` al entrar en el mapa, el id
de script de un NPC al hablarle y los disparadores de casilla.

## Instrucciones

Un byte de opcode y sus operandos, en little-endian. Los operandos de 16 y 32 bits se leen alineados, así que las
codificaciones llevan bytes de relleno que el intérprete salta; las instrucciones de un byte (`FC`–`FF`) van seguidas
de un `00`. Toda instrucción empieza en offset par, salvo los bloques `DATA` (abajo). Los saltos son offsets `u16`
absolutos desde el principio del script.

Tipos de operando (`ArgumentKind`):

| Tipo | Bytes | Qué es |
|---|---|---|
| `Pad` | 1 | relleno; si no es 0 se guarda en `pads` |
| `U8`, `U16`, `S16`, `S32` | 1, 2, 2, 4 | números; `S32` se lee en dos mitades (`pollNextInt`) |
| `Pstat` | 1 | índice de una variable de script (`ScriptState.pstats`) |
| `Trigger` | 2 | número de un bit de disparador (`ScriptState.triggers`) |
| `Entity` | 1 | id de script de un NPC; `0xFD` es el domador y `0xFC` el compañero (`scriptIdToEntityId`) |
| `Label` | 2 | destino de un salto |

Los nombres de opcode son los `SCRIPT_OP_*` del decomp en minúsculas (`show_textbox`, `jump_to`...); los que no tienen
nombre allí son `unused_XX`. Grupos (`tickScript` despacha `10`–`3F`, `46`–`58`, `5A`–`5F`, `64`–`7E` y `FB`–`FF`):

| Opcodes | Qué hacen |
|---|---|
| `10`–`1A` | flujo: selección, saltos (`13` con retorno, `16` sin él, `14`/`17` a otro script), `15` retorno, `18` `SWITCH`, `19` `IF`, `1A` texto |
| `1B`–`27` | diálogo, disparadores, variables, fecha, tamaño de la caja de texto |
| `28`–`3F` | objetos, dinero, técnicas, cartas, méritos, estadísticas, fecha |
| `46`–`58` | Digimon y entidades: cargar, mover, girar, cámara, animaciones, warps |
| `5A`–`5F` | sonido y música |
| `64`–`7E` | rutinas del motor, combate, esperas, daño, `6B` `DATA`, objetos y sprites del mapa |
| `FB` | `SET_SCRIPT`: carga otro script y mapa y apila un marco de tipo 3 |
| `FC`, `FD` | sin caso en `tickScript`: esperan un fotograma |
| `FE`, `FF` | `END_SECTION` y `END_SCRIPT`: los dos llaman a `returnFromScriptFile` y deshacen la pila entera |

Las de longitud variable:

- **`IF` (`19`)**: tras `19 00`, entradas `código, relleno, datos` que se evalúan de izquierda a derecha, hasta una
  entrada cuyo código es exactamente `19` (`script_interp.c: scriptIfInstruction`, `script_value.c`). Bit 7 del código:
  `resultado = resultado AND c`; si no, bit 6: `OR`; si no, `resultado = c`. `código & 0x38` elige la prueba:
  `00` disparador (bits bajos 0 = puesto, otro = quitado), `08` variable contra valor (bits bajos = comparación),
  `10` / `18` saltar si cierto / falso, `20` extendida por los bits bajos: estadística, carta, técnica, condición,
  número de objetos y dinero. Las comparaciones son `eq`, `ne`, `ge`, `le`, `gt`, `lt` (0–5); 6 y 7 no tienen caso.
  `28`, `30`, `38` y las extendidas 6 y 7 no tienen manejador: son `unknown`.
- **`SWITCH` (`18`)**: variable, `u16` número de casos y un `u16` destino por caso.
- **`SET_SELECTION` (`10`)**: `u8` número de opciones y un destino por opción, la palabra `1A 00` (que
  `scriptShowSelection` cuenta como una entrada más) y el texto de la pregunta. Si sigue `12 00`, es la marca que
  salta la cancelación (`tickScriptDialogueBox`) y forma parte de la instrucción.
- **`SHOW_TEXTBOX` (`1A`)**: relleno y texto (ver [Textos](textos.md)).
- **`DATA` (`6B`)**: relleno y bytes hasta un `0xFF`. Nunca se ejecuta: lo leen los torneos por tablas de saltos
  (`tournament.c: getCupDataJumpTableEntry`, `dget.c: isTournamentEnabled`, `tournamentCheckEligible`), que paran en
  el primer byte `≥ 0xFE`; una lista que empieza por `0xFE` vale por todos los disparadores (`FE FF`). Como se leen
  byte a byte, los bloques van seguidos sin alinear.

## Barrido

`disassembleScript` decodifica de forma lineal desde `codeStart`:

1. Decodifica la instrucción; si no puede, anota un `unknown` y para.
2. Tras una instrucción que acaba en offset impar, un `00` es relleno. Otra cosa que no sea un `DATA` es una
   desincronización: `unknown` y para.
3. Para en el primer `END_SCRIPT` que queda más allá de toda sección y todo destino visto. Los scripts de las arenas
   guardan bloques `DATA` después de su primer `END_SCRIPT`, a los que llegan sus tablas de saltos; el barrido sigue
   por ellos.

Después comprueba que cada sección y cada destino caen en el inicio de una instrucción (si no, `unknown`) y cuenta las
instrucciones a las que no llega ningún flujo desde una sección. Esas se quedan en el listado: la mayoría son un
salto tras un `END_SECTION` que el compilador dejó, y las tablas de saltos de `MAPHEAD` parecerían muertas.

En el disco US todos los bytes de código decodifican, con 0 `unknown`, y el texto de cada instrucción vuelve a sus
mismos bytes.

## Listado

`dwx extract` escribe un YAML por script: `script/maphead.yaml` y `script/dg/<id>.yaml` (`script/dg/007.yaml`).

```yaml
code_start: 0x000c
code_end: 0x0020
sections:
- id: 0x00fe
  at: l_000c
- id: 0x0005
  at: l_001e
unreachable_instructions: 0
instructions:
- at: 0x000c
  label: l_000c
  op: if
  conditions:
  - test: pstat
    compare: ge
    pstat: 5
    value: 2
  - test: goto_if_false
    target: l_001e
- at: 0x0018
  op: entity_set_rotation
  entity: partner
  rotation: -200
- at: 0x001c
  op: end_section
- at: 0x001e
  label: l_001e
  op: end_script
```

- `at` es el offset; `label` (`l_<offset>`) solo si algo salta ahí; luego `op` y los operandos por nombre.
- Los destinos son nombres de etiqueta. Entidades `0xFD` y `0xFC` salen como `tamer` y `partner`.
- `conditions` (las entradas de `IF`, con `combine: and|or`, `clear: true` para un disparador quitado y `code` solo si
  el byte no es el que darían los campos), `cases` (`SWITCH`), `options` (`SET_SELECTION`), `text`,
  `cancel_marker: true` (la marca `12 00`), `data` (los bytes de `DATA` en hex) y `pads` (rellenos que no son 0).
- `unknowns` solo si los hay.

El id estable de un texto de script es el script y el offset de su instrucción (`dg/007@0x0040`); el catálogo de
textos de la fase 4 (Lua) lo usará. El listado no es Lua: la traducción es trabajo de la fase 4.

## Control de flujo

El paso a Lua (fase 4) parte del listado, no de los bytes, y no puede emitir saltos: el Lua convertido es código
anidado que se lee y se modifica. `formats/script` lo prepara en tres pasos:

1. **`ScriptListing`** es la representación de los scripts que usa todo lo demás: secciones, offsets, el papel de
   cada orden (salto, llamada, retorno, `IF`, `SWITCH`, `SET_SELECTION`, cambio de mapa, fin), sus destinos, sus
   operandos y condiciones con nombre y el texto de `show_textbox` y `set_selection` (tal como lo escribe el listado;
   las opciones de una selección, una por línea). Sale del YAML (`readScriptListing`, el de un mod) o del
   desensamblado (`listingOf`, el de `dwx`) con la misma tabla de órdenes; un test de disco comprueba que dan lo mismo
   en todos los scripts, pasando por el texto del YAML. Solo los bytes de `DATA` y los de relleno se quedan en el
   YAML. `warp_to` y `teleport` acaban el flujo (`FlowRole::MapChange`): nada de lo que sigue corre. El formato del
   YAML no cambia.
2. **`buildScriptFlow`** parte el código en bloques básicos. Un `IF` es una cadena de bifurcaciones, una por cada
   entrada `goto_if_*`, en su orden. Un `SWITCH` elige entre sus casos; un `SET_SELECTION`, entre sus opciones y, al
   cancelar, sigue detrás del bloque. Las rutinas son las secciones y los destinos de `jump_and_link`.
3. **`structureScript`** convierte cada rutina en sentencias: `if`/`else` cuyas ramas se juntan en el
   post-dominador, bucles naturales con `break` a su salida, elecciones y llamadas finales a otra rutina. El código al
   que saltan varias rutinas, o varios caminos de una, se vuelve una **cola compartida** (una rutina más) si tiene al
   menos 3 órdenes; si es más corto se copia. Lo que no se deja anidar (un bucle con dos entradas, una salida de dos
   bucles a la vez, demasiadas copias) se vuelve una **máquina de estados**: un bucle que despacha cada bloque según
   una variable. Así toda rutina se convierte, sin saltos.

Encima del árbol, **`walkSection`** (`guarded_walk.hpp`) recorre una sección y da cada orden que se le pide con
lo que hace falta para llegar a ella: las decisiones de sus `IF`, `SWITCH` y `SET_SELECTION`, los disparadores que
pone por el camino y los que conoce al llegar. Un `IF` cuyas pruebas son de disparadores que el camino conoce toma su
rama sin dejar condición. Una llamada a una rutina que ya se está recorriendo no se sigue: deja un término
`RecursionTerm` (la rutina llamada) que dice que lo que decide no se sabe, y el camino sigue después con lo que
conocía olvidado. Una orden que corre código del juego que puede escribir disparadores (`give_item`, `compare_date` y
las rutinas de `call_routine` y `call_digimon_routine` que lo hacen, según `routine_writes.hpp`) deja un término
`ForgetTerm` y el camino sigue sin saber esos disparadores. Lo usan los [warps de script](../architecture/warps-de-script.md); lo usarán la conversión
a Lua y el intérprete de historia.

Lua no tiene `continue`: una rama que vuelve a la cabeza del bucle se lleva el resto del cuerpo a la otra rama. Las
cifras sobre el disco están en la [ficha de scripts](../reference/scripts.md#forma-del-control-de-flujo).

## Lua

`formats/script/lua_emitter` escribe cada script estructurado como un módulo Lua, y `dwx extract` lo deja junto al
listado (que no cambia: sigue siendo el formato que edita `dw1fe`):

| Fichero | Qué es |
|---|---|
| `story/<n>.lua` | El módulo: devuelve `{ script = <n>, sections = { [0x0051] = <función>, ... } }` |
| `story/<n>_lines.yaml` | Por cada línea que ejecuta una orden, su offset en el listado ([esquema](../../content/schemas/story_lines.schema.json)) |
| `text/en/story_<n>.yaml` | Los textos de `show_textbox` y `set_selection`, con la clave `story_<n>.text.<offset>` |

`<n>` es el id del script con tres cifras, el mismo número que usan los NPC de los mapas (`script_id`): `000` es
`MAPHEAD.SCN`, que el juego carga como script 0, y `001`–`221` los de `DG.SCN` (`script/maphead.yaml` y
`script/dg/<n>.yaml` en el listado). Los nombres de las funciones y de las claves están en
[la API de historia](../modding/historia.md).

La forma del módulo:

- Cada rutina (sección, subrutina, cola compartida) es una función de la tabla `routines`, nombrada por el offset de
  su entrada (`routines.at_04c0`); las que empiezan en la misma orden son una sola.
- Si la tabla de secciones repite un id, `sections` lleva solo la primera, que es la que encuentra `getScriptSection`;
  las demás quedan como comentario. En el disco pasa en algunos scripts (el 4 y el 52, por ejemplo).
- Cada orden es una llamada `story.<nombre>(operandos)` con los operandos del listado en su orden, sin relleno ni
  etiquetas, y un comentario con sus nombres si son dos o más. `<nombre>` es el de la orden, salvo las que la API
  renombra ([tabla](../modding/historia.md#nombres)). Una entidad es `PLAYER`, `PARTNER` o el id del NPC.
- Una rutina devuelve `ENDED` si el script acabó (`end_section` y `end_script` lo acaban desde cualquier
  profundidad) y nada en un `jump_return`. Un `jump_and_link` es `if routines.at_X() == ENDED then return ENDED end`;
  un salto a código compartido, la llamada final `return routines.at_X()`; `jump_to_file`, `warp_to` y `teleport`,
  `return story.<nombre>(...)`.
- Un `IF` es el pliegue de sus entradas de izquierda a derecha con paréntesis explícitos
  (`(story.trigger(3) and story.var(5) >= 2) or story.money() < 4294967295`). Cada constante va en el dominio en que
  el original compara: sin signo de 32 bits, salvo la felicidad (stat 9), con signo de 16.
- Un `SWITCH` es una cadena `if`/`elseif` sobre `story.var(n)` en la que un valor de más va al último caso; un
  `SET_SELECTION`, `local choice <const> = story.set_selection(clave, n)` con la cancelación (`choice == n`) en el
  último `else`. Los bucles son `while true do` con `break`; una máquina de estados, un `while true do` que despacha
  sobre una variable `stateN`.
- Nunca hay saltos. La salida es la misma byte a byte para el mismo listado, venga del YAML o del desensamblado.

`dwx convert-script <listado> -o <carpeta>` convierte un listado suelto (el YAML que edita un mod) con el mismo
código, y `dwx validate` compila todo el Lua de los paquetes. Los tests corren cada sección en Lua, con una API
`story` falsa, contra un intérprete de referencia del listado, y comparan las llamadas y cómo acaba
(`formats/tests/lua_emitter_tests.cpp`, y sobre el disco `lua_emitter_disc_tests.cpp`). Los mismos tests comprueban
que ningún nombre generado tiene una palabra prohibida (`ForbiddenNameWords`).

La prueba con listados aleatorios tiene dos tamaños. La de cada commit (`FastParity`: 120 listados, 4 mundos, unos
5 s) corta antes los bucles: uno que llama menos de una vez cada ~25 pasos del listado se corta antes de 200 llamadas,
y dos ejecuciones cortadas solo se comparan en las llamadas que hicieron las dos, así que no ve una diferencia
posterior al corte más corto. La completa (`FullParity`: 300 listados, 8 mundos, límites con los que todo bucle con
llamadas llega a 200 y las ejecuciones cortadas deben coincidir llamada a llamada) corre con `DWE_FULL_PARITY=1` y
tarda minutos; la de paridad sobre el disco usa siempre esos límites.

## Estadísticas de la partida nueva

Ningún código C pone los stats con los que empieza el partner: los pone el script 193 de `DG.SCN` tras la
introducción. Guarda el tipo del partner en una variable (`store_digimon_type`), pregunta con un `if` si es un tipo
concreto y, en cada rama, una serie de `set_stat` pone los ocho valores (ataque, defensa, velocidad, inteligencia, HP y
MP máximos, HP y MP actuales); la primera rama acaba con un `jump_to` que salta la otra. `findNewGameStats`
(`formats/script/new_game_stats.hpp`) busca ese bloque en el listado y `dwx` lo escribe en `data/new_game_stats`
([esquema](../../content/schemas/new_game_stats.schema.json)): `script`, `at` (el offset del `if`), `partners` (el
Digimon por el que pregunta, por su id, y sus stats) y `others` (los de cualquier otro partner). Si el script no tiene el
bloque, se anota y no se escribe el documento. La sesión lo lee al empezar una partida (`apps/session`).

## Cobertura

Un fichero queda `extracted` si todos sus scripts decodifican hasta el final del código sin `unknown`; el relleno de
sector, los restos de la tabla y la copia del script 0 son notas. Si no, queda `partial` con cada `unknown` precedido
de su script.
