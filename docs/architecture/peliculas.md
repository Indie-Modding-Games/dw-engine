# Películas (FMV)

Estado: **nota de port, pendiente del OK del coordinador** (VAN-02, 2026-10-22). Sin código todavía.

Las cuatro películas del disco (`OP1`, `OP2`, `ED2`, `EDR`) ya se extraen como Y4M + WAV + `movie.yaml`
([formato](../formats/video.md)), pero nada las reproduce. VAN-02 las convierte a un formato cocinado (AV1 + Opus,
sin MP4) y las reproduce donde lo hace el original, con sus reglas de salto y lo que viene después. Fase 4: solo se
traduce; sin tests ni rutas nuevos (regla «code now, test and fix in phase 5»).

## Lo que hace el original

| Hecho | Localizador |
|---|---|
| Tabla de 4 películas: fichero, primer frame y `frameCount`; la película acaba al mostrar el frame cuyo número llega a `frameCount` (OP1 se corta en el 3637 de 3719) | `mov.c: MOV_MOVIES`, `MOV_readFrame` |
| Reproducir: 320 × 176 a 30 fps, centrada en una pantalla de 240 de alto (y = 32), audio XA intercalado al ritmo del CD; si el fichero no está, vuelve sin hacer nada | `mov.c: MOV_playMovie`, `MOV_initEnvironment` |
| Saltar: el botón Start (`0x800`) en cualquier frame, salvo en la película 3 (EDR), que no se salta | `mov.c: MOV_playMovie` (condición del `do … while`) |
| Antes de cada película se para la música y después se reinicia; tras ella, pantalla en negro y fundido desde negro (40) | `main.c: main`, `utils2.c: playMovie`, `startMovie` |
| 0 OP1: al arrancar y cada vez que la pantalla «pulsa Start» pasa 600 fotogramas sin que se pulse (bucle de demostración) | `main.c: main`, `runLandingScreen` |
| 1 OP2: partida nueva, tras la escena de nombres y antes del mapa inicial (`MAPHEAD 0xCC`) | `main.c: main` (`MAIN_STATE` 0), `newGameScene` |
| 2 ED2: al cargar una partida guardada tras el final (`mainState` 2 del guardado), antes de entrar en el mapa | `main.c: main` (`MAIN_STATE` 2), `main_menu.c: gameClearSave`, `loadSavegame` |
| 3 EDR: al acabar el final (rutina 53 → `MAIN_STATE` 3), tras cerrar la partida; luego vuelve a la portada | `script_interp.c: tickScript` (caso 53), `main.c: main` |

Lo que no se porta (máquina de la PS1): la lectura en anillo del CD, el reintento de 2000 lecturas, los búferes del
MDEC, VRAM y el doble búfer, el modo de 24 bits. La versión EU (otra tabla y otra posición) queda para la fase 5.

## Funciones originales → módulos de dwe

| Original | Destino en dwe |
|---|---|
| `MOV_MOVIES` | `movie.yaml` del paquete (`table`, ya lo escribe `dwx`): el frame final es dato del disco |
| `MOV_playMovie` (bucle, condición de fin y de salto) | `engine/video`: `MoviePlayer` (reloj, frame actual, fin); `apps/game`: `MovieScreen` (salto, dibujo, voz) |
| `MOV_readFrame`, `MOV_decodeFrame`, `MOV_startStream`, `MOV_startRead`, `MOV_onSliceDecoded`, `MOV_waitForFlip` | `engine/video`: `IvfReader` + `Av1Decoder` (dav1d) + `toRgba`; el resto es máquina y desaparece |
| `MOV_initEnvironment` (y = 32 en 240) | `MovieScreen`: rectángulo con la proporción 320 × 240 y la película centrada, escalado a la ventana |
| `DecDCTvlc2`/`DecDCTin` + audio XA | ya en `formats/video` (`mdec`, `xa_adpcm`); `dwx` los usa al extraer |
| `startMovie`, `playMovie` (parar y reiniciar música, fundido) | `apps/game`: `MovieScreen` con `GameAudio` y `ScreenFade` |
| `main` (qué película y cuándo), `runLandingScreen` (600) | `apps/game`: estados de la app (`game.cpp`, `AppFlow`); números y reglas en el ruleset (`movies`) |
| Rutinas 53 (fin) y 54 (`gameClearSave`) | HIST-S7 las porta; VAN-02 deja los ganchos de EDR y ED2 (ver Q4) |

