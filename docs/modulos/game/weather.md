# weather

El clima del campo (`dwe::game::weather`, capa `game`): despejado (soleado de día), nublado, lluvia ligera y fuerte,
nieve ligera y fuerte, y las estaciones. **Mejora del motor**: el original no tiene clima ni estaciones. Son dos
opciones («Clima» / "Weather" y «Estaciones» / "Seasons"), activas por defecto, y nunca cambian una regla: stats,
encuentros, comida, evolución, precios, eventos, tiempos y colisiones son los mismos con o sin ellas.

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

## Estaciones

Primavera, verano, otoño e invierno se turnan cada `seasons.days` días de juego (3) desde el año 0, día 0, empezando
por la primavera (`seasonOfDay`, `seasonOfHour`): el mismo contador de horas que usa el clima. Con 3 días, un partner
nuevo (unos 15 días de vida, `rules/original/life.yaml`) ve las cuatro y vuelve la primavera; un día de juego son unos
24 minutos reales, así que una estación dura más de una hora de juego. El ciclo (12 días) no va con el año del
calendario (30 días): es solo aspecto.

Cada estación del bloque `seasons` de `field/weather.yaml` tiene:

- `odds`: multiplica el peso de cada clima de la zona. `rain_to_snow`: la parte de la lluvia que cae como nieve
  (ligera como ligera, fuerte como fuerte) en las zonas con `snow_from_rain` (sí por defecto). `seasonalWeights` da
  los pesos en centésimas.
- `grade`: el color del escenario. Los verdes del follaje (unos 60 a 170 grados de tono, con algo de color) giran
  hacia `foliage_hue` en `foliage`; luego `saturation`, `gain` por canal y `lift` (más pálido).
- `petals`, `leaves`: pétalos o hojas que caen, de 0 a 1.

| Estación | Clima (multiplica) | Color | Partículas |
|---|---|---|---|
| Primavera | lluvia ×1,5, lluvia fuerte ×1,2, nieve ×0,4 | verdes hacia 105° (frescos) 0,25, saturación 1,18, algo más verde | pétalos |
| Verano | despejado ×1,8, nublado ×0,8, lluvia ×0,6 | saturación 1,15, ganancia cálida (1,1 / 1,05 / 0,9), algo más claro | — |
| Otoño | nublado ×1,3, lluvia ×1,5, lluvia fuerte ×1,3 | verdes hacia 25° (naranja, algo más oscuros) 0,42, ganancia (1,06 / 0,97 / 0,86) | hojas |
| Invierno | nieve ×1,6, nublado ×1,3; 75 % de la lluvia como nieve | saturación 0,6, ganancia fría (0,94 / 0,99 / 1,08), más pálido (0,07) | — |

Las zonas con clima propio guardan su identidad: `season_strength` acerca la estación a «sin cambio» (los factores,
el color y las partículas) y `snow_from_rain: false` impide la nieve.

| Zona | `season_strength` | `snow_from_rain` |
|---|---|---|
| Jungla tropical | 0,5 | no |
| Gran Cañón | 0,4 | no |
| Sabana de engranajes | 0,7 | no |
| Región glaciar antigua, Freezeland | 0,4 | (sin lluvia) |
| Resto | 1 | sí |

Con estaciones, `weatherAt(..., seasonal = true)` saca el clima con los pesos de la estación del periodo que tira
(un clima sacado al final de una estación puede seguir en las primeras horas de la siguiente). Apagadas (o sin bloque
`seasons`), sale exactamente el clima de siempre: mismas tiradas, mismos pesos.

El aspecto es `SeasonLook` (color, pétalos y hojas); `SeasonBlend` lo funde como el clima (`fade_seconds`) al cambiar
de estación a medianoche, y cambia de golpe al entrar a otra zona, a un mapa sin clima (neutro) o al cambiar la
opción. Solo hay estación visible en los mapas con clima: los interiores, cuevas y mapas de hora fija no cambian.

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
- `seasonQuads` (`weather_particles.cpp`) genera los pétalos (óvalos rosados) y las hojas (puntiagudas, con nervio;
  naranja, rojo, amarillo y marrón) que caen despacio con el viento, giran y se voltean. Pocos (18 pétalos o 14
  hojas por el ancho del original), un 40 % bajo lluvia o nieve fuerte, más tenues de noche.
- El color de la estación lo aplica `render::MapRenderer::setGrade` (`MapGrade`, shader `map.slang`) al fondo, las
  capas de profundidad y los objetos del suelo, nunca a los modelos ni a las bandas; el `lift` es más suave de noche.
  Un color neutro dibuja los colores tal cual.
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
| Opción «Estaciones» / "Seasons" | Opciones del título y del menú de pausa, debajo de «Clima»; ajuste `seasons` de `settings.yaml` | Mejora, activa por defecto; se aplica al momento; apagada = como sin estaciones |
| Nombre de la estación | Menú de pausa, tras el día (piel moderna; ajuste de UI `season`) | Vacío con la opción apagada; la piel original no tiene sitio |
| `--season <estación>` | Línea de órdenes | Fuerza el aspecto y el nombre de una estación (`spring`, `summer`, `autumn`/`fall`, `winter`; el clima sigue el calendario); `none` las apaga como la opción |

## Pruebas

- `dwe_weather_tests`: zonas del paquete y mapas sin clima, la nieve solo en zonas frías, función pura y periodos,
  fundido, nombres, reglas malas. Estaciones: estación por día (3 días, primavera primero, días negativos), nieve en
  invierno y más sol en verano en el bosque, las zonas de clima propio (Freezeland sigue nevando, la jungla y el
  cañón nunca nievan), apagadas = mismo clima en cada periodo que sin estaciones, fundido del aspecto, estaciones
  malas.
- `dwe_session_tests` (`weather_seed_tests.cpp`): los números aleatorios del juego y del partner son los mismos con y
  sin clima; ida y vuelta del guardado; guardado viejo sin sección; sección mala.
- `dwe_game_tests` (`weather_particles_tests.cpp`): nada en despejado de noche, gotas y salpicaduras, determinismo,
  copos, fundido a la mitad, nubes; pétalos y hojas (cuántos, se mueven, menos con lluvia, más tenues de noche).

## Pendiente

- Las estaciones no cambian los personajes ni el mapa del mundo; tampoco hay nieve acumulada en el suelo.

- Niebla como clima propio.
- La lluvia no se tapa bajo techos ni aleros (se dibuja sobre todo el fondo).
- Grabaciones reales de lluvia y viento, y volumen por voz en la API de audio.
