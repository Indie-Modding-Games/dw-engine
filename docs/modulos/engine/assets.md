# assets

Contenido del juego por paquetes ([tubería de assets](../../architecture/assets-pipeline.md)). `AssetId` es el nombre estable de un asset (`map/file_city_01/background`) y `PackManifest` lee el `pack.yaml` de cada paquete: id, versión, capa, prioridad y dependencias. La clave opcional `api` dice qué versión de la API de Lua piden los scripts del paquete; `lua_api.hpp` tiene la versión que da el motor (`SupportedLuaApi`, de la que `script::LuaApiVersion` es un alias) y la regla que la compara (`checkApiCompatibility`, `checkPackApi`). Vive aquí, y no en `packages/engine/script`, porque es un contrato de datos con el `pack.yaml`: el juego y `dwx validate` la comprueban igual sin depender de Lua. El formato del manifiesto también está en [`content/schemas/pack.schema.json`](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/pack.schema.json). `findPacks` encuentra los paquetes de una carpeta de contenido (carpetas y archivos `.pak`), `openPacks` abre los archivos y `VirtualFileSystem` los monta, los apila por capa y prioridad y dice qué fichero gana para cada ID. Lee los ficheros a través de `FileSystem`: `DiskFileSystem` para el disco, `PakFileSystem` para un [archivo `.pak`](../../formats/pak.md) (que `writePak` escribe) y `MemoryFileSystem` para los tests; cada paquete se monta con el suyo (`PackLocation`). Solo depende de `core`. `decodePng` decodifica un PNG a RGBA de 8 bits con lodepng, para las imágenes que lee el motor (las de la UI, las texturas de los modelos, los mapas y el visor de assets del overlay de depuración), e `inspectPng` lee solo su cabecera. `decodeIndexedPng` da los índices de un PNG con paleta (un byte por píxel) y su paleta, y `applyPalette` los convierte a RGBA con cualquier paleta: así se dibujan los fondos y los sprites de los mapas con la paleta de la hora del día. `PngOnlyImages` (`png_only_images.hpp`) lista, por patrón de ID, las imágenes que el juego lee de su PNG y no como textura (los fondos y las páginas de textura de los mapas, el atlas de la fuente de mapa de bits): `pngReadingOf` es la única regla, que comparten `dwx cook` (las deja como PNG) y `checkContent` (rechaza para ellas cualquier fichero que su lector no lea, con `openPngOnlyImage`); recibe también el `_meta` de la imagen, para que una excepción declarada en los metadatos pueda eximir una imagen que el patrón nombra. `loadMapImages` (`map_images.hpp`) lee las imágenes de un mapa extraído: comprueba que el fondo mide lo que dicen sus tiles (128 píxeles cada uno) y que las variantes coinciden, sube cada entrada de textura a su rectángulo de VRAM como el original (las de 8 bits y luego las de 4, cada una con un `LoadImage`, así que una entrada tapa lo que haya debajo aunque sea de otra profundidad) y lee de ahí las páginas de 256 que miran los sprites; así cargan las entradas de varias páginas (tunn05 sube tres páginas de 8 bits en una), de media página (gias02) o de pocas filas (gomi02: 256 × 32 texels de 4 bits en (704, 216)), y resuelve con `buildMapSpriteSheet` la página y la paleta de cada sprite a partir de la disposición en VRAM que nombran los datos (`MapVramLayout`); `mapSpriteOrderValue` da su valor de orden (`MapOrder`). Son propiedades de los ficheros extraídos, resueltas aquí para que el render solo dibuje quads ([ADR 0020](../../adr/0020-camara-de-mapa-y-capas-de-profundidad.md)). `decodeOpus` decodifica un [Ogg Opus](../../formats/opus.md) de `dwx cook` a PCM de 16 bits con libopus, con un lector de páginas Ogg propio. Los `.pak` de versión 2 se descomprimen con zstd. `decodeKtx2` lee las texturas cocinadas ([KTX2 con UASTC](../../formats/ktx2.md)) con libktx y las transcodifica a BC7, o a RGBA de 8 bits si la GPU no muestrea BC7. `openAsset` abre los bytes de un asset con el lector del motor para su tipo (PNG, KTX2 transcodificada a BC7, GLB con `parseModel` como lo carga el juego, glTF con cgltf, YAML, Ogg Opus decodificado, y las cabeceras de WAV, SF2, MIDI e Y4M); con el ID del asset, también los documentos que el juego lee con un lector estricto (`map/<id>/data` con `parseMapDocument`, `data/maps` con `parseMapTable`, `data/items` con `parseItemTable`, `data/item_effects` con `parseItemEffectTable`, `data/shops` con `parseShopTable` y `data/new_game_stats` con
`parseNewGameStats`). `checkContent` lo hace con todos los assets de una pila: es la prueba de que el motor carga lo que hay en un paquete (`dwe-game --check-content <carpeta>`, `dwx validate`), también con paquetes en `.pak`. Abre varios a la vez; un asset que falla se lee y se abre otra vez solo antes de contar como problema, y el que entonces abre se devuelve aparte (`dwx extract` lo avisa con `dwx: warning:`). Si `DiskFileSystem` no puede abrir un archivo, el error dice el motivo del sistema (`lastOpenError`; en Windows distingue un archivo que otro programa tiene abierto de uno sin permiso). `readModel` resume un glTF: ficheros externos, padres de las articulaciones de cada skin y nombres de las animaciones. `parseModel` y `loadModel` leen un modelo glTF binario (`.glb`, los que extrae `dwx`) a `ModelData`: vértices con piel, materiales, imágenes, piel, nodos y clips con sus `extras`, todo validado; `loadCharacterModel` lee el `models/<nombre>.yaml` del paquete `game` que dice qué modelo y qué clips usa un personaje ([modelos y animación en el motor](../../architecture/models.md)).

