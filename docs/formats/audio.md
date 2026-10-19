# Audio

Cómo lee `formats/audio` los ficheros de sonido del original (`SOUND/VHB/*.VHB`) y cómo los escribe en formatos
abiertos: SoundFont 2 por banco, WAV por muestra de efecto y MIDI por secuencia. Los nombres de campo son los de la
librería de sonido que usa el decomp (`VabHdr`, `ProgAtr`, `VagAtr`); los campos reservados se llaman `reservedN`.

## Ficheros

El original carga siete ficheros en nueve ranuras de VAB (`sound.c`, `VHB_HEADER_ADDR`). La tabla está en
`formats/audio/vhb.hpp` (`soundFiles`):

| Fichero | Disposición | Ranuras | Lo carga |
|---|---|---|---|
| `SS.VHB` | un banco | 0 | `sound.c: initializeMusic`; sonidos del sistema y de los menús |
| `SL.VHB` | un banco | 1 | `sound.c: initializeMusic`; ninguna llamada literal lo toca |
| `SB.VHB` | un banco | 8 | `sound.c: VSLoadSounds`: sustituye los sonidos del mapa en el modo VS y las arenas |
| `FAALL.VHB` | 33 entradas de 0x27 sectores | 2 | `sound.c: loadMusicFont`: la fuente `n` es la entrada `n − 1` |
| `VLALL.VHB` | 16 entradas de 15 sectores | 3 | `sound.c: loadPartnerSounds`, por `DIGIMON_VLALL_SOUND_ID[tipo]` |
| `VBALL.VHB` | 71 entradas de 7 sectores | 4–7 | `sound.c: loadDigimonSounds`, por `DIGIMON_VBALL_SOUND_ID[tipo]` |
| `ESALL.VHB` | 22 trozos de `MAP_SOUND_PARA` | 8 | `sound.c: loadMapSounds`, por el id de sonido del mapa |

- Las entradas de paso fijo se leen con `readVHBFileSectors(fichero, entrada × sectores, sectores)`.
- `MAP_SOUND_PARA[id]` es `{u16 sectorId; u16 sectorCount}` en unidades de 1 KB: `loadMapSounds` divide los dos entre
  2 para tener sectores. El id es `MapEntry.flags & 0x1F` (`map.c: getMapSoundId`). Además del de cada mapa, hay
  cargas con id fijo: 0x10 (`std/std_setup.c: STD_tournamentMain`), 0x12 (`main/evl.c: getEvoSequenceState`), 0x13
  (`main/partner.c: partnerTickDying`, `dooa/dooa.c: DOOA_tickDissolve`), 0x14 y 0x15
  (`main/script_interp.c: scriptInstruction64to7E`, operaciones 0x34 y 0x33).
- `loadPartnerSounds` usa la entrada 15 para cualquier tipo fuera de 1–65.

### Tablas del ejecutable

`formats/exe` las lee con las demás ([Tablas](tablas.md)); el extractor las usa para decir quién usa cada banco.

| Tabla | Entradas (US) | Contenido |
|---|---|---|
| `DIGIMON_VLALL_SOUND_ID` | 66 `u8` | entrada de `VLALL` por tipo de compañero |
| `DIGIMON_VBALL_SOUND_ID` | 180 `u8` | entrada de `VBALL` por tipo |
| `MAP_SOUND_PARA` | 22 × 4 | `MapSoundPara {u16 sectorId; u16 sectorCount}` por id de sonido de mapa |
| `MAIN_D_801303B8` | 64 × 2 `u8` | fuente y pista de cada entrada de la lista de música de la tienda (`shop/script_menu.c`) |

## VHB

Un VHB es una cabecera de `u32`, el VH (atributos de VAB) y el VB (muestras ADPCM) seguidos:

| Cabecera | Campos | Dónde |
|---|---|---|
| Banco de sonidos | `{u32 vh = 12; u32 vb; u32 vbEnd}` | `sound.c: readVHBFile` |
| Fuente de música (`FAALL`) | `{u32 vh = 16; u32 vb; u32 seq; u32 seqEnd}` | `sound.c: loadMusicFont` |

`readVhb` comprueba que `vb − vh` es el tamaño del VH que dan sus contadores, que el VB cabe en el fichero, que el
tercer `u32` de un banco es el final del VB y que el banco de SEQ de una fuente empieza tras el VB y cabe.

## VH

| Parte | Tamaño | Contenido |
|---|---|---|
| `VabHdr` | 0x20 | `"pBAV"`, versión 7, id, `fsize`, `ps` (programas), `ts` (tonos), `vs` (muestras), volumen y pan maestros, `attr1`, `attr2` |
| `ProgAtr[128]` | 128 × 16 | por ranura: número de tonos, volumen, prioridad, modo, pan, atributos |
| `VagAtr[ps][16]` | `ps` × 512 | un bloque de 16 tonos por programa con tonos |
| tamaños | 256 × `u16` | tamaño de cada muestra entre 8; la 0 no se usa |

