# Tablas del ejecutable

Cómo lee `formats/exe` las tablas de datos del ejecutable principal y cómo las escribe en YAML. Las estructuras y los
nombres de campo son los del decomp (`include/dw/*.h`); un campo que el decomp no nombra se llama `unknownXX`, con XX
su offset dentro del registro.

## Direcciones

`MemoryImage` presenta un ejecutable o un overlay como lo ve la memoria de la consola, para leer en las direcciones que
da el decomp:

- **Ejecutable principal:** la cabecera PS-X EXE (`readPsxExeHeader`) da la dirección y el tamaño del texto, que
  empieza en el byte 0x800 del fichero. En el US el texto se carga en `0x80090800`, así que
  `offset = dirección − 0x80090000`.
- **Overlays** (`*_REL.BIN`): se cargan enteros en una dirección fija (`utils2.c: loadDynamicLibrary`, que no mira
  su cabecera), que se pasa a `MemoryImage::fromOverlay`; su estructura está en [Overlays](overlays.md). Las del US
  salen del primer `vram` de `config/us/<overlay>.yaml` (y de `shop_START` en `utils2.c` para SHOP):

  | Overlays | Dirección de carga |
  |---|---|
  | BTL, STD, VS | `0x80052AE0` |
  | KAR | `0x80053800` |
  | EVL, ENDI, EAB | `0x80060000` |
  | FISH, DOO2 | `0x80070000` |
  | MURD | `0x8007C000` |
  | DOOA | `0x80080000` |
  | DGET, SHOP | `0x80080800` |
  | TRN, TRN2 | `0x80088800` |
  | MOV | `0x80010000` |

Toda lectura comprueba que los bytes caen dentro de la imagen; una tabla o un puntero fuera de ella da un error que
nombra la tabla y el índice.

## Versiones

`ExecutableLayout` guarda, por versión, la dirección y el número de entradas de cada tabla (`executableLayouts`,
`findExecutableLayout`). Solo el US está relleno, desde `config/us/symbols.txt` y las declaraciones de los arrays en
`src/main`. EU, JP y JP rev. 1 existen con sus tablas sin localizar (dirección 0): leerlas da un error hasta que
alguien las rellene desde `config/<versión>/symbols.txt`. La única diferencia de estructura conocida es que el EU no
tiene el nombre `char name[20]` al principio de `DigimonPara` ni de `Item` (`inlineNames`).

## Tablas

| Tabla | Entradas (US) | Registro | Índice |
|---|---|---|---|
| `DIGIMON_DATA` | 180 × 0x34 | `DigimonPara` (`params.h`) | tipo de Digimon |
| `PTR_DIGIMON_FILE_NAMES` | 180 `char*` | nombre de 4 letras del MMD | tipo |
| `DIGIMON_SKELETONS` | 180 punteros | `SkeletonBone {s8 objIndex, s8 parentIndex}` × `boneCount` | tipo |
| `MOVE_DATA` | 122 × 16 | `Move` (`move.h`) | técnica |
| `MOVE_NAMES` | 122 `char*` | | técnica |
| `MOVE_LEARN_CHANCES` | 58 × 3 | `u8` por ranura de especialidad del que aprende (`battle_ui.c`) | técnica aprendible |
| `ITEM_PARA` | 128 × 32 | `Item` (`item.h`) | objeto |
| `ITEM_DESCS` | 128 `char*` | | objeto |
| `ITEM_CLUT_DATA` | 128 `u8` | paleta del icono en `ETCDAT/ITEM.TIM`: fila de CLUT 0x1E8 + el valor (`utils2.c: setItemTexture`) | objeto |
| `ITEM_FUNCTIONS` | 128 punteros | función que aplica el objeto, o nulo (`item.c`) | objeto |
| `HEALING_VALUES` | 4 `s16` | HP o MP que cura (`handleHPHealingItem`, `handleMPHealingItem`) | objeto, u objeto − 4 para MP |
| `EVOLUTION_ITEM_TARGET` | 44 `u8` | tipo destino (`handleEvoItems`) | objeto − 0x47 |
| `EVOLUTION_REQS` | 63 × 28 | `EvoRequirements` (`evl.h`) | tipo destino |
| `EVOLUTION_STATS_GAINS` | 66 × 14 | `EvoStatsGains` | tipo destino |
| `EVOLUTION_PATHS` | 62 × 11 | `EvolutionPath {s8 from[5], s8 to[6]}` | tipo − 1 (`evolution.c`, `game_menu.c`) |
| `RAISE_DATA` | 66 × 28 | `RaiseData` (`partner.h`) | tipo |
| `CARD_DATA` | 66 × 4 | `CardData` (`script.h`) | carta |
| `CARD_PRICES` | 5 `s32` | | |
| `TOURNAMENT_NAMES`, `TOURNAMENT_GRADES` | 23 `char*` | | torneo |
| `TOURNAMENT_SCHEDULE` | 30 × 6 `u8` | torneos del día; `0xFF` termina (`dget.c`) | día % 30 |
| `TOURNAMENT_MEDAL_IDS` | 6 `u16` | | |
| `MEDAL_NAMES` | 15 `char*` | | medalla |
| `MEDAL_DESCRIPTIONS` | 45 `char*` | tres líneas por medalla | |
| `BGM_TRACK_NAMES` | 63 `char*` | menú de música | |
| `ENEMY_MUSIC_MAPPING` | 48 `u8` | | |
| `SHOP_MAPS` | 8 `s16` | mapas que cargan la tienda (`checkShopMap`) | |
| `SHOPKEEP_DIGIMON_TYPES` | 11 `u8` | `0xFF` termina | |
| `MAP_ENTRIES`, `MAP_NAME_PTR`, `DOOR_MAPDATA`, `TOILET_DATA`, `MAP_LIGHT_UPDATE_DATA` | | ver [Mapas](mapas.md) | |
| `DIGIMON_VLALL_SOUND_ID`, `DIGIMON_VBALL_SOUND_ID`, `MAP_SOUND_PARA`, `MAIN_D_801303B8` | | ver [Audio](audio.md) | |

