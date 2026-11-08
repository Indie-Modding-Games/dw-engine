# `dwx extract`: el paquete base y el informe de cobertura

`dwx extract` lee el disco del usuario y escribe el **paquete base**: los assets en formato fuente que monta el
motor ([tubería de assets](../architecture/assets-pipeline.md)). Junto a ellos deja un informe de cobertura con lo que
se extrajo de cada fichero del disco y lo que falta. El código está en `apps/dwx`; los lectores, en `formats`.

## Uso

```sh
build/<preset>/apps/dwx/dwx extract [--out <carpeta>] [--force] [--progress] <imagen.cue|imagen.bin|imagen.iso>
build/<preset>/apps/dwx/dwx extract --cache <carpeta> <imagen>
```

- `--out`: dónde se escribe el paquete. Por defecto `content/base`, que git ignora.
- La carpeta de salida tiene que estar vacía o haberla escrito `dwx extract` antes (lleva el fichero oculto
  `.dwx-extract`). En ese caso se vacía primero, para que no queden ficheros de una extracción anterior.
- Una carpeta con otros ficheros se rechaza. Con `--force` se escribe encima **sin borrar nada**; si lo que ya había
  rompe el paquete, la comprobación del final lo dice.
- Nada se escribe fuera de `--out`: las rutas de salida salen solo de ids de asset ya validados (minúsculas, cifras y
  `_`), nunca de nombres leídos del disco.
