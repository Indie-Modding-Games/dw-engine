# `dwx extract`: el paquete base y el informe de cobertura

`dwx extract` lee el disco del usuario y escribe el **paquete base**: los assets en formato fuente que monta el
motor ([tubería de assets](../architecture/assets-pipeline.md)). Junto a ellos deja un informe de cobertura con lo que
se extrajo de cada fichero del disco y lo que falta. El código está en `apps/dwx`; los lectores, en `formats`.

## Uso

```sh
build/<preset>/apps/dwx/dwx extract [--out <carpeta>] [--force] <imagen.cue|imagen.bin|imagen.iso>
```

- `--out`: dónde se escribe el paquete. Por defecto `content/base`, que git ignora.
- La carpeta de salida tiene que estar vacía o haberla escrito `dwx extract` antes (lleva el fichero oculto
  `.dwx-extract`). En ese caso se vacía primero, para que no queden ficheros de una extracción anterior.
- Una carpeta con otros ficheros se rechaza. Con `--force` se escribe encima **sin borrar nada**; si lo que ya había
  rompe el paquete, la comprobación del final lo dice.
- Nada se escribe fuera de `--out`: las rutas de salida salen solo de ids de asset ya validados (minúsculas, cifras y
  `_`), nunca de nombres leídos del disco.
- Códigos de salida: 0 si el paquete se escribió y el motor lo monta (aunque queden ficheros pendientes), 2 si hubo
  un error.

Con la imagen fuera del repositorio, desde el contenedor:

```sh
DWE_DISC="/ruta/al/disco.bin" scripts/dev.sh test -R dwx   # incluye la prueba con el disco real
```

## Qué hace

1. Abre la imagen y el volumen ISO9660, e identifica la versión por el SHA-1 del ejecutable (como `--list`).
2. Si conoce la versión, lee las **tablas del ejecutable** ([Tablas](tablas.md)) y sus ids de texto
   (`makeTableIds`). Todos los extractores las reciben: los Digimon y los mapas se nombran con esos ids. Sin tablas
   (una versión desconocida) los Digimon llevan números y los mapas quedan parciales; se avisa por la salida de error.
3. Calcula el SHA-1 de cada fichero del volumen y se lo da al primer **extractor** del registro que lo acepta. Los
   ficheros se reparten entre varios hilos (como mucho 16); los resultados se juntan en orden de ruta, así que la
   salida no depende del número de hilos.
4. Prepara la carpeta de salida y escribe el paquete: `pack.yaml`, las salidas, `dwx/coverage.yaml` y `.dwx-extract`.
5. Monta el paquete con el `VirtualFileSystem` del motor (`engine/assets`, sobre `DiskFileSystem`) y comprueba que
   cada salida se resuelve a su fichero. Es la primera prueba de que el motor carga lo extraído.
6. Imprime el resumen: ficheros totales, extraídos, parciales y pendientes, bytes cubiertos y número de salidas.

La salida es **determinista**: dos extracciones del mismo disco dan los mismos bytes en todos los ficheros. No hay
fechas ni rutas absolutas en lo que se escribe.

## Extractores

Un extractor es una entrada del registro (`apps/dwx/src/extractors.cpp`) con un nombre y dos funciones: `handles`,
que dice si acepta un fichero por su ruta en el volumen, y `extract`, que lee el fichero del disco y devuelve sus
salidas (id de asset, extensión y bytes), lo que no entendió (`unknowns`) y notas para quien lea el informe
(`notes`). Gana el primero que acepta el fichero, así que los específicos van antes que los patrones. Recibe el
`Disc`: la fuente de sectores, el volumen (`findFile` busca otro fichero por ruta), la versión y las tablas
(`tables()`, nulo si no se conocen). Como varios ficheros se extraen a la vez, un extractor no guarda estado entre
llamadas y solo lee el disco a través del `Disc` que recibe.

