# Efectos de batalla

Cómo lee `formats/effect` `ETCHI/EFEDAT.EFE`, el fichero de los efectos de las técnicas, y qué saca `dwx extract` de
él. Los nombres de campo son los del decomp (`include/dw/battle_effect.h`, `src/btl/battle_effect.c`); las funciones
se citan como `fichero: función`.

## El fichero

Son 122 ranuras de 0x5000 bytes, una por técnica: la ranura `n` es el efecto de la técnica `n` y su id de efecto es
`0x100 + n`. El original carga una ranura con un `CdRead` de 0x5000 bytes al offset `n × 0x5000`
(`BTL_loadEFESlot`) y la prepara en `BTL_setupLoadedEFEFile`.

- La ranura 121 es toda relleno (`0xCD`): ninguna técnica la usa.
- La ranura 110 guarda el id `0x158`, el de la ranura 88, aunque su contenido es otro. `BTL_setupLoadedEFEFile`
  compara el id guardado con el pedido y, si no coinciden, marca la ranura como fallida (`isLoaded = -3`) y no la
  usa: esa técnica sale sin efecto. `dwx` la extrae tal cual y lo dice en el informe y en su `effect.yaml`
  (`loads: false`).

`efeSlotCount` exige un número entero de ranuras; `efeSlot` da los bytes de una, e `isEmptyEfeSlot` dice si es toda
relleno.

## Cabecera

`EfeFileHeader`, 0x34 bytes, todo `s32`. Los offsets cuentan desde el final de la cabecera (la base, a la que el
decomp llama `EFE_SCRIPT_HEAD`):

| Offset | Campo | Qué es |
|---|---|---|
| 0x00 | `startScript` | script con el que arranca el efecto (`BTL_startEFE`) |
| 0x04 | `initScript` | script que corre mientras la ranura se carga, cuatro instrucciones por fotograma |
| 0x08 | `uvAnims` | sala de `EfeUvAnim`, 0x20 bytes por objeto del TMD |
| 0x0C | `uvAnimsEnd` | |
| 0x10 | `subEffects` | sala de `EfeSubEffect`, 0x1C bytes cada uno |
| 0x14 | `tmdStart` | el TMD (vacío si la ranura no tiene modelo) |
| 0x18 | `tmdEnd` | |
| 0x1C | `timStart` | el TIM |
| 0x20 | `timEnd` | |
| 0x24 | `dataEnd` | fin de lo que la cabecera direcciona |
| 0x28 | `spawnCount` | `unk28` en el decomp: ningún código lo lee; es el número de instrucciones `spawn` de la ranura |
| 0x2C | `numSubEffects` | registros de la sala de sub-efectos |
| 0x30 | `effectId` | `0x100 +` ranura |

`spawnCount` se llama así porque en las 121 ranuras usadas coincide con el número de `spawn` (y de tablas de
instancias); el original no lo usa.

`readEfeHeader` comprueba que las partes van en este orden y sin huecos, que es como las deja la herramienta original:

```
0 ≤ startScript ≤ initScript ≤ uvAnims ≤ uvAnimsEnd = subEffects ≤ tmdStart ≤ tmdEnd = timStart ≤ timEnd = dataEnd
```

además de que `dataEnd` cabe en la ranura, que la sala de UV es un número entero de registros y que la de
sub-efectos mide `0x1C × numSubEffects`.

## Partes de una ranura

| Desde | Hasta | Contenido |
|---|---|---|
| 0 | `startScript` | área de datos de los scripts: variables, tablas, listas, instancias |
| `startScript` | `uvAnims` | los dos scripts, uno detrás del otro |
| `uvAnims` | `tmdStart` | salas de trabajo: animaciones de UV y sub-efectos |
| `tmdStart` | `timStart` | TMD, con offsets relativos a su tabla de objetos |
| `timStart` | `dataEnd` | TIM de 4 bits |
| `dataEnd` | fin de ranura | relleno |

- **Salas de trabajo.** Lo que el fichero guarda ahí no son datos: el cargador limpia `EfeSubEffect.inst` y
  `EfeUvAnim.unk0` y `numKeyframes`, y los subopcodes escriben el resto de campos antes de leerlos. Muchas salas
  traen bytes viejos de la herramienta.
- **Relleno.** Después de `dataEnd` todo es `0xCD` salvo en las ranuras 110 y 112, que tienen restos: bloques
  iguales a bytes de otras ranuras (55, 78, 89). Nada los direcciona; el informe los anota.
