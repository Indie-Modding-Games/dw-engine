# Formatos del original: la librería `formats` y `dwx`

Plan de la fase 1 ([roadmap](../roadmap.md)). Describe qué tiene que leer `formats/`, cómo se organiza y qué hace cada
comando de `apps/dwx`. Lo que sabemos de cada formato sale del decomp (`dw_decomp`); se cita el fichero y la función
que lo lee para poder comprobarlo. Este documento no contiene datos del juego, solo estructuras.

Las reglas que no cambian ([dependencias](dependency-rules.md), [tubería de assets](assets-pipeline.md),
[política legal](../legal/assets.md)):

- `formats` solo depende de `core`, y solo lo usa `apps/dwx`. El runtime nunca lee un formato de PS1.
- La extracción va en un sentido: original → fuente. No se reconstruye el disco.
- Ningún fichero del disco ni nada derivado de él entra en un repositorio. Los tests usan fixtures hechos por el equipo
  (`content/fixtures/`), nunca recortes del disco.

## Versión del disco

- Se empieza por el disco **US** (`SLUS_010.32`), que es la versión por defecto del decomp. EU y JP vienen después: el
  decomp tiene una tabla de ficheros por versión.
- `dwx extract` identifica la versión por el SHA-1 de la imagen y de su ejecutable. El decomp solo tiene SHA-1 del
  ejecutable y de los overlays (`config/<ver>/*.yaml`); la lista de SHA-1 de cada fichero de datos la generamos
  nosotros la primera vez, desde un disco verificado, y se guarda en `formats/` como tabla (solo hashes y nombres).
- Ya existe la primera pieza: `dwx verify <fichero>` calcula el SHA-1 de un fichero y lo busca en la tabla de
  `formats/disc` (`known_releases.cpp`). Solo se soportan las versiones comerciales: **US, EU y JP** (la original y la revisión 1). Las
  demás versiones del decomp (la demo y `jp_bombom`) quedan fuera. Hoy la tabla tiene esos cuatro ejecutables; **faltan
  los hashes de las imágenes completas** (pistas de Redump), que se añaden desde la ficha de Redump o desde un volcado
  verificado, nunca a ojo.

## Organización de `formats/`

```
formats/
├─ include/dwe/formats/
│  ├─ disc/        imagen (BIN/CUE, ISO), ISO9660, tabla de ficheros, lectura por sectores
│  ├─ image/       TIM, packs de TIM
│  ├─ model/       TMD, MMD, animaciones, esqueletos
│  ├─ map/         MAP, TFS, colisión
│  ├─ text/        Shift-JIS + ASCII del US, códigos de control, fuente
│  ├─ script/      contenedor SCN, opcodes; EFE
│  ├─ audio/       VHB (VH+VB), bancos empaquetados, SEQ
│  ├─ video/       STR (MDEC + XA)
│  ├─ exe/         PS-X EXE, tablas del ejecutable, overlays
│  └─ save/        partida de la tarjeta de memoria
├─ src/
└─ tests/
```

Principios:

- **Lectores puros.** Cada lector recibe un `std::span<const std::byte>` y devuelve una estructura propia o un
  `core::Result` con el error. No abren ficheros ni conocen el disco: así se prueban con fixtures pequeños.
- **Sin confiar en los datos.** Todos los offsets y contadores se comprueban contra el tamaño del buffer. Un fichero
  malformado da un error, nunca una lectura fuera de rango.
- **Little-endian explícito.** Lectura por campos con un `ByteReader`, nunca `reinterpret_cast` de structs.
- **Nombres del decomp.** Los campos se llaman como en el decomp; los desconocidos, `unknownXX` con su offset.

## El disco

