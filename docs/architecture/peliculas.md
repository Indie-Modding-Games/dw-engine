# Películas (FMV)

Estado: **aprobada por el coordinador** (VAN-02, 2026-10-22) con sus decisiones (al final); en código.

Las cuatro películas del disco (`OP1`, `OP2`, `ED2`, `EDR`) ya se extraen como Y4M + WAV + `movie.yaml`
([formato](../formats/video.md)), pero nada las reproducía. VAN-02 las reproduce **tal como las escribe `dwx`**
donde lo hace el original, con sus reglas de salto y lo que viene después. Fase 4: solo se traduce; sin tests ni
rutas nuevos (regla «code now, test and fix in phase 5»). El formato cocinado (AV1 + Opus) llegó después (#105,
abajo).

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
| `MOV_initEnvironment` (y = 32 en 240) | `MovieScreen`: rectángulo con la proporción 320 × 240 y la película centrada, escalado a la ventana; con «Imágenes a pantalla ancha» (encendida por defecto), la imagen de 320 × 176 sola, ajustada a la ventana con su proporción (`movieFrame`; [game](../modulos/apps/game.md#imágenes-a-pantalla-ancha)) |
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
  `assets::FileReader`: nunca tiene el fichero entero, que son unos 300 MB), `toRgba` (de YCbCr a RGBA con el croma interpolado y la BT.601 exacta;
  [su página](../modulos/engine/video.md#color)) y `MoviePlayer`: lee
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

## Escalado

Una mejora del motor (`apps/game/src/movie_drawer.cpp`), con el filtro de texturas `texture_filter`. Con «Ampliadas»
(`upscaled`, el de por defecto) la imagen de 320 × 176 se amplía en dos pasos, un escalado adaptativo a los bordes y
un enfoque adaptativo al contraste, escritos para este caso (no hay código de terceros):

- **Escalado adaptativo a los bordes** (`movie_upscale.slang`, en el pase de escena): cada píxel de la ventana es un
  Lanczos-2 de los 12 texels más cercanos. El gradiente local del luma gira el núcleo a lo largo del borde y lo
  estira en esa dirección, así que los bordes quedan rectos y nítidos en vez de escalonados o borrosos; en zonas
  planas el núcleo es redondo y más suave. El resultado se sujeta al mínimo y al máximo de los 4 texels más
  cercanos: los lóbulos negativos no dejan halos.
- **Enfoque** (`movie_sharpen.slang`, en el pase posterior `FrameDesc::post`, sobre la escena dibujada en una
  textura del tamaño de la ventana): una cruz de 5 tomas con un peso limitado por píxel (sin pasarse del mínimo ni del
  máximo de los vecinos), menos fuerte donde hay ruido; fuerza 0,8.

Con «Suaves» o «Píxeles» la película se estira con filtro lineal, como siempre. El marco (`showPicture`, el
rectángulo de la imagen) no cambia, ni el reloj: el frame sigue siendo `floor(t · 30)` con `t` la posición del sonido.

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

- **Formato cocinado AV1 + Opus (hecho, #105).** `dwx cook` cocina cada película: el Y4M a `video.ivf` (AV1 en un
  IVF, con libaom: BSD-2-Clause + AOMedia Patent License 1.0, vía vcpkg) y el WAV a `audio.opus` con el cocinero de
  sonido de siempre; `movie.yaml` se copia. `dwx extract` sigue escribiendo Y4M + WAV: es la fuente que se cocina, y
  el paquete distribuible es el cocinado (#227). El motor elige el lector por la extensión (`video::Av1Reader` o
  `Y4mReader`, `decodeOpus` o `decodeWav`), así que juega las dos formas. Se usa libaom también para decodificar
  (una sola dependencia nueva, que compila en `windows-cross`); IVF en vez de WebM, sin contenedor que añadir.
  Ajustes fijos y deterministas: calidad constante `cq-level` 24, `cpu-used` 6 del modo de buena calidad, un hilo
  por película (el cocinado ya reparte las películas entre hilos), un fotograma clave cada 300 como mucho, rango
  completo. El `nasm` del contenedor de desarrollo compila el ensamblador de libaom.
  - **Medidas** (extracción US, 4 películas, `dwx` de depuración con libaom en release, máquina compartida): 1171,8 MB
    de Y4M + WAV pasan a 27,4 MB (vídeo 20,7 MB, sonido 6,7 MB); el `.pak` de las cuatro, 27,3 MB. Cocinar las
    cuatro en paralelo, 4 min 33 s (una sola, 3 min); `dwx pack`, 3,5 s. Calidad de ED2 frente al Y4M: PSNR medio
    42,6 dB (Y 41,7; mínimo 38,1 en un fotograma).
  - **Calidad (hecho, 2026-11-08).** Lo que se perdía por el camino: la IDCT del hardware (tabla redondeada hacia
    abajo, 8 bits, vuelta en 9 bits), la decuantización truncada, el croma repetido en cuadros de 2 × 2, la matriz
    aproximada del MDEC y un AV1 de 8 bits a `cq-level` 24. Ahora `dwx extract` decodifica con precisión, quita bloques
    y halos (MPEG-4 anexo F.3) y escribe Y4M de 10 bits ([vídeo](../formats/video.md#restauración)); `dwx cook` cocina
    AV1 de 10 bits a `cq-level` 14; `toRgba` interpola el croma y usa la BT.601 exacta; con «Ampliadas» la imagen se
    amplía y enfoca en la GPU ([Escalado](#escalado)). Sin filtro temporal ni fotogramas intermedios: las películas
    son de 30 fps reales (en OP1 se repiten 539 de 3719 frames y en ED2 216 de 2652, y un frame se ve 2 ticks), y
    mezclar frames emborronaría el movimiento rápido. El reloj y el marco no cambian: a los mismos ticks se ve el
    mismo frame de la fuente antes y después.
    - **Medidas** (extracción US, 4 películas): Y4M de 1105,8 MB a 2211,6 MB (el doble, por los 10 bits); vídeo
      cocinado de 20,7 MB a 37,3 MB (ED2 3,8 → 7,2; EDR 4,9 → 9,2; OP1 6,0 → 10,3; OP2 6,0 → 10,6); el sonido no
      cambia. Cocinar el paquete entero, 5 min 15 s → 5 min 48 s. ED2 cocinada frente a su Y4M: PSNR medio 46,7 dB
      (antes 42,6), el peor fotograma 41,6. En la misma prueba con `cq-level` 24 / 18 / 14 / 10: 3,3 / 5,0 / 7,4 /
      11,0 MB y 43,1 / 44,8 / 46,8 / 49,3 dB (SSIM 0,988 / 0,991 / 0,994 / 0,996); 14 es donde la diferencia deja de
      verse ampliada.
  - **Música durante la película.** El original para la música antes de la película y la vuelve a arrancar después
    (`main.c:3441` `finalizeMusic`, `playMovie`, `initializeMusic`); dwe no lo hacía, así que ED2 sobre una partida
    terminada podía sonar con la canción del mapa debajo. Ahora `SessionMusic::holdForMovie` la para y retiene lo que
    el campo pida mientras dure, y `releaseAfterMovie` toca después la canción del mapa desde el principio.
  - **Queda:** el campo sigue actualizándose debajo de ED2 (animaciones y algún efecto de sonido del compañero);
    el original no ha cargado el campo todavía en ese momento.
- La versión EU (otra tabla y otra posición). La pantalla «pulsa Start» del original (`runLandingScreen`) ya está
  (`title_landing`, arriba).

## Decisiones (coordinador, 2026-10-22)

- Q1/Q2: sin codificar en la fase 4; el motor reproduce Y4M + WAV + `movie.yaml` tal cual; AV1/Opus a la fase 5
  (hecho después, en `dwx cook`: arriba).
- Q3: el reloj es la posición de la voz (`Audio::position`).
- Q4: solo los ganchos; HIST-S7 conecta las rutinas 53 y 54 y el campo del guardado.
- Q5: `landing_idle_frames: 600` sobre la portada actual.
- Q6: no aplica.