| Extractor | Ficheros | Salida |
|---|---|---|
| `tim_pack` | Los packs de paso fijo conocidos: `CHDAT/ALLTIM.TIM` y `CARD/CARD.ALL` | Un PNG y un YAML por entrada |
| `stacked_tim` | Los TIM apilados: `ETCDAT/ETCTIM.BIN` y `STDDAT/STDTIM.BIN` | Un PNG y un YAML por TIM |
| `tim_file` | Cualquier otro `.TIM`: un TIM, o varios seguidos | Un PNG y un YAML por TIM |
| `executable` | El ejecutable que arranca `SYSTEM.CNF` (`SLUS_010.32` en el US) | Los 12 documentos de sus tablas |
| `boot_config` | `SYSTEM.CNF` | `disc/boot.yaml` |
| `map` | `MAP/MAP<n>/<nombre>.MAP` | `data.yaml` del mapa y sus TIM de 8 y 4 bits |
| `map_background` | `MAP/MAP<n>/<nombre>.TFS` | Un PNG del fondo por paleta |
| `arena_collision` | `STDDAT/<nombre>.ATR` | La rejilla de colisión de la arena |
| `digimon_model` | `CHDAT/MMD<n>/<nombre>.MMD` | Un GLB con esqueleto, animaciones y textura |
| `static_model` | Cualquier `.TMD` | Un GLB con un nodo por objeto, con textura si se sabe cuál |
| `sound_bank` | Los siete `SOUND/VHB/*.VHB` | Un SoundFont y un YAML por banco, un WAV por muestra de efecto y un MIDI por pista de música |

Los formatos están en [TIM y packs](tim.md), [Tablas del ejecutable](tablas.md), [Mapas](mapas.md) y
[Audio](audio.md).

### El ejecutable y `SYSTEM.CNF`

Del ejecutable solo se toman las tablas de datos: el código y el resto de datos no son assets. El fichero queda
`extracted` con notas que lo dicen, la lista de tablas tomadas, las notas de `tablesToYaml` (textos con bytes
escapados, referencias fuera de su tabla) y las entradas de `MAP_ENTRIES` cuyo fichero no está en el disco (el
original nunca carga esos mapas). Si la versión no se conoce, el ejecutable queda parcial.

`SYSTEM.CNF` pasa a un mapping de claves en minúsculas (`boot`, `tcb`, `event`, `stack`) con los valores como texto,
escapados como los de las tablas (una barra invertida se escribe `\\`). Una línea sin `=` es un `unknown`.

### Mapas

Cada `.MAP` se lee con la entrada de `MAP_ENTRIES` que lo nombra, porque esa entrada dice cuántos TIM tiene y si
lleva NPC; la ruta sale del id del mapa como en el original (`map.c: buildMapPath`). Sin entrada, el fichero queda
parcial. Si dos entradas nombran el mismo fichero, se extrae una vez con el id de la primera y la otra se anota.

- `map/<id>/data.yaml`: el documento de `mapToYaml` (cámara, luces, rejilla de tiles, secciones de imagen, sprites,
  warps, NPC y colisión), con los ids de mapas y Digimon de las tablas.
- `map/<id>/texture8/<n>` y `texture4/<n>`: los TIM de las secciones de 8 y 4 bits, numerados desde 0 en cada tipo,
  como cualquier otra imagen (PNG y `_meta.yaml`). Los sprites del mapa no son imágenes aparte: son rectángulos de
  esas texturas y están en `data.yaml`.
- `map/<id>/background_<variante>.png` (del `.TFS`): el fondo compuesto sobre la rejilla, uno por paleta, como PNG
  indexado de 8 bits con su paleta de 256 colores. Las variantes son `day`, `dusk`, `night` y, después,
  `palette3`, `palette4`...
- Son `unknowns`: las paletas de `.TFS` después de la tercera (no se sabe para qué son), los tiles que no caen en la
  rejilla y el bit `0x20` de los flags de la entrada.