| Qué | Cómo es | Dónde lo lee el original |
|---|---|---|
| Imagen | BIN/CUE con sectores de 2352 bytes (modo 2) o ISO de 2048 | — |
| Sistema de ficheros | ISO9660 estándar. Carpetas `CARD`, `CHDAT`, `DOOR`, `ETCDAT`, `ETCHI`, `ETCNA`, `MAP/MAP1..17`, `MOVIE`, `SCN`, `SOUND/VHB`, `STDDAT`, y en la raíz los overlays `*_REL.BIN`, `MMM.DAT` y el ejecutable | — |
| Tabla de ficheros | `FILE_OFFSET_TABLE[819]` en el ejecutable (US: 818 ficheros y la entrada final): `{s32 sector LBA, s32 tamaño, char* nombre}` en orden de disco; termina en tamaño 0. Se busca por el nombre sin carpeta. Los STR cuentan 2336 bytes por sector y `PSX.EXE` es otro nombre del ejecutable | `src/main/file.c: lookupFileTable, readFile, readFileSectors` |
| Ficheros que se abren por ruta | `SCN\DG.SCN`, `ETCHI\EFEDAT.EFE`, `ETCDAT\FEEL_EF.TMD`, `ETCDAT\EX.TIM`, `ETCDAT\EX.TMD` y los STR se buscan con `CdSearchFile`; en el US también están en la tabla | `script_interp.c`, `efe.c`, `efe_table.c`, `mov.c` |

`formats/disc` lee el ISO9660 y además valida que la tabla del ejecutable coincide con él (detalle en
[Disco](../formats/disco.md)). Los vídeos STR se leen en
modo 2 forma 2 (2336 bytes por sector), así que la imagen tiene que ser BIN, no ISO.

**Packs sin índice.** Muchos ficheros son una concatenación de entradas a un **paso fijo de sectores**, sin cabecera
ni compresión. El paso está en el código del original, no en el fichero, así que `formats` lo declara en una tabla:

| Fichero | Paso | Entradas | Índice |
|---|---|---|---|
| `CHDAT\ALLTIM.TIM` | 9 sectores (0x4800) | 180 | tipo de Digimon |
| `CARD\CARD.ALL` | 14 sectores (0x7000) | 66 | id de carta |
| `SOUND\VHB\VBALL.VHB` | 7 sectores | | `DIGIMON_VBALL_SOUND_ID[]` |
| `SOUND\VHB\VLALL.VHB` | 15 sectores | 66 | `DIGIMON_VLALL_SOUND_ID[66]` |
| `SOUND\VHB\FAALL.VHB` | 0x27 sectores | 33 | fuente de música |
| `SOUND\VHB\ESALL.VHB` | variable | 22 | `MAP_SOUND_PARA[22] {u16 sector, u16 sectores}`, ambos a la mitad |
| `ETCHI\EFEDAT.EFE` | 10 sectores | por técnica | id de técnica |

## Imágenes

- **TIM:** el formato estándar de PsyQ (4/8/16 bits, con o sin CLUT). Se extrae a PNG con la paleta como metadato
  ([tubería](assets-pipeline.md)).
- **TIM apilados** (`ETCDAT\ETCTIM.BIN`, `STDDAT\STDTIM.BIN`): TIM seguidos, cada uno con su magia `0x10`; la lista
  termina en la primera palabra que no es `0x10`. El original calcula el tamaño de cada bloque con ancho × alto, no con
  el campo de longitud: hacemos lo mismo y avisamos si no coinciden (`utils2.c: loadStackedTIMFile`).
- **ALLTIM y CARD.ALL:** packs de paso fijo (tabla anterior) con un TIM al principio de cada entrada. En ALLTIM el
  original ignora la posición de VRAM del TIM y usa la del modelo (`model.c: uploadModelTexture`). Detalle en
  [TIM y packs](../formats/tim.md).

## Modelos y animaciones