- El bloque `k` de tonos es del `k`-ésimo programa **no vacío**: en dos fuentes de `FAALL` los programas no son
  consecutivos, y `tone.prog` de cada tono confirma a qué programa pertenece.
- Un tono tiene `min`/`max` (rango de teclas), `center` y `shift` (tecla raíz y ajuste fino en 1/128 de semitono),
  `vol`, `pan`, `mode` (bit 2: reverb), `pbmin`/`pbmax` (rango de pitch bend), vibrato, portamento, `adsr1`, `adsr2`
  y `vag` (la muestra).
- Las muestras van seguidas en el VB, en el orden de la tabla de tamaños; las de tamaño 0 no ocupan sitio.
- `readVab` exige: firma, `vs` < 256, tamaño del VH dentro del buffer, `fsize` ≥ VH, tantos programas no vacíos como
  `ps`, como mucho 16 tonos por programa, `tone.prog` igual a su programa y `vag` en 1..`vs` con tamaño no nulo.
- Algunas entradas de `VBALL` tienen una muestra más allá de `vs` en la tabla de tamaños; `locateVags` la incluye si
  cabe en el VB y la marca (`beyondCount`).

## ADPCM

Cada muestra es una lista de bloques de 16 bytes con 28 muestras: byte 0 = `filtro << 4 | shift`, byte 1 = flags, y
14 bytes de nibbles (el bajo primero). Cada muestra decodificada es:

```text
s = (int16(nibble << 12) >> shift) + ((s1 · f0 + s2 · f1 + 32) >> 6)
```

con `(f0, f1)` = (0, 0), (60, 0), (115, −52), (98, −55), (122, −60), limitada a 16 bits. Un `shift` mayor que 12
se comporta como 9 y un filtro mayor que 4 como el 4, como en el hardware; `decodeAdpcm` los cuenta como reservados.

| Flag | Valor | Efecto |
|---|---|---|
| fin | 1 | último bloque |
| repetir | 2 | con fin: vuelve al inicio del bucle |
| inicio de bucle | 4 | el bucle empieza en este bloque |

- Una muestra sin bucle acaba en un bloque con flags 1 seguido del bloque de cierre que dejan las herramientas de la
  librería de sonido: `00 07` y 14 bytes `77` (un bucle sobre sí mismo). El canal se libera antes de llegar a él, así
  que no forma parte de la muestra.
- Una con bucle va de un bloque 4 o 6 a uno 3. Sin bloque 4, el bucle empieza en el primer bloque
  (`loopStartImplied`).
- La decodificación para tras el primer bloque con fin; lo que quede detrás se informa.

## SEQ

El banco de SEQ de una fuente es una tabla de `u32` (cuántos = `offsets[0] / 4`) con el offset de cada SEQ desde el
inicio del banco (`sound.c: playMusic`). Una SEQ es:

| Parte | Formato |
|---|---|
| cabecera (15 bytes, big-endian) | `"pQES"`, versión `u32`, resolución `u16` (ticks por negra), tempo `u24` (µs por negra), ritmo `u8 u8` |
| eventos | delta en VLQ (hasta 4 bytes) y un evento MIDI de canal, con running status |
| tempo | `FF 51 tt tt tt`: **sin** byte de longitud |
| fin | `FF 2F 00` |

- Un meta borra el running status.
- Cada SEQ del banco empieza alineada a 4 bytes; el relleno tras `FF 2F 00` son bytes `CD` (el valor con que un heap
  de depuración llena la memoria sin inicializar) o ceros.
- El bucle va en NRPN: `CC99 = 20` seguido de `CC6 = n` marca el inicio de un bucle de `n` vueltas,
  y `CC99 = 30` el final. `readSeq` los convierte en eventos `LoopStart` (con la cuenta) y `LoopEnd`.
- Cualquier otro meta, un sysex o un delta que desborde 32 bits es un error.
- `sound.c: seqPlay` toca una vez las pistas de la fuente 0x1D salvo la 1 y las de las fuentes 0x1E–0x20 salvo la 2;
  las demás, en bucle infinito. Pone el volumen a 0x50.

## Escritores

Los escritores no usan dependencias nuevas y dan los mismos bytes con la misma entrada.

### WAV

PCM de 16 bits mono. Una muestra con bucle lleva un chunk `smpl` con un bucle hacia delante (el final es inclusivo:
`loop.end − 1`) y la tecla de unidad.

### SoundFont

Un preset por programa (banco 0, preset = número de programa) con un instrumento de una zona por tono:

