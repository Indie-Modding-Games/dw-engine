# Vídeo: `MOVIE/*.STR`

Las cuatro películas del disco (`MOVIE/OP1.STR`, `OP2.STR`, `ED2.STR`, `EDR.STR`) son ficheros STR: sectores de
modo 2 que el original lee a velocidad fija, con vídeo MDEC y audio XA intercalados. `formats/video` los separa y
los decodifica entero (vídeo y audio) sin dependencias nuevas; el extractor `movie` de `dwx` escribe cada película
como YUV4MPEG2 + WAV + YAML.

El original las reproduce en el overlay `MOV_REL.BIN` (`mov.c`), que se carga en `0x80010000`
(`utils2.c: playMovie` → `loadDynamicLibrary`): `MOV_startStream` lee a doble velocidad
(`CdRead2(CdlModeStream | CdlModeSpeed | CdlModeRT | CdlModeSize1)`), `DecDCTvlc2` pasa el bitstream a códigos MDEC y
`DecDCTin(..., 3)` saca color de 24 bits en franjas de 16 píxeles (`MOV_onSliceDecoded`).

## Sectores

Un STR hay que leerlo como **sectores en bruto con su subcabecera** (imagen de 2352 bytes). La vista de 2048 bytes
de una ISO pierde el audio (forma 2, 2324 bytes) y los tipos de sector; con una imagen así el extractor falla con
un error que lo dice. `disc::SectorData::subheader` guarda `{file, channel, submode, coding}` de cada sector de modo
2 de una imagen en bruto.

| Tipo | Subcabecera | Contenido |
|---|---|---|
| Vídeo | forma 1, `submode` con el bit de datos (`0x08`) o de vídeo (`0x02`); en el disco `0x48` | `StHEADER` de 0x20 bytes y 0x7E0 bytes del bitstream del frame |
| Audio | forma 2, bit de audio (`0x04`); en el disco `0x64`, el último `0xE4` (fin de fichero) | 18 grupos de sonido XA de 128 bytes y 20 bytes sin usar |
| Relleno | ningún bit de tipo (solo `0x01`/`0x80` como mucho) y 2048 ceros | nada; 3 a 5 al final de cada fichero |

Cualquier otro sector es un error del lector y el extractor lo cuenta en `unknowns`. En las cuatro películas el
patrón es estricto: 7 sectores de vídeo y 1 de audio.

`StHEADER` (libpress), little-endian:

| Offset | Campo | Comprobación |
|---|---|---|
| 0x00 | `u16 0x0160`, `u16 0x8001` | marca |
| 0x04 | `u16 sectorInFrame`, `u16 sectorsInFrame` | `sectorInFrame < sectorsInFrame` |
| 0x08 | `u32 frameNumber` | desde 1, consecutivos |
| 0x0C | `u32 frameBytes` | cabe en sus sectores |
| 0x10 | `u16 width`, `u16 height` | iguales en todo el fichero (320 × 176) |
| 0x14 | `u32 headm`, `u32 headv` | son los 8 primeros bytes del bitstream |
| 0x1C | `u32` reservado | 0 |

`StrFrameAssembler` junta los trozos de cada frame en orden y comprueba que todos sus sectores tienen la misma
cabecera, que el frame cabe, que el relleno tras `frameBytes` es cero y que `headm`/`headv` repiten el inicio del
bitstream. Un frame al que le faltan sectores es un error, y un primer sector que interrumpe otro frame empieza el
siguiente.

**Frecuencia.** A doble velocidad pasan 150 sectores por segundo; el audio lo confirma (un sector estéreo de 2016
muestras cada 8 sectores son 37 800 muestras por segundo). Un frame empieza cada 5 sectores de media, así que son
**30 fps**. El extractor calcula 150 / (sectores entre el primer y el último inicio de frame / frames − 1) y, si el
periodo está a menos de 0,05 sectores de un entero, lo redondea; si no, deja la fracción reducida.

## Bitstream (`DecDCTvlc2`)

Cabecera de 8 bytes: `u16` número de códigos MDEC (la mitad, redondeada hacia arriba a 32), `u16 0x3800`, `u16`
escala de cuantización (1..63, la misma para todo el frame) y `u16` versión (2 o 3; las películas son de la 3). Los
bits se leen de palabras de 16 bits little-endian, del más significativo al menos.

Cada macrobloque de 16 × 16 son seis bloques en el orden Cr, Cb, Y0, Y1, Y2, Y3:

- **DC, versión 3:** un código de tamaño (tablas B.12 de luma y B.13 de croma de MPEG-1) y tantos bits de
  diferencia; un primer bit 0 es negativo. La diferencia cuenta de 4 en 4 y se suma al anterior del mismo tipo (un
  predictor para Y0..Y3, otro para Cb y otro para Cr), que empiezan en 0 al principio del frame y siguen de un
  macrobloque al siguiente. El resultado tiene que caber en −512..511.
