# 0020. Cámara de mapa con proyección descentrada y capas de profundidad como planos

- **Estado:** Aceptada
- **Fecha:** 2026-10-19

## Contexto

Los mapas de campo son un fondo prerenderizado (los tiles del `.TFS`) con los modelos 3D encima. El original lo
dibuja en una pantalla de 320 × 240 y hace que los dos casen exactamente: la cámara del mapa está fija, el fondo entero
es la imagen de esa cámara con su centro óptico en el centro del fondo, y la pantalla es una ventana que sigue al
jugador y se para en los bordes ([Mapas: cómo se dibuja](../formats/mapas.md#cómo-se-dibuja)). Los objetos del fondo
que tienen que tapar a los personajes son sprites que el original mete en la tabla de orden a una profundidad
calculada al cargar el mapa.

El motor es un juego de PC ([ADR 0013](0013-juego-nativo-de-pc.md)): cualquier resolución y proporción, sin estirar
nada, con un buffer de profundidad normal y modelos que no se ordenan por polígono.

## Decisión

- **Proyección:** una sola cámara por mapa con el *look-at* de `vp` a `vr` (los dos doblados, como el original) y una
  proyección perspectiva **descentrada**: distancia focal `H` en píxeles del fondo y centro óptico en el centro del
  fondo. Lo que se ve es una ventana del fondo; la matriz de proyección se construye a partir de esa ventana, así que
  el modelo y el fondo casan por construcción a cualquier tamaño.
- **Escala:** `s = min(ancho / 320, alto / 240)` píxeles de ventana por píxel del fondo, sin redondear a entero. Se ve
  `ancho / s × alto / s` píxeles del fondo: a 4:3 exactamente lo que el original; más ancho (16:9, 21:9) o más alto
  (16:10, 5:4), se ve más fondo por ese lado. Nunca se estira y nunca se ve menos que el original.
- **Desplazamiento:** la ventana se centra en el punto que se sigue (el jugador) y se sujeta a los bordes del fondo,
  como `cameraX = clamp(...)` del original. Si el fondo es más pequeño que la ventana por un lado, se centra y lo que
  sobra son **bandas negras**: el render las dibuja al final de las capas (`drawLayers`), encima de todo, modelos
  incluidos; no usa scissor. La esquina de la ventana se redondea a píxeles enteros de la ventana para que el fondo
  no tiemble al moverse.
- **Capas de profundidad:** cada instancia de sprite es un rectángulo alineado con la pantalla, en su posición en
  píxeles del fondo, dibujado **con profundidad** como un plano a la distancia `orderValue · 16` de la cámara, con
  `orderValue` calculado al cargar como en `calcMapObjectOrder`. Los modelos se dibujan con la misma cámara y el
  buffer de profundidad normal (prueba `LessOrEqual`, 0 cerca y 1 lejos, cerca = 100, lejos = 65 536, el final de la
  tabla de orden); las capas usan la misma prueba. Así un fragmento de modelo queda tapado cuando su distancia es al
  menos `orderValue · 16`, que es la regla del original (`SZ medio >> 4 >= orderValue`) aplicada por píxel en vez de
  por polígono.
- **Orden de dibujo:** fondo (sin profundidad), modelos, sprites de lejos a cerca. Un sprite semitransparente se
  dibuja dos veces, como hace la consola por texel: sus colores sin STP opacos (escriben profundidad) y los que
  tienen STP con su modo de mezcla (no la escriben).
- **Datos del original fuera del render:** cómo se refieren entre sí los ficheros extraídos del mapa (la X de textura
  que elige página a partir de la X 384 de VRAM con anchos de 64 o 128, las filas de CLUT 480, 481 + i y
  `clut + 468`, el centinela `y == 10000` y los límites 0x23, 0xFF6 y 0xFFE del orden) son propiedades de esos datos,
  no del dibujo. Se resuelven al cargar en `assets` (`loadMapImages`, `MapSpriteSheet`, `mapSpriteOrderValue`), con
  constantes con nombre; el render solo dibuja quads con textura y un valor de orden. El seguimiento previsto es
  resolverlo en la extracción, como una salida nueva y aditiva de `dwx` (sin cambiar las que ya hay), y que el motor
  lea el resultado directamente.

## Consecuencias

- Cualquier resolución y proporción funciona sin casos especiales, y el encuadre a 4:3 es el del original.
- Los helpers de `MapCamera` (mundo ↔ fondo ↔ ventana, picking en el suelo) son la misma cuenta que la proyección, así
  que el clic para moverse cae donde se ve.
- Con profundidad por píxel, un polígono grande que cruza el plano de un sprite queda tapado solo en la parte de
  detrás; el original lo tapaba entero o nada según su media. Es mejor así y no cambia ningún caso del recorrido.
- Los empates del original (polígono en el mismo índice que el sprite) dependían del orden de inserción; aquí gana el
  sprite.
- Con escalas no enteras los píxeles del fondo no miden todos lo mismo: el render filtra el fondo para que no se note
  (ver `engine/render`).

## Alternativas descartadas

- **Escala entera con bandas:** píxeles perfectos, pero a 1080p deja 120 píxeles de bandas arriba y abajo y a muchas
  resoluciones desperdicia media pantalla.
- **Escalar por la altura siempre (`alto / 240`):** igual en pantallas anchas, pero en una ventana más alta que 4:3
  vería menos ancho que el original.
- **Estirar el fondo a la ventana:** deforma la imagen y el 3D dejaría de casar sin cambiar la proyección.
- **Reproducir la tabla de orden (ordenar polígonos):** fiel, pero obliga a la pista de modelos a ordenar triángulos
  en la CPU y pierde el buffer de profundidad.
