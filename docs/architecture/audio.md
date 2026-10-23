# Audio

Estado: **diseño aprobado** (VAN-01; decidido por el coordinador el 2026-10-22: las 11 preguntas, como se
recomienda, más un tramo de reverberación tras el 3; y Q12–Q14 del tramo 2). El tramo 1 está implementado.

Este documento dice cómo suena el juego: el módulo `engine/audio` (salida al dispositivo, mezclador, voces y
volúmenes), cómo llegan a él los efectos (WAV u Opus) y la música (MIDI + SF2), cómo los piden los scripts y qué ve un
mod. Las citas al decomp (`dw_decomp`, `src/`, versión `us`) son como `fichero.c: función`, solo para leer el
comportamiento, nunca para copiar código ni tablas.

## 1. Alcance y tramos

VAN-01 se entrega en cuatro tramos de unas 800 líneas cambiadas (sin contar tests ni documentación). Cada uno pasa
el gate y se sube por separado.

| Tramo | Qué entra | Dónde acaba |
|---|---|---|
| 1 | Este diseño y el núcleo de `engine/audio`: salida por SDL3, salida nula, mezclador, voces, volúmenes maestro, de música y de efectos, y el límite de voces | `dwe-game` abre el dispositivo y no suena nada todavía (nadie pide sonidos); los tests cubren la mezcla, los volúmenes, el límite y la salida nula |
| 2a | Efectos (§6.4): decodificar WAV, leer `bank_meta.yaml`, envolvente ADSR, resolver `banco + sonido` a sus tonos y muestras, y el puerto `SoundPort` con sus ranuras en la sesión | La sesión ya puede tocar efectos por su puerto (nadie los pide todavía) |
| 2b | Los sonidos de los menús (`ui/ui_sounds.yaml`) y la orden `story.play_sound` de los scripts sobre el puerto de 2a, cuando la API `story` de S2 esté en `main` | Suenan los menús y los efectos de los scripts; se publica `story.play_sound` |
| 3 | Música: el secuenciador MIDI (tempo, bucles), el sintetizador SF2 y las transiciones de música; las órdenes `set_bgm` y `stop_bgm` | Suena la música del mapa, del combate y de los scripts; se publican `story.set_bgm` y `story.stop_bgm` |
| 3b | La reverberación del chip (§7.4, Q10) | Envío por voz según el bit de reverberación del tono |
| 4 | El volumen en la pantalla de opciones (maestro, música, efectos), guardado con los demás ajustes, y una ruta de autotest | La ruta cambia los volúmenes y comprueba el estado del mezclador con la salida nula |

## 2. Módulos y dependencias

```text
apps/game ──► engine/audio ──► engine/platform (SDL3), engine/assets (tramo 2), engine/core
apps/session ──► engine/audio (solo la API pública: pedir sonidos y música)
game/story ──► (puerto SoundPort, declarado en game/story; lo implementa apps/session)
```

- **`engine/audio`** es un módulo de la capa de sistemas (`dwe_add_module(audio LAYER systems ...)`), como dice ya la
  [visión general](overview.md) («mezcla y síntesis»). Depende de `core`, de `platform` (que enlaza SDL3) y, desde el
  tramo 2, de `assets` (el tipo `assets::Sound` y `decodeOpus`). **No conoce `game/` ni `apps/`**: no sabe qué es un
  mapa, un script ni un Digimon; solo reproduce clips y secuencias en buses.
- **El dispositivo (SDL3).** El [ADR 0003](../adr/0003-vulkan-y-sdl3.md) ya elige SDL3 para el audio. La salida SDL
  vive en `engine/audio/src` (como `rhi` usa SDL para la superficie de Vulkan) y es privada: ningún header público de
  `audio` incluye SDL. `platform::Platform` no cambia: la salida inicia su propio subsistema
  (`SDL_InitSubSystem(SDL_INIT_AUDIO)`, que SDL cuenta por referencias) y lo cierra al destruirse.
- **`game/*` no ve `audio`** (regla 3 de las [reglas de dependencias](dependency-rules.md): pide las cosas por la API
  pública, y la regla 4 obliga a hacerlo por interfaces). `game/story` declara un puerto (`SoundPort`: tocar un
  sonido, poner o parar la música) como ya declara `StoryStatePort`, y `apps/session` lo implementa sobre
  `audio::Audio`. Así el runner sigue probándose con un doble y `dwe-sim` corre con la salida nula.
- **`apps/session`** puede usar `audio` (es un sistema). No crea el dispositivo: recibe un `audio::Audio` ya hecho.
  `apps/game` lo crea con la salida SDL; `dwe-sim` y los tests, con la salida nula.
- **La resolución de qué suena** (qué banco ocupa la ranura 8 en este mapa, qué pista corresponde a un combate) es
  del juego, no de `audio`: vive en `apps/session` (o en el sistema de `game/` que la necesite, por su puerto) y lee
  los datos del paquete (`data/maps`, `data/music`, los `bank_meta.yaml` y `font_meta.yaml`).

Estructura del módulo (tramo 1):

| Fichero | Responsabilidad |
|---|---|
| `include/dwe/audio/audio.hpp` | `Audio`: la fachada del hilo del juego (pedir y parar voces, volúmenes, `update`) |
| `include/dwe/audio/clip.hpp` | `Clip`: PCM de 16 bits, frecuencia, canales y bucle; `checkClip` |
| `include/dwe/audio/voice.hpp` | `VoiceId` (hueco + generación) y `PlayRequest` (clip, bus, ganancia, panorama, tono) |
| `include/dwe/audio/mixing.hpp` | `Bus`, `VolumeControl`, el formato de la mezcla y las guardias (`MaxVoices`, `CommandQueueCapacity`) |
| `include/dwe/audio/output.hpp` | `MixSource`, la interfaz `Output`, `NullOutput` y `openDeviceOutput` (SDL) |
| `src/mixer.{hpp,cpp}` | El lado del hilo de audio: aplica las órdenes, rampas de volumen, mezcla y recorte |
| `src/voice_player.{hpp,cpp}` | Una voz: posición, remuestreo, bucle, panorama y fundido de parada |
| `src/spsc_queue.hpp`, `src/orders.hpp` | La cola SPSC sin bloqueos y las órdenes y avisos que pasan por ella |
| `src/sdl_output.cpp`, `src/null_output.cpp` | Las dos salidas |
| `src/messages.hpp` | Los mensajes de log y de error del módulo |

