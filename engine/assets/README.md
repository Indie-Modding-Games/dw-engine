# assets

Contenido del juego por paquetes ([tubería de assets](../../docs/architecture/assets-pipeline.md)). `AssetId` es el nombre estable de un asset (`digimon/agumon/model`) y `PackManifest` lee el `pack.yaml` de cada paquete: id, versión, capa, prioridad y dependencias. El formato del manifiesto también está en [`content/schemas/pack.schema.json`](../../content/schemas/pack.schema.json). `findPacks` encuentra los paquetes de una carpeta de contenido (carpetas y archivos `.pak`), `openPacks` abre los archivos y `VirtualFileSystem` los monta, los apila por capa y prioridad y dice qué fichero gana para cada ID. Lee los ficheros a través de `FileSystem`: `DiskFileSystem` para el disco, `PakFileSystem` para un [archivo `.pak`](../../docs/formats/pak.md) (que `writePak` escribe) y `MemoryFileSystem` para los tests; cada paquete se monta con el suyo (`PackLocation`). Solo depende de `core`. `decodePng` decodifica un PNG a RGBA de 8 bits con lodepng, para las texturas que lee el motor (el visor de assets del overlay de depuración, por ahora), e `inspectPng` lee solo su cabecera. `decodeIndexedPng` da los índices de un PNG con paleta (un byte por píxel) y su paleta, y `applyPalette` los convierte a RGBA con cualquier paleta: así se dibujan los fondos y los sprites de los mapas con la paleta de la hora del día. `loadMapImages` (`map_images.hpp`) lee las imágenes de un mapa extraído: comprueba que el fondo mide lo que dicen sus tiles (128 píxeles cada uno) y que las variantes coinciden, y resuelve con `buildMapSpriteSheet` la página y la paleta de cada sprite a partir de la disposición en VRAM que nombran los datos (`MapVramLayout`); `mapSpriteOrderValue` da su valor de orden (`MapOrder`). Son propiedades de los ficheros extraídos, resueltas aquí para que el render solo dibuje quads ([ADR 0020](../../docs/adr/0020-camara-de-mapa-y-capas-de-profundidad.md)). `decodeOpus` decodifica un [Ogg Opus](../../docs/formats/opus.md) de `dwx cook` a PCM de 16 bits con libopus, con un lector de páginas Ogg propio. Los `.pak` de versión 2 se descomprimen con zstd. `decodeKtx2` lee las texturas cocinadas ([KTX2 con UASTC](../../docs/formats/ktx2.md)) con libktx y las transcodifica a BC7, o a RGBA de 8 bits si la GPU no muestrea BC7. `openAsset` abre los bytes de un asset con el lector del motor para su tipo (PNG, KTX2 transcodificada a BC7, GLB con `parseModel` como lo carga el juego, glTF con cgltf, YAML, Ogg Opus decodificado, y las cabeceras de WAV, SF2, MIDI e Y4M); con el ID del asset, también los documentos que el juego lee con un lector estricto (`map/<id>/data` con `parseMapDocument`, `data/maps` con `parseMapTable`, `data/items` con `parseItemTable`, `data/item_effects` con `parseItemEffectTable`, `data/shops` con `parseShopTable` y `data/new_game_stats` con
`parseNewGameStats`). `checkContent` lo hace con todos los assets de una pila: es la prueba de que el motor carga lo que hay en un paquete (`dwe-game --check-content <carpeta>`, `dwx validate`), también con paquetes en `.pak`. `readModel` resume un glTF: ficheros externos, padres de las articulaciones de cada skin y nombres de las animaciones. `parseModel` y `loadModel` leen un modelo glTF binario (`.glb`, los que extrae `dwx`) a `ModelData`: vértices con piel, materiales, imágenes, piel, nodos y clips con sus `extras`, todo validado; `loadCharacterModel` lee el `models/<nombre>.yaml` del paquete `game` que dice qué modelo y qué clips usa un personaje ([modelos y animación en el motor](../../docs/architecture/models.md)).

## Mapas