- **DC, versión 2:** 10 bits con signo.
- **AC:** la tabla B.14 de MPEG-1 entera (111 códigos, también los de 16 bits `0000 0000 0001 xxxx`), que es la que
  construye `DecDCTvlcBuild`, con el bit de signo detrás; `10` es fin de bloque y `000001` escape, con 6 bits de
  carrera y 10 de nivel con signo.

El decodificador da los códigos MDEC: `escala << 10 | DC` para el DC, `carrera << 10 | nivel` para cada AC y `0xFE00`
como fin de bloque. Es un error un código desconocido, un bloque que pasa de 64 coeficientes, un bitstream que
acaba a mitad de un macrobloque o un número de códigos que no coincide con la cabecera. En las cuatro películas la
cabecera coincide en todos los frames.

## MDEC

`decodeMacroblocks` hace lo que el MDEC con esos códigos, modelado como Mednafen (`mdec.c`) y comprobado con las
tablas que sube libpress (`DecDCTReset`):

- **Decuantización.** El DC se multiplica por el primer peso de la matriz (2), sin la escala; cada AC por
  `(nivel · escala · Q[k]) >> 3`. Los dos se guardan × 16 con un sesgo de 8 hacia cero y saturados a 15 bits. `Q`
  es la matriz intra por defecto de MPEG-1 con DC 2, en orden natural; el coeficiente k va a su posición por el
  zigzag estándar.
- **IDCT.** Matriz `M[x][u] = floor(32768 · c(u) · cos((2x + 1)uπ/16)) >> 3` con `c(0) = 1/√2` y `c(u) = 1` en el
  resto. Es `floor` y no redondeo: así es la tabla del ejecutable. Está escrita en el código (`IdctTable`, la que
  lista psx-spx para el comando del MDEC que la carga) en vez de calcularse con `cos`, para no depender de la libm de
  cada plataforma; una prueba la compara con la fórmula. Primero columnas, luego filas, cada pasada con
  `(suma + 0x4000) >> 15`; el resultado se queda en 9 bits y se satura a un byte con signo.
- **Planos.** Las muestras con signo se guardan + 128 como bytes sin signo. Los macrobloques llenan el frame por
  columnas: de arriba abajo y luego de izquierda a derecha.

**Por qué esta IDCT.** Una IDCT en coma flotante da casi lo mismo pero no lo mismo, y el redondeo cambiaría según el
compilador. La entera es la del hardware: la salida es determinista, igual en cualquier plataforma, y es lo que el
original ve antes de su paso a RGB.

**Color.** `toRgb` es la conversión del MDEC (Mednafen): `r = y + ((359 · cr + 0x80) >> 8)`,
`b = y + ((454 · cb + 0x80) >> 8)` y en el verde cada producto pierde bits bajos
(`(−88 · cb) & ~0x1F` y `(−183 · cr) & ~0x07`) antes de sumar; cada canal en 9 bits saturado y + 128. Se usa para
comprobar frames como RGB; el vídeo se escribe en YCbCr, sin pasar por ella. Un reproductor que use la matriz
BT.601 de rango completo da colores a menos de un nivel de diferencia, salvo el verde en algún caso.

## Audio XA (ADPCM)

Cada sector de audio tiene 18 grupos de sonido de 128 bytes: 16 de cabecera (los parámetros de las unidades en los
bytes 4..11, con copias en 0..3 y 12..15) y 112 de muestras. Con 4 bits son 8 unidades de 28 muestras (la muestra n
de la unidad u en el byte `16 + 4n + u/2`, nibble bajo para u par); con 8 bits, 4 unidades (byte `16 + 4n + u`).

- El parámetro de una unidad: los 4 bits bajos son el desplazamiento (13..15 se toman como 9), los bits 4..5 el
  filtro.
- Muestra: `(nibble << 12) >> desplazamiento` (o `byte << 8`) más la predicción
  `(ant · K0 + ant2 · K1 + 32) >> 6` con `K0/K1` = (0, 0), (60, 0), (115, −52), (98, −55), saturada a 16 bits.
- En estéreo las unidades alternan izquierda y derecha, y cada canal guarda su historia de un grupo y de un sector al
  siguiente.
- `coding`: bit 0 estéreo, bit 2 media frecuencia (18 900 Hz), bit 4 8 bits; el bit 6 (énfasis) y los valores
  reservados son error.

El redondeo es el de psx-spx (+32 antes del desplazamiento de la suma). Mednafen desplaza cada término por separado y
puede dar una unidad de diferencia en alguna muestra; ninguna referencia es el hardware medido, así que se elige la
documentada. Un parámetro distinto de su copia se cuenta (se usa el de 4..11) y el extractor lo pone en `unknowns`;
en el disco US no hay ninguno.

Las cuatro películas son estéreo a 37 800 Hz y 4 bits, con un único canal (`file 1`, `channel 1`); el original no
filtra canales (`mov.c` no usa `CdlSetfilter`).

## `MOV_MOVIES`