- **TMD.** `efeModelGaps` comprueba que la cabecera, la tabla de objetos, los paquetes de primitivas, los vértices y
  las normales cubren el TMD entero.

## Texturas en VRAM

El original ignora las posiciones que trae el TIM. Puede tener 16 efectos cargados a la vez, cada uno en su celda de
VRAM (`BTL_setupLoadedEFEFile`):

- píxeles: página `0x19 + celda / 8`, en `x = (página % 16) × 64`, `y = (página / 16) × 256 + (celda % 8) × 32`;
- CLUT: `x = 128 + (celda / 4) × 16`, `y = 488 + (celda % 4) × 6`.

Al cargar el TMD lo reescribe para su celda con `graphics.c: updateTMDTextureData`: en cada objeto, hasta la primera
primitiva sin textura, cambia la página de textura por la de la celda, suma a las UV el desplazamiento de la celda
dentro de su página y mueve el CBA desde la CLUT de la celda 0 (`0x7A08`) a la de la celda. `efeVramCell` da la
celda y `placeEfeModel` hace la misma reescritura. Los TIM del disco ya traen la posición de la celda 0, así que los
modelos se texturizan como los ve la celda 0.

## Scripts

Una instrucción empieza con un `u16`: el opcode en el byte bajo y una variante en el alto. El intérprete
(`BTL_runEFEScript`) despacha por la tabla de 18 opcodes; cada manejador avanza un número fijo de bytes, así que los
dos scripts se decodifican de corrido, sin seguir saltos (`decodeEfeScripts`). Un script puede acabar con un `0xCDCD`
después del último `return` (73 casos en el US): es relleno y se anota.

Una variable es un par `(s16 offset, u16 flag)`: con flag 0 cuenta desde los datos de la instancia que corre, con otro
valor (`0xDAAD` en la práctica) desde la base. El ancho va en un nibble de la variante: 4 es `s32`, 2 `s16` y lo demás
`s8`. Hay un registro de 32 bits y una pila de datos.

| Opcode | Nombre | Bytes | Operandos | Manejador |
|---|---|---|---|---|
| 0x00 | `load_immediate` | 6 | valor `s16` al registro | `BTL_loadEFEImmediate` |
| 0x01 | `load_variable` | 6 | variable al registro | `BTL_loadEFEVariable` |
| 0x02 | `load_random` | 2 | número aleatorio al registro | `BTL_loadEFERandomValue` |
| 0x03 | `load_indexed` | 10 | tabla (desde la base), índice: `tabla[índice]` al registro | `BTL_loadEFEIndexedVariable` |
| 0x04 | `apply_operator` | 6 | variable `op=` registro | `BTL_applyEFEVariableOperator` |
| 0x05, 0x06, 0x08 | `stop`, `stop_6`, `stop_8` | 2 | para el script | `BTL_stopEFEScript` |
| 0x07 | `branch_if` | 10 | variable, destino: salta si `variable cmp registro` | `BTL_branchEFEOnComparison` |
| 0x09 | `jump` | 6 | destino | `BTL_jumpEFEScript` |
| 0x0A | `push_immediate` | 6 | valor `s16` a la pila | `BTL_pushEFEImmediate` |
| 0x0B | `push_variable` | 6 | variable a la pila | `BTL_pushEFEVariable` |
| 0x0C | `push_address` | 6 | dirección de una variable a la pila | `BTL_pushEFEVariableAddress` |
| 0x0D | `call` | 6 | destino | `BTL_callEFESubroutine` |
| 0x0E | `sub` | 2 | subopcode en la variante | `BTL_dispatchEFESubOpcode` |
| 0x0F | `return` | 2 | | `BTL_returnFromEFESubroutine` |
| 0x10 | `pop_to_variable` | 6 | saca de la pila a una variable `s32` | `BTL_popEFEValueToVariable` |
| 0x11 | `spawn` | 12 | cuántas, tabla de instancias, paso | `BTL_spawnEFESubEffect` |

Detalles de cada uno:

- **Inmediatos** (0x00, 0x0A): el manejador lee un solo `s16`. La herramienta escribió un literal de 32 bits, cuya
  mitad alta (el segundo `u16`) es casi siempre la extensión de signo; cuando no lo es, el YAML la muestra como
  `value_high`.