## Assets de desarrollo

`DevAssetSources` (`dev_asset_sources.hpp`) describe los assets locales que un desarrollador usa con `--dev-assets`:
la imagen del disco, las carpetas extraídas y la raíz del decomp, solo para leer. `validateDevAssetSources` comprueba
que cada raíz existe y es de su tipo. `findDevPacks` y `selectDevPacks` (`dev_pack_selection.hpp`) eligen, por el id de
su manifiesto, los paquetes de las carpetas extraídas que se montan: un paquete normal gana al de desarrollo con su id
y una carpeta anterior a una posterior. Los elegidos rellenan huecos (`PackLocation::fillsGaps`): la VFS los apila
debajo de todos los demás paquetes ([assets de desarrollo](../../architecture/assets-de-desarrollo.md)).

## Imágenes del jugador

Las [imágenes personalizadas](../../guides/imagenes-personalizadas.md) llegan como un paquete de la capa `user`
(`Layer::User`) que monta el lanzador. Sus PNG no sustituyen el asset, solo **cómo se dibuja**: el tamaño, los
rectángulos y las posiciones siguen siendo los de la imagen original, y la nueva solo añade detalle.

- `image_replacement.hpp`: `replacementScale(anchoOriginal, altoOriginal, ancho, alto)` da cuántas veces más grande es
  una imagen (un entero de 1 a `MaxReplacementScale`, 8, igual en los dos ejes) o nada si no sirve.
  `paletteImageId(imagen, paleta)` da el id de la versión de una imagen indexada con una paleta:
  `texture/etcdat/fi_info` con la paleta 12 es `texture/etcdat/fi_info_palette12` (`PaletteImageSuffix`).
- `VirtualFileSystem::read(fuente)` lee una fuente concreta de un asset, gane o no, y `packOf(fuente)` dice de qué
  paquete es: el cargador de una imagen sigue leyendo la del juego aunque un paquete `user` tenga la suya encima.
- `loadMapImages` (`map_images.hpp`) lee los fondos del juego por debajo de los paquetes `user` y deja en
  `MapImages::replacement` (`MapBackgroundReplacement`: escala y una imagen RGBA por variante) los del jugador que
  encajan. Los índices y las paletas siguen siendo los del juego, así que los sprites, los recortes y el orden de
  dibujo no cambian; el alfa de cada píxel es la clase del texel del fondo que tiene debajo. Una variante sin imagen
  propia usa la del juego a la misma escala. Una imagen que no encaja (no es PNG, otra proporción, una escala que no
  es entera o distinta de la de las otras variantes) se descarta con un aviso en el log y se dibuja la del juego.

Las imágenes de la UI pasan por `ui::loadDrawnImage` ([ui](ui.md)) con las mismas reglas. Los tests:
`tests/image_replacement_tests.cpp`.

## Mapas

