# 0017. Cocinado sin dependencias nuevas y archivos `.pak` propios

- **Estado:** Propuesta
- **Fecha:** 2026-10-19

## Contexto

La [tubería de assets](../architecture/assets-pipeline.md) ([ADR 0005](0005-tuberia-de-assets.md)) tiene un tercer
estado, el **cocinado**: lo que el juego carga rápido, y un paquete distribuible en un solo fichero. Hasta ahora solo
existía `dwx extract`, que escribe la base en formato fuente (PNG, glTF binario, YAML, WAV, MIDI, SF2).

Lo ideal para las texturas es KTX2 con supercompresión Basis o BC7, y para el audio, Opus. Las dos cosas necesitan
librerías nuevas, y añadir una dependencia es decisión del equipo. Esta primera versión tiene que funcionar sin
ninguna y dejar el camino preparado.

## Decisión

- **`dwx cook <paquete> -o <carpeta>`** pasa cada fichero por el **cocinero de su extensión**, en un registro como el
  de los extractores. Lo que no tiene cocinero se copia tal cual. Los cocineros de la v1:
  - **PNG → KTX2** RGBA8 sin comprimir, con la cadena de mipmaps completa, en sRGB para el color y lineal para los
    datos. El contenedor KTX2 se escribe a mano según la especificación de Khronos ([KTX2](../formats/ktx2.md)).
    Lo deciden los metadatos, no el nombre: el espacio de color sale de la clave `color_space` del YAML lateral
    (`<id>_meta.yaml`; sRGB si no la tiene), y los PNG con datos de paleta se copian tal cual (ver abajo).
  - **Datos de paleta, tal cual:** hasta que se decida cómo dibuja el motor las paletas, un PNG indexado, o uno cuyo
    YAML lateral dice `png: indexed` o tiene `clut_rect`, no se convierte: se copia como PNG para no perder los
    índices ni la paleta.
  - **GLB → GLB**: se comprueba la cabecera y se copia.
  - **WAV → Ogg Opus** a 48 kHz con libopusenc, con ajustes fijos (64 kbps por canal, VBR, complejidad 10, número de
    serie fijo) y el bucle y la nota del WAV en comentarios ([Opus](../formats/opus.md)). El motor lo decodifica con
    libopus (`assets::decodeOpus`) y un lector de páginas Ogg propio. SF2 y MIDI se copian tal cual, con las muestras
    del SF2 sin tocar.
- **Caché por contenido:** la carpeta de salida guarda en `.dwx-cook` el SHA-1 de la fuente, el nombre, la versión
  y las opciones del cocinero y el SHA-1 de la salida de cada fichero. Lo que no cambió no se vuelve a cocinar, y dos
  assets con los mismos bytes se cocinan una vez. El resultado es el mismo con caché o sin ella.
- **`dwx pack <paquete> -o <fichero.pak>`** escribe un archivo `.pak` propio, versionado y documentado
  ([formato](../formats/pak.md)): cabecera, datos alineados a 16 bytes, y al final la tabla de contenidos ordenada por
  id de asset (offset, tamaños, SHA-1 y compresión), los nombres y el `pack.yaml`. La versión 2 comprime cada
  fichero con **zstd** (nivel 9 y ajustes fijos) y lo guarda tal cual si así no ocupa menos; el SHA-1 es del fichero
  sin comprimir. El lector acepta también la versión 1, que es la 2 sin nada comprimido. Es determinista: los mismos
  ficheros dan los mismos bytes.
- **El motor monta `.pak`:** `PakFileSystem` es un `FileSystem` más, y cada paquete se monta con el suyo
  (`PackLocation`), así que una capa puede ser una carpeta o un `.pak`.

## Consecuencias

- Cocinar y empaquetar funcionan hoy, sin dependencias, sobre la extracción real, y el motor ya carga paquetes en un
  solo fichero.
- El KTX2 sin comprimir ocupa mucho más que el PNG (RGBA8 completo más un tercio por los mipmaps) y no ahorra
  memoria de GPU. Con la extracción de la versión US, los 1408 PNG pasan de 181 MB a 1453 MB de KTX2, y el paquete
  cocinado entero de 344 MB a 1617 MB. Es correcto y el motor lo puede subir tal cual, pero no es el formato final:
  la supercompresión es la primera de las propuestas de abajo.
- Con la extracción de la versión US (4619 ficheros), en release:

  | | Antes | Después |
  |---|---|---|
  | `.pak` del paquete base (versión 1 → 2 con zstd) | 351,9 MB | 240,7 MB (2,9 s con la relectura) |
  | Los 642 WAV → Opus | 17,1 MB | 7,0 MB |
  | `.pak` del paquete cocinado (WAV a Opus, sin KTX2 porque todo PNG es de paleta) | 351,9 MB | 233,9 MB |

  Por tipo, zstd deja los PNG (ya comprimidos con deflate) y los Opus igual, y gana sobre todo en el GLB (114,6 →
  24,8 MB) y el YAML (15,5 → 2,0 MB); el SF2 baja un 19 %. Dentro del `.pak`, Opus ahorra 6,8 MB frente al WAV ya
  comprimido con zstd (13,8 → 7,0 MB).
- Opus es con pérdida: un bucle corto de un instrumento puede notar el salto en el punto de vuelta. Las muestras sin
  pérdida siguen en el SF2 de cada banco. 62 WAV a muy baja frecuencia salen algo más grandes en Opus (159 KB en
  total).
- zstd, opus y libopusenc son dependencias nuevas, fijadas por el baseline de vcpkg; otra versión de zstd o de libopus
  podría escribir otros bytes (el lector lee igual cualquier frame o paquete válido).
- Cada lectura de un `.pak` comprueba el SHA-1 de la entrada: detecta archivos dañados a cambio de un poco de CPU.
- Subir la versión de un cocinero invalida su caché; hay que hacerlo cada vez que cambie su salida.

### Propuestas para decidir

Cada una necesita una dependencia nueva (todas tienen port en vcpkg en el baseline actual):

| Propuesta | Dependencia | Qué da |
|---|---|---|
| Texturas con supercompresión Basis (UASTC/ETC1S) o BC7 | `ktx` 4.4.2 (KTX-Software, incluye basisu) o `basisu` 2.50 | Texturas varias veces más pequeñas en disco y en GPU; el motor transcodifica a BC7 al cargar |

El usuario aprobó el 2026-10-19 Opus para el audio y zstd para las entradas del `.pak`, y están hechos (arriba). Para
Opus bastan `opus` 1.6.1 y `libopusenc` 0.3, que trae su propio empaquetador Ogg: el motor lee las páginas Ogg con
código propio, así que no hacen falta `libogg` ni `opusfile`. Se eligió zstd frente a `lz4` porque comprime más y
descomprime de sobra rápido para cargar paquetes.

## Alternativas descartadas

- **zip:** leerlo y escribirlo sin una librería es casi tan largo como el formato propio, no garantiza la alineación
  de los datos y su índice no está ordenado por id.
- **tar:** no tiene índice; hay que recorrerlo entero para encontrar un fichero.
- **KTX2 con libktx desde ya:** es la dependencia que este ADR deja para decidir.
- **DDS o KTX 1:** KTX2 es el contenedor estándar para Vulkan y el que admite supercompresión Basis.
- **Ver los `.pak` como carpetas dentro de `DiskFileSystem`:** escondería de dónde sale cada fichero; un sistema de
  ficheros por paquete es más claro y no cambia nada de lo que ya montaba carpetas.
