# Mapas

Cómo lee `formats/map` los mapas de campo: el `.MAP` (cámara, luces, imágenes, sprites, entidades y colisión), el
`.TFS` (el fondo prerenderizado) y las tablas del ejecutable que los describen. Los nombres de campo son los del
decomp; un campo que el decomp no nombra se llama `unknownXX`, con XX su offset en el registro.

## Tablas del ejecutable

Se leen con `formats/exe` (ver [Tablas](tablas.md)) y van al documento `maps`:

| Tabla | Entradas (US) | Contenido |
|---|---|---|
| `MAP_ENTRIES` | 255 × 16 | `MapEntry`: nombre de fichero, número de TIM de 8 y 4 bits, flags, puertas, baño y nombre de carga |
| `MAP_NAME_PTR` | 70 `char*` | nombres que se ven al cargar un mapa |
| `DOOR_MAPDATA` | 24 | `DoorStruct`: seis puertas con modelo (`DOOR\DOORnn.TMD`, −1 = ninguna), posición y giro |
| `TOILET_DATA` | 10 | rectángulo del baño (`posX1`, `posY1`, `posX2`, `posY2`) |
| `MAP_LIGHT_UPDATE_DATA` | 1 | objetos del mapa que se ven solo de día o de noche, y su disparador |

Bits de `MapEntry.flags` (`map.c: getMapSoundId`, `overworld.c: loadMapDigimon`, `map.c: updateTimeOfDay`):

| Bits | Significado |
|---|---|
| `0x1F` | sonido del mapa |
| `0x20` | ningún código conocido lo lee |
| `0x40` | hora fija: el fondo usa siempre la primera paleta |
| `0x80` | la sección de entidades tiene registros de NPC |

Las entradas con nombre vacío no tienen fichero. La ruta del fichero es `\MAP\MAP<id / 15 + 1>\<nombre>.MAP` (o
`.TFS`) (`map.c: buildMapPath`, `mapFilePath`). Dos entradas del US comparten nombre de fichero; en el YAML la segunda
lleva el sufijo `_<índice>` en su id.

## `.MAP`

Empieza con una tabla de offsets `u32`, uno por sección, en este orden (`map.c: loadMap`):

1. configuración;
2. los TIM de 8 bits (`num8bppImages`);
3. los TIM de 4 bits (`num4bppImages`);
4. los sprites, solo si el mapa tiene alguna imagen;
5. las entidades;
6. la colisión.

El número de secciones depende de la entrada de `MAP_ENTRIES`, así que `readMapFile` recibe `MapReadOptions` con los
dos números de imágenes y el bit de NPC. Cada sección va de su offset al siguiente (la última, al final del fichero).
El lector comprueba que la tabla cabe, que los offsets crecen y que ninguno sale del fichero.

### Configuración

Todo `s32` (`map.c: loadMapSetup`):

- punto de vista y de referencia de la cámara (`GsRVIEW2`; el original dobla cada valor al aplicarlo);
- tres luces (`GsF_LIGHT`: dirección y color);
- el ambiente, que el original lee y no usa (siempre pone `0x800`);
- la distancia de proyección (`GsSetProjection`);
- dos regiones, que se comparan con `RAISE_DATA[compañero].favoredRegion`;
- ancho y alto del fondo en tiles y la rejilla de tiles, fila a fila, con −1 en las celdas vacías.

El original reserva 35 tiles; una rejilla mayor es un error.

### Imágenes

Los TIM son TIM estándar y se dejan como rango de bytes (`MapImageSection`: bits por píxel, offset y tamaño) para el
lector de imágenes. Los de 8 bits se cargan con `overworld.c: loadMapImage1` y los de 4 bits con `loadMapImage2`.

### Sprites

`s16` con el número de sprites y los sprites, de 9 `s16` cada uno en este orden: `texX`, `texY`, `width`, `height`,
`someX`, `someY`, `someZ`, `clut`, `transparency` (`overworld.c: loadMapObjects`, `buildMapOverlayPrim`). `clut` −1 es
la paleta de 8 bits del mapa, 0–15 una CLUT de 4 bits y 16 o más una fila de 8 bits (`clut + 468`).

Después, un `s16` con el número de instancias (como mucho 188) y las instancias (`LocalMapObjectInstance`): 8 sprites
por fotograma (−1 termina la lista, −2 es un fotograma vacío), 8 duraciones, posición `x`, `y` en píxeles del fondo y
`flag` (1 = oculto, 0x80 = en coordenadas de pantalla).

El original guarda los sprites en `LOCAL_MAP_OBJECTS[180]` sin comprobar el número. Un mapa del US tiene más de 180 y
escribe más allá del array, así que el lector no pone límite a los sprites.

### Entidades

- **Warps** (`tamer.c: MapWarps`): seis listas de 10 `s16`: `spawnX`, `spawnY`, `spawnZ`, `rotation` (4096 = una
  vuelta), `targetMap` y `targetExit`. La salida `i` dice dónde aparece el jugador al entrar por ella y adónde lleva.
  Las salidas que no se usan tienen valores sin sentido, incluido `targetMap` fuera de `MAP_ENTRIES`.