Tramo 2a: `include/dwe/audio/effect_bank.hpp` (`EffectBank`: un banco de efectos con sus muestras; cada efecto, una
voz por tono) y `src/envelope.{hpp,cpp}` (la envolvente ADSR del chip). En `assets`, `decodeWav` (`sound.hpp`) y
`readSoundBank` (`sound_bank.hpp`).

`apps/game/src/game_audio.cpp` abre el dispositivo o, si no hay, la salida nula con un aviso, y el bucle llama a
`Audio::update` con el tiempo de los ticks que avanzó. Desde el tramo 2a la sesión recibe el `audio::Audio` para sus
ranuras (`session::SessionSound`, el `SoundPort`); en el 2b, `MenuSounds` toca los sonidos de los menús.

La API pública y su uso, en resumen, están en la [página del módulo](../modulos/engine/audio.md).

## 3. Hilos

Hay dos hilos: el del juego (el bucle de `apps/game`, a 60 Hz) y el de audio (la llamada de SDL, que pide muestras
cuando las necesita, cada pocos milisegundos).

- **Regla de oro: la lógica del juego nunca lee el estado del hilo de audio.** Si una espera depende de un sonido
  (una orden que espera a que acabe, si el original la tiene), su duración se calcula en el hilo del juego con la
  duración del clip y el reloj del juego, no preguntando al dispositivo. Así una partida es determinista y guardable
  igual con sonido, sin él o con la salida nula, y las rutas de autotest no dependen de la tarjeta de sonido.
- **Del juego al audio:** cada llamada a `Audio` (tocar, parar, cambiar un volumen) escribe una orden en una cola
  SPSC de capacidad fija, sin bloqueos ni reservas de memoria. Las órdenes de un tick se **publican juntas** al final
  de `Audio::update()` (un solo `store` con `memory_order_release`), así el hilo de audio aplica todas las de un tick
  en el mismo bloque y el orden de los sonidos de un tick se conserva.
- **Del audio al juego:** una segunda cola SPSC devuelve avisos: «voz terminada» (por id). `update()` los lee.
- **Memoria de los clips.** El hilo de audio nunca reserva ni libera memoria, nunca bloquea y nunca escribe en el log.
  Los clips son inmutables; `Audio` guarda el `std::shared_ptr<const Clip>` de cada voz viva en el hilo del juego y el
  hilo de audio solo usa un puntero sin propiedad. La referencia se suelta en `update()` cuando llega el aviso de que
  la voz terminó, nunca dentro de la llamada de SDL.
- **Ids de voz.** `Audio::play` devuelve el `VoiceId` al momento (lo reparte el hilo del juego: índice + generación),
  así se puede parar una voz en el mismo tick en que se pidió.
- **Cola llena.** La capacidad (`CommandQueueCapacity`, 1024 órdenes por tick) es una guardia generosa, no un límite
  del original: si se llena, la orden se descarta y `update()` lo registra una vez por tick (aviso, no error).
- **Volúmenes.** También son órdenes. El hilo de audio los aplica con una rampa lineal de 10 ms para que un cambio
  brusco no haga clic.

## 4. Salidas

`Output` es la frontera con el dispositivo. El mezclador no sabe cuál hay detrás.

| Salida | Cuándo | Qué hace |
|---|---|---|
| SDL3 | `dwe-game` con audio disponible | `SDL_OpenAudioDeviceStream` sobre el dispositivo por defecto, `float` estéreo a 48 kHz. La llamada de SDL pide bytes; la salida mezcla en un búfer reservado de antemano, en trozos de como mucho `MaxRenderFrames`, y los entrega con `SDL_PutAudioStreamData`. SDL convierte al formato del dispositivo y sigue al dispositivo por defecto si cambia |
| Nula | tests, `dwe-sim`, `--check-content` y cualquier ejecución sin dispositivo | No hay hilo: `Audio::update(seconds)` hace avanzar el mezclador en el hilo del juego tantos fotogramas como tocan (48 000 × segundos, acumulando la fracción) y tira el resultado. Las voces avanzan, acaban y avisan igual que con sonido. Los tests pueden pedir el búfer mezclado para comprobarlo |

- **Sin dispositivo no es un error.** Si SDL no puede abrir el audio (un contenedor sin servidor de sonido, el Xvfb de
  las rutas, un equipo sin tarjeta), `apps/game` registra un **aviso** con el motivo y sigue con la salida nula. Las
  rutas de autotest siguen con 0 `[error]`.
- **Pausa del dispositivo.** Al minimizar o perder el foco el juego sigue como hoy (el bucle decide); la salida no se
  pausa sola. Lo que haga el original al pausar (si la música sigue) se aplica en el tramo 3 desde el juego.

## 5. Mezclador y voces

- **Formato interno:** `float` estéreo a 48 kHz (la frecuencia del Opus cocinado y la que se pide al dispositivo). La
  suma se hace en `float` y la salida se recorta a [−1, 1]. El chip del original también satura (16 bits), así que un
  recorte duro es lo fiel; no se añade compresor.
- **Buses:** `Music` y `Effects`, más el maestro. La ganancia de una voz es
  `ganancia de la voz × volumen de su bus × volumen maestro`. Los volúmenes que recibe `audio` son lineales, de 0 a 1
  (se limitan a ese rango). La curva que va del deslizador de opciones a esa ganancia es del tramo 4 (pregunta Q5).
- **Panorama:** de −1 (izquierda) a 1 (derecha), por voz. Un clip mono usa potencia constante (Q4: −3 dB por lado
  en el centro, como TinySoundFont); uno estéreo, balance (los dos lados enteros en el centro).
- **Remuestreo:** cada voz avanza con paso `frecuencia del clip / 48 000 × tono`, con interpolación lineal. Los
  efectos cocinados ya vienen a 48 kHz (paso 1); los WAV del camino de desarrollo van de 2 756 a 44 100 Hz.
- **Bucles:** una voz con bucle vuelve a `loop.start` al llegar a `loop.start + loop.length` hasta que se la para;
  sin bucle, acaba al final del clip y avisa.