`MapDocument` (`<dwe/assets/map_document.hpp>`) es el `map/<id>/data` de un mapa de campo
([esquema](../../content/schemas/map.schema.json), [formato](../../docs/formats/mapas.md#en-el-motor)), con todos los
campos tipados y los números tal como los escribió el extractor: cámara, luces, regiones, rejilla de tiles, imágenes,
sprites e instancias, warps, NPC y la rejilla de colisión de 100 × 100 (`MapCollision`: libre, bloqueada o disparador).
`parseMapDocument` lee el texto y `loadMapDocument(vfs, id)` lo lee del VFS y añade el id y sus assets: los fondos
(`map/<id>/background_<variante>`, en orden día, atardecer, noche, `palette3`...) y las texturas (`texture8/<nnn>`,
`texture4/<nnn>` y su `_meta`).

`MapTable` (`<dwe/assets/map_table.hpp>`) es `data/maps` ([esquema](../../content/schemas/maps.schema.json)): por mapa,
su id, su índice en el original (el que usan los warps), el fichero, los flags (hora fija, NPC), las puertas, el baño y
el nombre que se ve al cargarlo (`displayName`, sin el relleno de espacios).

`DigimonTable` (`<dwe/assets/digimon_table.hpp>`) es `data/digimon`
([esquema](../../content/schemas/digimon.schema.json)): por especie, su id, nombre, modelo, nivel, especiales, objeto
que suelta y técnicas. `RaiseTable` (`<dwe/assets/raise_table.hpp>`) es `data/raise`
([esquema](../../content/schemas/raise.schema.json)): los datos de cuidado de cada partner (horario de comida, energía,
baño, comida favorita, patrón de sueño, aptitud de entrenamiento y peso). Los dos se leen con un lector estricto (una
clave de más o de menos es un error) y los lee [`game/partner`](../../game/partner/README.md) por el rol de tabla del
ruleset.

Las conversiones al [espacio del mundo](../../docs/architecture/world-space.md) están en el mismo header y solo ahí:
celda de colisión ↔ posición (`collisionCellAt`, `collisionCellCenter`), posición y giro de un warp (`warpSpawn`) y
giro del original ↔ yaw en radianes (`yawFromOriginal`, `forwardOfYaw`).

`ItemTable` (`item_table.hpp`) es `data/items`: por objeto, su id, su número en el original, el precio, el valor de
mérito, la categoría de orden, dónde se usa y si se puede tirar; el nombre y la descripción son textos de la UI y no se
leen aquí. `ItemEffectTable` (`item_effect_table.hpp`) es `data/item_effects`: la función de cada objeto
(`ItemHandler`), lo que cura, a qué evoluciona y los valores de su caso de comida (`FoodValues`) o de chip
(`ChipValues`). `ShopTable` (`shop_table.hpp`) es `data/shops`: mapas con tienda y tenderos. Los usa
[`game/inventory`](../../game/inventory/README.md). `NewGameStatsTable` (`new_game_stats.hpp`) es
`data/new_game_stats`: los stats con los que el guion de la partida nueva hace empezar al partner
([scripts](../../docs/formats/scripts.md#estadísticas-de-la-partida-nueva)); `loadNewGameStats` no da nada si ningún
paquete lo trae. Lo usa [`apps/session`](../../apps/session/README.md).

`YamlReader` (`<dwe/assets/yaml_reader.hpp>`) lee valores tipados de un documento YAML y guarda el primer error con
su línea y columna; lo usan los lectores de documentos del motor.

Los tests de `map_content_tests.cpp` leen todos los mapas del contenido extraído si `DWE_CONTENT` apunta a él.

## Técnicas y evolución

Junto a `DigimonTable` y `RaiseTable`, dos tablas más que `dwx` extrae del disco del usuario al paquete `base`, con el
mismo lector estricto (cada fila con exactamente sus claves, cada id una sola vez); sus esquemas están en
`content/schemas` y las leen [`game/evolution`](../../game/evolution/README.md) y la batalla:

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
