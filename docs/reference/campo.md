# Ficha de referencia: campo

Cómo se movían el jugador y el partner por los mapas de campo en el juego original, según el decomp, y qué hace con
ello `game/field`. Es el paso 1 del [ciclo de diseño](../conventions/design-cycle.md).

Marcas: **(comprobado)**, leído en el código del decomp; **(medido)**, calculado a partir de los datos del disco con
las reglas del código; **(suposición)**, interpretación nuestra pendiente de confirmar. Unidades del original: 100 son
una celda de colisión, 4096 una vuelta. Para los ejes y las conversiones ver
[espacio del mundo](../architecture/world-space.md) y [mapas](../formats/mapas.md#en-el-motor).

## Qué hace

El jugador se mueve con la cruceta en ocho direcciones relativas a la cámara del mapa. Por defecto corre; con un
botón pulsado anda. La rejilla de colisión del mapa le para en las paredes y deja deslizarse al ir en diagonal. Al
pisar ciertas celdas pasa algo: un script, el baño o un warp a otro mapa. El partner le sigue a cierta distancia,
corre si se queda atrás y, si pierde de vista al jugador, sigue su rastro de celdas.

## Reglas

### Ritmo

- El bucle del juego espera tres retrazados verticales por fotograma (`gameLoop`, `VSync(3)` en `main.c`): unos
  **20 fotogramas por segundo** en NTSC **(comprobado)**; que `VSync(3)` sean tres retrazados es el comportamiento de
  la librería de la consola **(suposición, casi segura)**. Todas las velocidades por fotograma de esta ficha son a
  ese ritmo. El motor expresa todo por segundo y corre a tick fijo (60 Hz en la app).

### Movimiento del jugador

- **Dirección** (`tamer.c: tamerTickWalkingState`) **(comprobado)**: `getMapRotation` da el ángulo de la cámara (de
  la referencia al punto de vista) y se redondea a múltiplos de 512 (45°). Arriba suma 2048, abajo 0, izquierda 1024,
  derecha 3072 y las diagonales los intermedios. El giro es instantáneo (`setTamerDirection`).
- **Velocidad**: no hay una constante. El jugador avanza con el movimiento de la raíz de su animación
  (`anim.c: tickAnimation`, `applyRootMomentum`), girado por su orientación **(comprobado)**. Con los datos del
  modelo del jugador (`BOYS.MMD`): la animación 3 (correr, la de por defecto) avanza 50 por fotograma y la 2 (andar,
  con el botón) 20; la 0 (quieto) nada **(medido)**. A 20 por segundo: **1000 y 400 por segundo**.
- El primer fotograma tras cambiar de animación no avanza **(comprobado)**; el motor no lo reproduce.

### Colisión

- La rejilla es de 100 × 100 celdas de 100 unidades; 0 libre, bit `0x80` bloqueado, el resto disparadores
  (`map_collision.c`) **(comprobado)**.
- Antes de mover, el juego prueba el paso (`map.c: entityCheckCollision`): coloca al personaje un paso adelante y,
  según el signo del paso en cada eje, mira la línea de celdas a **radio** por delante en ese eje, a lo ancho de
  **medio radio** a cada lado (`checkMapCollisionX/Z`) **(comprobado)**. El radio del jugador es 100
  (`DIGIMON_DATA[0].radius`) **(medido)**.
- Si choca (`map.c: collisionGrace`), anula la componente del movimiento hacia la pared y gira al personaje hasta
  0x50 por fotograma hacia la dirección cardinal libre más cercana **(comprobado)**. En diagonal contra una pared, se
  desliza; de frente, se queda quieto salvo ese giro.
- El jugador no choca con el partner (la prueba lo excluye) **(comprobado)**; con un NPC se para.
- Como el sondeo lateral mide un radio (100) de ancho, siempre toca dos filas de celdas salvo en posiciones exactas: un
  pasillo de una celda de ancho no se puede recorrer y uno de dos sí, por la línea entre las dos **(medido con las
  reglas del código)**. Los bordes de celda caen en una u otra según el signo de la coordenada (el código trunca), una
  diferencia de una unidad que el motor no copia: usa `floor`.

### Disparadores y warps

- Cada fotograma en estado normal se mira la celda bajo el centro del jugador (`tamer.c: checkMapInteraction`,
  `getTileTrigger`) **(comprobado)**:
  - 51–79: llama a la sección de ese número del script del mapa al pisarla;
  - 80–109: lo mismo, pero al pulsar confirmar;
  - 110–119: warp por la salida `valor - 110` del mapa: va al mapa `targetMap` y aparece en la salida `targetExit`;
  - 120: un script si el partner tiene el bit 8 de su condición (el baño, **suposición**).
- Al warp: fundido a negro de 20 fotogramas mientras el jugador sigue andando sin colisión, `changeMap`, fundido de
  vuelta (`tamerTickChangeMap`) **(comprobado)**.
- No hay protección contra volver a disparar el warp al aparecer: los puntos de aparición de los datos nunca caen en
  celdas de warp **(comprobado)**.
- **Aparición** (`tamer.c: loadMapEntities`) **(comprobado)**: el jugador aparece en `spawn` de la salida, con su
  giro. El partner aparece a 200 en la dirección cardinal de detrás del jugador (por bandas del giro: `z + 200` si
  mira hacia −z, etc.), mirándole.
- Algunas salidas no están en la tabla de warps del mapa sino en su script de eventos: la puerta de la casa de
  Jijimon (`room08`, disparador 51) llama al script 178, que lleva al jugador a la puerta y hace `warp_to` a
  `twna01` salida 2 mientras el flag 203 esté apagado **(comprobado en el script extraído)**.

### El partner

- Se mueve también con la raíz de sus animaciones **(comprobado)**. Con el partner inicial (Agumon): andar (2) 22,6
  por fotograma, andar despacio (3) 14,9 y correr (4) 43,5 **(medido)**: 452, 298 y 870 por segundo. Corre más
  despacio que el jugador.
- **Distancia** (`partner.c: getPartnerTamerCloseness`) **(comprobado)**, con R su radio (120 en Agumon):
  más lejos que 3,5 R corre, entre 2,5 R y 3,5 R anda, más cerca termina el ciclo de andar y se para. Parado, mira al
  jugador y al rato hace una animación de emoción.
- **Rumbo** (`map.c: partnerTickCollision`) **(comprobado)**: mira cada fotograma hacia el jugador, o hacia el
  centro de la celda más antigua de su rastro si lo tiene, y la quita al pisarla.
- **Rastro**: cuando el jugador cambia de celda, si la línea entre los dos (cinco líneas de celdas en paralelo,
  `isFiveTileWidePathBlocked`) está bloqueada, se apunta la celda anterior del jugador (hasta 30); si está libre, se
  borra el rastro. Si la 2.ª, 3.ª o 4.ª celda más nueva se ve desde el jugador, se quitan las posteriores
  **(comprobado)**.
- **Atascos**: si tiene rastro y tanto la celda más antigua como el partner están fuera de la pantalla, aparece en
  esa celda **(comprobado)**. Contra las paredes no hace nada: según el código el partner no choca con el mapa y
  confía en el rastro **(comprobado; no verificado en el juego)**.
- Al cambiar de mapa se borra el rastro y se coloca como en la aparición **(comprobado)**.

### Cámara

- El fondo es una imagen; al andar el jugador, se desplaza para seguirle y en los bordes del mapa el jugador se
  descentra (`map.c: tickCameraFollowPlayer`, `updateDrawingOffsets`) **(comprobado)**. Lo hace la línea de render
  del mapa, no `game/field`.

## Qué hace el motor (`game/field`)

| Original | Motor | Por qué |
|---|---|---|
| 20 fps, velocidad de la raíz de la animación | Tick fijo (60 Hz), velocidad por segundo en `field/rules.yaml` | Cualquier frecuencia; las animaciones se escalan con `AnimationIntent::speedScale` |
| Ocho direcciones, cámara redondeada a 45° | Dirección analógica relativa a la cámara exacta del mapa | Mando analógico; con teclado salen las mismas ocho |
| Prueba de colisión de un paso y anulación por eje | Igual (radio adelante, medio radio a los lados, X y luego Z), pero el paso se corta justo antes de la celda bloqueada | Nada atraviesa paredes a ningún tick |
| Giro de 0x50 hacia una dirección libre al chocar de frente | No | Con mando analógico basta el deslizamiento |
| Giro instantáneo | La cara gira a `turn_speed` (0 = instantáneo); el movimiento sigue a la entrada al momento | Se ve mejor sin cambiar el control |
| Sin clic | Clic para ir: camino A* por puntos cada media celda (centros, bordes y esquinas), en 8 direcciones, solo por pasos que el jugador anda con la colisión del original aunque vaya 20 unidades desviado, y sin pisar warps que no son el destino | Juego de PC ([ADR 0013](../adr/0013-juego-nativo-de-pc.md)); el seguidor nunca se queda en un borde |
| Disparador cada fotograma sobre la celda | Al entrar en una celda con disparador (no mientras se está encima) | Evita warps repetidos aunque un dato cambie |
| Warps de script | `field/warps.yaml`: mapa + disparador → mapa + salida, con la prueba | No se leen scripts en tiempo de ejecución (fase 4) |
| Partner sin colisión con el mapa, aparece en el rastro si está fuera de pantalla | Partner con la misma colisión; si no avanza durante `stuck_seconds` o se aleja más de `teleport_distance`, aparece detrás del jugador | `game/field` no sabe qué hay en pantalla; nunca se queda atascado |
| Animaciones de emoción del partner parado | No | Fase 3 (cuidado) |

## Datos

- `map/<id>/data`: colisión, warps y cámara de cada mapa ([mapas](../formats/mapas.md)).
- `data/maps`: nombres de carga. File City es el nombre 43 y la casa de Jijimon el 48 **(comprobado)**.
- `content/packs/game/field/rules.yaml` y `warps.yaml` (esquemas `field_rules` y `field_warps` en
  `content/schemas/`).

## El recorrido de la fase 2

- **File City** es `twna01` (índice 204, nombre de carga 43): los mapas `twna01`–`twna13` y `twnb01`–`twnb24` son la
  ciudad en sus distintas fases; `twna01` es la del principio, la única cuyo NPC es solo Jijimon y su gente, y la que
  el script de la casa elige con el flag 203 apagado **(comprobado; que sea la primera fase, suposición razonable)**.
- **La casa de Jijimon** es `room08` (índice 218, nombre de carga 48): la salida 2 de `twna01` (disparador 112) lleva
  a su salida 0. `room10` y `room11` también se llaman así al cargar; son otra versión de la casa con escaleras
  **(suposición)**.
- **La vuelta**: `room08` no tiene warps en su tabla; su puerta (disparador 51) es el warp de script a `twna01`
  salida 2, delante de la casa.
- El campo empieza en `twna01`, salida 1 (la que viene de `trai00`, la entrada a la ciudad).

## Dónde está en el decomp

- `src/main/tamer.c`: `tamerTickWalkingState`, `getMapRotation`, `setTamerDirection`, `checkMapInteraction`,
  `loadMapEntities`, `tamerTickChangeMap`.
- `src/main/map_collision.c`: `getTileTrigger`, `checkMapCollisionX/Z`, `getModelTile`.
- `src/main/map.c`: `entityCheckCollision`, `collisionGrace`, `partnerTickCollision`, `isFiveTileWidePathBlocked`,
  `tickCameraFollowPlayer`.
- `src/main/anim.c`: `tickAnimation`, `tickMomentum`, `applyRootMomentum`.
- `src/main/partner.c`: `partnerTickNormal`, `partnerTickWalking`, `getPartnerTamerCloseness`.
- `src/main/partner_impl.c`: `setupPartnerOnWarp`.
- Scripts: `MAPHEAD` sección 218 (`set_script 178`) y el script 178, sección 0x33.