Tabla de 4 entradas de 0x28 bytes en `MOV_REL.BIN` (`mov.c`): `char path[0x20]` (`"\MOVIE\OP1.STR;1"`), `s32
startFrame`, `s32 frameCount`. `MOV_readFrame` deja de leer cuando el número de frame llega a `frameCount` o tras 2000
lecturas vacías, así que la película que se ve puede acabar antes que el fichero. Direcciones: US `0x8001094C`, EU
`0x80010B64`, JP y JP rev 1 `0x80010B70` (`movieTableAddress`).

## Salida de `dwx`

| Fichero | Contenido |
|---|---|
| `movie/<nombre>/video.y4m` | YUV4MPEG2 `C420jpeg`, `XCOLORRANGE=FULL`, `Ip`, `A1:1`, a la frecuencia real (`F30:1`): cada frame son los planos Y, Cb y Cr tal como salen del MDEC |
| `movie/<nombre>/audio.wav` | PCM de 16 bits little-endian, los canales y la frecuencia del XA |
| `movie/<nombre>/movie.yaml` | `source`, `video` (tamaño, frames, primer frame, fps, versión del bitstream, `chroma: 420jpeg`, `range: full`), `audio` (frecuencia, canales, muestras, bits del XA) o `null`, `sectors` (total, vídeo, audio, relleno) y `playback` (índice, ruta, `start_frame` y `frame_count` de `MOV_MOVIES`) o `null` si no se conoce la versión. Esquema: `content/schemas/movie.schema.json` |

- **Croma.** El MDEC usa la misma muestra de croma para los 4 píxeles de su cuadro de 2 × 2 (vecino más cercano), así
  que la muestra está en el centro del cuadro: `C420jpeg`. Los planos son de rango completo (0..255 sin escalar).
- **Escritura por partes.** `OutputFile::stream` (extractor.hpp) es una salida que el paquete base pide por trozos:
  el extractor guarda solo los bitstreams (unos 35 MB por película) y el vídeo se decodifica frame a frame al
  escribirlo, así que nunca está entero en memoria (unos 300 MB por película). Los bitstreams y los WAV de las cuatro
  películas (unos 200 MB) sí se quedan en memoria hasta que se escribe el paquete. Las salidas por partes no entran en
  la búsqueda de duplicados.
- **Cobertura.** Un STR está `extracted` cuando cada sector es vídeo de un frame que decodifica, audio del flujo de
  la película o relleno. Todo lo demás (un sector que no se reconoce, un frame que no decodifica, cuyo tamaño no es de
  macrobloques de 16 × 16 o al que le faltan sectores, audio de otro canal, un parámetro XA distinto de su copia, `MOV_MOVIES` ilegible) va a `unknowns`; si
  hay más de 32 problemas, el resto se cuenta en uno.

Para verlo o recodificarlo: `ffmpeg -i video.y4m -i audio.wav ...` (fuera del proyecto).

`formats/src/video/mdec.cpp` se compila optimizado también en los builds de debug (como `sha1.cpp`): sin eso, la
extracción en `ci` (debug con sanitizers) llevaba casi 9 minutos; ahora tarda unos 3,5. Al extraer, cada frame solo
pasa por la etapa VLC (`decodeBitstream`, que comprueba su bitstream y su tamaño); la IDCT corre una sola vez, al
escribir el Y4M.

## Pruebas

- `formats/tests/mdec_tests.cpp`, `xa_adpcm_tests.cpp`, `str_tests.cpp` y `movie_tests.cpp`: bitstreams y sectores
  sintéticos (DC, predictores, versión 2, AC de 15 y 16 bits, escape, fin de bloque, IDCT, macrobloques, todos los
  filtros XA, estéreo, 8 bits, intercalado de 8 sectores, entradas mal formadas, Y4M, WAV, `MOV_MOVIES` y esquema).
- `apps/dwx/tests/video_extractors_tests.cpp`: una imagen en bruto sintética con un STR.
- Con `DWE_DISC`: `formats/tests/video_real_disc_tests.cpp` decodifica los primeros frames y el audio de cada
  película, y `apps/dwx/tests/video_real_disc_tests.cpp` extrae las cuatro (sin `unknowns`) y decodifica entera la
  más corta sin escribir nada. Decodificar las cuatro lleva minutos en una compilación con sanitizers; la prueba del
  paquete base entero (`real_disc_tests.cpp`) lo hace al escribirlas.

## Estado con el disco US

Las cuatro películas quedan `extracted`, sin `unknowns`:

| Fichero | Sectores (vídeo / audio / relleno) | Frames | Duración |
|---|---|---|---|
| `MOVIE/ED2.STR` | 13 264 (11 603 / 1 658 / 3) | 2652 | 88,4 s |
| `MOVIE/EDR.STR` | 17 264 (15 103 / 2158 / 3) | 3452 | 115,1 s |
| `MOVIE/OP1.STR` | 18 600 (16 271 / 2325 / 4) | 3719 | 124,0 s |
| `MOVIE/OP2.STR` | 16 336 (14 289 / 2042 / 5) | 3266 | 108,9 s |
