# 0017. Cocinado a KTX2 UASTC y Opus, y archivos `.pak` propios con zstd

- **Estado:** Aceptada
- **Fecha:** 2026-10-19

## Contexto

La [tubería de assets](../architecture/assets-pipeline.md) ([ADR 0005](0005-tuberia-de-assets.md)) tiene un tercer
estado, el **cocinado**: lo que el juego carga rápido, y un paquete distribuible en un solo fichero. Hasta ahora solo
existía `dwx extract`, que escribe la base en formato fuente (PNG, glTF binario, YAML, WAV, MIDI, SF2).

Una primera versión sin dependencias nuevas cocinaba los PNG a KTX2 RGBA8 sin comprimir, con un escritor propio, y
guardaba el `.pak` sin comprimir. Funcionaba, pero el paquete cocinado ocupaba mucho más que la extracción
(1,6 GB frente a unos 350 MB) y no ahorraba memoria de GPU. El usuario decidió el 2026-10-19 los formatos finales y las
dependencias que hacen falta para ellos.

## Decisión

- **`dwx cook <paquete> -o <carpeta>`** pasa cada fichero por el **cocinero de su extensión**, en un registro como el
  de los extractores. Lo que no tiene cocinero se copia tal cual. Los cocineros:
  - **PNG → KTX2 con Basis Universal UASTC y supercompresión zstd** ([KTX2](../formats/ktx2.md)), con la cadena de
    mipmaps completa, en sRGB para el color y lineal para los datos. UASTC nivel 2, sin RDO, un hilo por textura,
    zstd nivel 18 y metadatos fijos, escrito con libktx. El motor lo **transcodifica al cargarlo a BC7**, o a RGBA8
    si la GPU no muestrea BC7 (`assets::decodeKtx2`, `rhi::Device::supportsBc7`). Lo deciden los metadatos, no el
    nombre: el espacio de color sale de la clave `color_space` del YAML lateral (`<id>_meta.yaml`; sRGB si no la
    tiene), y los PNG cuya paleta se elige al jugar se copian tal cual (abajo).
  - **GLB → GLB**: se comprueba la cabecera y se copia.
  - **WAV → Ogg Opus** a 48 kHz con libopusenc, con ajustes fijos (64 kbps por canal, VBR, complejidad 10, número de
    serie fijo) y el bucle y la nota del WAV en comentarios ([Opus](../formats/opus.md)). El motor lo decodifica con
    libopus (`assets::decodeOpus`) y un lector de páginas Ogg propio. SF2 y MIDI se copian tal cual, con las muestras
    del SF2 sin tocar.
  - **Y4M → AV1 en IVF** con libaom (añadido después, #105): cada película pasa a `video.ivf` y su WAV a Opus como
    los demás sonidos; `movie.yaml` se copia ([vídeo](../formats/video.md), medidas en
    [películas](../architecture/peliculas.md#fase-5)). Las películas, tres cuartas partes del paquete extraído
    (abajo), quedan en unos 27 MB.
- **Imágenes que el juego lee de su PNG, tal cual.** Las que un lector del motor decodifica él mismo, y no como
  textura, se copian como PNG: lo dice, por tipo de asset y no por el YAML lateral, la lista `assets::PngOnlyImages`
  (`packages/engine/assets/include/dwe/assets/png_only_images.hpp`). Hoy son los fondos de mapa (`map/<id>/background_*`) y las
  páginas de textura (`map/<id>/texture8/NNN`, `texture4/NNN`), de los que `loadMapImages` lee los índices y la paleta
  (la hora del día elige la paleta del fondo, y los sprites con `clut` −1 se dibujan con ella), y el atlas de la
  fuente de mapa de bits (`font/<nombre>`), del que `ui::BitmapFont` lee el alfa exacto de cada píxel. `dwx validate`
  y `dwe-game --check-content` rechazan cualquier otro fichero para esos IDs, así que un paquete cocinado que el juego
  no puede leer falla antes de jugar.
- **Paletas que se eligen al jugar, tal cual.** Un PNG indexado se copia también como PNG, con sus índices y su
  paleta, si su YAML lateral muestra que el juego dibuja los mismos píxeles con más de una fila de CLUT:
  - lista **más de una paleta** (`palettes`): las texturas de los Digimon, cuyas primitivas eligen cada una su fila;
    las de los efectos; los TIM de varias paletas; o
  - es un **TIM de mapa con CLUT** (`source` acaba en `.MAP`): cada sprite del mapa elige su fila (`clut` en
    `data.yaml`); −1 es la fila 480, la de la hora del día, así que el mismo TIM se ve con la paleta de día, de
    atardecer o de noche.

  Todo lo demás, también los PNG indexados de una sola paleta (cartas, texturas sueltas), se convierte a UASTC.
  **Corrección (2026-10-21):** la primera versión de esta regla convertía también los fondos de mapa y el atlas de la
  fuente, pero `loadMapImages` y `ui::BitmapFont` los leen de su PNG, así que un paquete cocinado no cargaba ningún
  mapa; desde entonces la regla parte del lector de cada tipo de asset. «Decidido por el manager, 2026-10-21»: se
  mantiene que los PNG indexados de una sola paleta cuyo lector lee colores (cartas, texturas sueltas) se cocinen a
  UASTC; una imagen sigue en PNG exactamente cuando su lector lee los índices o los píxeles exactos (`PngOnlyImages`),
  y todas las demás pueden ser KTX2. **Esta regla es la interpretación del responsable de la integración**, no una
  decisión del usuario: el usuario decidió que las texturas cocinadas son UASTC, y qué imagen es una textura y cuál
  guarda una paleta que se elige al jugar lo decidió el responsable a partir de los metadatos. El usuario la puede
  revisar; si el motor llega a dibujar las paletas de otra forma (por ejemplo, componiendo cada variante al cocinar),
  cambia la regla y no el formato.
- **Caché por contenido:** la carpeta de salida guarda en `.dwx-cook` el SHA-1 de la fuente, el nombre, la versión
  y las opciones del cocinero y el SHA-1 de la salida de cada fichero. Lo que no cambió no se vuelve a cocinar, y dos
  assets con los mismos bytes se cocinan una vez. El resultado es el mismo con caché o sin ella. El cocinero de
  texturas pasa a la versión 3 con UASTC.
- **`dwx pack <paquete> -o <fichero.pak>`** escribe un archivo `.pak` propio, versionado y documentado
  ([formato](../formats/pak.md)): cabecera, datos alineados a 16 bytes, y al final la tabla de contenidos ordenada por
  id de asset (offset, tamaños, SHA-1 y compresión), los nombres y el `pack.yaml`. La versión 2 comprime cada
  fichero con **zstd** (nivel 9 y ajustes fijos) y lo guarda tal cual si así no ocupa menos; el SHA-1 es del fichero
  sin comprimir. El lector acepta también la versión 1, que es la 2 sin nada comprimido. Es determinista: los mismos
  ficheros dan los mismos bytes.
- **El motor monta `.pak`:** `PakFileSystem` es un `FileSystem` más, y cada paquete se monta con el suyo
  (`PackLocation`), así que una capa puede ser una carpeta o un `.pak`.
- **Los paquetes del equipo viajan en `.pak`:** los scripts de empaquetado (`scripts/package/cook_team_packs.sh`)
  cocinan y empaquetan cada carpeta de `packs/` con el `dwx` del propio paquete (`game.pak`, `es-translation.pak`) y
  borran la carpeta. Una compilación de paquete (`DWE_PACKAGE`) solo monta los paquetes del equipo como `.pak` y avisa
  si encuentra una carpeta; la pantalla de primer arranque lee su fuente y sus textos del `.pak`
  (`content::readTeamPackFile`). En desarrollo se siguen montando las carpetas de `content/packs`. `dwx cook` y
  `dwx pack` aceptan `--progress` para que quien los ejecuta pueda mostrar el avance.
- **Dependencias** (fijadas por el baseline de vcpkg): `ktx` 4.4.2 (KTX-Software, Apache-2.0, con Basis Universal
  dentro) para escribir y leer KTX2; `zstd` 1.5.7; `opus` 1.6.1 y `libopusenc` 0.3, que trae su propio empaquetador
  Ogg (no hacen falta `libogg` ni `opusfile`). El port de `ktx` exporta solo `KTX::ktx` (no la variante de solo
  lectura `ktx_read`), así que el motor enlaza la librería entera.

## Determinismo

Cocinar y empaquetar dan los mismos bytes con las mismas entradas, dentro de este alcance:

- las versiones fijadas por el baseline de vcpkg (libktx con su Basis Universal, zstd, libopus y libopusenc);
- el mismo camino de CPU: Basis Universal, y libopus si se compila con detección en tiempo de ejecución, eligen al
  arrancar código SIMD (SSE 4.1, AVX...) si la CPU lo tiene, y los caminos no prometen los mismos bytes; otra
  arquitectura tampoco;
- el mismo compilador y la misma librería estándar para los mipmaps (`pow`).

Fuera de ese alcance la salida puede cambiar, pero sigue siendo válida y el motor la lee igual. La caché de
`dwx cook` guarda cada salida por el SHA-1 de la fuente y el nombre, la versión y las opciones del cocinero, así que
subir una dependencia exige subir la versión del cocinero para no mezclar salidas viejas y nuevas. Los tests
comprueban que cocinar la misma textura dos veces, también en varios hilos a la vez, da los mismos bytes.

## Medidas

Con la extracción de la versión US (4813 assets, 1530 PNG, las 4 películas incluidas), `dwx` del 2026-10-19
compilado en release (`linux-clang-release`). Cada tamaño, con la carpeta `movie/` y sin ella (en MB de 10⁶ bytes):

| | Extraído | Cocinado |
|---|---|---|
| Carpeta del paquete base, sin `movie/` | 351,7 MB | 420,9 MB (el cocinado RGBA8 sin comprimir ocupaba 1617 MB) |
| Carpeta del paquete base, con `movie/` | 1523,5 MB | 1592,7 MB |
| `.pak` (versión 2, zstd), sin `movie/` | 240,9 MB | 313,3 MB |
| `.pak` (versión 2, zstd), con `movie/` | 611,1 MB | 683,5 MB |
| Los 645 PNG que pasan a KTX2 UASTC | 167,5 MB | 246,7 MB (en KTX2 RGBA8 sin comprimir, 1190,7 MB) |
| — de ellos, los 513 fondos de mapa | 166,1 MB | 244,1 MB |
| Los 642 WAV → Opus | 17,1 MB | 7,0 MB |

- **Películas:** `movie/` ocupa 1171,8 MB (4 Y4M, 4 WAV y 4 YAML), tres cuartas partes del paquete. Estas medidas
  son de cuando se copiaba tal cual (en el `.pak`, zstd la dejaba en unos 370 MB); cocinada a AV1 y Opus ocupa
  27,4 MB ([películas](../architecture/peliculas.md#fase-5)).
- **Paquetes del equipo** (2026-11-07, `dwx` de depuración): `game` pasa de 3,5 MB en carpeta a 0,9 MB en
  `game.pak` (cocinar 0,66 s, empaquetar 0,41 s; solo las dos banderas PNG pasan a KTX2) y `es-translation` de
  1,6 MB a 0,16 MB (0,13 s y 0,15 s). `dwe-game --check-content` con el base, `ui_original` y los dos paquetes abre
  6037 assets sin fallos en 11,7 a 13,2 s con los `.pak` y en 13,5 a 14,4 s con las carpetas: el coste lo pone el
  base, y leer los paquetes del equipo de un `.pak` no se nota en la primera carga.
- **Qué pasa tal cual:** 885 PNG indexados se quedan como PNG (14,2 MB): 580 TIM de mapa, 179 texturas de Digimon,
  93 de efectos y 33 texturas sueltas de varias paletas. Se convierten 645: los 513 fondos de mapa, 66 cartas,
  36 texturas sueltas, 28 de efectos, una de Digimon y la fuente. `dwx cook` cuenta 611 copias de PNG porque los
  ficheros repetidos byte a byte se cuentan aparte (403 assets compartidos en total).
- **Tamaño:** UASTC guarda 8 bits por texel más un tercio por los mipmaps, y zstd lo baja poco; un PNG indexado de
  un fondo prerenderizado guarda 8 bits por texel y deflate lo comprime mejor. Por eso las texturas convertidas
  ocupan 1,47 veces su PNG en disco (los fondos, 1,47; el resto, 1,86), aunque 4,8 veces menos que en KTX2 RGBA8.
  En la GPU, BC7 ocupa la cuarta parte que RGBA8, y el PNG indexado no se puede subir sin decodificar.
- **Tiempos** en release, con 12 hilos compartidos con otras compilaciones: la extracción, 15,1 s; `dwx cook` del
  paquete entero sin caché, 285 s (antes de las películas, 222 s; con la compilación de depuración con sanitizers,
  `ci`, 1337 s y los mismos bytes en todos los ficheros); `dwx pack`, 20,7 s el base y 20,2 s el cocinado, relectura
  incluida (2,7 s y 3,3 s sin `movie/`).
- **Comprobación:** `dwx validate` no encuentra ningún problema en la carpeta cocinada (5,8 s) ni en su `.pak`
  (10,1 s): 4813 assets, 2078 documentos, 9271 referencias y 178 contratos, y cada asset se abre con los lectores del
  motor, KTX2 transcodificada a BC7 y Opus decodificado incluidos. `dwe-game --check-content` (compilación `ci`)
  abre los 4813 en la carpeta (27 s) y en el `.pak` (36 s) sin fallos, y `dwe-game --content` monta los dos sin
  avisos.

## Consecuencias

- Las texturas ocupan en disco 4,8 veces menos que el KTX2 sin comprimir pero 1,47 veces más que los PNG indexados de
  los que salen, y en la GPU una cuarta parte de RGBA8. UASTC es casi sin pérdida, y transcodificar a BC7 al cargar
  es rápido. Si el tamaño en disco llega a importar, UASTC tiene un modo RDO que prepara los bloques para que zstd
  los comprima mejor a cambio de algo de calidad; esta decisión lo deja apagado.
- Cocinar es mucho más lento que antes (285 s en release para el paquete entero, películas incluidas), casi todo
  por codificar UASTC. La caché hace que solo se pague una vez por textura.
- Las paletas que se eligen al jugar siguen como PNG: el motor tendrá que dibujarlas con su paleta (un shader de
  búsqueda o variantes compuestas) cuando llegue el render de modelos y mapas.
- Opus es con pérdida: un bucle corto de un instrumento puede notar el salto en el punto de vuelta. Las muestras sin
  pérdida siguen en el SF2 de cada banco. Unos pocos WAV a muy baja frecuencia salen algo más grandes en Opus.
- El port de `ktx` compila Basis Universal con `-msse4.1` en x86-64, así que `dwx` y el motor necesitan una CPU con
  SSE 4.1 (todas las x86-64 desde 2008).
- Cada lectura de un `.pak` comprueba el SHA-1 de la entrada: detecta archivos dañados a cambio de un poco de CPU. Al
  descomprimir, la memoria crece con lo que el frame produce de verdad, no con el tamaño que dice la tabla, así que
  un archivo falsificado no hace reservar 2 GiB.
- Subir la versión de un cocinero invalida su caché; hay que hacerlo cada vez que cambie su salida.
- El paquete `base` sigue instalándose como carpeta suelta: que el lanzador lo cocine a `base.pak` (con avance) y
  borre los ficheros sueltos queda pendiente. Antes hay que leer del `.pak` lo que hoy lee de `content/base` (el fondo
  del lanzador y los PNG que usa `dwx export-images`, que al cocinar pasan a KTX2), convertir las instalaciones que ya
  existen, dar a la instalación desde el propio juego el mismo camino y probar un paquete entero instalado desde cero.
  Hasta entonces el juego empaquetado monta el base de la carpeta.

## Alternativas descartadas

- **KTX2 RGBA8 sin comprimir con un escritor propio** (la primera versión): sin dependencias, pero el paquete
  cocinado pasaba de 344 MB a 1,6 GB y no ahorraba memoria de GPU.
- **Basis Universal ETC1S:** más pequeño en disco, pero con mucha más pérdida, que en texturas de pocos colores y
  bordes duros se notaría. UASTC transcodifica a BC7 casi sin pérdida.
- **BC7 directo en el KTX2:** sin transcodificar al cargar, pero sin camino para las GPU sin BC7 y más grande en disco
  que UASTC con zstd.
- **El port `basisu` en vez de `ktx`:** trae el mismo códec, pero deja el contenedor KTX2 a nuestro cargo en los dos
  lados.
- **`lz4` en el `.pak`:** descomprime aún más rápido, pero comprime menos; zstd descomprime de sobra rápido para
  cargar paquetes.
- **zip:** leerlo y escribirlo sin una librería es casi tan largo como el formato propio, no garantiza la alineación
  de los datos y su índice no está ordenado por id.
- **tar:** no tiene índice; hay que recorrerlo entero para encontrar un fichero.
- **DDS o KTX 1:** KTX2 es el contenedor estándar para Vulkan y el que admite supercompresión Basis.
- **Ver los `.pak` como carpetas dentro de `DiskFileSystem`:** escondería de dónde sale cada fichero; un sistema de
  ficheros por paquete es más claro y no cambia nada de lo que ya montaba carpetas.