### Arenas

Los `.ATR` de `STDDAT` son la rejilla de colisión de 100 × 100 de una arena del torneo, la misma que la de un mapa
(`std_setup.c: STD_loadArenaAssets`, `vs_scene.c: VS_loadArenaAssets`). Se escriben con la misma codificación que la
colisión de `data.yaml`: `width`, `height`, `encoding`, `legend` y `rows`.

### Modelos

Cada modelo es un `.glb` con todo dentro (ver [glTF](modelos.md#gltf)). La textura no está en el fichero del modelo:
cada primitiva lee la VRAM en la página, las `u v` y la paleta que dice. El extractor monta una **escena de VRAM**
(`apps/dwx/src/model_textures.cpp`) con lo que el original ha subido cuando dibuja el modelo, cada TIM en su
rectángulo y lo posterior tapando lo anterior. Para cada primitiva busca la última imagen de su profundidad que
contiene sus texels y la última paleta que hay en su `CBA`, y escribe cada imagen usada con cada paleta usada como
PNG indexado (el color `0x0000` con alfa 0). Las primitivas que no encuentran imagen o paleta conservan sus `u v`
sobre su página, sin imagen, y van a `unknowns`.

- **Digimon** (`digimon_model`): el tipo sale del nombre del fichero y de su carpeta, como en `model.c: loadMMD`
  (`PTR_DIGIMON_FILE_NAMES`, 30 tipos por carpeta `MMD<n>`). El esqueleto es el del tipo en `DIGIMON_SKELETONS`, las
  animaciones son todas las del MMD y la escena es solo la entrada del tipo en `CHDAT/ALLTIM.TIM`, que se lee sin
  leer el pack entero (9 sectores por tipo, `model.c: loadDigimonTexture`). Una animación que no se puede reproducir
  va a `unknowns` y el resto del modelo se escribe igual.
- **Otros modelos** (`static_model`): un nodo por objeto, todos en la raíz, sin animaciones. La escena depende del
  modelo:
  - Puertas (`DOOR/DOOR<nn>.TMD`): los mapas cuya entrada de `DOOR_MAPDATA` incluye la puerta
    (`map_object.c: loadDoors` las carga después de las imágenes del mapa).
  - Los de `ETCDAT`, `ETCHI`, `ETCNA` y `STDDAT` cuyo código está localizado: una tabla en
    `apps/dwx/src/model_extractors.cpp` dice con qué TIM o qué mapas los dibuja el original, con la función del
    decomp que lo hace. `ETCDAT/ETCTIM.BIN` está en VRAM desde el arranque y `STDDAT/STDTIM.BIN` durante los torneos.
  - Los mapas se abren por nombre, como el original (`file.c: lookupFileTable`), y se suben como lo hace
    `overworld.c`: las imágenes de 8 bits con su CLUT salvo si está en la fila 480 (la del fondo) y la fila `i` de la
    CLUT de la primera imagen de 4 bits en `(16 · i, 486)`.
  - Si hay varias escenas, la textura sale de la primera que tiene todos los texels; las que dan otros colores van a
    `unknowns` (el GLB solo lleva una textura). La escena usada va a `notes`.
  - Sin escena, el modelo sale sin imagen y `unknowns` dice por qué: ningún mapa tiene esa puerta, no se pudo leer
    el mapa, el original nunca lo carga, o no se ha localizado el código que lo dibuja.
  - Cuando la escena se dedujo de los scripts que lo colocan y no de código que lo una al modelo, va a `unknowns`
    como `texture inferred`.
  - `ETCDAT/DEFEFF.TMD` y `ETCNA/MEDAL.TMD` cambian de textura mientras se dibujan (`buff_model.c: morphBuffModel`
    alterna la paleta, `map_object.c: activateMedalTexture` copia la medalla). El GLB tiene la textura del fichero y
    el cambio va a `notes`.

## El paquete base

```text
content/base/
├─ pack.yaml            id base, capa base, versión 0.1.0
├─ .dwx-extract         marca de que lo escribió dwx; oculto, el motor no lo ve
├─ dwx/coverage.yaml    informe de cobertura (asset dwx/coverage)
├─ data/<tabla>.yaml    tablas del ejecutable: digimon, skeletons, moves, items, evolution, raise, cards,
│                       tournaments, medals, music, shops, maps
├─ disc/boot.yaml       SYSTEM.CNF
├─ digimon/<id>/texture.png + texture_meta.yaml, model.glb
├─ card/<n>.png + <n>_meta.yaml
├─ map/<id>/data.yaml, background_<variante>.png, texture8/<n>.png, texture4/<n>.png (+ _meta.yaml)
├─ arena/<nombre>/collision.yaml
├─ model/<carpetas>/<nombre>.glb
├─ sound/<banco>/[<entrada>/]bank.sf2, sample_<n>.wav, bank_meta.yaml
├─ music/<fuente>/font.sf2, track_<n>.mid, font_meta.yaml
└─ texture/<carpetas>/<nombre>.png + <nombre>_meta.yaml
```

### Ids de asset

Todos los ids salen de `apps/dwx/src/asset_names.cpp`, para que el esquema se cambie en un solo sitio. Cada fichero
cumple las reglas de `engine/assets`: segmentos de `[a-z0-9_]` y un solo punto en el nombre.

| Origen | Id | Ejemplo |
|---|---|---|
| Asset de un Digimon (`names::digimonAsset`) | `digimon/<id>/<hoja>` | la textura de `ALLTIM.TIM`: `digimon/<id>/texture`; el modelo: `digimon/<id>/model` |
| Otro modelo (`names::discModel`) | `model/<carpetas>/<nombre>` | `DOOR/DOOR05.TMD` → `model/door/door05` |
| Entrada de `CARD.ALL` | `card/<n con 3 cifras>` | `card/065` |
| Documento de las tablas | `data/<tabla>` | `data/moves` |
| `SYSTEM.CNF` | `disc/boot` | |
| Mapa (`names::mapAsset`, `names::mapImage`) | `map/<id>/<hoja>` | `map/<id>/background_day`, `map/<id>/texture8/000` |
| Colisión de arena | `arena/<nombre>/collision` | `STDDAT/B_TOGI.ATR` → `arena/b_togi/collision` |
| Fichero con un TIM | `texture/<carpetas>/<nombre>` | `STDDAT/GRADE/AN.TIM` → `texture/stddat/grade/an` |
| Fichero con varios TIM | `texture/<carpetas>/<nombre>/<n con 3 cifras>` | el tercero de `STDDAT/T_TOGI.TIM` → `texture/stddat/t_togi/002` |
| Banco de sonidos (`names::soundAsset`) | `sound/<banco>/<hoja>`, o `sound/<banco>/<entrada con 3 cifras>/<hoja>` en un fichero empaquetado | `sound/ss/bank`, `sound/vball/003/sample_001` |
| Fuente de música (`names::musicAsset`) | `music/<fuente con 3 cifras>/<hoja>` | `music/001/font`, `music/001/track_00` |
| Metadatos de una imagen | `<id de la imagen>_meta` | `card/065_meta` |

- El id de un Digimon es el de `makeTableIds`, que sale del nombre de fichero de su modelo
  (`PTR_DIGIMON_FILE_NAMES`, cuatro letras en minúsculas). Sin tablas es el tipo con 3 cifras. Todos los assets de un
  Digimon (textura, modelo...) se nombran con `names::digimonAsset`, así que comparten carpeta.
- El id de un mapa es el de `makeTableIds`, que sale del nombre de fichero de `MAP_ENTRIES`; un nombre repetido lleva
  `_<índice>` detrás.
- Las cartas siguen con número: las tablas no les dan nombre. El número es su índice en `data/cards.yaml`.
- Los nombres del disco se pasan a minúsculas y cualquier carácter que no sea letra, cifra o `_` se cambia por `_`.
- Dos salidas con el mismo id, una con el id del manifiesto (`pack`) o una dentro de `dwx/` (la carpeta de la
  herramienta) son un error de la herramienta.

### Imágenes

- **PNG:** indexado de 8 bits con la paleta 0 (`PLTE` y `tRNS`) si el TIM es de 4 u 8 bits con CLUT; RGBA si es de 16
  o 24 bits. Así se conservan los índices y la paleta (ver [TIM y packs](tim.md#png)). Los fondos de mapa son
  indexados con su paleta entera; el color `0x0000` es transparente, como en cualquier TIM.
- **`<id>_meta.yaml`:** `source` (fichero del disco), `entry` (número en un pack, en una lista de varios o entre los
  TIM de su tipo en un mapa), `offset`, `mode` (`indexed4`, `indexed8`, `direct16`, `direct24`), `width` y `height`
  en píxeles, `image_rect` y `clut_rect` (dónde lo sube el original a la VRAM, en unidades de 16 bits), `png`
  (`indexed` o `rgba`), `png_palette` y `palettes`: cada paleta como una cadena de colores de 4 cifras hex separados
  por espacios, en bruto (bit STP incluido). Si algo no se pudo escribir, va en `warnings`.
- **Texturas de Digimon:** el PNG usa la paleta 0, pero cada primitiva del modelo elige su propia fila de la CLUT; el
  extractor de modelos tiene que usar las paletas del YAML para cada parte.

## Informe de cobertura

`dwx/coverage.yaml` tiene cuatro partes:

- `disc`: volumen, ruta y SHA-1 del ejecutable, y versión (vacía si no se conoce).
- `summary`: `files`, `extracted`, `partial`, `pending`, `bytes`, `bytes_covered` (los de ficheros extraídos o
  parciales), `outputs` y `duplicate_outputs` (salidas que repiten byte a byte otra anterior).
- `files`: por cada fichero del volumen, en orden de ruta, `path`, `size`, `sha1`, `status`, y si un extractor lo
  aceptó, `extractor`, `outputs` (rutas dentro del paquete), `unknowns` y `notes`.
- `duplicates`: grupos de salidas con la misma extensión y los mismos bytes, en orden de salida. Se escriben todas;
  el grupo dice cuáles son la misma imagen guardada en varios ficheros del disco.

Los estados:

| Estado | Significado |
|---|---|
| `extracted` | Un extractor lo aceptó y lo entendió entero; las `notes` no cambian el estado |
| `partial` | Un extractor lo aceptó pero dejó algo en `unknowns`: entradas que no se leen, bytes que no son cero y nadie explica, tamaños que no cuadran, partes de uso desconocido, o el error si falló |
| `pending` | Ningún extractor lo acepta todavía |

La fase 1 se cierra cuando no quede ningún fichero pendiente ni parcial.

## Estado con el disco US

Con el disco US (`SLUS_010.32`): 818 ficheros, 765 extraídos, 29 parciales y 24 pendientes; 164,8 de 331,2 MB
(49,8 %). Salen 3808 salidas, de las que 356 repiten otra byte a byte:

| Tipo | Salidas |
|---|---|
| Texturas de Digimon (`digimon/`) | 180 PNG + 180 YAML |
| Cartas (`card/`) | 66 PNG + 66 YAML |
| Otras imágenes (`texture/`) | 69 PNG + 69 YAML |
| Tablas (`data/`) y `SYSTEM.CNF` (`disc/`) | 12 + 1 YAML |
| Mapas: `data.yaml` | 241 |
| Mapas: fondos | 512 PNG |
| Mapas: TIM de 8 y 4 bits | 413 + 167 PNG, con sus YAML |
| Colisión de arenas (`arena/`) | 2 YAML |
| Modelos de Digimon (`digimon/<id>/model`) | 178 GLB |
| Otros modelos (`model/`) | 66 GLB |
| Sonidos (`sound/`): 112 bancos | 112 SF2 + 642 WAV + 112 YAML |
| Música (`music/`): 33 fuentes | 33 SF2 + 74 MIDI + 33 YAML |

- Parciales: los `.TFS` de los tres mapas con más de tres paletas, y `MAP/MAP16/MGEN17.MAP` y su `.TFS`, que ninguna
  entrada de `MAP_ENTRIES` nombra (sin ella no se sabe cuántos TIM tiene el `.MAP`).
- Tres entradas de `MAP_ENTRIES` nombran ficheros que no están en el disco; van como notas del ejecutable. La 65 y la
  232 tienen el mismo nombre en carpetas distintas: solo existe el de la 232, que se extrae con el id de la 232
  (`<nombre>_232`), el que dan las tablas.
- Modelos: los 244 GLB pasan `cgltf_validate` y ocupan 112,6 MB; 231 tienen textura (1215 imágenes) y 13 no. Llevan
  4843 animaciones. Son parciales 24:
  - `CHDAT/MMD1/TANE.MMD`, que tiene primitivas con normales fuera de su objeto.
  - Once puertas sin textura: diez no están en ningún mapa (01-04, 08, 19, 27-30) y la 00 solo está en el mapa 65,
    cuyo fichero no existe en su carpeta (ver abajo).
  - Las puertas 16-18, que `YAKA26` dibuja con otros colores, `ETCDAT/SEA.TMD` (con `FISHING2.TIM`) y
    `ETCNA/WARP.TMD` (cada mapa de cristales tiene los suyos).
  - Seis con la escena deducida (`BOSS_EFE`, `ABOX`, `BIGBOX`, `GAND`, `SMABOX`, `T_YAMA`) y `ETCNA/DOOR.TMD`, que
    el original no carga nunca (es igual que `DOOR00.TMD`).
- Pendientes: los vídeos (`.STR`), los scripts (`.SCN`), los efectos (`.EFE`), los overlays (`*_REL.BIN` y otros
  `.BIN`) y `MMM.DAT`.
- Duplicados: texturas de Digimon que comparten imagen (48 grupos), TIM repetidos entre mapas (62), los nueve TIM de
  `ETCDAT/ETCTIM.BIN`, que también están sueltos en `ETCDAT`, muestras repetidas entre bancos de sonidos (43) y
  pistas repetidas entre fuentes de música (6).
- Audio: los siete `.VHB` se extraen enteros. Los 112 bancos de sonidos tienen 642 muestras (19 con bucle, 13 más
  allá de `vs` en `VBALL`, 16 que ningún tono toca) y 661 valores de sonido; las 33 fuentes, 205 muestras y 74
  pistas (14 suenan una vez, 60 en bucle; 63 están en la lista de música de la tienda). 102 tonos tienen un
  sostenimiento con pendiente, aproximado en el SF2. La entrada 13 de `VLALL` no la usa ningún tipo.
- `STDDAT/T_TOGI.TIM` tiene cuatro TIM seguidos: el original lee las texturas de las arenas con un número de TIM
  fijo (`std_setup.c: STD_loadArenaTIMToVRAM`). Por eso `tim_file` lee siempre una lista.

## Tiempo

En el build `ci` (debug con sanitizers) la extracción del disco US tarda unos 9 s con 12 hilos. Para eso, `sha1.cpp`
se compila optimizado también en debug, los builds de debug enlazan el lodepng de release (salvo en Windows) y el
codificador de PNG no copia los píxeles. La salida es la misma byte a byte. Con los modelos tarda entre 21 y 28 s: cada
modelo de puerta o de mapa lee y sube a su escena los mapas donde aparece.
