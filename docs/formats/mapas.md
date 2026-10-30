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
| `0x40` | hora fija: el fondo usa siempre la primera paleta y no hay fundido al cambiar la hora (`map.c: renderMap`) |
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

Un `.MAP` que ninguna entrada nombra dice parte de eso él mismo (`readMapFileShape`): el primer offset es el tamaño de
la tabla, y con él el número de secciones; cada sección de imagen empieza con un TIM cuyo modo dice si es de 8 o de 4
bits. `inferMapReadOptions` saca de ahí las opciones, pero solo si la sección de entidades acaba justo después de los
warps y el número de NPC: algunos mapas sin el bit de NPC guardan bytes detrás que el juego no lee, así que una
sección más larga no se puede decidir. En el US, el número de imágenes cuadra siempre con la entrada; el reparto por
modo cuadra en todos los mapas menos uno, que el juego carga con un TIM de 4 bits por el cargador de 8.

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

`loadMap` sube primero los de 8 bits y luego los de 4, en orden, cada uno con un solo `LoadImage` a su rectángulo de
VRAM, sin más comprobación: un TIM puede ocupar varias páginas (tunn05), media página (gias02, 128 × 256 texels de 8
bits) o unas pocas filas (gomi02 y betl04, 256 × 32 texels de 4 bits en (704, 216)), y uno posterior tapa lo que
cubre de otro anterior aunque sea de otra profundidad (en gomi02 el de 4 bits cae dentro de la página de 8 bits de la
x 640). En el US, 20 de los 241 mapas tienen alguno así. El motor hace lo mismo (`loadMapImages`): sube las entradas a
una VRAM del mapa y lee de ella las páginas.

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

## En el motor

