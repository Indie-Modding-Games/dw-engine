# weather

El clima del campo (`dwe::game::weather`, capa `game`): despejado (soleado de día), nublado, lluvia ligera y fuerte,
nieve ligera y fuerte. **Mejora del motor**: el original no tiene clima. Es una opción («Clima» / "Weather"), activa
por defecto, y nunca cambia una regla: stats, encuentros, tiempos y colisiones son los mismos con o sin clima.

El módulo no dibuja nada: depende de `core` y `assets`. Decide qué clima toca; el juego (`apps/game`) lo dibuja y lo
suena, y la sesión guarda su semilla.

## Reglas por zona

Las reglas son datos del paquete: `field/weather.yaml` (esquema `field_weather.schema.json`, clave `field_weather`
del `pack.yaml`), que lee `loadClimateRules`.

- `period_hours` (3): cada tantas horas de juego la zona vuelve a tirar.
- `keep_chance` (0,45): probabilidad de seguir con el clima del periodo anterior; si no, sale uno por los pesos.
- `fade_seconds` (8): segundos reales que tarda en fundirse un cambio.
- `no_weather`: mapas sin clima (patrones con `*` al final). Además, ningún mapa con hora fija (interiores, cuevas,
  edificios, la arena del torneo) tiene clima, lo liste o no una zona.
- `areas`: zonas con sus mapas y pesos (`clear`, `cloudy`, `light_rain`, `heavy_rain`, `light_snow`, `heavy_snow`;
  el que falta no sale nunca). Todos los mapas de una zona comparten clima, así que no cambia al pasar de un mapa a
  otro de la misma zona. La primera zona que casa gana.

| Zona | Mapas | Pesos (despejado / nublado / lluvia / lluvia fuerte / nieve / nieve fuerte) |
|---|---|---|
| Bosque Nativo | `mayo*` | 5 / 3 / 2 / 1 / - / - |
| Jungla tropical | `trop*` | 3 / 2 / 3 / 2 / - / - |
| Monte Panorama | `miha*` | 4 / 4 / 2 / 1 / - / - |
| Overdell | `dgha*` | 1 / 5 / 3 / 1 / - / - |
| Gran Cañón | `gcan*` (no `gcan11`) | 7 / 3 / 1 / - / - / - |
| Sabana de engranajes | `gias*` | 6 / 3 / 1 / 1 / - / - |
| Región glaciar antigua | `koda01`-`koda03` | 2 / 3 / - / - / 4 / 2 |
| Regiones antiguas | resto de `koda*` | 5 / 3 / 2 / 1 / - / - |
| Freezeland | `frzl*` | 2 / 2 / - / - / 4 / 3 |
| Tierra de escarabajos | `betl*` | 4 / 3 / 2 / 1 / - / - |
| Árboles Brumosos | `mist*` | 1 / 4 / 3 / 1 / - / - |
| Pantano Geko | `stic*` | 2 / 3 / 3 / 2 / - / - |
| Villa Volumen | `gkyo*` | 4 / 3 / 2 / - / - / - |
| Ciudad de los Juguetes | `omoc*` | 5 / 3 / 1 / - / - / - |
| Ciudad Factorial | `fact*`, `gomi*` | 3 / 4 / 2 / - / - / - |
| Ciudad File | `twna*`, `twnb*`, `trai*` | 5 / 3 / 2 / 1 / - / - |

Sin clima: `tunn*`, `room*`, `tend*`, `topn*`, `mgen*`, `yaka*`, `chka*`, `saib*`, `icsa*`, `leom*`, `ogre*`,
`gcan11` y los mapas de hora fija.

## Determinismo

`weatherAt(rules, area, seed, period)` es una función pura: el clima sale de un hash (splitmix64) de la semilla de
clima, la zona y el periodo (`periodOf(rules, gameHour)`, horas desde el principio del reloj). No tira nunca los
números aleatorios del juego, así que el orden de tiradas que compara el oráculo es el mismo con o sin clima.

