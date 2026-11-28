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
`software_mode` dice si aplica.

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
