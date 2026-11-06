# Películas (FMV)

Estado: **aprobada por el coordinador** (VAN-02, 2026-10-22) con sus decisiones (al final); en código.

Las cuatro películas del disco (`OP1`, `OP2`, `ED2`, `EDR`) ya se extraen como Y4M + WAV + `movie.yaml`
([formato](../formats/video.md)), pero nada las reproducía. VAN-02 las reproduce **tal como las escribe `dwx`**
donde lo hace el original, con sus reglas de salto y lo que viene después. Fase 4: solo se traduce; sin tests ni
rutas nuevos (regla «code now, test and fix in phase 5»). El formato cocinado (AV1 + Opus) va a la fase 5.

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
| 3 EDR: al acabar el final (rutina 53 → `MAIN_STATE` 3), tras cerrar la partida; luego OP1 y la portada | `script_interp.c: tickScript` (caso 53), `main.c: main` |

Lo que no se porta (máquina de la PS1): la lectura en anillo del CD, el reintento de 2000 lecturas, los búferes del
MDEC, VRAM y el doble búfer, el modo de 24 bits. La versión EU (otra tabla y otra posición) queda para la fase 5.

## Funciones originales → módulos de dwe

| Original | Destino en dwe |
|---|---|
| `MOV_MOVIES` | `movie.yaml` del paquete (`table`, ya lo escribe `dwx`): el frame final es dato del disco |
| `MOV_playMovie` (bucle, condición de fin y de salto) | `packages/engine/video`: `MoviePlayer` (reloj, frame actual, fin); `apps/game`: `MovieScreen` (salto, dibujo, voz) |
| `MOV_readFrame`, `MOV_decodeFrame`, `MOV_startStream`, `MOV_startRead`, `MOV_onSliceDecoded`, `MOV_waitForFlip` | `packages/engine/video`: `Y4mReader` (frame a frame, por trozos) + `toRgba`; el resto es máquina y desaparece |
| `MOV_initEnvironment` (y = 32 en 240) | `MovieScreen`: rectángulo con la proporción 320 × 240 y la película centrada, escalado a la ventana |
| `DecDCTvlc2`/`DecDCTin` + audio XA | ya en `formats/video` (`mdec`, `xa_adpcm`); `dwx` los usa al extraer |
| `startMovie`, `playMovie` (parar y reiniciar música, fundido) | `apps/game`: `MovieScreen` con `GameAudio` y `ScreenFade` |
| `main` (qué película y cuándo), `runLandingScreen` (600) | `apps/game`: estados de la app (`game.cpp`, `AppFlow`); números y reglas en el ruleset (`movies`) |
| Rutinas 53 (fin) y 54 (`gameClearSave`) | HIST-S7 las porta; VAN-02 deja los ganchos de EDR y ED2 (ver Q4) |

Ruleset, sección nueva `movies`: por película, `file` (`op1`…), `skippable` (EDR `false`) y `trigger` (`boot`,
`new_game`, `cleared_load`, `ending`); y `landing_idle_frames: 600`.

## Reproductor

```text
apps/game (MovieScreen) ──► packages/engine/video ──► packages/engine/assets (VFS por trozos, decodeWav), packages/engine/core
                        ──► packages/engine/render (textura + QuadBatch + ScreenFade), packages/engine/audio (voz en el bus de música)
```

- **`packages/engine/video`** ([su página](../modulos/engine/video.md)), módulo de la capa de sistemas: `Y4mReader` (lee el Y4M frame a frame de un
  `assets::FileReader`: nunca tiene el fichero entero, que son unos 300 MB), `toRgba` (la fórmula del MDEC, la de
  `formats/video` `toRgb`, reescrita en el motor porque el runtime no usa `formats`) y `MoviePlayer`: lee
  `movie.yaml`, da el frame RGBA que toca para un tiempo dado y dice cuándo acaba (frame final =
  `playback.frame_count`, o el último del fichero si es mayor). `MoviePlayer::loadSound` decodifica el WAV entero
  (unos 18 MB). No conoce ni `render` ni `audio`.
- **Lectura por trozos:** `assets::FileSystem::openFile` (por defecto lee el fichero entero; el disco lee con
  `ifstream` y el `.pak` expande zstd por trozos y comprueba el SHA-1 al leer el último byte) y
  `VirtualFileSystem::open`.
- **Sincronía:** el audio manda. El reloj es `Audio::position(VoiceId)` (el frame del clip que el hilo de audio
  mezcló por última vez) entre la frecuencia del WAV; sin audio, el tiempo de juego. Si el juego se atasca, se
  saltan frames (como el original, que nunca espera al vídeo).