Notas:

- La tienda no tiene tabla de existencias: el objeto `i` se vende mientras está activo el disparador `0x180 + i`
  (`script_common.c: shopFillBuyItemList`).
- Los participantes de cada torneo no están en el ejecutable: salen del script del mapa, sección 10
  (`tournament.c: getCupDataJumpTable`).
- `DIGIMON_SKELETONS` tiene punteros nulos en los tipos sin huesos (`boneCount` 0); se leen como esqueletos vacíos.
  Un puntero nulo con huesos es un error.
- En el US, el último puntero de `MOVE_NAMES` es nulo aunque `MOVE_DATA` tiene esa técnica. Un puntero nulo en una
  tabla de textos se lee como texto vacío.
- `RaiseData` tiene un byte de relleno en `0x0B` que no se guarda.
- Las tablas de juego de los overlays (pesca, torneo, versus, entrenamiento y batalla) están en [Overlays](overlays.md).

## Efectos de objetos

`readItemEffects` (`formats/exe/item_effects.hpp`) junta lo que el ejecutable dice de cada objeto. `ITEM_FUNCTIONS` da
la función de cada uno, que se reconoce por su dirección (`ItemCodeLayout::handlers`); `HEALING_VALUES` y
`EVOLUTION_ITEM_TARGET` dan los números de las curas y de la evolución. Los valores de la comida y de los chips no
están en datos: cada objeto es un caso de un `switch` de `handleFood` o `handleChips` que solo pone constantes en
variables locales y salta al final común.

`evaluateConstantSwitch` (`formats/exe/constant_switch.hpp`) los lee sin copiar código: ejecuta la función desde su
entrada con el número del objeto en `a0` hasta la dirección del final común (`join`) y devuelve los valores de las
variables que da la versión (`ValueLocation`: un registro o un hueco de la pila). Solo entiende las instrucciones de
un `switch` así (sumas, comparaciones, cargas y guardados, saltos con su *delay slot* y tablas de saltos); una
llamada, una instrucción que no conoce, un salto sobre un valor desconocido o una variable sin valor detienen la
lectura y el caso se anota en vez de adivinarse. En el US se leen los 35 casos de comida y 14 de los 16 chips: el
piloto automático (llama al guion) y el orinal (llama a `handlePortaPotty`) quedan sin valores y su lógica la da el
ruleset. `packages/formats/tests/item_effects_disc_tests.cpp` lo comprueba con el disco.

## Textos

Los textos se guardan tal como están en el ejecutable: ASCII en el US y Shift-JIS (cp932) en el JP. Los nombres de
`DigimonPara` e `Item` son `char[20]` dentro del registro; los demás son tablas de `char*`. Un texto acaba en el primer
NUL, que tiene que llegar en 256 bytes como mucho.