- **Parar una voz** la baja a 0 en 5 ms antes de soltarla, para que no haga clic. (El original también la baja
  deprisa con `SsUtAutoVol` antes de soltar la nota, y luego suena la liberación de la envolvente; aquí el fundido
  hace ese papel, también con la envolvente del tramo 2, §6.4.)
- **Límite de voces (`MaxVoices` = 256).** El original tiene 24 canales en el chip de sonido: los efectos usan solo
  14 (del 10 al 23) y la música puede usar cualquiera, así que en escenas cargadas un efecto puede cortar otro o una
  nota de la música (§7.3). Aquí **no** se reproduce ese límite: es de la máquina, no una regla del juego. 256 es una
  **guardia no original**, generosa, contra un script o un mod que pidan sonidos sin parar; el disco no se acerca. Al
  llegar al límite, la voz nueva sustituye a la **más antigua** (el original, sin prioridades, roba por turno, que es
  lo más parecido) y se registra un aviso, una vez por tick. Está probado.
- **Guardias del tono y la frecuencia (no originales).** Un clip suena a una frecuencia de 1 a 192 000 Hz
  (`MaxClipSampleRate`) y una voz con un tono de 1/64 a 64 (`MinPitch`, `MaxPitch`); un panorama tiene que ser un
  número finito. Fuera de eso `Audio::play` falla con el motivo, así una voz nunca salta más de unos cientos de
  fotogramas de su clip por fotograma de salida, y la vuelta del bucle se calcula de una vez (`fmod`). Si aun así
  llega un NaN a la mezcla, la salida lo convierte en silencio. Están probadas.
- **Ningún fin se pierde.** El mezclador solo toma una orden si cabe el aviso de fin que puede causar (la voz que
  sustituye); si no cabe, la orden espera a la siguiente pasada. Una voz acabada cuyo aviso no cupo lo reintenta en
  cada pasada, y su hueco no se reutiliza antes.
- **Coste.** 256 voces con interpolación lineal son unos pocos millones de operaciones por segundo: muy por debajo de
  un núcleo. El sintetizador del tramo 3 tiene su propio límite de voces (§6.3).

## 6. Assets

`audio` no lee el sistema de ficheros: recibe clips y, desde el tramo 3, el SoundFont y la secuencia ya leídos. Quien
los carga (la sesión, con el VFS) usa lo que ya produce `dwx` ([Audio](../formats/audio.md),
[Opus](../formats/opus.md)); nada de esto cambia en VAN-01.

### 6.1 Lo que hay en el paquete base

| Asset | Extraído (`dwx extract`) | Cocinado (`dwx cook`, `.pak`) |
|---|---|---|
| Muestras de efectos: `sound/<banco>/sample_<n>` (`ss`, `sl`, `sb`) y `sound/<banco>/<entrada>/sample_<n>` (`esall`, `vball`, `vlall`) | WAV PCM 16 bits mono, a la frecuencia de su tecla, con `smpl` si tiene bucle | Ogg Opus a 48 kHz con `LOOPSTART`, `LOOPLENGTH` y `DWE_UNITY_NOTE`; **mismo id** |
| Bancos: `bank.sf2` y `bank_meta.yaml` | SF2 sin pérdida + YAML (programas, tonos, envolventes, `sounds`: valor → programa, nota y tonos) | Copiados tal cual |
| Música: `music/<fuente>/font.sf2`, `track_<nn>.mid`, `font_meta.yaml` | SF2 + SMF de formato 0 con marcadores `loopStart`/`loopEnd` (meta 06) + YAML (`plays: forever` u `once`, la cuenta de vueltas) | Copiados tal cual |
| `data/music.yaml` | `bgm_tracks` (la lista de la tienda de música) y `enemy_music_mapping` | Igual |
| `data/maps` | `sound_id` de cada mapa (el trozo de `ESALL` del mapa) | Igual |

Los ids de asset no llevan extensión, así que la misma referencia (`sound/ss/sample_002`) da el WAV en el camino de
desarrollo y el Opus en el paquete cocinado. El tramo 2 añade a `assets` un lector de WAV PCM (hoy solo se lee la
cabecera) que devuelve el mismo `assets::Sound` que `decodeOpus`, con su bucle y su nota.

### 6.2 Efectos (tramo 2)

Un efecto del original es una **nota** de un programa de un banco: `playSound(ranura, v)` toca la nota 60 + `v % 16`
del programa `v / 16` (ranuras 0, 1 y 8) o la nota `v` del programa 0 (ranuras 3–7), y suenan todos los tonos cuyo
`max` es esa nota. `bank_meta.yaml` ya lo resuelve en `sounds` (valor → programa, nota, tonos). En el disco US hay
661 sonidos; 15 tienen más de un tono y 19 muestras tienen bucle.

Cada tono que suena es una voz de `audio` con su muestra, su volumen (tono × programa × banco), su panorama, su tono
(1 salvo que la muestra la comparta un tono con otra raíz: la razón sale del YAML) y la envolvente `adsr` del tono,
con la misma aproximación que documenta [Audio](../formats/audio.md#soundfont). La alternativa (tocar el efecto como
nota del `bank.sf2` con el sintetizador del tramo 3) es la pregunta Q2.

Qué banco ocupa cada ranura lo decide el juego, como en el original: 0 `ss`, 1 `sl`, 3 el `vlall` del compañero, 4–7
los `vball` de los Digimon cargados, 8 el `esall` del mapa (`sound_id`) o `sb` en los torneos y el modo VS. La tabla
tipo → entrada de `VLALL`/`VBALL` está hoy solo en `used_by` de cada `bank_meta.yaml` (al revés: banco → tipos); ver
la pregunta Q3.

### 6.3 Música (tramo 3)

- **Secuenciador propio** (en `audio`): lee el SMF de formato 0, aplica el tempo, los cambios de programa, el pitch
  bend con su rango (RPN 0) y los marcadores `loopStart`/`loopEnd`; `plays: once` para al final. Es pequeño, sin
  dependencias y determinista, y los marcadores de bucle no los entiende ningún reproductor MIDI genérico.
