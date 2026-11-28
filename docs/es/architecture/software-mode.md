# Modo software

Cuando ninguna tarjeta gráfica puede mover el juego, el lanzador lo arranca con lavapipe, el driver Vulkan de Mesa que
dibuja con el procesador ([lanzador: modo de compatibilidad](../modules/apps/launcher.md#requisitos-del-equipo)). El
juego lo nota solo, arranca con un ajuste ligero, adapta la calidad al tiempo de cada fotograma mientras corre y se lo
dice al jugador una vez por partida. Todo sigue siendo una opción que el jugador puede cambiar.

## Cuándo se activa

`apps/game/src/software_mode.hpp`. El juego pregunta al dispositivo que creó por su tipo (`rhi::deviceGpu`): un
dispositivo `VK_PHYSICAL_DEVICE_TYPE_CPU` (lavapipe, llvmpipe) es software. `--software-mode auto|on|off` decide:

| Valor | Qué hace |
|---|---|
| `auto` (por defecto) | Activo con un dispositivo de CPU. Los autotests lo dejan apagado, para que las rutas conserven el aspecto que esperan sus capturas |
| `on` | Siempre activo, también con una GPU real: para probar el ajuste, el aviso y el ajustador, y para medirlos |
| `off` | Nunca |

## El ajuste inicial

Al arrancar el juego elige un nivel de calidad según los hilos con los que dibuja lavapipe (`LP_NUM_THREADS` si está
puesto, si no los de la máquina): con 2 o menos empieza en 0, hasta 4 en 1, hasta 8 en 2, con más en 3. Cada nivel
pone:

| Nivel | Escala de renderizado | Sombras | Texturas de modelos | Luz mejorada, colores vivos | Clima |
|---|---|---|---|---|---|
| 0 | 50 % | No | Píxeles | No | No |
| 1 | 75 % | No | Píxeles | No | No |
| 2 | 100 % | Redondas | Píxeles | No | No |
| 3 | 100 % | Redondas | Suaves | No | Sí |
| 4 | 100 % | Del sol | Suaves | Sí | Sí |

En todos los niveles el suavizado de bordes está apagado, la calidad de escenarios es suave (no corre el ampliador) y
el límite de fotogramas es 30. "Calidad automática" empieza activada.

**Mandan las elecciones del jugador.** El ajuste solo pone las opciones que `settings.yaml` no guarda, y nunca las
escribe ahí: se vuelve a aplicar en cada arranque mientras el jugador no elija. Qué opciones son automáticas lo lleva
`GraphicsChanger::automatic()` (`GraphicsKnobs`, que arma `unchosenKnobs`); en cuanto el jugador cambia una en el menú
se guarda, deja de ser automática y ni el ajuste ni el ajustador vuelven a tocarla. `--low-performance` sigue mandando
sobre el ajuste en las seis opciones de aspecto (la escala y el límite siguen automáticos).

## Calidad automática

`QualityTuner` y `decideQuality`, puros y probados en `apps/game/tests/software_mode_tests.cpp`. Solo mientras hay un
campo en pantalla (sin película ni carga), y solo sobre las opciones automáticas:

- Una **ventana** dura 3 s y al menos 20 fotogramas. Mide el tiempo ocupado de cada fotograma: su tiempo sin la espera
  del límite de fotogramas. Un fotograma de más de 1 s (una carga) empieza la ventana de nuevo.
- **Baja** un nivel cuando la media del tiempo ocupado de la ventana pasa del presupuesto (1 / límite, o 1/30 sin
  límite) en un 8 %, y **dos** cuando lo pasa en un 75 % (una máquina lenta llega a un nivel jugable en una ventana).
- **Sube** un nivel cuando el percentil 90 deja sin usar el 40 % del presupuesto.
- **Histéresis.** Tras un cambio no cuentan los primeros 2 s, y el nivel que el ajustador tuvo que dejar pasa a ser el
  techo el resto de la partida, así que nunca oscila entre dos niveles. Entre los dos umbrales no cambia nada.

La opción "Calidad automática" (`auto_quality`) lo enciende o lo apaga; solo viene activada en modo software.

## Las opciones nuevas

Están en Opciones > Gráficos en cualquier modo, después de la sincronización vertical, y se guardan en
`settings.yaml`:

| Opción | Valores | Qué hace |
|---|---|---|
| Calidad automática (`auto_quality`) | sí, no | El ajustador de arriba |
| Escala de renderizado (`render_scale`) | 100, 75, 50 | La pasada de escena dibuja a esa fracción de la ventana (`FrameDesc::sceneScale`, `sceneTargetExtent`) y la pasada de post la estira, con el texel más cercano; la interfaz sigue al tamaño de la ventana. Un fotograma sin pasada de post usa la del renderizador (un `SceneGrade` con valores neutros). Caminar con el ratón escala el puntero a la escena (`FieldScene::pickGround`) |
| Límite de fotogramas (`frame_limit`) | no, 60, 30 | El bucle del juego duerme para no pasar de esos fotogramas por segundo (`FrameLimiter`); un fotograma tardío no hace correr a los siguientes |

`--graphics scale=100|75|50,fps=off|30|60,auto=on|off` las pone para una partida.

## El aviso

`ui/software_notice` se ve 12 s al arrancar (una película no cuenta, así que se ve tras la intro), encima de todo y
sin tomar teclas: "Jugando sin tarjeta gráfica", que el procesador dibuja el juego y puede ir lento, que la calidad se
ajusta sola y dónde cambiarla, y cómo arreglarlo (instalar o actualizar el controlador de la tarjeta gráfica desde la
web de su fabricante). Está traducido a los nueve idiomas (`localization/<idioma>/software_notice.yaml`). El ajuste
`software_mode` dice si aplica. Cuando el launcher sabe por qué no hay tarjeta gráfica (`DWE_GRAPHICS_REASON`,
[diagnóstico gráfico](graphics-diagnosis.md)), la última línea nombra esa razón y su arreglo. El mismo aviso, con el
título «Modo gráfico compatible», sale una vez por partida cuando la tarjeta va por el modo compatible de Vulkan 1.2
(`software_notice.kind`).

## Tarjetas gráficas poco potentes y ajustes gráficos

Una tarjeta que mueve el juego pero puede ir justa, integrada o en el modo compatible de Vulkan 1.2 (`game.cpp`,
`weakGpu`), empieza las opciones que el jugador no eligió desde el ajuste **Equilibrado** y activa la calidad
automática salvo que el jugador la haya apagado. `SlowFrameWatch` (`software_mode.hpp`) mira ventanas de 5 s mientras
hay un campo en pantalla: si la mayoría de imágenes tarda más de 25 ms (menos de 40 fps) baja un ajuste, espera 3 s y
nunca vuelve a subir. Una tarjeta dedicada en la ruta Vulkan 1.3 o 1.4 empieza exactamente igual que antes, con el
aspecto de serie; las pruebas automáticas y `--low-performance` mantienen su propio inicio.

Opciones › Gráficos tiene una fila **Ajuste gráfico** (`graphics_preset`: rendimiento, equilibrado, calidad;
«personalizado» si las opciones no coinciden con ninguno) que pone todas las opciones gráficas a la vez y las guarda
como elección del jugador, y una fila **Dibuja con** que muestra el modo (tarjeta gráfica, tarjeta en modo compatible,
procesador); su descripción nombra el dispositivo y, mientras la calidad automática elige, por qué
(`graphics.auto_reason`: software, integrated, compatible, slow).

| Ajuste | Escala | Sombras | Texturas de modelos | Escenario | Iluminación, colores vivos | Antialiasing |
|---|---|---|---|---|---|---|
| Rendimiento | 75 % | No | Píxeles | Suave | No | No |
| Equilibrado | 100 % | Redondas | Suaves | Suave | Sí | No |
| Calidad (el aspecto de serie) | 100 % | Sol | Mejoradas | Mejorado | Sí | Sí |

El límite de fps y la calidad automática no forman parte de un ajuste. La interfaz se dibuja al tamaño de la ventana
después de estirar la escena, así que sigue nítida al 75 y al 50 % de escala.

## La caché de pipelines

Todos los pipelines se construyen con un mismo `VkPipelineCache` (`Device::pipelineCache`), que se carga al arrancar
desde `<datos del usuario>/cache/pipelines.bin` y se vuelve a escribir a los 15 s de partida y al cerrar el juego, solo
si creció. Un archivo cuya cabecera nombra otra GPU, otro dispositivo u otro controlador
(`VkPipelineCacheHeaderVersionOne`) se ignora. Las pruebas automáticas y las partidas por canal de control no guardan
ninguna.

## Mediciones y requisitos mínimos

Medido el 2026-11-24 en una sola máquina: un AMD Ryzen 5 3600 (6 núcleos, 12 hilos, hasta 4,2 GHz, AVX2), 31 GB
de RAM, lavapipe de Mesa (LLVM 20, vectores de 256 bits) bajo Xvfb a 1280x720, compilación de release. La máquina
estaba compartida con otros trabajos (carga media de 2 a 4 durante las pruebas), así que los números son una
orientación, no un benchmark. La ruta se queda 20 s en File City; la media estable salta los primeros fotogramas.
"Antes" es el juego sin modo software (`--software-mode off`), "después" con él, "ajuste" con él pero sin límite de
fotogramas ni ajustador.

| `LP_NUM_THREADS` | Antes | Después | Nivel al que llegó el ajustador | Ajuste sin límite |
|---|---|---|---|---|
| 1 | 8,8 fps | 30,0 fps | 0 (50 %) | 39,2 fps |
| 2 | 15,4 fps | 30,0 fps | 1 (75 %) | 66,8 fps (50 %) |
| 4 | 26,4 fps | 29,8 fps | 3 (100 %) | 77,1 fps (75 %) |

En una ruta más larga con una evolución y 2 hilos el ajustador subió hasta el nivel 4, no llegó al presupuesto y se
quedó en 2 el resto de la partida (27,8 fps de media, con el límite de 30 como máximo). El juego llegó a 830 MB de
memoria.

Requisitos mínimos sin tarjeta gráfica, sacados de estos números y dejando margen para procesadores más viejos, que
hacen menos trabajo por núcleo que este:

- **Procesador:** de 64 bits con 4 hilos (2 núcleos con dos hilos cada uno, o 4 núcleos) a 3 GHz o más, con AVX2
  (casi todos desde 2015). lavapipe dibuja con todos menos lo que necesita el propio juego; con un solo hilo de
  dibujo esta máquina aguanta 30 fps al 50 %, así que un procesador más viejo de esta clase debería seguir siendo
  jugable en el nivel 0, con algún bajón.
- **Memoria:** 4 GB (el juego usa menos de 1 GB; el resto es del sistema).
- **Mejor:** 6 hilos o más a 3,5 GHz o más llegan a escala completa a 30 fps.

Por debajo de eso el juego arranca igual, pero va a menos de 30 fps incluso en el nivel 0.

### El primer arranque

Medido el 2026-11-28 en la misma máquina, tranquila (carga media de 3 a 12), con `--new-game --start twna02` y datos
de usuario nuevos cada vez; tres pruebas por fila, todas a menos de 0,1 s entre sí.

| `LP_NUM_THREADS` | Del arranque al campo, primera vez | Segunda vez | Pipelines construidos | Su tiempo de construcción (el más lento) |
|---|---|---|---|---|
| 2 | 1,7 s | 1,7 s | 61 | 0,9 s (145 ms) |
| 4 | 1,7 s | 1,7 s | 61 | 0,9 s (148 ms) |

Todos los pipelines se construyen antes de que aparezca el campo, así que ninguno da tirones durante el juego.
lavapipe no guarda nada en un `VkPipelineCache` (el archivo se queda en su cabecera de 32 bytes) y su propia caché de
shaders de Mesa tampoco cambió el tiempo, así que en un equipo sin tarjeta gráfica el segundo arranque va igual de
rápido que el primero. La caché de pipelines del juego es para los controladores que sí la llenan. Con la máquina
ocupada en otros trabajos (carga media de 30 a 55) el mismo arranque tardó de 2 a 22 s, que es lo que puede esperar un
jugador con muchas cosas abiertas.
