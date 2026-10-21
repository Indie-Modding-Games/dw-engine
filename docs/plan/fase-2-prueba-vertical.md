# Fase 2: Prueba vertical

## Objetivo

Un trozo pequeño del juego que se juega de punta a punta con el motor nuevo: el jugador camina de File City a la casa
de Jijimon y vuelve, con el partner siguiéndole y un minimapa en pantalla, a cualquier resolución y con teclado, ratón o
mando.

## Por qué

Una "prueba vertical" atraviesa todas las capas del motor en un caso real y pequeño: datos extraídos, render, escena,
lógica de campo, entrada e interfaz. Si la arquitectura tiene un fallo, aparece aquí, cuando arreglarlo es barato, y
no en la fase 3 con medio juego encima. También es la primera mejora visible respecto al original: resolución libre,
menús con ratón y minimapa.

## Alcance

- **Render del mapa:** fondos prerenderizados con capas de profundidad (los personajes quedan delante o detrás de los
  objetos del fondo según su posición) y la cámara propia de cada mapa.
- **Modelos:** el jugador y el partner, con skinning y animaciones, a partir de los `.glb` extraídos.
- **Campo:** movimiento, colisión, cambio de mapa por warps y el partner siguiendo al jugador.
- **Interfaz:** RmlUi integrado ([ADR 0009](../adr/0009-rmlui.md)), un primer menú nuevo que se usa con ratón y con
  mando, y el minimapa.

## Fuera de alcance

- Diálogos, NPCs con comportamiento y eventos de historia → fase 4.
- Stats, cuidado, evolución y batalla del partner → fase 3.
- Música y sonido de ambiente completos: pueden sonar, pero no forman parte del criterio de cierre.
- Mapas fuera del recorrido de la prueba: se cargan con el mismo código, pero no se revisan uno a uno.

## Requisitos

| ID | Requisito | Cómo se comprueba |
|---|---|---|
| R2.1 | El motor carga un mapa por su ID desde el VFS (fondo, capas de profundidad, cámara, colisión y warps) sin leer formatos de PS1 | Test de carga de los mapas del recorrido |
| R2.2 | El fondo se dibuja a la resolución de la ventana y con cualquier proporción (16:9, 16:10, ultrapanorámico), sin deformarse | Capturas a varias resoluciones; prueba manual al cambiar el tamaño |
| R2.3 | Las capas de profundidad tapan a los personajes cuando pasan por detrás de un objeto del fondo | Captura de referencia en puntos concretos del recorrido |
| R2.4 | Cada mapa usa su cámara: la vista del modelo 3D coincide con la del fondo | Comparación visual en los mapas del recorrido |
| R2.5 | Jugador y partner se dibujan con skinning y cambian de animación (quieto, andar, correr) | Tests de la pose; prueba manual |
| R2.6 | El jugador se mueve con acciones de entrada, igual con teclado que con mando, y la colisión le impide atravesar paredes y objetos | Test de colisión con trayectorias fijas |
| R2.7 | Al cruzar un warp, se carga el mapa de destino y el jugador aparece en el punto correcto | Test del recorrido: la secuencia de mapas es la esperada |
| R2.8 | El partner sigue al jugador sin quedarse atascado y cambia de mapa con él | Test del recorrido y prueba manual |
| R2.9 | Las entidades (jugador, partner) viven en el ECS de `scene` ([ADR 0012](../adr/0012-ecs-con-entt.md)) | Revisión de código |
| R2.10 | RmlUi dibuja con el módulo `render`, a resolución nativa, y su lógica va en Lua | Test de arranque de la UI; revisión |
| R2.11 | El primer menú se maneja entero con ratón, entero con teclado y entero con mando; los avisos de botones cambian según el dispositivo ([ADR 0013](../adr/0013-juego-nativo-de-pc.md)) | Prueba manual con los tres dispositivos |
| R2.12 | El minimapa se genera a partir de los datos del mapa (colisión y warps) y muestra la posición del jugador | Captura en cada mapa del recorrido |
| R2.13 | El juego corre sin errores de validación de Vulkan y sin bajar de 60 imágenes por segundo en el recorrido | Ejecución en el host con la capa de validación |

## Diseño

- Módulos nuevos o ampliados: `render` (fondos, capas de profundidad, skinning), `scene` (ECS con EnTT),
  `game/field` (movimiento, colisión, warps, seguimiento), `ui` (RmlUi y minimapa).
- El motor solo carga lo que deja la fase 1 por el VFS. Si a un mapa le falta un dato, se añade al extractor y a su
  esquema, no se lee del disco.
- Los valores de movimiento (velocidades, distancia del partner) van en datos, no fijos en C++. Son el primer paso
  hacia los rulesets de la fase 3 ([ADR 0010](../adr/0010-reglas-propias-con-rulesets.md)).
- Cada decisión grande (cómo se hacen las capas de profundidad, cómo se integra RmlUi) queda en un ADR.

## Criterio de cierre

Se puede caminar de File City a la casa de Jijimon y volver, con minimapa, a cualquier resolución y con cualquier
dispositivo de entrada. Se demuestra con dos pruebas:

- un test automático que recorre el camino y comprueba la secuencia de mapas y la llegada;
- una ejecución del juego en el host sin errores de validación.

## Dependencias y riesgos