- **Sintetizador SF2:** una biblioteca con **licencia permisiva**, y solo una. La propuesta es
  [TinySoundFont](https://github.com/schellingb/TinySoundFont) (`tsf.h`, MIT, un header, sin dependencias): lee
  cualquier SF2 (también los de un mod o un remaster), mezcla a `float` y deja fijar la frecuencia y el número de
  voces. No está en el baseline de vcpkg (se comprobó: tampoco hay otro port permisivo; `fluidsynth` es LGPL‑2.1 y
  queda descartado), así que entra como **port de overlay** del repositorio fijado a un commit y su SHA‑512, con su
  licencia en `share/`. Cómo entra es la pregunta Q1. No hay que aceptar ninguna licencia ni ningún paso con clic.
- El sintetizador corre **dentro del hilo de audio** como una fuente más del bus `Music`, con su propio límite de
  voces (guardia no original de 128; el tramo 3 mide la polifonía máxima de las 74 pistas y comprueba que queda muy
  por debajo). El secuenciador avanza por fotogramas de audio, así que el tempo no depende de los fotogramas del
  juego (el original tampoco: su secuenciador va a 240 ticks por segundo, aparte de los 20 fotogramas del juego).
- Cargar un SoundFont (el de una fuente de música ocupa hasta unos cientos de KB) se hace en el hilo del juego; el
  cambio de fuente pasa al hilo de audio como un puntero, y la fuente vieja se libera en `update()` (§3).

### 6.4 El tramo 2 en detalle

El tramo 2 se parte en dos para no pasar de unas 800 líneas y para no pisar al tramo 3 de S2 (`p4-story-s2-3`), que
publica la API `story` en `apps/session` y todavía no está en `main`:

| Parte | Qué entra | Líneas (aprox., sin tests) |
|---|---|---|
| 2a | Lector WAV y lector de `bank_meta.yaml` en `assets`; envolvente ADSR y efectos de banco en `audio`; `SoundPort` en `game/story` y su implementación en `apps/session` | ~840 |
| 2b | Los sonidos de los menús (avisos de `ui::Ui`, `ui/ui_sounds.yaml` y su esquema, `MenuSounds`); `story.play_sound(banco, sonido)` en la API de Lua de la historia, sobre el `SoundPort` de 2a, y la subida menor de `LuaApiVersion` | ~300 |

**`assets` (2a).**
- `decodeWav(bytes) → Result<Sound>`: PCM de 16 bits, mono o estéreo, con el bucle del bloque `smpl` y la nota raíz
  (`dwx` escribe así `sample_<n>.wav`). Rechaza con el motivo lo demás (otro formato, cabeceras que no cuadran,
  más de `MaxSoundFrames`). Es el camino de desarrollo; los paquetes cocinados traen Opus con el mismo id.
- `SoundBank` y `readSoundBank(yaml) → Result<SoundBank>`: de `bank_meta.yaml` lee `vab.master_volume`, los
  `programs` (volumen, panorama y sus tonos: `min`/`max`, raíz, `shift`, volumen, panorama, muestra, `adsr`), las
  `samples` (id de fichero y frecuencia de origen) y `sounds` (valor → programa, nota, tonos). Valida rangos e
  índices al leer; un `bank_meta.yaml` roto falla al cargar el banco, no al tocar el sonido.

**`audio` (2a).**
- **Envolvente** (`src/envelope.{hpp,cpp}`): la del chip a partir de los dos registros `adsr` del tono (ataque
  lineal o exponencial, caída exponencial hasta el nivel de sostén, sostén que sube o baja), contada en ticks de
  44,1 kHz y avanzada por fotogramas de la mezcla, sin asignar memoria. Es la documentación pública del chip, no el
  código del juego. Una voz con bucle que el sostén baja hasta 0 acaba sola. No hay fase de liberación: parar una
  voz sigue siendo el fundido de 5 ms del tramo 1, como la bajada rápida del original (§7.2).
- `PlayRequest` gana un campo opcional `envelope` (los dos registros); sin él la voz suena como en el tramo 1.
- **`EffectBank`** (`include/dwe/audio/effect_bank.hpp`): un `SoundBank` más sus muestras (`EffectSample`: el
  clip y la frecuencia a la que se codificó, cargados en el hilo del juego). `play(Audio&, sonido) →
  Result<vector<VoiceId>>` lanza una voz por tono con ganancia = (vol. tono/127)·(vol. programa/127)·(vol.
  banco/127), panorama = ((pan tono − 64) + (pan programa − 64))/64 recortado a ±1, la envolvente del tono y
  tono = frecuencia del chip / frecuencia de origen de la muestra (vale igual para WAV y Opus). La frecuencia del
  chip es 44100·2^(pasos/192), con pasos = (nota − raíz)·16 + (shift >> 3) en dieciseisavos de semitono (`shift`
  sube el tono), recortada al techo del registro de tono, 0x3FFF/0x1000·44100. Así lo calcula el original:
  `note2pitch2` de libsnd (`asm/main/psyq/libsnd.s:8717`, US `0x800D0C7C`), al que llama `SsUtKeyOnV` desde
  `sound.c:621`. La frecuencia de origen es la de la cabecera de la muestra; la de `bank_meta` solo cuenta si la
  cabecera no la trae, y si no coinciden se avisa una vez al cargar. `play` comprueba todas las voces (y que
  quepan sus órdenes) antes de lanzar ninguna (`Audio::playAll`): un efecto suena entero o no suena. No hay
  `stop` propio: cada voz se para con `Audio::stop`.

**Juego (2a).**
- `game/story` declara `SoundPort` (`playSound(slot, value)`), como `StoryStatePort`; el runner no sabe de bancos.
- `apps/session` lo implementa: ranura 0 → `sound/ss`, 1 → `sound/sl`, 8 → `sound/esall/<sound_id del mapa>` (o
  `sb` en combate y VS, cuando existan). Cada banco se carga la primera vez que su ranura lo pide, y el del mapa
  anterior se suelta al cambiar de mapa; un banco que no carga deja su ranura en silencio con un solo aviso. Las ranuras 3–7 no
  las piden los scripts del disco (§10, Q3): la tabla tipo → entrada se aplaza al paquete de los gritos (Q13).
- **Menús (tramo 2b):** `ui::Ui` informa de lo que pasó al navegar con el teclado o el mando (`takeCues()`: mover, confirmar,
  cancelar; los clics del ratón no dan aviso), sin saber de sonido, y como el original: `tickMenuInput`
  (`main_menu.c:4664`) toca el sonido 2 de la ranura 0 solo si el cursor se mueve (en el borde calla), el 3 al
  confirmar y el 4 al cancelar, y abrir el menú (`tickMainMenu`, `main_menu.c:3319`) no suena. `MenuSounds`
  (`apps/game/src/game_audio.cpp`) lee `ui/ui_sounds.yaml` y carga sus bancos al arrancar, y toca el sonido de cada
  aviso. Un aviso que falta o no vale, o cuyo banco no carga, queda en silencio con un aviso en el log al arrancar
  (Q12). El zumbido de rechazo del original (sonido 11, `script_textbox.c:260`, `script_common.c:898`: elecciones de
  diálogo y tienda) llega con esos menús; el combate del original no suena al rechazar una orden, así que el nuestro
  tampoco.
- **Historia (tramo 2b):** `StoryLuaHost::publishApi` recibe el `SoundPort` y publica `story.play_sound(ranura,
  sonido)`, la orden 0x5A del listado: como `scriptInstruction5Ato5F` (`script_interp.c:1550`), pasa los dos
  operandos a las ranuras (`playSound`, `sound.c:641`, que 2a ya sigue en `SessionSound`) y sigue sin esperar; un
  sonido que no suena no rompe el script. Es una función nueva de la API de Lua, así que `LuaApiVersion` sube de 1.2.0 a **1.3.0**, junto con `set_bgm` y `stop_bgm` (§6.5).

### 6.5 El tramo 3 en detalle (música del mapa)

Primero lo que pide el hito 1 («Inicio jugable»): la música que pone el script del mapa. El combate, el cambio de
día y noche con la hora (`updateBGM`, desde `tickGameClock`), los modos 2–10 y la gramola quedan para el tramo 3b.

| Función del original | Dónde vive ahora |
|---|---|
| `scriptInstruction5Ato5F` (`script_interp.c:1569`, 0x5D y 0x5E) | `apps/session/src/story_music_api.cpp`: `story.set_bgm(fuente)`, `story.stop_bgm(operando)` |
| `playBGM` (`script_interp.c:2587`), `resetBGM` (`:2377`), `handleMusicOverride` (`:1584`, modos 0 y 1) | `SessionMusic` (`apps/session`), detrás del puerto `MusicPort` de `game/story` |
| `playMusic` (`sound.c:753`), `stopBGM` (`:779`), `loadMusicFont` (`:738`), `seqOpen` (`:477`) | `SessionMusic::playMusic`, `stopBgm` y `load` |
| `seqPlay` (`sound.c:488`) y el secuenciador de la librería de sonido | `audio::Audio::playMusic` → `MusicPlayer` (hilo de audio) con TinySoundFont |
| El SMF y `font_meta.yaml` | `assets::readSong`, `assets::readMusicFont` (`engine/assets/src/music.cpp`) |

- **Como el original:** `set_bgm` reinicia la canción aunque sea la misma (`playBGM` llama a `stopBGM` antes de
  `playMusic`); `0xFF` para la música; una pista que la fuente no tiene suena la 0; la fuente se queda cargada entre
  canciones; volumen de secuencia 0x50 de 0x7F; `plays: once` para al final y deja sonar las colas; el resto vuelve
  del marcador `loopEnd` (o del final) al `loopStart` (o al principio). Los cortes son secos.
- **Diferencias con el original:** el volumen de canal (CC 7) se aplica lineal, como la librería de sonido, no con la
  curva cúbica del sintetizador; los modos 2–10 de la variable 245 suenan de momento la canción 0 de la fuente pedida
  (sus pares fuente/canción son datos del ejecutable, Q7, tramo 3b) y lo dicen en el log de depuración.
- **Hilos:** el SoundFont se carga en el hilo del juego (`audio::SoundFont`, con las voces y los 16 canales ya
  reservados, para que el hilo de audio no reserve memoria); la orden de música lleva el puntero, y `Audio` guarda la
  fuente y la canción hasta que el mezclador acepta una orden de música posterior (`musicTaken`).
- `TinySoundFont` entra como port de overlay (`vcpkg-ports/tinysoundfont`, commit `853a0a17`, MIT) con
  `VCPKG_OVERLAY_PORTS` en los presets.
- **Quién lo conecta:** `StoryAdapter` publica `publishStoryMusic` en la VM de la historia (en lugar de los
  marcadores mudos de antes) y, en cada tick, pasa las órdenes a `GameSession::music()` y `sound()`; una sesión muda
  (puerto nulo) no suena.

## 7. Fidelidad: lo que oye el jugador

Lo que sigue sale del decomp (`main/sound.c`, `main/script_interp.c`, `main/btl.c`, `btl/battle_main.c`,
`main/clock.c`, `shop/script_menu.c`). El juego corre a 20 fotogramas por segundo; el secuenciador del original, a 240
ticks por segundo. Lo que es regla del juego queda igual en el ruleset `original`; lo que es de la máquina, no.

### 7.1 Música

| Comportamiento del original | Localizador | Aquí |
|---|---|---|
| Una pista es un par (fuente, pista): fuentes 1–0x21; una fuente fuera de rango se ignora; una pista más allá de las de la fuente toca la 0 | `sound.c: playMusic`, `seqOpen` | Igual, con un aviso en el log en vez de silencio |
| Solo suena una secuencia a la vez | `sound.c: initializeMusic` | Igual (es lo que ve el jugador) |
| **Todos los cambios son cortes secos**: parar y empezar la nueva desde el principio. No hay fundidos ni fundidos cruzados en ningún sitio | `sound.c: seqStop`, `script_interp.c: playBGM` | Igual. Un fundido sería una mejora opcional, desactivada por defecto (pregunta Q8) |
| Cambiar de fuente lee el CD y congela el juego un momento | `sound.c: loadMusicFont` | **No** se reproduce (es de la máquina): la fuente se carga sin parar el juego |
| Volver a pedir la pista que ya suena la reinicia, porque cada camino del juego para la música antes; solo el cambio de día y noche (`updateBGM`) comprueba si la variante cambia | `script_interp.c: playBGM`, `forceUpdateBGM`, `updateBGM` | Igual |
| La música del campo la pone el script del mapa (`set_bgm` con la fuente: el operando que el listado llama `track` es la fuente, 1–28 en el disco); la variante sale de la variable 245: modo 0 día (6–20 h) o noche, modo 1 siempre la 0, modos 2–10 pares fijos | `script_interp.c: handleMusicOverride` | Igual, en el juego; los pares fijos son datos del ejecutable (pregunta Q7) |
| Al pasar las 6:00 y las 21:00 la música corta a la otra variante desde el principio | `clock.c: tickGameClock` → `updateBGM` | Igual |
| Cambiar de mapa no toca la música; la toca el script; `set_script` (0xFB) la para | `map.c: changeMap`, `script_interp.c: scriptInstructionFBtoFF` | Igual |
| Combate: `start_battle` para la música; el combate toca la fuente 0x21: pista 3 contra el jefe final, 0 en los combates con guion, `enemy_music_mapping` para los tipos 0x43–0x6F y 1 para los demás | `script_interp.c` (`START_BATTLE`), `btl.c: loadBattleData` | Igual (en el combate, tramo 3) |
| Victoria: para música y efectos y suena la fanfarria, que es un **efecto** (SS 0x15); al volver al campo la música del mapa empieza **desde el principio** (no se reanuda) | `battle_main.c: BTL_handleVictorySequence`, `tamer.c: startBattle` | Igual |
| No se pausa ni se reanuda nunca una secuencia | (no hay `SsSeqPause`) | Igual: la música del campo no se reanuda tras un combate |
| Bucles: los marcadores NRPN de la secuencia (en el MIDI, `loopStart`/`loopEnd`); las pistas de torneo de la fuente 0x1D (salvo la 1) y de las 0x1E–0x20 (salvo la 2) suenan una vez; el resto, sin fin | `sound.c: seqPlay`; la librería de sonido para los marcadores | Igual: `plays` de `font_meta.yaml` y los marcadores |
| Volumen de la secuencia fijo en 0x50/0x7F (≈ 63 %); el maestro, al máximo | `sound.c: seqPlay`, `initializeMusic` | Igual, como ganancia del bus de música **antes** del volumen del jugador: con los deslizadores al máximo suena como el original |
| La tienda de música toca (fuente, pista) al confirmar, sin cambiar la música activa, y al salir vuelve la del mapa desde el principio | `script_menu.c: tickJukeboxMenu` | La API lo permite (tocar sin cambiar la activa); la tienda es de su paquete |

### 7.2 Efectos

| Comportamiento del original | Localizador | Aquí |
|---|---|---|
| Un efecto es una nota de un programa del banco de su ranura; suenan todos los tonos del programa cuyo `max` es la nota, cada uno en su voz | `sound.c: playSound`, `startSound` | Igual (§6.2) |
| Siempre a volumen máximo y centrado: sin volumen ni panorama por llamada y **sin posición 3D** (la distancia no atenúa) | `sound.c: startSound` | Igual: el volumen y el panorama son los del tono y el programa. Sonido posicional sería una mejora opcional (fuera de VAN-01) |
| Volver a pedir un efecto que suena **no** lo reinicia: suena otra vez encima | `sound.c: playSound` | Igual |
| Parar un efecto: bajada rápida del volumen y suelta la nota | `sound.c: stopSoundMask` | Igual (§5) |
| Algunos efectos de combate y de minijuegos suenan en bucle hasta que su dueño los para | `battle_effect.c: BTL_playEFESound` | La API lo permite (voz con bucle + parar por id); cada sistema lo usa en su paquete. El fallo de `BTL_stopEFESounds` (solo para la primera ranura) se decide con el combate |
| Las órdenes de script de sonido no esperan: `play_sound` dispara y sigue; `set_bgm(0xFF)` y `stop_bgm` paran la música | `script_interp.c: scriptInstruction5Ato5F` | Igual. Las esperas de carga del banco del mapa (rutinas 0x28, 0x35) acaban en el acto: son del runner de la historia |
| Los gritos de los Digimon van en las animaciones (instrucción de animación con banco y sonido; solo si la entidad está en pantalla) | `anim.c` | Con su sistema (pregunta Q6) |

### 7.3 Lo que es de la máquina y no se reproduce

- **14 voces para efectos y 24 en total, compartidas con la música**, robando por turno: en el original un efecto
  puede cortar otro o una nota de la música en escenas muy cargadas. Aquí la música y los efectos no compiten y el
  límite es la guardia de §5 (pregunta Q9).
- **La congelación al cargar una fuente** desde el CD.
- **La interpolación del chip** (gaussiana de 4 puntos): la de aquí es lineal. Con los Opus cocinados a 48 kHz casi no
  se nota; con los WAV del camino de desarrollo a 2–11 kHz, sí un poco.

### 7.4 Reverberación

El original enciende la reverberación del chip al arrancar y no la cambia: modo `STUDIO_B`, profundidad 0x7000
(≈ 88 %), en las voces 0–22 (`sound.c: initializeMusic`), y cada tono decide si la usa (bit 2 de `mode`, que el SF2 ya
lleva como `reverbEffectsSend`). Es parte de cómo suena el juego. Los buses del mezclador admiten después un envío
de reverberación por voz sin cambiar la API de `Audio`; hacerla es la pregunta Q10.

## 8. Qué ve un mod

En el tramo 1 **no se publica nada** a los mods: ni funciones de Lua ni claves nuevas. Lo que se publique después lo
hará el tramo en que funcione y tenga test («publicar solo lo que funciona»).

| Superficie | Hoy | Plan | Contrato |
|---|---|---|---|
| Assets de sonido y música (`sound/**`, `music/**`, sus `*_meta.yaml`, `data/music.yaml`) | Los escribe `dwx`; esquemas `sound_bank` y `music` | Sin cambios: un mod que sustituye un `sample_<n>`, un `font.sf2` o un `track_<nn>.mid` por id ya se oye en los tramos 2 y 3 | Ninguno |
| Formato cocinado (Opus, SF2 y MIDI copiados) | ADR 0017 | Sin cambios | Ninguno |
| `pack.yaml` | — | Sin claves nuevas | Ninguno |
| `story.play_sound(ranura, sonido)` | Lo emite `dwx` en el Lua convertido | Publicada en el tramo 2b (Q14) sobre el `SoundPort` | **CAMBIO DE CONTRATO** (aditivo, tramo 2b): `LuaApiVersion` sube de 1.2.0 a 1.3.0 |
| `story.set_bgm(fuente)`, `story.stop_bgm(operando)` | Ídem | Publicadas en el tramo 3 (§6.5) sobre el `MusicPort` | **CAMBIO DE CONTRATO** (aditivo, tramo 3): la misma subida a 1.3.0 que `play_sound` |
| Qué sonido toca cada aviso de los menús | — | Un documento del paquete `game` (`ui/ui_sounds.yaml`: `cursor`, `confirm`, `cancel` → `{bank, sound}`) | **CAMBIO DE CONTRATO** (aditivo, tramo 2b): documento nuevo del paquete `game` con su esquema |
| Volúmenes del jugador | — | Ajustes del usuario (tramo 4), no del paquete | Ninguno (no los ve un mod) |
| Una API de sonido general para mods (`sound.play("sound/...")` por id) | — | Fuera de VAN-01: va a la fase 5 | — |

Internos (sin contrato): todo `engine/audio` (sus clases, la cola, las salidas, los límites), el puerto `SoundPort` de
`game/story` y la resolución de ranuras de la sesión.

## 9. Tests

| Tramo | Qué se prueba (sin dispositivo, con la salida nula) |
|---|---|
| 1 | Mezcla: una voz de un clip conocido sale con su ganancia y su panorama; dos voces se suman; el recorte a ±1. Volúmenes: maestro × bus × voz, el límite a [0, 1], la rampa sin saltos. Voces: acaban y avisan, el bucle vuelve, parar baja a 0. Límite de voces: la voz 257 sustituye a la más antigua, y el aviso. Cola: el orden de un tick, la cola llena. Salida nula: avanza el tiempo del juego (60 ticks = 48 000 fotogramas) |
| 2 | Lector de WAV (con y sin `smpl`); de `bank + sonido` a voces con fixtures sintéticas; `story.play_sound` con un doble del puerto; y, con `DWE_CONTENT`, que cada sonido de cada `bank_meta.yaml` resuelve a muestras que se leen |
| 3 | Secuenciador: tempo, bucles, `once`; el sintetizador con un SF2 sintético mínimo; las transiciones de §7 con la salida nula; con `DWE_CONTENT`, que las 74 pistas se leen y suenan sin error |
| 4 | Ajustes de volumen guardados y leídos; una ruta de autotest (en y es) que cambia los volúmenes desde opciones |

## 10. Decisiones

Decidido por el coordinador el 2026-10-22: en cada pregunta, la opción recomendada (la primera). Q7 es un **CAMBIO DE
CONTRATO** aditivo (claves nuevas en `data/music.yaml`, `ExtractorVersion` y el esquema `music`): si S2 tramo 4 sube
`ExtractorVersion` antes, el tramo 3 toma el número siguiente. El `SoundPort` de `game/story` lo añade el tramo 2. La
licencia de TinySoundFont va a los avisos de terceros. Se conservan las opciones para que se vea qué se descartó.

Tramo 2 (Q12–Q14), decidido por el coordinador el 2026-10-22: la opción recomendada en las tres. Q14: 2a ahora; 2b,
rama pequeña desde `main` cuando entre el tramo 3 de S2, con `story.play_sound` y la subida menor de `LuaApiVersion`
(si la subida del tramo 4 de S2 entra antes, 2b toma el número siguiente). Q12: `ui/ui_sounds.yaml` y su esquema
entran en 2a, **CAMBIO DE CONTRATO** aditivo; una entrada que falta o no vale deja ese aviso en silencio con un aviso
en el log, y el juego nunca falla por ello; si 2a se pasa mucho de unas 800 líneas, los avisos de los menús pasan a
2b. Q13: la tabla tipo → entrada va al paquete de los gritos y del combate. Al medirlo, 2a con los menús pasaba de 1000 líneas, así que, como
se decidió, los menús pasan a 2b (unas 240 líneas) y 2a queda en unas 840.

2b, con la regla de portar del decomp (2026-10-22): los avisos siguen `tickMenuInput` y `tickMainMenu` (§6.4), así que
un movimiento que no mueve el cursor y abrir el menú callan, y `ui_sounds.yaml` lleva solo `cursor`, `confirm` y
`cancel`. El aviso `error` y su zumbido en el combate salen: el combate del original no suena al rechazar una orden, y
el zumbido 11 llega con los menús de diálogo y tienda que lo tocan.

**Q1. Cómo entra el sintetizador SF2 (tramo 3).**
1. *(Recomendada)* TinySoundFont (MIT) como **port de overlay de vcpkg** en el repositorio
   (`vcpkg-ports/tinysoundfont`, `VCPKG_OVERLAY_PORTS` en los presets), fijado a un commit con su SHA‑512 y su
   licencia instalada. Sigue la regla de que las dependencias vienen de vcpkg; cuesta un port pequeño y que la caché
   binaria lo construya una vez.
2. TinySoundFont copiado en `third_party/tinysoundfont/` con su `LICENSE`. Lo más simple, pero es la primera
   dependencia copiada a mano en el repositorio.
3. Un sampler SF2 propio (~700 líneas) que solo entiende los generadores que escribe `dwx` (rango de teclas, raíz,
   afinación, atenuación, panorama, bucle, envolvente). Sin dependencia, pero un SF2 de un mod o de un remaster con
   otros generadores o moduladores sonaría distinto.
FluidSynth (LGPL‑2.1) queda fuera por la licencia. Ninguna opción pide aceptar licencias.

**Q2. Cómo suenan los efectos (tramo 2).**
1. *(Recomendada, la del encargo)* Cada tono es una voz con su muestra WAV/Opus, volumen, panorama, tono y la
   envolvente `adsr` del tono. Usa el Opus cocinado y no depende del tramo 3.
2. Tocar el efecto como nota del `bank.sf2` con el sintetizador del tramo 3. La envolvente y las capas salen solas
   (la misma aproximación del SF2), pero los efectos esperan al tramo 3 y el Opus cocinado de los efectos no se usa.

**Q3. Qué entrada de `VLALL`/`VBALL` usa cada tipo de Digimon (tramo 2, solo para las ranuras 3–7).**
1. *(Recomendada)* La sesión construye la tabla al montar el paquete leyendo `used_by.types` de cada
   `bank_meta.yaml` (ya está en el paquete; sin cambio de contrato). Un tipo que no aparece usa la entrada de reserva
   (`used_by.fallback`).
2. **CAMBIO DE CONTRATO**: `dwx` escribe la entrada de sonido en los datos de cada Digimon (`data/digimon`), sube
   `ExtractorVersion` y el esquema. Más directo para un mod que añade un Digimon, pero toca la extracción.
Los scripts del disco solo piden las ranuras 0 (63 veces) y 8 (433); las 3–7 son de los gritos y del combate (Q6).

**Q4. Ley de panorama de las voces de efectos (tramo 2).**
1. *(Recomendada)* Potencia constante (seno/coseno), la misma que usa TinySoundFont, para que efectos y música
   panoramicen igual. Se compara con el original en el banco de pruebas de DuckStation en el tramo 2.
2. Lineal (L = 1 − p, R = p), si la comparación muestra que el chip del original panoramiza así.

**Q5. Curva de los deslizadores de volumen (tramo 4).**
1. *(Recomendada)* En decibelios: de 0 % (silencio) a 100 % (0 dB), con 1 % ≈ −50 dB, en pasos del 10 %. Es lo que
   espera el oído y lo que hacen casi todos los juegos de PC.
2. Lineal en amplitud: más simple, pero casi todo el cambio que se oye está en el primer tercio del deslizador.
El valor por defecto es 100 % en los tres (suena como el original).

**Q6. Qué efectos entran en VAN-01 (tramo 2).**
1. *(Recomendada)* Las órdenes `play_sound` de los scripts y los sonidos del sistema y los menús (banco `ss`:
   cursor, confirmar, cancelar, error, abrir y cerrar cajas, objeto obtenido) en las pantallas que ya existen. Los
   gritos ligados a las animaciones y los efectos del combate van con el paquete de cada sistema (VAN-xx), que usa la
   misma API.
2. Solo las órdenes de los scripts; los menús, con cada pantalla.
3. Todo, gritos de animación incluidos: más de 800 líneas, haría falta un quinto tramo.

**Q7. De dónde salen los datos de la música del campo y del combate (tramo 3).** Los pares fijos de la variable 245
(modos 2–10), la fuente del combate (0x21) con sus pistas (0, 1, 3) y el tipo del jefe final son números del
ejecutable: no pueden ir en git.
1. *(Recomendada)* **CAMBIO DE CONTRATO** (aditivo): `dwx` los escribe en `data/music.yaml` (claves nuevas, por
   ejemplo `field_overrides` y `battle`), sube `ExtractorVersion` y el esquema `music`. El juego los lee del paquete,
   y un mod puede cambiar la música del combate o de una variante.
2. Ponerlos en el ruleset `original` como reglas. Más rápido, pero son valores del ejecutable dentro de git: choca con
   la política de assets.

**Q8. Fundidos de música.** El original corta en seco.
1. *(Recomendada)* Cortes secos como el original; un fundido corto como mejora opcional queda para la fase 5, no
   para VAN-01.
2. Un ajuste `music_fades` (desactivado por defecto) ya en el tramo 3.

**Q9. Las 14 voces de efectos del original.** Recortar a 14 haría que, con mucho a la vez, unos efectos corten otros
como en el original, pero es un límite de la máquina, no una regla.
1. *(Recomendada)* No reproducirlo: guardia de 256 voces (§5).
2. Reproducirlo como `quirk` del ruleset `original` (14 voces de efectos, robo por turno), desactivable.

**Q10. Reverberación del chip (§7.4).**
1. *(Recomendada)* Un tramo propio después del 3 (unas 300 líneas): la reverberación del chip con el modo `STUDIO_B`,
   a partir de la documentación pública del hardware (no del decomp), como envío por voz según el bit de reverb del
   tono. Mantiene el sonido del original; alarga VAN-01 en un tramo.
2. Una reverberación genérica (tipo Freeverb) aproximada, en el tramo 3. Más corta, menos fiel.
3. Sin reverberación en VAN-01; se apunta a la fase 5 como hueco de fidelidad conocido.

**Q11. El audio en las rutas de autotest.** Con Xvfb no hay servidor de sonido y SDL no abre el dispositivo.
1. *(Recomendada)* Lo que dice §4: aviso y salida nula; la ruta del tramo 4 comprueba el estado del mezclador (voces y
   volúmenes) por la salida nula, que es determinista.
2. Forzar `SDL_AUDIO_DRIVER=dummy` en el gate para pasar por la salida SDL. Prueba más código, pero mete el hilo de
   audio y su tiempo real en las rutas.

**Q12. Dónde se dice qué sonido toca cada aviso de los menús (tramo 2a).**
1. *(Recomendada)* **CAMBIO DE CONTRATO** (aditivo): un documento del paquete `game`, `ui/ui_sounds.yaml`, con
   `cursor`, `confirm`, `cancel` y `error`, cada uno `{bank, sound}` (hoy `ss` 2, 3, 4 y 0xB). Un mod de interfaz
   cambia los sonidos sin tocar código; el esquema va con los demás de `ui/`.
2. Constantes en `apps/game` (sin contrato). Menos líneas, pero un mod no puede cambiarlos.

**Q13. La tabla tipo → entrada de `VLALL`/`VBALL` (Q3).**
1. *(Recomendada)* Se aplaza al paquete de los gritos y del combate: en el tramo 2 no la usa nadie (los scripts solo
   piden las ranuras 0 y 8) y no se publica código sin consumidor.
2. Entra en 2a, leída de `used_by.types` (unas 60 líneas más, sin contrato), sin consumidor hasta ese paquete.

**Q14. Quién publica `story.play_sound`.**
1. *(Recomendada)* 2a deja `SoundPort` en `game/story` y su implementación en `apps/session`; 2b, una rama pequeña
   sobre `main` cuando el tramo 3 de S2 haya entrado, añade el nombre a la API `story` y sube la versión menor de
   `LuaApiVersion`. Así el nombre y la subida de versión salen una sola vez y funcionando de punta a punta.
2. El tramo de S2 que publica la API registra `play_sound` contra el `SoundPort` de 2a (2a entra antes).