Ruleset, sección nueva `movies`: por película, `file` (`op1`…), `skippable` (EDR `false`) y `trigger` (`boot`,
`new_game`, `cleared_load`, `ending`); y `landing_idle_frames: 600`.

## Formato cocinado (CAMBIO DE CONTRATO 1)

`dwx extract` escribe, por película, en `movie/<nombre>/`:

| Fichero | Contenido |
|---|---|
| `video.ivf` | AV1 en IVF (cabecera de 32 bytes + 12 por frame; sin librería), 320 × 176, 4:2:0 de 8 bits, rango completo, 30 fps (la base de tiempos del STR) |
| `audio.opus` | Ogg Opus estéreo, como los sonidos que ya cocina `dwx` (`libopusenc`), a 128 kbit/s |
| `movie.yaml` | el mismo documento, con `video.codec: av1`, `video.container: ivf`, `audio.codec: opus` (esquema `movie.schema.json` actualizado) |

- **Se codifica al extraer, no al cocinar.** La caché de desarrollo monta la salida de `dwx extract` sin cocinar,
  así que el motor ve un solo formato en los dos caminos; `dwx cook` las sigue copiando. Sube **`ExtractorVersion`**
  (hoy 5 → 6, o el siguiente libre si otro aterriza antes) con una fila `.unseenBySyntheticDisc` (el disco sintético
  no trae STR).
- **Deja de escribir Y4M y WAV** (Q2): el paquete base baja de unos 1,2 GB a unos 40 MB de películas.
- `content_check` aprende `ivf` (cabecera) y `opus`; ADR 0017 y `assets-pipeline.md` dejan de decir «pendiente».

**Bibliotecas** (vcpkg, en el baseline actual):

| Uso | Biblioteca | Licencia | Por qué |
|---|---|---|---|
| Decodificar (motor) | `dav1d` 1.5.4 | BSD-2-Clause + ISC | El decodificador de referencia de VideoLAN: rápido, pequeño, solo decodifica, sin dependencias |
| Codificar (solo `dwx`) | `aom` 3.15.2 (libaom) | BSD-2-Clause (+ BSD-3, MIT, ISC); patentes: AOMedia Patent License 1.0 | El codificador de referencia de AV1, con port en vcpkg y CMake; `svt-av1` no declara licencia en vcpkg y `rav1e` no tiene port |
| Opus | `opus`, `libopusenc` | BSD-3-Clause | Ya están |

Todas permisivas; nada que escalar. La licencia de patentes de AOMedia es gratuita, con terminación defensiva
(solo se avisa). IVF en vez de WebM: cero dependencias más y `ffmpeg`/`mpv` lo abren igual.

## Reproductor

```text
apps/game (MovieScreen) ──► engine/video ──► engine/assets (VFS, decodeOpus), engine/core, dav1d
                        ──► engine/render (textura + QuadBatch + ScreenFade), engine/audio (voz en el bus de música)
```

- **`engine/video`**, módulo nuevo de la capa de sistemas: `IvfReader`, `Av1Decoder` (envoltorio RAII de dav1d,
  un hilo), `toRgba` (la fórmula del MDEC, la de `formats/video` `toRgb`, reescrita en el motor porque el runtime no
  usa `formats`) y `MoviePlayer`: abre `movie/<nombre>` por el VFS, entrega el frame RGBA que toca para un tiempo
  dado y dice cuándo acaba (frame final = `table.frame_count`, o el último del fichero si es mayor). No conoce ni
  `render` ni `audio`.
- **Sincronía:** el audio manda. El reloj es la posición de la voz de la película (Q3: una consulta nueva en
  `audio::Audio`); sin audio o con la salida nula, el tiempo de juego. Si el juego se atasca, se saltan frames
  (como el original, que nunca espera al vídeo).
- **`MovieScreen`** (`apps/game`): para la música, crea la textura de 320 × 176 una vez y la actualiza con
  `queueTextureUpdate` cada frame nuevo, la dibuja centrada en 320 × 240 con bandas negras, toca el audio en el bus de
  música (el volumen de música del jugador lo afecta), salta con la acción `menu` (Start del mando, Tab) si la
  película es `skippable`, y al acabar: negro, reinicia la música, fundido desde negro.