- `--progress` imprime el porcentaje y el paso, para el lanzador; `--cache` extrae en una caché por disco y solo
  imprime la carpeta, para los assets de desarrollo ([página de dwx](../modulos/apps/dwx.md#la-caché-de-desarrollo-dwx-extract---cache)).
- Códigos de salida: 0 si el paquete se escribió, el motor lo monta y abre todos sus assets (aunque queden ficheros
  pendientes), 2 si hubo un error.

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
5. Monta el paquete con el `VirtualFileSystem` del motor (`packages/engine/assets`, sobre `DiskFileSystem`), comprueba que
   cada salida se resuelve a su fichero y abre cada asset del paquete con los lectores del motor
   (`assets::checkContent`, lo mismo que `dwe-game --check-content` y el último paso de
   [`dwx validate`](../architecture/assets-pipeline.md#dwx-validate)). Es la prueba de que el motor carga lo extraído:
   si un asset no se abre, lo lista y termina con error. Los esquemas, las referencias y los contratos no se miran
   aquí; para eso está `dwx validate`.
6. Imprime el resumen: ficheros totales, extraídos, parciales y pendientes, bytes cubiertos y número de salidas.
7. Ejecuta las **recetas incluidas** (`content/enhance/*.recipe.yaml`, metidas en `dwx` al compilarlo): hoy solo
   `ui_original`, que agranda ×4 con `index_scale` en modo `edge` las imágenes de la UI que dibuja el aspecto
   original (iconos, marcos, reloj, barras). Las dos imágenes de la portada no están: el aspecto las suaviza con el
   reescalador de texturas de los modelos (`upscale: smooth` en `skin.yaml`; desde la versión 14). Escribe el paquete `ui_original`, de capa `remaster`, **al
   lado** del paquete base (`--out content/base` da `content/ui_original`) e imprime dónde:
   `Upscaled pack: <carpeta> (5 images grown 4x, 0 not in the base pack)`. Las imágenes guardan su asset id, así que
   tapan las de la base, y su `_meta.yaml` gana `scale: 4`; el aspecto mide sus sprites en píxeles del original y
   los ve más nítidos, del mismo tamaño en pantalla. Una imagen que el disco no trae se salta. Es un escalado de
   pixel art determinista, por reglas, sin modelos ni red: no inventa detalle
   ([tubería del remaster](../architecture/remaster-pipeline.md#lo-que-ya-hay-dwx-upscale)). Para volver a hacerlo a
   mano con otra receta está `dwx upscale` ([página de dwx](../modulos/apps/dwx.md#dwx-upscale-y-las-recetas-incluidas)).

   **Para el jugador:** el juego se ve como el original por defecto (Decidido por el usuario (2026-10-21)), y ese
   aspecto dibuja sus iconos con estas imágenes. Un contenido extraído con una versión anterior de `dwx` (sin el
   paquete `ui_original`) sigue valiendo, pero el juego empieza con el aspecto moderno y lo dice en el log: hay que
   extraer otra vez para ver el original. Con el contenido nuevo, el moderno sigue en «Aspecto de la interfaz»
   (menú principal u opciones del menú de pausa), y quien ya lo había elegido lo conserva.

La salida es **determinista**: dos extracciones del mismo disco dan los mismos bytes en todos los ficheros, también
los del paquete `ui_original`. No hay
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
| `executable` | El ejecutable que arranca `SYSTEM.CNF` (`SLUS_010.32` en el US) | Los 15 documentos de sus tablas, las cadenas suyas y de los overlays y la fuente |
| `boot_config` | `SYSTEM.CNF` | `disc/boot.yaml` |
| `map` | `MAP/MAP<n>/<nombre>.MAP` | `data.yaml` del mapa y sus TIM de 8 y 4 bits |
| `map_background` | `MAP/MAP<n>/<nombre>.TFS` | Un PNG del fondo por paleta |
| `arena_collision` | `STDDAT/<nombre>.ATR` | La rejilla de colisión de la arena |
| `digimon_model` | `CHDAT/MMD<n>/<nombre>.MMD` | Un GLB con esqueleto, animaciones y textura |
| `static_model` | Cualquier `.TMD` | Un GLB con un nodo por objeto, con textura si se sabe cuál |
| `sound_bank` | Los siete `SOUND/VHB/*.VHB` | Un SoundFont y un YAML por banco, un WAV por muestra de efecto y un MIDI por pista de música |
| `event_script` | `SCN/MAPHEAD.SCN` y `SCN/DG.SCN` | Un listado YAML por script, su módulo Lua (`story/<n>` y `story/<n>_lines`) y sus textos; de todos juntos, `data/new_game_stats`, `data/new_game_story` y `field/script_warps` ([Scripts](scripts.md)) |
| `overlay` | `<nombre>_REL.BIN` en la raíz | Los documentos de sus tablas de juego (FISH, STD, VS, TRN y BTL) |
| `padding` | `MMM.DAT` | Nada: comprueba que es todo ceros |
| `effect` | `ETCHI/EFEDAT.EFE` | Por técnica: `effect.yaml`, `script.yaml`, la textura y, si tiene TMD, un GLB ([Efectos](efectos.md)) |
| `movie` | `MOVIE/*.STR` (hace falta una imagen en bruto) | `video.y4m`, `audio.wav` y `movie.yaml` (ver [Vídeo](video.md)) |

Los formatos están en [TIM y packs](tim.md), [Tablas del ejecutable](tablas.md), [Mapas](mapas.md),
[Overlays](overlays.md), [Audio](audio.md), [Textos y fuente](textos.md), [Scripts de eventos](scripts.md),
[Efectos](efectos.md) y [Vídeo](video.md).

### El ejecutable y `SYSTEM.CNF`

Del ejecutable solo se toman las tablas de datos: el código y el resto de datos no son assets. El fichero queda
`extracted` con notas que lo dicen, la lista de tablas tomadas, las notas de `tablesToYaml` (textos con bytes
escapados, referencias fuera de su tabla) y las entradas de `MAP_ENTRIES` cuyo fichero no está en el disco (el
original nunca carga esos mapas). Si la versión no se conoce, el ejecutable queda parcial.

El mismo extractor escribe las cadenas del ejecutable y de los overlays que lista el manifiesto de la versión
(`text/en/<fuente>.yaml`) y la fuente (`font/main.png` y `font/main_meta.yaml`); los overlays solo se leen para eso y
siguen teniendo su propia cobertura. Si la versión no tiene manifiesto ni fuente localizada, una nota lo dice.

`SYSTEM.CNF` pasa a un mapping de claves en minúsculas (`boot`, `tcb`, `event`, `stack`) con los valores como texto,
escapados como los de las tablas (una barra invertida se escribe `\\`). Una línea sin `=` es un `unknown`.

### Mapas

Cada `.MAP` se lee con la entrada de `MAP_ENTRIES` que lo nombra, porque esa entrada dice cuántos TIM tiene y si
lleva NPC; la ruta sale del id del mapa como en el original (`map.c: buildMapPath`). Si dos entradas nombran el mismo
fichero, se extrae una vez con el id de la primera y la otra se anota. Un fichero sin entrada no lo carga nunca el
juego; se extrae con su nombre como id y las opciones que da el propio fichero (`inferMapReadOptions`, ver
[Mapas](mapas.md)), con una nota. Si el fichero no las decide, o su nombre ya es el id de una entrada, queda parcial.

- `map/<id>/data.yaml`: el documento de `mapToYaml` (cámara, luces, rejilla de tiles, secciones de imagen, sprites,
  warps, NPC y colisión), con los ids de mapas y Digimon de las tablas.
- `map/<id>/texture8/<n>` y `texture4/<n>`: los TIM de las secciones de 8 y 4 bits, numerados desde 0 en cada tipo,
  como cualquier otra imagen (PNG y `_meta.yaml`). Los sprites del mapa no son imágenes aparte: son rectángulos de
  esas texturas y están en `data.yaml`.
- `map/<id>/background_<variante>.png` (del `.TFS`): el fondo compuesto sobre la rejilla, uno por paleta, como PNG
  indexado de 8 bits con su paleta de 256 colores. Las variantes son `day`, `dusk`, `night` y, después,
  `palette3`, `palette4`...
- Las paletas de `.TFS` después de la tercera se escriben igual. Si ningún sprite del mapa lee su fila de VRAM, una
  nota dice que nunca se ven; si alguno la lee, es un `unknown`, porque un TIM del `.MAP` puede poner su CLUT en la
  misma fila.
- Son `unknowns`: los tiles que no caen en la rejilla y el bit `0x20` de los flags de la entrada.

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
- Junto a cada modelo de Digimon va su **contrato**, `contract.yaml`: el fichero del modelo, el Digimon cuyo esqueleto
  tiene, el padre de cada hueso (de `DIGIMON_SKELETONS`) y en `animations` el nombre (`anim_<nn>`) de cada animación
  del MMD que está en el modelo. Las que no se pudieron reproducir no están en el `.glb`, así que van aparte, en
  `unconverted_animations` (solo si hay alguna), además de a `unknowns`: el contrato sigue diciendo que el juego las
  tiene sin que el modelo extraído lo incumpla. [`dwx validate`](../architecture/assets-pipeline.md#dwx-validate)
  comprueba que el modelo, el original o el de un mod, cumple `animations` y no pide las de
  `unconverted_animations`.
- **Otros modelos** (`static_model`): un nodo por objeto, todos en la raíz, sin animaciones. La escena depende del
  modelo:
  - Puertas (`DOOR/DOOR<nn>.TMD`): los mapas cuya entrada de `DOOR_MAPDATA` incluye la puerta
    (`map_object.c: loadDoors` las carga después de las imágenes del mapa).
  - Los de `ETCDAT`, `ETCHI`, `ETCNA` y `STDDAT` cuyo código está localizado: una tabla en
    `apps/dwx/src/model_extractors.cpp` dice con qué TIM, qué mapas o qué rutina de los scripts los dibuja el
    original, con la función del decomp que lo hace. `ETCDAT/ETCTIM.BIN` está en VRAM desde el arranque y
    `STDDAT/STDTIM.BIN` durante los torneos.
  - Los que pone un script (`BOSS_EFE`, `T_YAMA`, `GAND`, `BIGBOX`, `SMABOX` y `ABOX`) los carga una rutina de
    `CALL_ROUTINE` (`script_interp.c: scriptInstruction64to7E`) que no sube ninguna imagen, así que leen las del
    mapa. El extractor desensambla los scripts del disco (`apps/dwx/src/script_maps.cpp`) y busca los mapas donde se
    llama a la rutina: los que `SET_SCRIPT` carga con el script que la llama, o con uno que salta a él con
    `JUMP_TO_FILE` o `JUMP_TO_FILE_AND_LINK`. Cada mapa es una escena y va a `notes`.
  - Los mapas se abren por nombre, como el original (`file.c: lookupFileTable`), y se suben como lo hace
    `overworld.c`: las imágenes de 8 bits con su CLUT salvo si está en la fila 480 (la del fondo) y la fila `i` de la
    CLUT de la primera imagen de 4 bits en `(16 · i, 486)`.
  - Las escenas que tienen todos los texels se agrupan por los colores que dan, tal como los guarda el PNG (sin el
    bit de semitransparencia de los colores opacos). El primer grupo texturiza el modelo y cada uno de los demás da
    una **variante**: el mismo modelo con los texels de esa escena, con el id del modelo y
    el slug de su primera escena (`model/door/door16_yaka26`, `model/etcdat/sea_fishing2`). Las escenas de cada grupo
    van a `notes`. Una escena a la que le faltan texels va a `unknowns`.
  - Un mapa sin imágenes en su entrada no sube ninguna: allí el modelo enseña lo que dejó en VRAM el mapa anterior.
    No es una escena y va a `notes`.
  - Sin escena, el modelo sale sin imagen, con las `u v` sobre su página. Va a `notes` si se sabe por qué: ningún
    mapa tiene esa puerta en sus datos (el original no la dibuja nunca), el original nunca carga el fichero, o
    ninguno de sus mapas sube imágenes. Va a `unknowns` si no se pudo leer una escena o si no se ha localizado el
    código que lo dibuja.
  - `ETCDAT/DEFEFF.TMD`, `ETCNA/MEDAL.TMD` y `ETCHI/BOSS_EFE.TMD` cambian mientras se dibujan
    (`buff_model.c: morphBuffModel` alterna la paleta, `map_object.c: activateMedalTexture` copia la medalla,
    `eab.c: EAB_setModelColor` cambia el color de las primitivas). El GLB tiene lo del fichero y el cambio va a
    `notes`.

### Relleno

`MMM.DAT` (27,6 MB) solo aparece en la tabla de ficheros del ejecutable; ningún código lo abre. El extractor `padding`
comprueba que todos sus bytes son cero: si lo son, queda `extracted` sin salidas y con una nota; si no, queda parcial y
el `unknown` lista los rangos que no son cero.

### Overlays

Cada `*_REL.BIN` se corta con el layout de su versión (ver [Overlays](overlays.md)) y cada parte va a una nota con su
rango de bytes: los ficheros incrustados de BTL, STD y VS (comparados con el fichero suelto: `same bytes as
ETCDAT/EX.TIM`; el asset sale de ese fichero), la cabecera, el código (no es un asset), los datos (constantes del
código con nombre en los símbolos del decomp), la memoria guardada (restos del build; en MOV, los buffers del
decodificador) y la palabra final. Los datos de SHOP, que el decomp no compila para el US, van rango a rango con el
mapa de su layout (tablas de saltos, textos, punteros y tablas), y los que el ejecutable también tiene se comparan con
su copia. Las tablas de juego salen a `data/fishing`, `data/stadium`, `data/versus`,
`data/training` y `data/battle`; el número de objetos y TIM de las arenas se lee del ejecutable. TRN2 tiene las mismas tablas que TRN:
se comparan y se anota.

Queda parcial un overlay sin layout para la versión, uno que no cuadra con su layout, uno cuyo fichero incrustado no
es igual al suelto, uno cuyas tablas no se pueden leer (o sin las tablas del ejecutable) y uno cuyos datos no cuadran con
su mapa.

## El paquete base

```text
content/base/
├─ pack.yaml            id base, capa base, versión 0.1.0 y el esquema de cada tipo de documento
├─ .dwx-extract         marca de que lo escribió dwx; oculto, el motor no lo ve
├─ dwx/coverage.yaml    informe de cobertura (asset dwx/coverage)
├─ data/<tabla>.yaml    tablas del ejecutable: digimon, skeletons, moves, items, item_effects, evolution, raise,
│                       evolution_chart, evolution_chart_detail, cards, tournaments, medals, music, shops, maps;
│                       de los overlays: fishing, stadium, versus, training, battle; de los scripts:
│                       new_game_stats, new_game_story
├─ disc/boot.yaml       SYSTEM.CNF
├─ field/script_warps.yaml  los cambios de mapa de los scripts
├─ script/maphead.yaml, script/dg/<n>.yaml  listados de los scripts
├─ story/<n>.lua + <n>_lines.yaml  los scripts como módulos Lua
├─ text/en/<fuente>.yaml, font/main.png + main_meta.yaml
├─ digimon/<id>/texture.png + texture_meta.yaml, model.glb + contract.yaml
├─ card/<n>.png + <n>_meta.yaml
├─ map/<id>/data.yaml, background_<variante>.png, texture8/<n>.png, texture4/<n>.png (+ _meta.yaml)
├─ arena/<nombre>/collision.yaml
├─ model/<carpetas>/<nombre>.glb
├─ sound/<banco>/[<entrada>/]bank.sf2, sample_<n>.wav, bank_meta.yaml
├─ music/<fuente>/font.sf2, track_<n>.mid, font_meta.yaml
├─ effect/<técnica>/effect.yaml, script.yaml, texture.png + texture_meta.yaml, model.glb
├─ texture/<carpetas>/<nombre>.png + <nombre>_meta.yaml
└─ movie/<nombre>/video.y4m, audio.wav, movie.yaml
```

### Esquemas

Cada documento YAML que escribe el extractor tiene su JSON Schema en `content/schemas/`, y el `pack.yaml` de la base
dice cuál sigue cada uno ([reglas `schemas`](../architecture/vfs.md#esquemas-de-los-documentos)):

| Esquema | Documentos |
|---|---|
| `battle`, `cards`, `digimon`, `evolution`, `evolution_chart`, `evolution_chart_detail`, `fishing`, `item_effects`, `items`, `maps`, `medals`, `moves`, `music`, `new_game_stats`, `new_game_story`, `raise`, `shops`, `skeletons`, `stadium`, `tournaments`, `training`, `versus` | `data/<el mismo nombre>` |
| `boot` | `disc/boot` |
| `script_warps` | `field/script_warps` |
| `map` | `map/*/data` |
| `collision` | `arena/*/collision` |
| `image` | los `_meta` de las imágenes: `map/*/texture8/*_meta`, `map/*/texture4/*_meta`, `digimon/*/texture_meta`, `card/*_meta`, `effect/*/texture_meta`, `texture/**_meta` |
| `model_contract` | `digimon/*/contract` |
| `sound_bank` | `sound/*/bank_meta`, `sound/*/*/bank_meta`, `music/*/font_meta` |
| `script` | `script/maphead`, `script/*/*` |
| `story_lines` | `story/*_lines` |
| `text` | `text/*/*` |
| `font` | `font/main_meta` |
| `effect` | `effect/*/effect` |
| `effect_script` | `effect/*/script` |
| `movie` | `movie/*/movie` |
| `coverage` | `dwx/coverage` |

Las reglas salen de `names::documentSchemas` (`apps/dwx/src/asset_names.cpp`). Un tipo de documento nuevo necesita
esquema y regla: sin ellos, `dwx validate` falla en el test del disco.

### Ids de asset

Todos los ids salen de `apps/dwx/src/asset_names.cpp`, para que el esquema se cambie en un solo sitio. Cada fichero
cumple las reglas de `packages/engine/assets`: segmentos de `[a-z0-9_]` y un solo punto en el nombre.

| Origen | Id | Ejemplo |
|---|---|---|
| Asset de un Digimon (`names::digimonAsset`) | `digimon/<id>/<hoja>` | la textura de `ALLTIM.TIM`: `digimon/<id>/texture`; el modelo: `digimon/<id>/model` |
| Otro modelo (`names::discModel`) | `model/<carpetas>/<nombre>` | `DOOR/DOOR05.TMD` → `model/door/door05` |
| Variante de otro modelo | `model/<carpetas>/<nombre>_<escena>` | `DOOR/DOOR16.TMD` en `YAKA26` → `model/door/door16_yaka26` |
| Entrada de `CARD.ALL` | `card/<n con 3 cifras>` | `card/065` |
| Documento de las tablas | `data/<tabla>` | `data/moves` |
| `SYSTEM.CNF` | `disc/boot` | |
| Mapa (`names::mapAsset`, `names::mapImage`) | `map/<id>/<hoja>` | `map/<id>/background_day`, `map/<id>/texture8/000` |
| Colisión de arena | `arena/<nombre>/collision` | `STDDAT/B_TOGI.ATR` → `arena/b_togi/collision` |
| Fichero con un TIM | `texture/<carpetas>/<nombre>` | `STDDAT/GRADE/AN.TIM` → `texture/stddat/grade/an` |
| Fichero con varios TIM | `texture/<carpetas>/<nombre>/<n con 3 cifras>` | el tercero de `STDDAT/T_TOGI.TIM` → `texture/stddat/t_togi/002` |
| Banco de sonidos (`names::soundAsset`) | `sound/<banco>/<hoja>`, o `sound/<banco>/<entrada con 3 cifras>/<hoja>` en un fichero empaquetado | `sound/ss/bank`, `sound/vball/003/sample_001` |
| Fuente de música (`names::musicAsset`) | `music/<fuente con 3 cifras>/<hoja>` | `music/001/font`, `music/001/track_00` |
| Efecto de una técnica (`names::effectAsset`) | `effect/<id de la técnica>/<hoja>`, o la ranura con 3 cifras sin tablas | `effect/<id>/script`, `effect/<id>/model` |
| Película (`names::movieAsset`) | `movie/<nombre>/<hoja>` | `MOVIE/OP1.STR` → `movie/op1/video` |
| Listado de un script (`names::scriptListing`) | `script/maphead`, `script/dg/<n con 3 cifras>` | `script/dg/001` |
| Script como Lua (`names::storyModule`, `names::storyLines`) | `story/<n con 3 cifras>`, `story/<n>_lines` | `story/001`, `story/001_lines` |
| Metadatos de una imagen | `<id de la imagen>_meta` | `card/065_meta` |
| Contrato del modelo de un Digimon (`names::modelContract`) | `digimon/<id>/contract` | `digimon/species_a/contract` |

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
  por espacios, en bruto (bit STP incluido). Los avisos del lector van en `warnings`. Un TIM que no se puede
  convertir a PNG no da ni el PNG ni el `_meta`, porque el `_meta` describe un PNG: el motivo va a `unknowns`.
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
  el grupo dice cuáles son la misma imagen guardada en varios ficheros del disco. Las salidas que se escriben por
  partes (el vídeo de las películas, `OutputFile::stream`) no se comparan.

Los estados:

| Estado | Significado |
|---|---|
| `extracted` | Un extractor lo aceptó y lo entendió entero; las `notes` no cambian el estado |
| `partial` | Un extractor lo aceptó pero dejó algo en `unknowns`: entradas que no se leen, bytes que no son cero y nadie explica, tamaños que no cuadran, partes de uso desconocido, o el error si falló |
| `pending` | Ningún extractor lo acepta todavía |

La fase 1 se cerró cuando no quedó ningún fichero pendiente ni parcial (ver abajo).

## Estado con el disco US

Con el disco US (`SLUS_010.32`): 818 ficheros, 818 extraídos, ninguno parcial y ninguno pendiente; 331,2 de 331,2 MB
(100 %). Salen 5475 salidas, de las que 404 repiten otra byte a byte, y dos extracciones seguidas dan los mismos
bytes en todas:

| Tipo | Salidas |
|---|---|
| Texturas de Digimon (`digimon/`) | 180 PNG + 180 YAML |
| Cartas (`card/`) | 66 PNG + 66 YAML |
| Otras imágenes (`texture/`) | 69 PNG + 69 YAML |
| Tablas del ejecutable y de los overlays (`data/`) y `SYSTEM.CNF` (`disc/`) | 15 + 5 + 1 YAML |
| Mapas: `data.yaml` | 242 |
| Mapas: fondos | 513 PNG |
| Mapas: TIM de 8 y 4 bits | 413 + 167 PNG, con sus YAML |
| Colisión de arenas (`arena/`) | 2 YAML |
| Modelos de Digimon (`digimon/<id>/model`) | 178 GLB + 178 contratos YAML |
| Otros modelos (`model/`) | 66 GLB + 4 variantes |
| Sonidos (`sound/`): 112 bancos | 112 SF2 + 642 WAV + 112 YAML |
| Música (`music/`): 33 fuentes | 33 SF2 + 74 MIDI + 33 YAML |
| Efectos (`effect/`): 121 técnicas | 363 YAML, 121 PNG, 89 GLB |
| Películas (`movie/`) | 4 Y4M + 4 WAV + 4 YAML |
| Scripts de eventos (`script/`, `story/`) | 222 YAML; 222 Lua + 222 YAML de líneas; 3 documentos (`data/`, `field/`) |
| Textos (`text/`) y fuente (`font/`) | 219 YAML (7 del ejecutable y los overlays, 212 de los scripts); 1 PNG + 1 YAML |

- Efectos: `ETCHI/EFEDAT.EFE` queda extraído; sus 40 rangos de datos a los que nada llega (590 bytes en 33 ranuras)
  van como `unused` en cada `script.yaml` y en las notas ([Efectos](efectos.md#datos-sin-uso)).
- Overlays: los 16 extraídos. `SHOP_REL.BIN` es el build `jp_rev1`, byte a byte; sus datos van rango a rango con el
  mapa de [Overlays](overlays.md#los-datos-de-shop).
- `MAP/MAP16/MGEN17.MAP`, que ninguna entrada de `MAP_ENTRIES` nombra, se extrae como `map/mgen17` con las opciones
  que da el propio fichero.
- Las paletas de `.TFS` después de la tercera (tres mapas) nunca se ven: ningún sprite lee su fila de VRAM.
- Tres entradas de `MAP_ENTRIES` nombran ficheros que no están en el disco; van como notas del ejecutable. La 65 y la
  232 tienen el mismo nombre en carpetas distintas: solo existe el de la 232, que se extrae con el id de la 232
  (`<nombre>_232`), el que dan las tablas.
- Modelos: los 248 GLB pasan `cgltf_validate` y ocupan 112,8 MB; 235 tienen textura (1219 imágenes) y 13 no. Llevan
  4843 animaciones. Los 244 ficheros se extraen enteros:
  - Variantes: `ETCDAT/SEA.TMD` con `FISHING2.TIM` y las puertas 16-18 en `YAKA26`. `ETCDAT/SAO.TMD` se ve igual
    con los dos TIM de la pesca, y `ETCNA/WARP.TMD` igual en `ICSA05`, `ICSA06` e `ICSA07`: los dos últimos solo
    cambian el bit de semitransparencia.
  - Los seis que pone un script salen de un solo mapa cada uno: `BOSS_EFE` del 225, `T_YAMA` del 28, `GAND` del
    146, `BIGBOX` y `SMABOX` del 151 y `ABOX` del 97.
  - Sin textura: diez puertas que no están en los datos de puertas de ningún mapa (01-04, 08, 19, 27-30), la 00, que
    solo está en el mapa 65, cuya entrada no tiene imágenes (ver arriba), `ETCNA/DOOR.TMD`, que el original no carga
    nunca (es igual que `DOOR00.TMD`), y `ETCDAT/FEEL_EF.TMD`, que no tiene primitivas con textura.
  - `CHDAT/MMD1/TANE.MMD` usa normales de otros objetos, como la consola (ver
    [Normales fuera del objeto](modelos.md#normales-fuera-del-objeto)).
- Pendientes: ninguno.
- Duplicados: texturas de Digimon que comparten imagen (48 grupos), TIM repetidos entre mapas (62), los nueve TIM de
  `ETCDAT/ETCTIM.BIN`, que también están sueltos en `ETCDAT`, muestras repetidas entre bancos de sonidos (43),
  pistas repetidas entre fuentes de música (6) y texturas, modelos y un script que comparten varias técnicas (47).
- Audio: los siete `.VHB` se extraen enteros. Los 112 bancos de sonidos tienen 642 muestras (19 con bucle, 13 más
  allá de `vs` en `VBALL`, 16 que ningún tono toca) y 661 valores de sonido; las 33 fuentes, 205 muestras y 74
  pistas (14 suenan una vez, 60 en bucle; 63 están en la lista de música de la tienda). 102 tonos tienen un
  sostenimiento con pendiente, aproximado en el SF2. La entrada 13 de `VLALL` no la usa ningún tipo.
- `STDDAT/T_TOGI.TIM` tiene cuatro TIM seguidos: el original lee las texturas de las arenas con un número de TIM
  fijo (`std_setup.c: STD_loadArenaTIMToVRAM`). Por eso `tim_file` lee siempre una lista.

## Tiempo

En el build `ci` (debug con sanitizers), con 12 hilos, la extracción del disco US tardó 2 min 40 s (medido el
2026-10-19 con el host ocupado por compilaciones de otras ramas: carga media de 9 a 10 en 12 núcleos). Unos 13 s de eso
son la comprobación del final, que abre los 4813 assets del paquete repartidos entre hilos. En las mismas condiciones,
sin las películas, tardaba unos 40 s. Para eso, `sha1.cpp` se compila optimizado también en debug, los builds de debug
enlazan el lodepng de release (salvo en Windows) y el codificador de PNG no copia los píxeles. La salida es la misma
byte a byte. Sin películas, lo más lento son los modelos: cada modelo de puerta o de mapa lee y sube a su escena los
mapas donde aparece, y cada uno de los seis modelos que pone un script desensambla todos los scripts para buscar sus
mapas.

Las películas son la mayor parte del tiempo: al extraer, cada uno de los 13 089 frames solo pasa por la etapa VLC
(`decodeBitstream`, para comprobar su bitstream); la IDCT corre una sola vez, al escribir el Y4M, y se escriben 1,1 GB.
Los bitstreams de las películas y sus WAV (unos 200 MB) se quedan en memoria hasta que se escribe el paquete.
`packages/formats/src/video/mdec.cpp` se compila optimizado también en debug, como `sha1.cpp`; sin eso eran casi 9 min.

Con `dwx` compilado en release (`linux-clang-release`), la extracción entera tarda unos 15 s. Medidas del cierre de la
fase 1 (2026-10-20, host compartido con otras compilaciones): 2 min 40 s a 3 min en `ci`, 15 s en release.