- **Número de Digimon** (`s16`), que se guarda tal cual aunque el mapa no tenga NPC.
- **NPC** (`overworld.c: loadMapDigimon`), solo si la entrada tiene el bit `0x80`; como mucho 8. Cada registro son
  `s16`: 34 fijos (tipo, modo de seguimiento, posición, giro, alcance, `unknown12`, script, PV y PM, atributos, cerebro,
  `bits`, modo de carga, `unknown2A`, 4 técnicas y sus prioridades, punto de huida y número de puntos de ruta), 8
  secciones de IA y 3 por punto de ruta (como mucho 8). `unknown12` y `unknown2A` son `NPCEntity.unk2` y `unk1`.

### Colisión

100 × 100 bytes, fila a fila, una celda por cada 100 unidades del mundo (`map_collision.c`). 0 es libre, el bit `0x80`
es bloqueado y el resto de valores son disparadores. La sección tiene que medir exactamente 10 000 bytes.

Las arenas del torneo guardan la misma rejilla en `STDDAT\*.ATR`, 10 000 bytes sin cabecera que el original lee tal
cual en `MAP_COLLISION_DATA` (`std_setup.c: STD_loadArenaAssets`, `vs_scene.c: VS_loadArenaAssets`).
`collisionToYaml` escribe las dos igual.

## `.TFS`

El fondo prerenderizado (`map.c: readMapTFS`, `setupMap`, `updateTimeOfDay`):

1. `u32` que el original salta; en los ficheros US es `alto << 16 | ancho` del fondo en píxeles;
2. `u32` con el número de paletas (como mucho 31);
3. las paletas, de 256 colores `u16` en el formato de 15 bits de la consola;
4. los tiles, uno por celda no vacía de la rejilla y en su orden: un `u32` que el original salta (en el US,
   `fila * 128 << 16 | columna * 64`, la posición en VRAM) y 128 × 128 índices de 8 bits.

El número de tiles sale del tamaño del fichero, que tiene que ser un número entero de tiles.

La paleta 0 es el día, la 1 el atardecer y la 2 la noche; los mapas de hora fija usan solo la 0
(`map.c: updateTimeOfDay`). El original sube la paleta `i` a la fila `481 + i` de VRAM (`map.c: setupMap`) y copia a
la fila 480 la que toca. Las paletas después de la tercera solo se verían a través de un sprite del mapa cuyo `clut`
apunte a su fila (`clut + 468`); en los 3 mapas del US que las tienen ningún sprite lo hace, así que nunca se ven.
`setupMap` guarda además un puntero a cada paleta en `MAP_CLUTS`, que tiene 3 entradas: las que sobran pisan la
variable siguiente.

`composeBackgrounds` coloca los tiles en la rejilla del `.MAP` y da una `IndexedImage` (`ancho * 128` por `alto * 128`
índices y su paleta) por cada paleta; las celdas vacías quedan con índice 0, como el negro con que las limpia el
original. Falla si la rejilla tiene más celdas que tiles el fichero. `backgroundVariantName` da el nombre de cada
variante: `day`, `dusk`, `night` y después `palette3`, `palette4`...

## YAML

`mapToYaml` escribe un documento por mapa con el esquema `content/schemas/map.schema.json`:

| Clave | Contenido |
|---|---|
| `camera` | `viewpoint`, `reference` y `projection` |
| `lights`, `ambient` | dirección y color de las tres luces; el ambiente |
| `regions` | `a` y `b` |
| `tiles` | `width`, `height` y `rows`: una cadena por fila con los números separados por espacios |
| `images` | bits por píxel, offset y tamaño de cada TIM |
| `objects` | `null` si no hay sección; si no, `sprites` e `instances` (los fotogramas, hasta el −1) |
| `warps` | las 10 salidas con `spawn`, `rotation`, `target_map`, `target_map_id` y `target_exit` |
| `digimon_count`, `npcs` | número guardado y registros de NPC, con `type_id` y `digimon` |
| `collision` | `width`, `height`, `encoding`, `legend` y `rows` |

Los números van tal como están en el fichero. Cuando un número apunta a otra tabla se escribe además su id de texto
(`target_map_id`, `digimon`), o `null` si cae fuera de la tabla: así no se pierde un valor sin sentido del original.

La colisión se escribe como 100 cadenas de 100 caracteres con una leyenda: `.` libre, `#` bloqueado y una letra o cifra
por cada valor de disparador, en orden de valor (`A`–`Z`, `a`–`z`, `0`–`9`). Si un mapa tuviera más de 62 valores de
disparador, `encoding` pasa de `legend` a `hex` y cada celda son dos cifras hexadecimales.

## Comprobación con el disco

Con `DWE_EXE_SAMPLE` y `DWE_MAP_DIR` (la carpeta `MAP` del disco US), `formats/tests/real_data_tests.cpp` lee todas las
entradas con fichero, compone sus fondos y comprueba el YAML contra el esquema. En el US: 244 entradas tienen nombre de
fichero, 3 de ellas no están en el disco y las otras 241 se leen sin errores (216 con NPC); todas las colisiones caben
en la leyenda, con como mucho 26 valores distintos. Salen 512 fondos: 108 mapas con 1 paleta, 130 con 3, 1 con 4 y 2
con 5. Ninguna entrada usa el bit `0x20`.

## Lo que no se sabe

- El bit `0x20` de los flags.
- Un mapa del US de una sola paleta no tiene el bit de hora fija.
- `unknown12`, `unknown2A`, `followMode` y `bits` de los NPC.