- **Película que falta** (paquete sin `movie/`, p. ej. una extracción vieja): aviso en el log y se sigue el flujo,
  como `CdSearchFile` fallido en US.
- **Estados de la app:** arranque → OP1 → portada; portada sin tocar `landing_idle_frames` → OP1 → portada; partida
  nueva → OP2 → mapa inicial; continuar con una partida terminada → ED2 → mapa; final → cerrar la partida → EDR →
  portada.

## Tramos (≤ ~800 líneas cambiadas cada uno, sin docs)

| Tramo | Qué entra | Líneas estim. | Contrato |
|---|---|---|---|
| 1 `dwx` | `aom` y `dav1d` en `vcpkg.json` (y su build en `windows-cross`); `av1_encoder` y escritor IVF en `apps/dwx`; codificar Opus compartido con `audio_cooker`; el extractor `movie` escribe IVF + Opus; `movie.yaml` y su esquema; `content_check` (`ivf`, `opus`); `ExtractorVersion` 6; docs de formato y ADR 0017 | ~600 | 1 |
| 2 `engine/video` | Módulo nuevo: `IvfReader`, `Av1Decoder`, `toRgba`, `MoviePlayer`, lectura de `movie.yaml`; `Audio::position(VoiceId)` en `engine/audio` | ~650 | 2 |
| 3 `apps/game` | Sección `movies` del ruleset; `MovieScreen`; estados de la app: OP1 al arrancar y en la portada ociosa, OP2 en partida nueva, ganchos de ED2 y EDR; mensajes | ~650 | 3 |

Total ~1900. Cada tramo pasa `gate.sh --tier short --steps check,windows` y se sube solo. El primero que toca
dependencias compila `aom` y `dav1d` una vez en la caché de vcpkg (Linux y `windows-cross`).

## Cambios de contrato

1. **Formato de las películas en el paquete** (`video.ivf` + `audio.opus` en vez de Y4M + WAV, `movie.yaml` con
   `codec`) y **`ExtractorVersion` 5 → 6** (quien aterrice segundo toma el siguiente número).
2. **API pública de `engine/audio`**: `Audio::position(VoiceId)` (frames reproducidos de una voz), aditiva.
3. **Ruleset**: sección `movies` y `landing_idle_frames`, aditiva.

Sin cambios en el formato de guardado ni en la API `story` de los scripts en VAN-02 (ver Q4).

## Rutas del gate

Ninguna nueva (regla de la fase 4). El push usa `--steps check,windows`; las rutas y la comparación con DuckStation
(cortes, sincronía, ED2 con `frameCount` 2654 sobre 2652 frames en el fichero) van a la fase 5.

## Preguntas al coordinador (la recomendada primero)

- **Q1 Dónde se codifica.** (a) En `dwx extract`, con `ExtractorVersion` 6: un solo formato en la caché de
  desarrollo y en el producto. (b) En `dwx cook`: el motor tendría que leer también Y4M + WAV para la caché.
- **Q2 Y4M y WAV.** (a) Dejar de escribirlos: −1,1 GB por extracción; el STR sigue en el disco del usuario. (b)
  Escribir las dos cosas (fuente sin pérdidas para el remaster), a costa del tamaño.
- **Q3 Reloj.** (a) La posición de la voz (contrato 2): no se desincroniza si el juego se atasca. (b) El tiempo de
  juego, sin tocar `audio`.
- **Q4 ED2 y EDR.** (a) VAN-02 deja los ganchos en la app (`MovieScreen` y los estados) y HIST-S7, al portar las
  rutinas 53 y 54, los llama y añade al guardado el estado «partida terminada». (b) VAN-02 añade ya ese campo al
  guardado (cambio de contrato del guardado).
- **Q5 Portada ociosa.** (a) La portada actual de dwe (el menú) cuenta `landing_idle_frames` sin entrada y vuelve
  a OP1. (b) Solo OP1 al arrancar; el bucle de demostración, a la fase 5 junto a la pantalla «pulsa Start».
- **Q6 Decodificador.** (a) `dav1d`; si su build con meson falla en `windows-cross`, (b) el decodificador de
  `aom`, que ya entra para `dwx`.
