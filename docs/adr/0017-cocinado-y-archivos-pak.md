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
    datos (ids que acaban en `_depth`, `_mask` o `_normal`). El contenedor KTX2 se escribe a mano según la
    especificación de Khronos ([KTX2](../formats/ktx2.md)).
  - **GLB → GLB**: se comprueba la cabecera y se copia.
- **Caché por contenido:** la carpeta de salida guarda en `.dwx-cook` el SHA-1 de la fuente, el nombre y la versión
  del cocinero y el SHA-1 de la salida de cada fichero. Lo que no cambió no se vuelve a cocinar, y dos assets con los
  mismos bytes se cocinan una vez. El resultado es el mismo con caché o sin ella.
- **`dwx pack <paquete> -o <fichero.pak>`** escribe un archivo `.pak` propio, versionado y documentado
  ([formato](../formats/pak.md)): cabecera, datos alineados a 16 bytes, y al final la tabla de contenidos ordenada por
  id de asset (offset, tamaño, SHA-1 y un campo de compresión reservado), los nombres y el `pack.yaml`. Sin
  compresión en la v1. Es determinista: los mismos ficheros dan los mismos bytes.
- **El motor monta `.pak`:** `PakFileSystem` es un `FileSystem` más, y cada paquete se monta con el suyo
  (`PackLocation`), así que una capa puede ser una carpeta o un `.pak`.

## Consecuencias

- Cocinar y empaquetar funcionan hoy, sin dependencias, sobre la extracción real, y el motor ya carga paquetes en un
  solo fichero.
- El KTX2 sin comprimir ocupa mucho más que el PNG (RGBA8 completo más un tercio por los mipmaps) y no ahorra
  memoria de GPU. Con la extracción de la versión US, los 1408 PNG pasan de 181 MB a 1453 MB de KTX2, y el paquete
  cocinado entero de 344 MB a 1617 MB. Es correcto y el motor lo puede subir tal cual, pero no es el formato final:
  la supercompresión es la primera de las propuestas de abajo.
- El `.pak` sin compresión ocupa lo mismo que la carpeta. El campo de compresión de cada entrada permite añadirla en
  una versión 2 sin cambiar el resto.
- Cada lectura de un `.pak` comprueba el SHA-1 de la entrada: detecta archivos dañados a cambio de un poco de CPU.
- Subir la versión de un cocinero invalida su caché; hay que hacerlo cada vez que cambie su salida.

### Propuestas para decidir

Cada una necesita una dependencia nueva (todas tienen port en vcpkg en el baseline actual):

| Propuesta | Dependencia | Qué da |
|---|---|---|
| Texturas con supercompresión Basis (UASTC/ETC1S) o BC7 | `ktx` 4.4.2 (KTX-Software, incluye basisu) o `basisu` 2.50 | Texturas varias veces más pequeñas en disco y en GPU; el motor transcodifica a BC7 al cargar |
| Efectos de sonido (y música remasterizada) a Opus | `opus` 1.6.1, `libogg` 1.3.6 y `libopusenc` 0.3 para escribir; `opusfile` para leer | Audio mucho más pequeño que WAV |
| Compresión en el `.pak` (versión 2) | `zstd` 1.5.7 o `lz4` 1.10.0 | Paquetes más pequeños; zstd comprime más, lz4 descomprime más rápido |

## Alternativas descartadas

- **zip:** leerlo y escribirlo sin una librería es casi tan largo como el formato propio, no garantiza la alineación
  de los datos y su índice no está ordenado por id.
- **tar:** no tiene índice; hay que recorrerlo entero para encontrar un fichero.
- **KTX2 con libktx desde ya:** es la dependencia que este ADR deja para decidir.
- **DDS o KTX 1:** KTX2 es el contenedor estándar para Vulkan y el que admite supercompresión Basis.
- **Ver los `.pak` como carpetas dentro de `DiskFileSystem`:** escondería de dónde sale cada fichero; un sistema de
  ficheros por paquete es más claro y no cambia nada de lo que ya montaba carpetas.