El motor no lee el `.MAP` ni el `.TFS`: lee lo que deja `dwx` por el VFS. `assets::MapDocument` es `map/<id>/data`
y `assets::MapTable` es `data/maps` (ver el [página de `assets`](../modulos/engine/assets.md#mapas)). Guardan los
números como están; las conversiones al [espacio del mundo](../architecture/world-space.md) se hacen en
`map_document.hpp` y en ningún otro sitio:

| Dato | Original | Mundo |
|---|---|---|
| Posición (`spawn`, NPC, puertas) | `(x, y, z)`, Y hacia abajo | `(x, -y, -z)` (`toWorldPosition`) |
| Celda de colisión | columna `x / 100 + 50`, fila `50 - z / 100` (`map_collision.c: getTileTrigger`, `getModelTile`) | columna `floor(x / 100) + 50`, fila `floor(z / 100) + 50` (`collisionCellAt`) |
| Giro en Y | 4096 por vuelta; 0 mira a −Z, 1024 a −X, 2048 a +Z | `yaw = -giro * 2π / 4096`; un personaje con yaw 0 mira a +Z y `forwardOfYaw` da `(sin yaw, 0, cos yaw)` |

La celda: el original divide truncando y luego corrige los negativos, así que en las rectas exactas `x = -100 k` y
`z = 100 k` (en sus ejes) toma la celda vecina. El motor usa `floor` en todos los casos; la diferencia es de un punto
sin grosor.

El sentido del giro sale de `tamer.c: tamerTickWalkingState`: arriba en el mando da `getMapRotation() + 0x800`, y
`getMapRotation` es `_atan` del vector de la referencia al punto de vista de la cámara; en un mapa con la cámara
mirando a +Z (en el original), arriba da 2048 y el jugador se aleja de la cámara. Lo confirman los warps: en `twna01`,
la salida 1 (`rotation` 3072) aparece junto a sus celdas de warp, al oeste, y con 3072 mira al este, hacia dentro del
mapa.

La colisión: 0 es libre, cualquier valor con el bit `0x80` bloquea (`checkMapCollisionX/Z`) y el resto son
disparadores (`tamer.c: checkMapInteraction`): 51–79 llaman a un script al pisarlos, 80–109 al pulsar confirmar,
110–119 son los warps (el 110 + i lleva por la salida i de este mapa a `target_map` / `target_exit`) y 120 llama a un
script si el compañero tiene el bit 8 de `condition` (probablemente, que necesita el baño).
En el US solo aparecen 0, 51–86, 110–116, 120 y 128.

### Destino de una salida

Una salida nombra su destino dos veces: `target_map`, el índice crudo del original, y `target_map_id`, el id de
`data/maps`. `assets::resolveWarpTargets` (`map_warps.hpp`) decide adónde lleva al entrar en el mapa:

1. Un `target_map_id` que nombra un mapa de la tabla compuesta manda.
2. Si no, un `target_map` de 0 o más lleva al mapa **del original** con ese índice; nunca a uno que añade un paquete,
   cuyo índice lo pone el motor y depende de los paquetes montados.
3. Si no, la salida no lleva a ningún sitio y el campo la ignora.

El paquete de la base es el paquete propio del motor con la capa `base`: lo decide su origen y su capa, como los
demás derechos de la base ([tablas por entradas](../architecture/vfs.md#tablas-por-entradas)). Un mod que declara
`layer: base` cuenta como otro paquete.

| Caso | Paquete de la base | Otro paquete |
|---|---|---|
| Los dos y coinciden | válido (lo que escribe `dwx`) | válido |
| Los dos y nombran mapas distintos | error; el warp se desactiva | error; el warp se desactiva |
| Un id de un mapa y un `target_map` fuera de la tabla | error; el warp se desactiva | válido: manda el id |
| Solo `target_map_id` (`target_map: -1`) | error: la base escribe los dos | válido; así se llega a un mapa de un paquete |
| Solo `target_map` de un mapa (`target_map_id` `null` o ausente) | error | válido |
| `target_map_id` que no es ningún mapa y un `target_map` del original | error; el warp se desactiva | lleva al mapa del original con ese índice, pero `dwx validate` da el id como error y el juego avisa en el registro |
| `target_map_id` que no es ningún mapa y sin índice que lo sea | error | error |
| Sin id y un `target_map` fuera de la tabla, sin su disparador en la colisión | sin destino, como en el original | salida sin usar |
| Sin id y un `target_map` fuera de la tabla, con su disparador | sin destino, como en el original | **warp muerto**: error |
| `target_map: -1` sin id, sin su disparador (110 + i) en la colisión | salida sin usar | salida sin usar |
| `target_map: -1` sin id, con su disparador en la colisión | salida sin usar | **warp muerto**: error |

Un `target_map` fuera de la tabla de otro paquete incluye el número que el motor dio a un mapa de un paquete: ese
número depende de los paquetes montados y un paquete nunca lo escribe.

El warp muerto es lo que deja un editor que solo conoce los índices del original al guardar una salida hacia un mapa de
un paquete: escribe `-1` (o el número que el motor dio al mapa) y pierde el id. `dwx validate` lo da como error con el
mapa, la salida y su línea, y avisa de que el editor pudo perder `target_map_id`. En el juego, cada salida con un
problema se desactiva con un aviso en el registro que la nombra, salvo la de un id desconocido junto a un índice del
original, que sigue llevando a ese mapa; nunca se cae. Dos destinos que no coinciden desactivan el warp en cualquier
paquete, y `dwx validate` lo da como error.

## Cómo se dibuja

Lo que hace el original para dibujar un mapa de campo, que es la especificación del render de mapas del motor
([ADR 0020](../adr/0020-camara-de-mapa-y-capas-de-profundidad.md), [`engine/render`](../modulos/engine/render.md)).
`W` y `H` son el ancho y el alto del fondo en tiles de 128 píxeles.

### Cámara

- La cámara de un mapa **no se mueve**. `map.c: loadMapSetup` llena `GS_VIEWPOINT` con el punto de vista y el de
  referencia del fichero **multiplicados por 2** (`<< 1`), y llama a `GsSetProjection` con la distancia de proyección
  tal cual. `rz` y `super` valen 0 y el plano cercano es 100 (`main.c: view_init`). El bucle del juego vuelve a aplicar
  la misma vista cada fotograma; solo el modo versus mueve la cámara.
- La vista de `GsSetRefView2` mira de `vp` a `vr` sin giro sobre su eje: es un *look-at* con la Y del original
  (hacia abajo) como "abajo" de la pantalla. En el mundo del motor es el *look-at* de siempre con +Y arriba.
- La pantalla es de 320 × 240 sin entrelazar (`GsInitGraph(320, 240, ...)`), con píxeles cuadrados para el cálculo:
  la misma `H` vale en los dos ejes. La GTE proyecta con desplazamiento 0 (`sx = H · Xv / Zv`, `sy = H · Yv / Zv`)
  y el centro de la pantalla lo pone el desplazamiento de dibujo de la GPU (`GsSetOrign`).

### Fondo y desplazamiento

- Los tiles del `.TFS` se dibujan como `POLY_FT4` de 128 × 128 en la última posición de la tabla de orden (`0xFFF`):
  detrás de todo (`map.c: renderMap`, `setPosDataMapTile`). Una celda vacía (−1) sale negra.
- `map.c: setupMap`, `initializeDrawingOffsets`, `tickCameraFollowPlayer` y `updateDrawingOffsets` mueven a la vez
  la esquina visible del fondo (`cameraX`, `cameraY`) y el desplazamiento de dibujo, y siempre mantienen
  `cameraX + DRAW_X = W · 64` y `cameraY + DRAW_Y = H · 64`. De ahí sale la relación exacta entre el mundo y el fondo:

  ```
  píxel del fondo = (sx + W · 64, sy + H · 64)
  ```

  Es decir: el punto de referencia `vr` cae en el centro del fondo, y el fondo entero es la imagen de **una sola
  cámara fija** con su centro óptico en el centro del fondo. La pantalla es una ventana de 320 × 240 sobre esa imagen;
  moverla desplaza igual el fondo, los sprites y los modelos (para los modelos es un desplazamiento del centro óptico,
  no un giro de la cámara).
- La ventana sigue al jugador sin zona muerta ni suavizado:
  `cameraX = clamp(W · 64 + sx(jugador) − 160, 0, W · 128 − 320)`, y lo mismo en Y con 120 y 240. Solo se mueve
  mientras el jugador anda con la cruceta; los scripts la mueven con `moveCameraByDiff` y `createCameraMovement`.
  La vuelta al centro tras un borde (`updateDrawingOffsets`) solo mira la dirección pulsada de la cruceta, así que en
  un borde la ventana puede quedar algo descentrada; el motor no lo copia.
  El original aplica el desplazamiento un fotograma tarde, sin que se desalineen fondo y modelos.

### Capas de profundidad

- Los sprites del mapa (`LOCAL_MAP_OBJECTS`) son recortes del fondo que se vuelven a dibujar **por delante** de los
  modelos que pasan por detrás. `someX`, `someY` y `someZ` son un punto del mundo (en las unidades del fichero, sin
  doblar) que solo sirve para su profundidad.
- `overworld.c: calcMapObjectOrder` calcula una vez, al cargar el mapa, la posición de cada instancia en la tabla de
  orden: proyecta el punto del primer sprite válido de sus fotogramas y toma `orderValue = SZ >> 4`, entre `0x23` y
  `0xFF6` (`0x1000` o más da `0xFF6`); si `someY` es 10 000 da `0xFFE` (justo delante del fondo), y las instancias con
  el bit `0x80` dan `0x1E`.
- Los modelos se ordenan por polígono (`GsSortObject4J` con desplazamiento 2 y `ZSF4 = 0x100`): su posición es
  `SZ medio >> 4`. Un índice mayor se dibuja antes. Un sprite tapa un polígono cuando
  `SZ medio >> 4 > orderValue`: el sprite equivale a un plano a la distancia `orderValue · 16` de la cámara.
- `overworld.c: renderMapOverlays` dibuja cada instancia visible (`flag` distinto de 1) en `inst.x − cameraX`,
  `inst.y − cameraY`, con el tamaño del sprite, y avanza su animación una vez por fotograma del juego (el campo va a
  20 fotogramas por segundo: `VSync(3)` en `main.c: gameLoop`). Un fotograma −2 no dibuja nada pero cuenta su tiempo.
  Las instancias ocultas también avanzan. El bit `0x80` (nieve, en unos pocos mapas) dibuja en coordenadas de
  pantalla con posiciones aleatorias.
- Textura (`overworld.c: buildMapOverlayPrim`): la página es `384 + (texX / 256) · 128` en VRAM para 8 bits y
  `384 + (texX / 256) · 64` para 4 bits, con `u = texX % 256`, `v = texY`. La paleta:
  - `clut` −1: la fila 480, es decir, la paleta del fondo que toca por la hora;
  - `clut` 0–15: la fila `clut` de la CLUT del **primer** TIM de 4 bits, que `loadMapImage2` sube por filas a
    `(fila · 16, 486)`; las CLUT de los demás TIM de 4 bits no se suben;
  - `clut` 16 o más: la fila `clut + 468` de VRAM. Ahí está la CLUT de un TIM de 8 bits que vaya a esa fila
    (`loadMapImage1` sube todas menos las de la fila 480, que manda el `.TFS`) o, si no, la paleta `clut − 13` del
    `.TFS` (la fila `481 + i`).
- `transparency` 4 es opaco; 0–3 son los modos semitransparentes de la consola (media, suma, resta, suma de un
  cuarto), que solo afectan a los colores con el bit STP (`renderMapOverlays` marca la primitiva semitransparente
  cuando no es 4). El color `0x0000` no se dibuja nunca. En los `.TFS` del US 71 de 242 tienen colores con STP (en
  `twna01`, 506 de los 768 de sus tres paletas; en `room08`, ninguno). El truco habitual es el negro `0x8000` (negro
  con STP) alrededor del recorte en modo suma: sumar negro no cambia nada, así que el recorte no tiene borde.
- El motor guarda el STP en el alfa (`colorFromOriginal`: 128 con STP, 255 sin él, 0 para `0x0000`) y dibuja un
  sprite semitransparente en dos pasadas, sus texels sin STP opacos y los que lo tienen con su mezcla. Las paletas
  del `.TFS` llegan como PNG, que no guarda el STP; el único color que se puede recuperar es el negro opaco, que solo
  puede ser `0x8000` (`paletteFromPng`). Con eso los sprites con `clut` −1 salen exactos en 97 de los 100 mapas del
  US que los tienen semitransparentes; en `fact11a`, `gias06b` y `miha01` las paletas tienen además otros colores con
  STP que hoy se dibujan opacos, hasta que `dwx` guarde las paletas del `.TFS` con su bit STP.

### Hora del día

`map.c: updateTimeOfDay`: de 6:00 a 15:59 la paleta 0 (día), de 16:00 a 19:59 la 1 (atardecer) y el resto la 2
(noche); al atardecer las luces pierden un 30 % de verde y azul y de noche la mitad de todo. Los mapas de hora fija
(`0x40`) usan siempre la 0 y no tocan las luces. El cambio de paleta empieza justo a las 6:00, 16:00 y 20:00
(`map.c: renderMap`, salvo en los mapas de hora fija) y es un fundido de 25 fotogramas
(`map.c: tickDaytimeTransition`) a escalones: cada color avanza 1/6, 1/5, 1/4, 1/3 y 1/2 del camino desde la paleta
de partida, cinco fotogramas cada uno, y al final salta a la nueva; las luces cambian de golpe al final. El fundido
se para mientras el juego no está en el campo (`GAME_STATE` distinto de 0). El reloj y su ritmo están en la
[ficha del campo](../reference/campo.md#hora-del-día). `light_updates` en `data/maps` dice qué
instancias se ven solo de día o solo de noche (`utils2.c: updateMapLightState`).

## Comprobación con el disco

Con `DWE_EXE_SAMPLE` y `DWE_MAP_DIR` (la carpeta `MAP` del disco US), `formats/tests/real_data_tests.cpp` lee todas las
entradas con fichero, compone sus fondos y comprueba el YAML contra el esquema. En el US: 244 entradas tienen nombre de
fichero, 3 de ellas no están en el disco y las otras 241 se leen sin errores (216 con NPC); todas las colisiones caben
en la leyenda, con como mucho 26 valores distintos. En 24 mapas las opciones sacadas del fichero leen lo mismo que
las de su entrada; 1 mapa sin el bit de NPC tiene bytes detrás de los warps. Salen 512 fondos: 108 mapas con 1 paleta, 130 con 3, 1 con 4 y 2
con 5. Ninguna entrada usa el bit `0x20`.

## Lo que no se sabe

- El bit `0x20` de los flags.
- Un mapa del US de una sola paleta no tiene el bit de hora fija.
- `unknown12`, `unknown2A`, `followMode` y `bits` de los NPC.