| SF2 | Sale de |
|---|---|
| `keyRange` | `min`–`max` |
| `overridingRootKey` | `center` |
| `fineTune` | `−round(shift · 100 / 128)`: libsnd calcula el tono como `(nota · 128) − (center · 128 + shift)` |
| `initialAttenuation` | `−200 · log10(vol/127 · mvol programa/127 · mvol banco/127)`, de 0 a 1440 cB |
| `pan` | `(pan tono − 64) + (pan programa − 64)`, limitado a ±64 y pasado a ±500 |
| `reverbEffectsSend` | 1000 (100 %) si el bit 2 de `mode` está puesto |
| `sampleModes` | 1 si la muestra tiene bucle |
| envolvente de volumen | ver abajo |

Las muestras van a 44 100 Hz (la frecuencia del chip con tono 0x1000) con 8 puntos de guarda para el bucle y 46
ceros detrás, como pide el formato.

**Envolvente (aproximación).** El SPU sube y baja el nivel por pasos con una velocidad que depende del nivel; SF2
solo tiene tiempos y un nivel de sostenimiento. `approximateEnvelope` simula el SPU a 44 100 Hz con `adsr1`/`adsr2`:

- ataque: el tiempo de 0 a 0x7FFF;
- caída y liberación exponenciales: el tiempo que tardan en bajar 60 dB, escalado a los 100 dB de SF2; la
  liberación lineal, el tiempo hasta 0;
- sostenimiento: `200 · log10(0x7FFF / SL)` cB con `SL = (n + 1) · 0x800`;
- un sostenimiento con pendiente (velocidad distinta de 0x7F) que baja pasa a ser la caída, con sostenimiento
  1000 cB; uno que sube se ignora. Los dos se cuentan en el YAML (`sloped_sustain_tones`);
- los tiempos se limitan a [−12000, 8000] timecents.

El vibrato, el portamento, las prioridades y un pitch bend hacia abajo distinto del de subida no van al SF2; el YAML
del banco los guarda y dice cuáles tienen valores (`not_in_soundfont`).

### MIDI

SMF de formato 0 con la resolución de la SEQ como división y su tempo en el tick 0. Los ticks y los tempos son
exactos. Tras cada cambio de programa se escribe el rango de pitch bend (RPN 0) con el `pbmax` del primer tono del
programa, y después el RPN nulo. Las notas con velocidad 0 se dejan como están. El bucle va como marcadores
`loopStart` y `loopEnd` (meta 06) en sus ticks; la cuenta de vueltas va al YAML. No se escribe compás: no se sabe
qué significa el campo de ritmo, así que se deja en el YAML tal como viene.

## Extracción

El extractor `sound_bank` (`apps/dwx/src/audio_extractors.cpp`) toma los siete ficheros. Por cada banco:

| Banco | Salida |
|---|---|
| `SS`, `SL`, `SB` | `sound/<banco>/bank.sf2`, `sample_<n>.wav` y `bank_meta.yaml` |
| entrada de `VBALL`, `VLALL` o `ESALL` | lo mismo en `sound/<banco>/<entrada con 3 cifras>/` |
| fuente de `FAALL` | `music/<fuente con 3 cifras>/font.sf2`, `track_<n>.mid` y `font_meta.yaml` |

- Cada WAV va a la frecuencia a la que suena su muestra en la tecla más alta del primer tono que la usa:
  `44100 · 2^(((max · 128) − (center · 128 + shift)) / 1536)`. Con `playSound`, los bancos de efectos tocan la nota
  60 + `v % 16` del programa `v / 16` (ranuras 0, 1 y 8) o la nota `v` del programa 0 (ranuras 3–7), y
  `startSound` toca todos los tonos cuyo `max` es esa nota: el YAML lista estos valores en `sounds`.
- El YAML guarda todos los campos del VH, los tonos de cada programa (`adsr` en hexadecimal), las muestras (puntos,
  bucle, frecuencia) y quién usa el banco según las tablas del ejecutable: tipos de Digimon (`VBALL`, `VLALL`), mapas
  y cargas fijas (`ESALL`), y para cada pista de música las entradas de la lista de música de la tienda
  (`MAIN_D_801303B8`, del mismo índice que `BGM_TRACK_NAMES`) y los enemigos de `ENEMY_MUSIC_MAPPING`.
- Es un desconocido (el fichero queda parcial): un banco que no se lee, bytes no nulos tras los datos de una entrada
  o tras las muestras del VB, una muestra sin bloque de fin o con valores reservados, bytes tras el bloque de fin que
  no sean el bloque de cierre, una SEQ que no se lee o con algo tras su fin que no sea el relleno de alineación, y
  un cambio a un programa vacío.
- Sin las tablas del ejecutable los bancos salen igual pero sin usuarios (una nota), y `ESALL` no se puede partir.