- **`apply_operator`**: el nibble bajo elige la fila de `BTL_EFE_VARIABLE_OPERATORS` (filas 0, 1 y 3 de `s8`, 2 de
  `s16`, 4 de `s32`) y el alto la operación: `set`, `add`, `subtract`, `multiply`, `divide`, `modulo`,
  `shift_left`, `shift_right`.
- **`branch_if`**: el nibble alto elige la comparación de `BTL_EFE_COMPARISONS`: `equal`, `not_equal`, `less`,
  `less_or_equal`, `greater`, `greater_or_equal`. El quinto `u16` es una bandera que nadie lee.
- **`load_indexed`**: el nibble bajo es el ancho de los elementos y el alto el del índice; el flag de la tabla no se
  lee. La longitud de la tabla no está en ninguna parte.
- **`push_address`**: el manejador no lee la variante; la herramienta dejó ahí el ancho de lo que apunta (4, 2 o 1)
  o relleno. El YAML lo muestra como `declared_width`.
- **`call`** apila destino, contexto y vuelta en una pila de llamadas aparte; la pila de datos se conserva, así que
  quien llama pasa argumentos por ella.
- **`spawn`** busca una instancia libre en la tabla (`count` entradas de `stride` bytes, cada una un `EfeInstance`
  seguido de las variables del sub-efecto) y sigue en su contexto; si no hay ninguna libre hace un `return`. La
  variante numera los `spawn` de la ranura desde 0 (la herramienta lo dejó así; no se lee). El cuarto y el sexto
  `u16` no se leen.
- Los `u16` que el manejador no lee se comprueban contra su valor de siempre (`efeIgnoredOperands`); si alguno es
  distinto, el YAML lo muestra.

`decodeEfeInstruction` rechaza un opcode de 0x12 en adelante, una instrucción que no cabe en su script, una fila de
operadores o una comparación fuera de su tabla y un subopcode de 0x61 en adelante.

### Puntos de entrada

- `startScript` lo ejecuta `BTL_startEFE`; `initScript`, la carga.
- El subopcode 2 (`set_sub_effect_scripts`) pone en el contexto el script de cada fotograma (`inst`, que corre
  `BTL_tickEFEEngine`) y el de dibujo (`someInst`, `BTL_renderEFEEngine`). Los dos llegan por la pila como
  `push_address` desde la base.

## Subopcodes

Los 97 están en `formats/src/effect/efe_subops.cpp`, cada uno con su manejador y sus argumentos en el orden en que se
apilan. Los nombres salen de lo que hace el manejador con cada argumento. Ninguno tiene aridad variable y ninguno deja
nada en la pila. El 4 apila siete valores y llama al 24, y el 6 apila −1 y llama al 91; sus argumentos son los netos.
`EFERotateVector` (7) y `EFECreateFlash` (18) están en `src/main/efe.c`.

Un argumento que es una dirección dice cuánto lee el manejador:

| Tipo | Lo que hay en la dirección |
|---|---|
| `int32` | un `s32` que el manejador lee o escribe |
| `vector`, `color` | tres `s32` (`EfeVector`, `EfeColor`) |
| `bone_offsets` | `EfeTechBoneOffset` de 10 bytes (tipo de Digimon, hueso, desplazamiento) hasta un tipo < 0 |
| `parallax_sprites` | entradas de 10 bytes hasta una con tamaño < 0 |
| `uv_keyframes` | `s32` n y n `EfeUvKeyframe` (`s16` fotogramas, `s8` du, dv) |
| `parent_field` | tantos bytes como dice el argumento `size` (12, 8, 4, 2 o 1) |
| `int32_array` | tantos `s32` como dice el argumento de cuenta |
| `clut_colors` | tantos colores `{u16 r, g, b, stp}` como dice la cuenta |
| `clut_row` | 16 colores desde el índice que da otro argumento |
| `tile_map` | 11 × 9 ids de tile |
| `tile_offsets` | un `s16` por id hasta el mayor del mapa de tiles |
| `ribbon_points` | 10 puntos de cuatro `s16` |
| `quad_corner` | con transparencia < 0, un punto de 8 bytes; si no, el argumento es un número |
| `code` | un script |

Quedan dos argumentos sin significado, con nombre `unknown0`:

