# assets

Contenido del juego por paquetes ([tubería de assets](../../docs/architecture/assets-pipeline.md)). `AssetId` es el nombre estable de un asset (`digimon/agumon/model`) y `PackManifest` lee el `pack.yaml` de cada paquete: id, versión, capa, prioridad y dependencias. El formato del manifiesto también está en [`content/schemas/pack.schema.json`](../../content/schemas/pack.schema.json). `findPacks` encuentra los paquetes de una carpeta de contenido (carpetas y archivos `.pak`), `openPacks` abre los archivos y `VirtualFileSystem` los monta, los apila por capa y prioridad y dice qué fichero gana para cada ID. Lee los ficheros a través de `FileSystem`: `DiskFileSystem` para el disco, `PakFileSystem` para un [archivo `.pak`](../../docs/formats/pak.md) (que `writePak` escribe) y `MemoryFileSystem` para los tests; cada paquete se monta con el suyo (`PackLocation`). Solo depende de `core`. `decodePng` decodifica un PNG a RGBA de 8 bits con lodepng, para las texturas que lee el motor (el visor de assets del overlay de depuración, por ahora), e `inspectPng` lee solo su cabecera. `decodeOpus` decodifica un [Ogg Opus](../../docs/formats/opus.md) de `dwx cook` a PCM de 16 bits con libopus, con un lector de páginas Ogg propio. Los `.pak` de versión 2 se descomprimen con zstd. `decodeKtx2` lee las texturas cocinadas ([KTX2 con UASTC](../../docs/formats/ktx2.md)) con libktx y las transcodifica a BC7, o a RGBA de 8 bits si la GPU no muestrea BC7. `openAsset` abre los bytes de un asset con el lector del motor para su tipo (PNG, KTX2 transcodificada a BC7, glTF/GLB con cgltf, YAML, Ogg Opus decodificado, y las cabeceras de WAV, SF2, MIDI e Y4M) y `checkContent` lo hace con todos los assets de una pila: es la prueba de que el motor carga lo que hay en un paquete (`dwe-game --check-content <carpeta>`, `dwx validate`), también con paquetes en `.pak`. `readModel` resume un glTF: ficheros externos, padres de las articulaciones de cada skin y nombres de las animaciones.

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

Las conversiones al [espacio del mundo](../../docs/architecture/world-space.md) están en el mismo header y solo ahí:
celda de colisión ↔ posición (`collisionCellAt`, `collisionCellCenter`), posición y giro de un warp (`warpSpawn`) y
giro del original ↔ yaw en radianes (`yawFromOriginal`, `forwardOfYaw`).

`YamlReader` (`<dwe/assets/yaml_reader.hpp>`) lee valores tipados de un documento YAML y guarda el primer error con
su línea y columna; lo usan los lectores de documentos del motor.

Los tests de `map_content_tests.cpp` leen todos los mapas del contenido extraído si `DWE_CONTENT` apunta a él.
