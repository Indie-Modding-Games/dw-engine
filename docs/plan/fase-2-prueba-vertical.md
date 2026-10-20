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

## Estado (2026-10-19)

En curso, en cuatro líneas en paralelo: render del mapa, modelos, campo e interfaz. Entran en `main` cuando la fase 1
termine de integrarse.