- **TMD:** estándar de PsyQ (`TMDModel {id, flags, nobj, obj[]}`, `include/dw/model.h`). El original reescribe la
  página de textura y la CLUT de cada primitiva al cargar un MMD (`updateTMDTextureData`) porque sube su textura a
  otro sitio de la VRAM. Los ficheros ya apuntan a la posición propia del TIM en `ALLTIM.TIM`, así que el extractor no
  corrige nada: las UV son coordenadas del TIM y la fila de paleta sale de la CBA de cada primitiva
  ([Modelos](../formats/modelos.md#mmd)).
- **MMD** (`CHDAT\MMDn\XXXX.MMD`, 180): `u32 offsetTmd; u32 offsetAnimaciones;` + TMD + tabla de animaciones. La
  carpeta es `tipo / 30` y el nombre de 4 letras sale de `PTR_DIGIMON_FILE_NAMES[180]` (`model.c: loadMMD`).
- **Animaciones** (`anim.c: startAnimation, tickAnimation`): tabla `u32[]` de offsets (0 = sin animación). Cada una:
  `s16 frames` (bit `0x8000` = hay escala por hueso), la pose inicial de cada hueso salvo el 0 (escala opcional,
  rotación y traslación, 3 × s16 cada una) y un flujo de instrucciones `s16` con el tipo en el nibble alto y el frame
  en los 12 bits bajos:

  | Tipo | Qué hace |
  |---|---|
  | `0x0` | Keyframe: hueso, divisor y hasta 9 canales, que son velocidades por tick y no posiciones; encadena mientras está el bit `0x8000` |
  | `0x1` / `0x2` | Inicio / fin de bucle |
  | `0x3` | Mueve un rectángulo de VRAM (animación de textura) |
  | `0x4` | Reproduce un sonido (banco, id) |

  Los eventos de sonido y de textura se exportan como marcadores de la animación en glTF. Detalle en
  [Modelos y animaciones](../formats/modelos.md).
- **Esqueletos:** `DIGIMON_SKELETONS` → listas de `{s8 objeto, s8 padre}`. El número de huesos está en
  `DIGIMON_DATA[tipo].boneCount`.

## Mapas

- **`MAP_ENTRIES[255]`** (16 bytes): nombre, número de TIM de 8 y 4 bits, flags (`& 0x1F` sonido, `& 0x40` hora fija,
  `& 0x80` tiene NPC), puertas, baño y nombre de carga. La ruta es `\MAP\MAP{id/15+1}\{nombre}.MAP|.TFS` (`map.c: buildMapPath`).
- **`.MAP`:** tabla de offsets `u32` a sus secciones (`map.c: loadMap`):
  1. configuración: cámara, 3 luces y ambiente, proyección, regiones y la rejilla de tiles (`s32`, −1 = vacío);
  2. y 3. los TIM de 8 y 4 bits;
  4. objetos: sprites y sus instancias animadas (solo si hay imágenes);
  5. entidades: warps (6 × 10 `s16`) y NPC (34 + 8 `s16` cada uno + 3 por punto de ruta) (`tamer.c`, `overworld.c`);
  6. colisión: rejilla de 100 × 100 `u8` (celdas de 100 unidades); otros valores distintos de 0 y 0x80 son disparadores
     (`map_collision.c`).
- **`.TFS`:** CLUTs de 512 bytes y tiles de 128 × 128 a 8 bits del fondo prerenderizado (`map.c: readMapTFS`). Las
  palabras que el original salta son, en los ficheros US, el tamaño del fondo (`alto << 16 | ancho`) y la posición de
  cada tile en VRAM (`fila * 128 << 16 | columna * 64`). Paleta 0 = día, 1 = atardecer, 2 = noche.
- **Tablas relacionadas:** `DOOR_MAPDATA[24]` (modelos `DOOR\DOORnn.TMD` y su posición), `TOILET_DATA`,
  `MAP_LIGHT_UPDATE_DATA`.
- **`STDDAT\*.ATR`:** colisión de la arena del torneo: la misma rejilla de 100 × 100 `u8` que la sección de colisión
  de un mapa, que el original lee tal cual en `MAP_COLLISION_DATA` (`std_setup.c: STD_loadArenaAssets`,
  `vs_scene.c: VS_loadArenaAssets`). `dwx extract` la escribe como `arena/<nombre>/collision.yaml`.

Salida: un PNG del fondo por paleta, YAML con cámara, luces, warps, NPC y colisión (detalle en
[Mapas](../formats/mapas.md)).

## Textos

- Codificación Shift-JIS (cp932). El US también acepta ASCII de un byte y lo convierte al vuelo (`sjis.c`).
- La fuente está en el ejecutable: `CHAR_TO_GLYPH_TABLE` y `GLYPH_DATA` (11 filas de 16 bits y un ancho) (`font.c`).
- Códigos de control (`script_textbox.c: drawString2`): `00` fin, `01` color, `04` número, `05`/`06` hablantes,
  `07` Digimon, `08` técnica, `09` objeto, `0A` dinero, `0B` méritos, `0C`/`0E` tabulaciones, `0D` salto de línea
  (`0D 00` = fin de página), `0F` espacio de 6 px, `10`/`13`–`15` variables numéricas, `11` música, `12` torneo,
  `16`–`1C` columnas fijas del US. `02` y `03` llevan argumentos cuyo efecto falta por confirmar.
- Salida: UTF-8 con los códigos de control como etiquetas legibles (`{color:2}`, `{item:5}`), un fichero por idioma.

## Scripts de eventos

- `SCN\MAPHEAD.SCN` es el script global (0). `SCN\DG.SCN` empieza con una tabla de 0x2000 bytes de offsets `u32` por
  id de script, alineados a sector.
- Cada script: un `u16` y luego pares `(u16 sección, u16 offset)` hasta `0xFFFF` (`getScriptSection`).
- Los opcodes (0x10–0x7E y 0xFB–0xFF) y sus argumentos están en `include/dw/script.h`; el intérprete es
  `script_interp.c: tickScript`. `formats/script` los decodifica a una lista de instrucciones; la traducción a Lua es
  trabajo de `dwx extract` y se valida con un informe de opcodes no traducidos.
- **EFE** (`ETCHI\EFEDAT.EFE`): scripts de efectos de batalla. Cabecera `EfeFileHeader` de 0x34 bytes con los offsets de
  sus partes; el intérprete (`battle_effect.c: BTL_runEFEScript`) tiene 97 subopcodes. El interior **está a medio
  estudiar**.

## Audio

- **VHB** (`SOUND\VHB\*.VHB`): `{u32 offsetVh; u32 offsetVb}` + VH (instrumentos) + VB (muestras ADPCM)
  (`sound.c: readVHBFile`).
- **Música:** cada bloque de `FAALL` lleva además el inicio y fin de un banco de SEQ: `u32 offsets[]` (cuántos =
  `offsets[0] / 4`) a datos SEQ estándar (`loadMusicFont`, `playMusic`). No se usa SEP.
- Salida: SF2 por banco + MIDI por secuencia; efectos a WAV/FLAC.

## Vídeo

- `MOVIE\OP1/OP2/ED2/EDR.STR`: STR estándar (MDEC, `StHEADER`) con audio XA intercalado. La tabla `MOV_MOVIES[4]`
  (`mov.c`) da el primer frame y cuántos hay. Salida: AV1/WebM.

## El ejecutable y los overlays

- **PS-X EXE** con cabecera de 0x800 bytes; en el US el texto empieza en `0x80090800`, así que
  `offset = dirección − 0x80090000`. Las direcciones de las tablas salen de
  `config/us/symbols.txt` del decomp; `formats/exe` guarda solo las direcciones y tamaños por versión, nunca los datos.
- **Tablas** (US):

  | Tabla | Entradas × tamaño | Estructura |
  |---|---|---|
  | `DIGIMON_DATA` | 180 × 0x34 | `DigimonPara`: nombre, huesos, radio, altura, tipo, nivel, especiales, objeto que suelta, técnicas |
  | `MOVE_DATA` | 122 × 16 | `Move` |
  | `ITEM_PARA` | 128 × 32 | `Item`: nombre, precio, méritos, orden, color, si se puede tirar |
  | `EVOLUTION_REQS` | 63 × 28 | requisitos |
  | `EVOLUTION_STATS_GAINS` | 66 × 14 | ganancias |
  | `EVOLUTION_PATHS` | 62 × 11 | de [5], a [6] |
  | `RAISE_DATA` | 66 × 28 | cuidado |
  | `CARD_DATA`, `TOURNAMENT_SCHEDULE`, `SHOP_MAPS`, nombres de medallas, torneos y música | | |

  La tienda no tiene tabla de stock: un objeto se vende si está su flag `0x180 + id`. Las tablas se extraen a YAML y
  siembran el primer ruleset (detalle en [Tablas](../formats/tablas.md)).
- **Overlays** `*_REL.BIN`: imágenes sin cabecera ni reubicaciones que se cargan enteras en una dirección fija
  (`utils2.c: loadDynamicLibrary`). Algunos (BTL, VS) empiezan con datos incrustados (TIM y TMD) que también se extraen.

## Partidas

- Fichero de tarjeta `BASLUS-01032DMR` + dígito de ranura, un bloque de 8 KB: cabecera `SC`, título, icono y dos copias
  de `SavegamePayload` (principal y respaldo) con su checksum (suma de bytes). Lectura opcional en `dwx import-save`
  para llevar una partida del original al motor; no es parte de la fase 1.

## Lo que no se sabe

- `MMM.DAT` (26 MB): está en la tabla de ficheros pero no se ha encontrado código que lo lea.
- Para qué son las paletas de `.TFS` después de la tercera y el interior de los EFE.
- Los códigos de texto `02` y `03`.

Cada uno es un issue antes de cerrar la fase 1, porque el criterio es extraer **cada** fichero.

## `apps/dwx`

| Comando | Entrada → salida | Qué comprueba |
|---|---|---|
| `dwx verify <fichero>` | Imagen o ejecutable → informe | SHA-1 y versión conocida (hecho en la fase 0) |
| `dwx extract <imagen> [--out content/base] [--force]` | Disco → fuente | Versión y tablas por el ejecutable; un extractor por tipo de fichero (imágenes, tablas, mapas, colisión de arenas); informe de cobertura con duplicados; el motor monta el resultado ([detalle](../formats/extract.md)) |
| `dwx validate <paquete>` | Fuente | Esquemas de `content/schemas/`, referencias rotas, contrato de cada Digimon (esqueleto y animaciones) |
| `dwx cook <paquete>` | Fuente → cocinado | Que todo lo cocinado se puede volver a leer |
| `dwx pack <paquete>` | Cocinado → `.pak` + manifiesto | Id, versión, dependencias y prioridad del manifiesto |

- **Informe de cobertura:** por cada fichero del disco, qué extractor lo leyó, qué produjo y qué partes no entendió
  (`dwx/coverage.yaml`, [detalle](../formats/extract.md)). La fase 1 se cierra con cero pendientes.
- **Determinista:** la misma entrada da la misma salida byte a byte, para poder comparar extracciones en la CI con un
  disco que el equipo tenga en local.
- **Tests:** cada lector se prueba con fixtures sintéticos generados por los propios tests (un TIM de 2 × 2, un MAP
  mínimo); la extracción completa solo se prueba en local, con el disco de cada uno.

## Orden propuesto

1. `disc` (BIN/CUE, ISO9660, tabla de ficheros) y `dwx extract --list`, que solo lista y comprueba hashes.
2. TIM y packs → PNG. Primer visor ImGui.
3. TMD/MMD + animaciones → glTF.
4. Tablas del ejecutable → YAML.
5. MAP/TFS → PNG + YAML.
6. Textos, VHB/SEQ, STR.
7. Scripts SCN → Lua (lo más largo).