- **`MovieScreen`** (`apps/game`): crea la textura de 320 × 176 una vez y la actualiza con `queueTextureUpdate`
  cada frame nuevo, la dibuja centrada en 320 × 240 con bandas negras (la película tapa la escena y la UI), toca el
  WAV en el bus de música y salta con la acción `pause` (Start del mando) o `menu` (Tab) si la película es `skippable`. La música ya
  existe (VAN-01 tramo 3), pero parar y reanudarla alrededor de la película todavía no está (`game.cpp` solo lo
  registra en el log); el fundido desde negro tras la película va a la fase 5.
- **Película que falta** (paquete sin `movie/`): aviso en el log y se sigue el flujo, como `CdSearchFile` fallido
  en US.
- **Estados de la app** (`game.cpp`): arranque → OP1 → portada; pantalla «Press Start» `landing_idle_frames` ticks
  sin Start → OP1 → portada. Como `runLandingScreen`, la cuenta empieza al entrar en esa pantalla y solo Start la
  corta (otras teclas no la reinician); el menú de inicio, las listas de ranuras y la caja de idiomas nunca vuelven
  a la película, como el `runMainMenu` del original. El guion del menú avisa con `title_landing`; el aspecto
  moderno, sin esa pantalla, no vuelve a la película;
  partida nueva → OP2 → mapa inicial; `PlaySession::requestMovie` es el gancho de ED2 (tras cargar una partida
  terminada, la partida espera) y de EDR (cierra la partida → EDR → OP1 → portada «Press Start»: el `main` del original da la vuelta a su
  bucle y pone `playMovie(0, 1)` antes de `runLandingScreen`; `movieAfter`), que HIST-S7 llama al portar las
  rutinas 53 y 54. Un autotest y los arranques directos (`--new-game`, `--load`, `--start`) no ven películas.

## Tramos (≤ ~800 líneas cambiadas cada uno, sin docs)

| Tramo | Qué entra | Líneas estim. | Contrato |
|---|---|---|---|
| 1 | `packages/engine/video` (`Y4mReader`, `toRgba`, `MoviePlayer`); lectura por trozos en `packages/engine/assets`; `Audio::position` | ~650 | 1, 2 |
| 2 | `apps/game`: sección `movies` del ruleset, `MovieScreen`, estados de la app y ganchos de ED2 y EDR | ~650 | 3 |

Cada tramo pasa `gate.sh --tier short --steps check,windows` y se sube solo.

## Cambios de contrato

1. **API pública de `packages/engine/audio`**: `Audio::position(VoiceId)`, aditiva.
2. **API pública de `packages/engine/assets`**: `FileReader`, `FileSystem::openFile` (virtual, con implementación por
   defecto) y `VirtualFileSystem::open`, aditivas.
3. **Ruleset**: sección `movies` y `landing_idle_frames`, aditiva.

Sin cambios en el paquete (`ExtractorVersion` sigue igual), el guardado ni la API `story`.

## Rutas del gate

Ninguna nueva (regla de la fase 4). La comparación con DuckStation (cortes, sincronía, ED2 con `frameCount` 2654
sobre 2652 frames en el fichero) va a la fase 5.

## Fase 5

- **Formato cocinado AV1 + Opus** (decisión del coordinador: no en la fase 4): `dwx` codifica cada película a
  `video.ivf` (AV1 con libaom, BSD-2-Clause + AOMedia Patent License 1.0) y `audio.opus` (libopusenc), deja de
  escribir Y4M y WAV (−1,1 GB por extracción), sube `ExtractorVersion`; el motor decodifica con dav1d (BSD-2-Clause
  + ISC), o con libaom si dav1d no compila en `windows-cross`. IVF en vez de WebM: sin dependencia más.
- La versión EU (otra tabla y otra posición). La pantalla «pulsa Start» del original (`runLandingScreen`) ya está
  (`title_landing`, arriba).

## Decisiones (coordinador, 2026-10-22)

- Q1/Q2: sin codificar en la fase 4; el motor reproduce Y4M + WAV + `movie.yaml` tal cual; AV1/Opus a la fase 5.
- Q3: el reloj es la posición de la voz (`Audio::position`).
- Q4: solo los ganchos; HIST-S7 conecta las rutinas 53 y 54 y el campo del guardado.
- Q5: `landing_idle_frames: 600` sobre la portada actual.
- Q6: no aplica.