Al escribir YAML, `escapeGameText` deja el ASCII imprimible como está, escribe la barra invertida como `\\` y cualquier
otro byte como `\xNN`. Así no se pierde nada aunque el texto no sea ASCII, y se puede decodificar más tarde. Cada texto
escapado se anota en las notas de la conversión.

## YAML

`tablesToYaml` convierte las tablas en trece documentos (quince si la versión tiene localizadas las dos tablas del
[árbol de evolución](arbol-de-evolucion.md), hoy solo la US), cada uno con su esquema en
`content/schemas/<nombre>.schema.json` (JSON Schema 2020-12):

| Documento | Contenido |
|---|---|
| `digimon` | `DIGIMON_DATA` con el nombre de fichero del modelo |
| `skeletons` | los huesos de cada tipo |
| `moves` | `MOVE_DATA` con su nombre, y `learn_chances` (`MOVE_LEARN_CHANCES`) en las técnicas que se aprenden |
| `items` | `ITEM_PARA` con su descripción y `icon_palette` (`ITEM_CLUT_DATA`): la paleta de su icono en `texture/etcdat/item` |
| `item_effects` | qué hace cada objeto: su función, lo que cura, a qué evoluciona y los valores de su caso de comida o chip |
| `evolution` | requisitos, ganancias y el árbol de evolución |
| `raise` | `RAISE_DATA` |
| `evolution_chart`, `evolution_chart_detail` | posiciones e iconos del árbol y geometría de su detalle ([Árbol de evolución](arbol-de-evolucion.md)) |
| `cards` | precios y `CARD_DATA` |
| `tournaments` | nombres, grados, calendario e ids de medalla |
| `medals` | nombres y las tres líneas de cada descripción |
| `music` | nombres del menú de música y `ENEMY_MUSIC_MAPPING` |
| `shops` | mapas con tienda y tenderos |
| `maps` | `MAP_ENTRIES`, puertas, baños, nombres de mapa y `MAP_LIGHT_UPDATE_DATA` |

Reglas:

- Las claves están en `snake_case`; los textos van entre comillas dobles y los números sin comillas.
- Lo que otras tablas referencian tiene un **id de texto**: los Digimon, por el nombre de su modelo
  (`PTR_DIGIMON_FILE_NAMES`, que no se repite); técnicas, objetos, torneos, medallas y músicas, por su nombre; los mapas,
  por su nombre de fichero. El id es el nombre en minúsculas con cualquier otro carácter convertido en `_`
  (`makeIds`). Si queda vacío es `<tipo>_<índice>`, y si se repite se le añade `_<índice>`. El índice original se
  guarda siempre en `index`.
- Una referencia se escribe como el id de lo referenciado, `null` si es el valor de «ninguno» (`-1` o `0xFF`) o el
  número tal cual si cae fuera de su tabla; esto último se anota.
- Las listas con posiciones con significado se escriben completas, con `null` en los huecos (las 16 técnicas de
  `DigimonPara`, `from` y `to` del árbol, las 6 puertas de `DOOR_MAPDATA`). Las que acaban en `0xFF` se cortan ahí.
- `doors` y `toilet` de cada mapa pasan de índice desde 1 (0 = ninguno) a índice desde 0 o `null`.
- La misma entrada da siempre el mismo texto.

Los tests comprueban cada documento contra su esquema con un validador mínimo (tipos, claves permitidas y obligatorias,
listas, `enum`, `const` y `$ref` locales) escrito en los propios tests, sin dependencias nuevas.

## Comprobación con el disco

`packages/formats/tests/real_data_tests.cpp` se ejecuta solo si `DWE_EXE_SAMPLE` apunta al ejecutable US (y `DWE_MAP_DIR` a la
carpeta `MAP` para los mapas). Con el ejecutable US extraído del disco: se leen todas las tablas, los quince documentos
cumplen su esquema y no hay ningún texto con bytes escapados. Queda una referencia fuera de tabla: un `-2` en
`EVOLUTION_PATHS`, que no es ni un tipo ni el `-1` de «ninguno».

## Lo que no se sabe

- El significado de `Move.unknown0D`–`unknown0F`, de `Item.unknown1E` y de `RaiseData.unknown0E`.
- El `-2` de `EVOLUTION_PATHS` (¿un hueco especial del gráfico de evolución?).