`MapDocument` (`<dwe/assets/map_document.hpp>`) es el `map/<id>/data` de un mapa de campo
([esquema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/map.schema.json), [formato](../../formats/mapas.md#en-el-motor)), con todos los
campos tipados y los números tal como los escribió el extractor: cámara, luces, regiones, rejilla de tiles, imágenes,
sprites e instancias, warps, NPC y la rejilla de colisión de 100 × 100 (`MapCollision`: libre, bloqueada o disparador).
`parseMapDocument` lee el texto y `loadMapDocument(vfs, id)` lo lee del VFS y añade el id y sus assets: los fondos
(`map/<id>/background_<variante>`, en orden día, atardecer, noche, `palette3`...) y las texturas (`texture8/<nnn>`,
`texture4/<nnn>` y su `_meta`).

`MapTable` (`<dwe/assets/map_table.hpp>`) es `data/maps` ([esquema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/maps.schema.json)): por mapa,
su id, su índice en el original (el que usan los warps), el fichero, los flags (hora fija, NPC), las puertas, el baño y
el nombre que se ve al cargarlo (`displayName`, sin el relleno de espacios). Un mapa que añade un paquete lleva el
índice que le da el motor (`originalIndex` falso), así que `findByOriginalIndex` solo encuentra los del original.
`resolveWarpTargets` (`<dwe/assets/map_warps.hpp>`) decide adónde lleva cada salida de un mapa con la regla tolerante de
[mapas](../../formats/mapas.md#destino-de-una-salida) y devuelve sus problemas, como el warp muerto.

`DigimonTable` (`<dwe/assets/digimon_table.hpp>`) es `data/digimon`
([esquema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/digimon.schema.json)): por especie, su id, nombre, modelo, nivel, especiales, objeto
que suelta y técnicas. `RaiseTable` (`<dwe/assets/raise_table.hpp>`) es `data/raise`
([esquema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/raise.schema.json)): los datos de cuidado de cada partner (horario de comida, energía,
baño, comida favorita, patrón de sueño, aptitud de entrenamiento y peso). Los dos se leen con un lector estricto (una
clave de más o de menos es un error) y los lee [`packages/game/partner`](../game/partner.md) por el rol de tabla del
ruleset.

Las conversiones al [espacio del mundo](../../architecture/world-space.md) están en el mismo header y solo ahí:
celda de colisión ↔ posición (`collisionCellAt`, `collisionCellCenter`), posición y giro de un warp (`warpSpawn`) y
giro del original ↔ yaw en radianes (`yawFromOriginal`, `forwardOfYaw`).
`originalCellAt` da la celda como la cuenta el original (getModelTile: trunca hacia 0 y resta uno en el lado
negativo), que en el borde exacto de una celda difiere de `collisionCellAt`: el punto (600, 1700) es la celda (56, 32)
para el original y la (56, 33) por suelo. La usa el paseo de un script para saber si ya llegó: con la regla por suelo,
la salida de `species_z` en tunn09 oscilaba sin fin entre (598, 1697) y (609, 1717). Antes de contar redondea la posición a la unidad entera
más cercana, como la guarda el original: `species_w` en mgen98 se desliza por x −400 a −399,99 y, truncada, esa
posición caía en la columna de al lado y el paseo a (−400, −500) no acababa nunca.

`ItemTable` (`item_table.hpp`) es `data/items`: por objeto, su id, su número en el original, el precio, el valor de
mérito, la categoría de orden, dónde se usa y si se puede tirar; el nombre y la descripción son textos de la UI y no se
leen aquí. `ItemEffectTable` (`item_effect_table.hpp`) es `data/item_effects`: la función de cada objeto
(`ItemHandler`), lo que cura, a qué evoluciona y los valores de su caso de comida (`FoodValues`) o de chip
(`ChipValues`). `ShopTable` (`shop_table.hpp`) es `data/shops`: mapas con tienda y tenderos. Los usa
[`packages/game/inventory`](../game/inventory.md). `NewGameStatsTable` (`new_game_stats.hpp`) es
`data/new_game_stats`: los stats con los que el guion de la partida nueva hace empezar al partner
([scripts](../../formats/scripts.md#estadísticas-de-la-partida-nueva)); `loadNewGameStats` no da nada si ningún
paquete lo trae. Lo usa [`packages/session`](../apps/session.md).

## IDs de contenido y tablas por entradas

`ContentId` (`<dwe/assets/content_id.hpp>`) es el id de una entrada de una tabla de contenido (un Digimon, un mapa, un
objeto): el nombre estable de un original (`species_a`, `twna01`) o, para lo que añade un paquete, `<id del paquete>:<nombre>`.
`assetPath` lo convierte en carpetas (`example_mod:meadow` → `example_mod/meadow`), así que los IDs de asset no
cambian de gramática. `composeTable` y `readComposedTable` (`<dwe/assets/table_composition.hpp>`) componen el fichero
de una tabla de todos los paquetes que lo tienen, de abajo arriba: una entrada con un id que ya está la sustituye en
su sitio, una nueva va al final con el siguiente `index` libre y una sección que no es de entradas se sustituye entera.
Cada tabla dice qué secciones son de entradas con su `…Composition` (`DigimonComposition`, `MapComposition`…), y su
lector la lee compuesta. `composedTableKinds` y `composedTablesOf` (`<dwe/assets/table_kinds.hpp>`) dicen qué tablas de los paquetes
se componen y de qué clase son (las de siempre y las que nombran los rulesets por rol), para que `checkContent` componga
las mismas que el juego. `requireNewSpeciesContracts` (`digimon_table.hpp`) exige el contrato del modelo de cada Digimon
que añade un paquete. Las reglas están en [tablas por entradas](../../architecture/vfs.md#tablas-por-entradas).

## IDs en la sesión

En los bordes todo se nombra por id: las partidas guardadas, los rulesets y los ficheros de datos, los scripts y
ganchos, las claves de texto y los logs. El `index` de una entrada es interno: el de un original es su número en el
juego (lo da el paquete base) y el de una entrada que añade un paquete lo pone el motor al componer (el siguiente
libre), así que cambia según los paquetes montados. `ComposedTableText::engineNumbered` dice qué entradas numeró el
motor y `ItemEntry::originalIndex` / `MoveEntry::originalIndex` lo guardan en cada entrada.

Dentro de la sesión el juego guarda un *handle* (`ItemHandle`, `MoveHandle`; `<dwe/assets/table_handle.hpp>`): el sitio
de la entrada en la tabla compuesta. Son tipos distintos para cada tabla y solo la tabla los crea
(`ItemTable::handleOf(id)`), nunca a partir de un número; un handle no sale nunca de la sesión. La composición es
inmutable durante la sesión: montar otros paquetes es empezar una sesión nueva desde los ids. Cada tabla compuesta
lleva su `CompositionGeneration` (sus copias comparten la misma) y cada handle la suya; `at(handle)` con un handle de
otra composición es un error de programación (`DWE_ASSERT`), y `owns(handle)` lo pregunta sin fallar.
`findByOriginalIndex` busca por el número del original y solo lo usan la migración de partidas antiguas y la
comprobación de los rangos de los rulesets.

`EntryClass` (`<dwe/assets/entry_class.hpp>`) es una clase de entradas que nombra un ruleset (las comidas, los
remates): rangos y umbrales de números del original, que solo cubren entradas del original, más una lista de ids, por
la que entran las de los mods. `checkEntryClass` comprueba al cargar que cada número de un rango es una entrada del
original y que cada id existe ([rangos e ids](../../architecture/rulesets.md#rangos-e-ids)).

`YamlReader` (`<dwe/assets/yaml_reader.hpp>`) lee valores tipados de un documento YAML y guarda el primer error con
su línea y columna; lo usan los lectores de documentos del motor.

Los tests de `map_content_tests.cpp` leen todos los mapas del contenido extraído si `DWE_CONTENT` apunta a él.

## Técnicas y evolución

Junto a `DigimonTable` y `RaiseTable`, dos tablas más que `dwx` extrae del disco del usuario al paquete `base`, con el
mismo lector estricto (cada fila con exactamente sus claves, cada id una sola vez); sus esquemas están en
`content/schemas` y las leen [`packages/game/evolution`](../game/evolution.md) y la batalla:

- `MoveTable` (`<dwe/assets/move_table.hpp>`, `data/moves`): cada técnica con su índice, potencia, coste y demás campos;
  `unknown_0d` se lee como `flags` (bit 0: necesita objetivo; bit 1: comparte enfriamiento) y, si están,
  `learn_chances` (la probabilidad de aprenderla en combate por ranura de especialidad). Un índice repetido también es
  un error.
- `EvolutionTable` (`<dwe/assets/evolution_table.hpp>`, `data/evolution`): requisitos, filas de stats de referencia,
  cartas de evolución y, si están, destinos de objetos de evolución escritos a mano (`item_targets`, que `dwx` no
  escribe: los extrae en `evolves_to` de `data/item_effects`, y `item_targets` los sustituye objeto a objeto).

Cada una tiene `parse…Table(texto)` y `load…Table(vfs, id)`. Las referencias a otras filas (`TableReference`,
`<dwe/assets/table_reference.hpp>`) quedan como texto: un id, nada, o un número crudo que el extractor no pudo
resolver; las resuelve quien las usa.