- Depende de la extracción de la fase 1: fondos, modelos, mapas, colisión y warps. No necesita la compresión del
  cocinado, por eso empezó antes de que la fase 1 cerrara del todo.
- Riesgo: las capas de profundidad del original pueden no casar con una cámara a otra resolución. Se compara con
  capturas del original en cada mapa del recorrido.
- Riesgo: integrar RmlUi con nuestro render Vulkan. Es la primera vez que una librería de terceros dibuja con `render`.

## Estado (2026-10-20)

Cerrada. Las cuatro líneas (render del mapa, modelos, campo e interfaz) están integradas en el juego (`apps/game`) y
el recorrido pasa con teclado, mando y ratón, con 0 mensajes de validación y 0 errores en el log.
Cómo jugarlo y cómo pasar las pruebas: [guía de la prueba vertical](../guides/prueba-vertical.md).

| Requisito | Cómo se cumple | Prueba |
|---|---|---|
| R2.1 | `loadMapDocument` y `loadMapImages` cargan fondo, capas, cámara, colisión y warps por asset id; el motor no incluye `formats/` | `engine/assets/tests/map_content_tests.cpp`; registro `field: entered ...` de cada cambio de mapa |
| R2.2 | El fondo se escala a la ventana conservando la proporción | Capturas de las rutas a 800x600, 1280x720, 1920x1080 y 2560x1080 |
| R2.3 | Capas de profundidad entre los modelos opacos y los translúcidos ([ADR 0020](../adr/0020-camara-de-mapa-y-capas-de-profundidad.md)) | `map_layers_tests.cpp`; captura `twna01-behind-layer` de cada ruta |
| R2.4 | La cámara del mapa se sincroniza con la de los modelos | `map_camera_tests.cpp`; capturas de los dos mapas |
| R2.5 | Animación en CPU y skinning en GPU ([ADR 0021](../adr/0021-animacion-en-cpu-y-skinning-en-gpu.md)); quieto, andar y correr | `animation_tests.cpp`, `model_renderer_tests.cpp`; paso `walk: true` de las rutas |
| R2.6 | Acciones `move_*` y `walk`, relativas a la cámara; colisión por celdas; clic para ir a un punto | `game/field/tests/field_tests.cpp`, `input_state_tests.cpp`; las tres rutas |
| R2.7 | Los warps cargan el destino y colocan al jugador en la salida | Rutas: secuencia `twna01 → room08 → twna01` |
| R2.8 | El partner sigue y cambia de mapa con el jugador | Pasos `wait` con `partner_within` de las rutas, tras cada cambio de mapa y tras caminar dentro de cada mapa |
| R2.9 | Jugador y partner son entidades de `scene` | `scene_tests.cpp`; revisión de `apps/game/src/field_scene.cpp` |
| R2.10 | RmlUi dibuja con `render` a resolución nativa; la lógica del HUD y del menú va en Lua | `minimap_tests.cpp`, `navigation_tests.cpp`, `prompts_tests.cpp` |
| R2.11 | Menú de pausa con ratón, teclado y mando; los avisos siguen al último dispositivo | Pasos `menu` y `menu_select` de cada ruta, con su dispositivo; capturas `menu-open` |
| R2.12 | Minimapa desde colisión y warps, con la posición del jugador; se oculta y se muestra desde el menú | `minimap_tests.cpp`; pasos `expect_setting` de las rutas |
| R2.13 | 0 mensajes de validación; por encima de 60 fps | Rutas en debug con validación y en release (abajo) |

Medidas en el host (NVIDIA, 1920x1080 a 144 Hz, build release, sin los frames lentos a propósito):

- Con vsync, 144 fps de media en las tres rutas y un mínimo de 78 a 119. En otras pasadas ha habido frames sueltos
  de ~34 ms: son esperas de la presentación, porque sin vsync el peor frame no pasa de 7 ms.
- Sin vsync (modo `immediate`), entre 3.700 y 5.000 fps de media, un mínimo de 144 a 290 y un percentil 99 de unos
  800 a 1.100.
- Cambio de mapa (lectura y preparación, sin contar el fundido de un segundo a cada lado): `twna01 → room08`
  4–6 ms, `room08 → twna01` 15–40 ms.
- En debug con la capa de validación, las tres rutas pasan con 0 mensajes.

El campo se ha comprobado contra el decomp (movimiento, cámara, warps y fundido, colisión, partner y hora del día):
lo que reproduce del original lo cita en el código, y lo que hace distinto está, con el motivo, en la
[ficha del campo](../reference/campo.md#qué-hace-el-motor-gamefield).

Pendiente o conocido:

- Una ventana no puede ser mayor que el escritorio usable: en una pantalla de 1920x1080 con paneles, los tamaños
  1920x1080 y 2560x1080 en ventana se saltan, y se cubren en un Xvfb con lavapipe (sin pantalla completa).
- Del partner faltan el paso según su estado y las animaciones de emoción: esperan al cuidado (fase 3).
- La salida de la casa del anciano queda en el borde de la imagen; con ratón se llega manteniendo el botón (el
  jugador sigue al puntero y, en el borde, sigue andando hacia fuera).
- `dwx validate --engine --schemas content/schemas --with <contenido>/base content/packs/game` sobre una extracción nueva de
  `base` da 0 problemas en los dos paquetes (4828 assets, 2084 documentos, 178 contratos). Una extracción anterior
  a los esquemas del paquete `base` da problemas en `base`; hay que volver a extraer.
