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
  devuelve el residente. `dwx` lo comprueba y lo extrae solo desde `MAPHEAD.SCN`.

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

1. **`readScriptListing`** lee del YAML solo lo que mueve el flujo: secciones, offsets, el papel de cada orden (salto,
   llamada, retorno, `IF`, `SWITCH`, `SET_SELECTION`, fin) y sus destinos. El formato del YAML no cambia.
2. **`buildScriptFlow`** parte el código en bloques básicos. Un `IF` es una cadena de bifurcaciones, una por cada
   entrada `goto_if_*`, en su orden. Un `SWITCH` elige entre sus casos; un `SET_SELECTION`, entre sus opciones y, al
   cancelar, sigue detrás del bloque. Las rutinas son las secciones y los destinos de `jump_and_link`.
3. **`structureScript`** convierte cada rutina en sentencias: `if`/`else` cuyas ramas se juntan en el
   post-dominador, bucles naturales con `break` a su salida, elecciones y llamadas finales a otra rutina. El código al
   que saltan varias rutinas, o varios caminos de una, se vuelve una **cola compartida** (una rutina más) si tiene al
   menos 3 órdenes; si es más corto se copia. Lo que no se deja anidar (un bucle con dos entradas, una salida de dos
   bucles a la vez, demasiadas copias) se vuelve una **máquina de estados**: un bucle que despacha cada bloque según
   una variable. Así toda rutina se convierte, sin saltos.

Lua no tiene `continue`: una rama que vuelve a la cabeza del bucle se lleva el resto del cuerpo a la otra rama. Las
cifras sobre el disco están en la [ficha de scripts](../reference/scripts.md#forma-del-control-de-flujo).

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