- subopcode 1 (`init_uv_anim`): va a `EfeUvAnim.unk0`, que solo lee `BTL_unloadEFESlot` para limpiarlo;
- subopcode 85 (`render_ribbon`): va a `EFE_RIBBON_SCRATCH->width`, que nada lee.

Los que el manejador saca de la pila y no usa se llaman `unused` (subopcodes 11, 36, 67 y 89).

## Análisis del área de datos

El área de datos no tiene índice; solo los scripts dicen qué es cada byte. `analyzeEfeScripts`:

1. Junta los destinos de saltos, llamadas y argumentos de código como etiquetas; un destino que cae entre
   instrucciones es un problema.
2. Recorre las instrucciones con una pila simbólica (vacía en cada etiqueta y tras un salto, llamada, `return` o
   `stop`) y une cada argumento de subopcode y cada `pop_to_variable` con la instrucción que lo apiló. Un valor que
   llega de quien llama queda sin unir. Repite hasta que no aparecen etiquetas nuevas.
3. Tipa los datos: variables de la base que leen o escriben las instrucciones, tablas de `load_indexed` (hasta el
   siguiente byte cubierto), tablas de instancias de `spawn` y lo que apunta cada argumento de dirección según su tipo.
   Una dirección apilada para otra cosa usa su `declared_width`. Si el argumento que da el tamaño o el índice de una
   lectura no es un número apilado, la lectura no tiene fin conocido; con una excepción: un índice que el mismo
   bloque deja como `load_immediate n` y `apply_operator variable modulo` toma los valores 0 a n − 1 (el dividendo en
   el disco es el contador de fotogramas de la instancia), así que la fila de CLUT cubre las n en que puede empezar.
4. Rellena los huecos: si son todo `0xCD` son relleno; si no, son datos sin uso (`unused`), salvo que antes empiece
   una lectura sin fin conocido, que podría llegar a ellos: entonces cuentan como desconocidos.

### Datos sin uso

El intérprete solo forma direcciones del área de datos de tres maneras, y el análisis las sigue todas:

- un operando de la instrucción sumado a la base (`EFE_SCRIPT_HEAD + ip[n]` en los manejadores de
  `battle_effect.c`), o a la instancia que corre, para las variables con flag 0;
- `load_indexed` (`BTL_loadEFEIndexedVariable`), que suma índice × ancho al operando;
- un puntero que apila `push_address` (`BTL_pushEFEVariableAddress`) y del que un subopcode lee lo que dice el tipo de
  su argumento.

El código en C no lee el área de otra forma: `EFE_DATA_ITERATOR->data` solo sirve para poner `EFE_SCRIPT_HEAD`. Los
scripts tampoco guardan punteros para hacer cuentas con ellos: los dos `pop_to_variable` del US sacan valores que pasa
quien llama. Las variables de instancia tienen offsets positivos y ninguna tabla de instancias va antes de un hueco.
Una tabla de `load_indexed` se extiende hasta el siguiente byte cubierto, así que un hueco nunca la sigue: para
llegar a él el índice tendría que saltarse datos que el script usa como otra cosa. En las tablas con índice acotado se
ve que la herramienta las midió justas: la ranura 77 lee la tabla de 6 `s16` en `0x12c` con el contador de la
instancia, y su script de fotograma (`l_1938`) la libera en cuanto pasa de 4. Un hueco antes de una tabla pediría un
índice negativo, y los índices son contadores.

Con el disco US quedan 40 rangos sin uso, 590 bytes en 33 ranuras. Se escriben enteros en `script.yaml` y el informe
los anota ranura a ranura. Son de estas clases:

| Qué hay | Ranuras |
|---|---|
| Vectores o colores de tres `s32` a los que no apunta ningún `push_address`, junto a otros que sí se usan | 10, 26, 29, 32, 34, 56, 77, 81, 86, 91 |
| El bloque de seis `s32` `76, 69, 3, 13, 6, 0`, igual en todas | 1, 33 y 61 al principio; 77 en `0x198` |
| Seis `s16` en rampa (130 a 592, o 130 a 905) al principio | 112 a 120 |
| Otras tablas de `s16` que ningún `load_indexed` lee | 3, 29, 54, 76, 104 |
| Un fotograma clave más de los que dice la cuenta: `BTL_tickEFEUVAnimation` vuelve al primero al llegar a `numKeyframes` | 34 (`0x40`, `0x54`) |
| Listas de huesos que nadie apila (una entrada y su fin, o solo el fin) | 62, 98 |
| El color 16 tras una ventana de 15: `BTL_loadClutColors` copia `count` colores y los scripts siempre apilan 15 | 31 |
| Lo que sigue a la fila de CLUT de la ranura 14: el último color de la paleta doble y 28 bytes más | 14 |
| Dos o cuatro bytes a cero tras una variable o un vector | 86, 94, 102, 109 |