La semilla es el sistema de sesión `WeatherSeed` (`apps/session`, sección `weather` del guardado, `{seed}`). En una
partida nueva sale de la semilla de la sesión (`weatherSeedOf`); una partida guardada antes de que existiera el clima
no trae la sección y toma la misma semilla derivada, que se guarda en el siguiente guardado.

## Transiciones

`WeatherLook` es el aspecto del clima: cobertura de nubes, lluvia, nieve y sol, cada uno de 0 a 1. `WeatherBlend`
lo lleva hacia el del clima que toca a `1 / fade_seconds` por segundo. Cambia de golpe (sin fundido) al entrar a otra
zona o a un mapa sin clima, al cambiar la opción y con `--weather`.

## Dibujo (`apps/game`)

- `FieldScene` (`field_scene_weather.cpp`) elige la zona al cargar cada mapa, avanza el fundido cada fotograma (la
  animación se para con la pausa) y pasa los quads a `WeatherDrawer`.
- `weatherQuads` (`weather_particles.cpp`) genera los quads sin estado, por hash del número de partícula y su ciclo:
  velo gris oscuro del nublado y la lluvia, sombras de nubes que pasan (textura de ruido que se repite), calor del sol
  de día, gotas inclinadas con salpicaduras en el suelo, copos que caen con deriva. Tamaños y velocidades en píxeles
  del original, así que se ven igual a cualquier escala; la densidad sigue el ancho. De noche la lluvia y la nieve
  son más tenues, y el sol y las sombras de nubes se van.
- `WeatherDrawer` (`weather_drawer.cpp`, shader `weather.slang`) dibuja en la pasada de escena, sin profundidad,
  recortado al rectángulo del fondo del mapa (nunca sobre las bandas), después de los modelos traslúcidos y antes de
  la niebla, los destellos, los fundidos de la historia y la UI.
- No se dibuja con el mapa oculto, en escenas de cámara de la historia ni con el escenario escondido.

## Sonido

`WeatherAmbience` (`weather_ambience.cpp`) suena un bucle de lluvia y ráfagas de viento con la nieve, con su propio
contador de hash. No hay grabaciones de lluvia ni viento en los bancos extraídos; usa sustitutos: lluvia
`sound/esall/008/sample_004` (siseo continuo, en bucle con fundido cruzado de 0,25 s) y viento
`sound/esall/011/sample_004`, cada 3 a 8 s. La API de audio no cambia el volumen de una voz sonando, así que el bucle
de lluvia se reinicia en escalones de volumen (`RainStep`). Un paquete puede cambiar esos clips por otros.

## Interfaz para la iluminación

`FieldScene::weather()` devuelve el `WeatherLook` fundido de este fotograma (solo lectura): `cloudCover`, `rain`,
`snow`, `sunshine`, `intensity()`. Una pasada de luz puede usarlo para bajar el sol o apagar las sombras con nubes sin
conocer el módulo ni sus reglas. `FieldScene::weatherArea()` dice la zona (`nullptr` sin clima).

## Opción y depuración

| Qué | Dónde | Notas |
|---|---|---|
| Opción «Clima» / "Weather" | Opciones del título y del menú de pausa; ajuste `weather` de `settings.yaml` | Mejora, activa por defecto; se aplica al momento |
| `--weather <clima>` | Línea de órdenes | Fuerza un clima (`clear`, `sunny`, `cloudy`, `rain`, `heavy_rain`, `snow`, `heavy_snow`, `none`) en los mapas con clima |

## Pruebas

- `dwe_weather_tests`: zonas del paquete y mapas sin clima, la nieve solo en zonas frías, función pura y periodos,
  fundido, nombres, reglas malas.
- `dwe_session_tests` (`weather_seed_tests.cpp`): los números aleatorios del juego y del partner son los mismos con y
  sin clima; ida y vuelta del guardado; guardado viejo sin sección; sección mala.
- `dwe_game_tests` (`weather_particles_tests.cpp`): nada en despejado de noche, gotas y salpicaduras, determinismo,
  copos, fundido a la mitad, nubes.

## Pendiente

- Niebla como clima propio.
- La lluvia no se tapa bajo techos ni aleros (se dibuja sobre todo el fondo).
- Grabaciones reales de lluvia y viento, y volumen por voz en la API de audio.