La fila de CLUT de la ranura 14 es lo único que sí se lee: `queue_clut_load` (`BTL_addClutLoadPrim`) copia 32 bytes
desde los colores más dos por el índice, y el índice es el contador de la instancia módulo 16. La paleta está dos veces
seguida para que la ventana rote, y la fila cubre 62 bytes de un hueco que medía 92.

## Lo que escribe `dwx`

Por cada ranura usada, en `effect/<técnica>/` (el id de la técnica de `data/moves`, o la ranura con 3 cifras sin
tablas):

| Fichero | Contenido |
|---|---|
| `effect.yaml` | ranura, técnica, cabecera, número de animaciones de UV, si el original la carga (`loads`) y los ficheros de la carpeta que escribió: `texture` (`texture.png`) y `model` (`model.glb`, solo si tiene TMD) |
| `script.yaml` | `start_script` e `init_script` instrucción a instrucción, `padding` y `data` |
| `texture.png` + `texture_meta.yaml` | el TIM, como en [TIM y packs](tim.md) |
| `model.glb` | el TMD reescrito para la celda 0 y texturizado con el TIM de la ranura en esa celda |

En `script.yaml` cada instrucción lleva `at` (offset desde la base), `op` y sus operandos ya decodificados; los
destinos son etiquetas (`start`, `init`, `l_<offset>`) y cada `sub` lista sus argumentos por nombre con lo que se
apiló: un número, una variable (`base+0x0040`, `instance+0x0008`), una dirección (`&base+0x0040`) o una etiqueta. Un
argumento sin unir es `null`. El área de datos va elemento a elemento con su tipo y sus valores; un valor que es
relleno (una variable sin valor inicial) es `null`. Los datos sin uso van como `kind: unused` con sus bytes y el
motivo (`reason`); una fila de CLUT con índice calculado lleva `row_starts` y todos los colores que puede leer.

En tiempo de juego lo lee `assets::readEffectScript` (`engine/assets/include/dwe/assets/effect_script.hpp`), sin
pasar por `formats`: junto con `effect.yaml` da un `EffectScript` con la técnica (`move`), el número de sub-efectos,
las instrucciones de los dos scripts y la imagen del área de datos, de la base al script de inicio, con los valores
de variables, tablas, instancias y argumentos numéricos; lo que solo lee el dibujo (huesos, sprites, UV, CLUT, mapas
de tiles, cintas) queda como relleno. De cada `sub` guarda el nombre y cuántos valores saca de la pila. Rechaza un
`op`, operador o comparación desconocidos y datos fuera del área.

Un ejemplo inventado:

```yaml
start_script:
- label: "start"
  at: "0x0010"
  op: push_immediate
  value: 0
- at: "0x0016"
  op: push_address
  address: "base+0x0000"
  declared_width: 4
- at: "0x001c"
  op: sub
  sub: draw_tmd
  args:
    object_index: 0
    scale: "&base+0x0000"
data:
- at: "0x0000"
  kind: vector
  size: 12
  values:
  - 4096
  - 4096
  - 4096
- at: "0x000c"
  kind: fill
  size: 4
```

## Estado con el disco US

- 121 ranuras usadas con 52530 instrucciones; 89 tienen TMD. Salen 121 `effect.yaml`, 121 `script.yaml`, 121 PNG con
  su YAML y 89 GLB (los 89 pasan `cgltf_validate` y tienen textura). 29 PNG y 17 GLB repiten otros: varias técnicas
  comparten textura y modelo.
- Todas las instrucciones decodifican, todos los argumentos se unen con lo que los apila y todos los punteros a código
  caen en instrucciones.
- El fichero queda **extraído**: cada byte del área de datos está tipado, es relleno o es un dato sin uso de los de
  [Datos sin uso](#datos-sin-uso) (40 rangos, 590 bytes en 33 ranuras), que el informe anota.

Se comprueba con `DWE_DISC` en `apps/dwx/tests/effect_real_disc_tests.cpp`.
