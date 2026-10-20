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

- **Dirección** (`tamer.c: tamerTickWalkingState`) **(comprobado)**: `getMapRotation` da el rumbo de la cámara (de la
  referencia al punto de vista) y se redondea a múltiplos de 512 (45°) con `rot / 0x200` y un redondeo hacia arriba
  si el resto es 0x100 o más; con rumbos negativos la división trunca hacia cero y el redondeo no es simétrico.
  Arriba suma 0x800, abajo 0, izquierda 0x400, derecha 0xC00 y las diagonales los intermedios; arriba gana a abajo e
  izquierda a derecha. El giro es instantáneo (`setTamerDirection`).
- **Correr y andar** **(comprobado)**: con la cruceta pulsada, la animación 2 (andar) si se mantiene R1 o el botón de
  cancelar, y si no la 3 (correr); sin cruceta, la 0 (quieto). Correr es lo normal.
- **Velocidad**: no hay una constante. El jugador avanza con el movimiento de la raíz de su animación
  (`anim.c: tickAnimation`, `applyRootMomentum`), girado por su orientación **(comprobado)**. Con los datos del
  modelo del jugador (`BOYS.MMD`): la animación 3 (correr) avanza 50 por fotograma y la 2 (andar) 20; la 0 (quieto)
  nada **(medido)**. A 20 por segundo: **1000 y 400 por segundo**.
- Contra una pared la animación sigue a su velocidad: la colisión anula el avance, no la animación **(comprobado)**.
- El primer fotograma tras cambiar de animación no avanza (`anim.c: startAnimation` pone a cero el impulso)
  **(comprobado)**; el motor no lo reproduce.

### Colisión

- La rejilla es de 100 × 100 celdas de 100 unidades; 0 libre, bit `0x80` bloqueado, el resto disparadores
  (`map_collision.c`) **(comprobado)**.
- Antes de mover, el juego prueba el paso entero (`map.c: entityCheckCollision`, `checkMapCollision`): coloca al
  personaje un paso adelante (`entityMoveForward`) y, según el signo del paso en cada eje, mira la línea de celdas a
  **radio** por delante en ese eje, a lo ancho de **medio radio** a cada lado (`checkMapCollisionX/Z`)
  **(comprobado)**. El radio del jugador es 100 (`DIGIMON_DATA[0].radius`) **(medido)**.
- Si choca (`map.c: collisionGrace`), prueba las cuatro direcciones cardinales en un orden fijo que depende de su giro
  (`COLLISION_GRACE_ROTATION`). Cada candidata bloqueada anula esa componente del movimiento; en la primera libre,
  el personaje se desvía hacia ella hasta 0x50 en ese fotograma. Como `setTamerDirection` vuelve a poner el giro de la
  cruceta cada fotograma, el desvío no se acumula: de frente contra una pared el jugador avanza de lado a un 12 % de
  su velocidad, y solo se para si las cuatro están bloqueadas **(comprobado en el código; no visto en el juego)**.
- La primera prueba excluye al partner; `collisionGrace` sí lo cuenta entre las candidatas **(comprobado)**. Con un
  NPC se para.
- Como el sondeo lateral mide un radio (100) de ancho, siempre toca dos filas de celdas salvo en posiciones exactas: un
  pasillo de una celda de ancho no se puede recorrer y uno de dos sí, por la línea entre las dos **(medido con las
  reglas del código)**. `getModelTile` calcula la columna como x / 100 + 50 y la fila como 50 - z / 100 con la
  división de C (que trunca hacia cero) y luego resta uno a la columna si x < 0 y a la fila si z > 0. Da lo mismo que
  `floor` salvo en los bordes exactos: una x múltiplo negativo de 100 cae una columna más a la izquierda, y una z igual
  a 0 o múltiplo negativo de 100, una fila más allá. `checkMapCollisionX/Z` calculan la fila de otra forma
  (49 - z / 100, más uno si z < 0), que solo se separa de `getModelTile` en z = 0. El motor usa `floor` en todos los
  casos.

### Disparadores y warps

- Cada fotograma en estado normal (`TAMER_STATE` 0) se mira la celda bajo el centro del jugador
  (`tamer.c: checkMapInteraction`, `getTileTrigger`) **(comprobado)**:
  - 51–79: llama a la sección de ese número del script del mapa, si no hay otro script en marcha;
  - 80–109: lo mismo, pero al pulsar confirmar;
  - 110–119: warp por la salida `valor - 110` del mapa: va al mapa `targetMap` y aparece en la salida `targetExit`;
  - 120: un script (sección 0x4E2) si el partner tiene el bit 8 de su condición (el baño, **suposición**).
- **El cambio de mapa** (`tamer.c: tamerTickChangeMap`, `fade.c`) **(comprobado)**: el reloj se para
  (`stopGameTime`) y la cámara deja de seguir. `fadeToBlack(20)` pone delante un rectángulo que **resta** un gris que
  sube 7 por fotograma; en el fotograma 10 aparece el nombre del mapa y en el 20 se carga el nuevo (`changeMap`).
  Mientras, el jugador sigue con la animación 2 (andar) y sin prueba de colisión: avanza unas 400 unidades. Después,
  `fadeFromBlack(20)` quita el gris en otros 20 fotogramas y solo entonces el jugador vuelve al estado normal, el
  partner al suyo y el reloj sigue (`startGameTime`). Cada fundido dura **un segundo**. Los warps de script hacen lo
  mismo (`map.c: scriptTickChangeMap`).
- **El fundido, fotograma a fotograma** (`fade.c: renderFadeOut`, `renderFadeIn`, `renderFade`) **(comprobado)**:
  - El rectángulo cubre los 320 × 240 en la capa 1 del orden (delante del mapa, de los personajes y del HUD) en el
    modo semitransparente 2 (fondo − rectángulo). Es un polígono con textura: su texel (página de 896, 256, u 250,
    v 253, CLUT 256, 502: la entrada 4 de `ETCDAT/ETCTIM.BIN`, paleta 6, índice 0) vale `0xFFFF`, blanco con el bit
    de semitransparencia. La consola multiplica el texel por el tono / 128, así que lo que se resta es el blanco ×
    tono / 128: con un tono de 128 o más la pantalla queda **negra del todo**, no en un gris de 140.
  - El tono de salida sube `160 / (20 + 1) = 7` por fotograma hasta 160. `fadeToBlack` no lo pone a 0: empieza en lo
    que dejó el último fundido de vuelta, que acaba en `160 − 20 × 7 = 20`. Así el de salida va 27, 34… y la pantalla
    está negra desde su fotograma 16 (132). El mapa nuevo se carga en el fotograma 20 (`FADE_OUT_CURRENT` llega a
    20), que ya dibuja el primer tono de vuelta.
  - El de vuelta baja de 160 de 7 en 7 (153, 146…): negro hasta su fotograma 4 y claro poco a poco hasta 20 en su
    fotograma 20, en el que el jugador ya se mueve; el siguiente fotograma ya no hay rectángulo.
- **El nombre del mapa** (`map.c: addMapNameObject`, `renderMapName`; `font.c: drawString`) **(comprobado)**: en el
  fotograma 10 del fundido de salida se escribe `MAP_NAME_PTR[MAP_ENTRIES[mapa].loadingName]` (en el paquete base,
  `loading_name` de `data/maps.yaml` y `map_names` de `text/en/main.yaml`) y se dibuja en la capa 0, delante del
  fundido; `changeMap` lo quita, así que se ve 10 fotogramas (medio segundo) mientras la pantalla se oscurece. Va en
  el centro de la pantalla (la versión americana lo coloca en `x = 12 − (largo / 2) × 8`, `y = −6`; los nombres
  traen espacios a los lados hasta 24 caracteres, así que el texto queda centrado a unos píxeles), con la fuente del
  juego (11 píxeles de alto), en gris claro (color 1 de la CLUT de la fuente, `0x6B5A`), sin caja y sin sombra. Sale
  en cada warp de la tabla del mapa; en los de script, si el script lo pide (`consumeMapChangeShowName`: siempre en
  las secciones de disparadores, no en las de eventos, salvo que la variable 6 diga otra cosa).
- No hay protección contra volver a disparar el warp al aparecer: los puntos de aparición de los datos nunca caen en
  celdas de warp **(comprobado)**.
- **Aparición** (`tamer.c: loadMapEntities`, `partner_impl.c: setupPartnerOnWarp`) **(comprobado)**: el jugador
  aparece en `spawn` de la salida, con su giro. El partner aparece a 200 en la dirección cardinal de detrás del
  jugador (por bandas del giro: `z + 200` si mira hacia −z, etc.), siempre, con el giro del jugador más media vuelta;
  `partnerTickCollision` le hace mirar al jugador desde el fotograma siguiente.
- Algunas salidas no están en la tabla de warps del mapa sino en su script de eventos: la puerta de la casa de
  Jijimon (`room08`, disparador 51) llama al script 178, que lleva al jugador a la puerta y hace `warp_to` a
  `twna01` salida 2 mientras el flag 203 esté apagado **(comprobado en el script extraído)**.

### El partner

- Se mueve también con la raíz de sus animaciones **(comprobado)**. Con el partner inicial (Agumon): andar (2) 22,6
  por fotograma, andar despacio (3) 14,9 y correr (4) 43,5 **(medido)**: 452, 298 y 870 por segundo. Corre más
  despacio que el jugador.
- **Distancia** (`partner.c: getPartnerTamerCloseness`) **(comprobado)**, con R su radio (120 en Agumon):
  más lejos que 3,5 R corre, entre 2,5 R y 3,5 R anda, más cerca termina el ciclo de andar y se para (o se para en
  seco si choca con el jugador).
- **Su estado cambia el paso** (`partner.c: partnerTickWalking`, `setPartnerSlowWalking`, `setPartnerIdle`)
  **(comprobado)**: infeliz (condición `0x10`) anda en vez de correr; con los bits de condición 1, 2, `0x20` o `0x40`
  anda despacio (animación 3) y parado usa la 1 en vez de la 0. De correr a andar cambia al momento; de andar a correr
  espera a que acabe el ciclo de andar.
- **Parado** (`partner.c`, `updateConditionAnimation`) **(comprobado)**: mira al jugador; tras 1 a 5 ciclos de la
  animación de quieto (`randomLimit(5) + 1`) hace una de emoción según su estado: contento (felicidad 31 o más),
  triste (menos de −30), enfermo o herido, ganas de ir al baño, hambre, cansancio (cansancio 80 o más), sueño o
  infelicidad; al acabar vuelve a quieto con otra cuenta. El contador es de 8 bits con signo y da la vuelta en 127.
- **Rumbo** (`map.c: partnerTickCollision`) **(comprobado)**: mira de golpe cada fotograma hacia el jugador, o hacia
  la celda más antigua de su rastro si lo tiene, y la quita al pisarla.
- **Rastro**: cuando el jugador cambia de celda, si la vista entre los dos está bloqueada
  (`isFiveTileWidePathBlocked`: cinco líneas de celdas, desplazadas hasta dos celdas a cada lado en el eje menor),
  se apunta la celda anterior del jugador (hasta 30), solo si la última del rastro tampoco se ve; si la vista está
  libre, se borra el rastro. Además, cada fotograma, la primera de la 2.ª, 3.ª y 4.ª celda más nuevas (en ese orden)
  que se ve desde el jugador hace sobrar las posteriores **(comprobado)**.
- **Colisión**: el partner prueba la colisión como el jugador y, si choca, aplica `collisionGrace` (fuera de
  combate) **(comprobado)**.
- **Atascos**: si tiene rastro, su celda más antigua está fuera de la pantalla (más de 200 píxeles en horizontal o
  160 en vertical del centro, `isTileOffScreen`) y el partner tampoco se ve, aparece en esa celda **(comprobado)**.
- Al cambiar de mapa se borra el rastro (`map.c: loadMap`, `initializePartnerWaypoint`) y se coloca como en la
  aparición **(comprobado)**.

### Cámara

- `map.c: setupMap` centra la ventana de 320 × 240 en el fondo y la lleva al jugador (`initializeDrawingOffsets`);
  después `tickCameraFollowPlayer` la mueve cada fotograma lo mismo que se ha movido el jugador en pantalla, sin
  suavizado ni zona muerta, y `updateDrawingOffsets` y `cameraIsAtEdge` la limitan al fondo, así que el jugador se
  descentra solo cerca de los bordes **(comprobado)**. Solo se mueve con el jugador en estado normal y con la cruceta
  pulsada, así que se queda quieta mientras el jugador sale de un mapa (`tamer.c: checkMapInteraction` llama a
  `unsetCameraFollowPlayer` al pisar un warp); los scripts la mueven por su cuenta. Detalle en [mapas](../formats/mapas.md#en-el-motor). Lo hace la línea
  de render del mapa, no `game/field`.

### Hora del día

- **Reloj** (`clock.c: tickGameClock`) **(comprobado)**: cuenta fotogramas y suma un minuto cada 20, es decir, un
  minuto de juego por segundo y una hora por minuto. Solo corre en el campo, sin pesca y con el reloj en marcha (se
  para en los cambios de mapa). La velocidad del reloj de las opciones lo dobla, lo reduce a la mitad o lo para. Una
  partida nueva empieza a las 8:00.
- **Paleta y luces** (`map.c: updateTimeOfDay`, al cargar el mapa) **(comprobado)**: de 6:00 a 15:59 día, de 16:00 a
  19:59 atardecer (las tres luces del mapa con 7/10 de verde y azul) y el resto noche (5/10 de todo). Los mapas con el
  bit `0x40` de sus flags (`MAP_ENTRIES`) se quedan de día y con sus luces.
- **Cambio** (`map.c: renderMap`, `tickDaytimeTransition`) **(comprobado)**: justo a las 6:00, 16:00 y 20:00, salvo
  en los mapas de hora fija, la paleta se funde en 25 fotogramas a escalones (1/6, 1/5, 1/4, 1/3 y 1/2 del camino,
  cinco fotogramas cada uno) y las luces cambian al final. El fundido espera mientras el juego no está en el campo
  (`GAME_STATE` distinto de 0); el menú de pausa del campo no cambia `GAME_STATE`, así que un fundido empezado sigue
  con el menú abierto. Las horas y las luces son reglas: `time_of_day` en `field/rules.yaml`.
- El icono del reloj del menú usa otro corte: día hasta las 16:59 (`clock.c: renderGameClock`).

## Qué hace el motor (`game/field`)

| Original | Motor | Por qué |
|---|---|---|
| 20 fps, velocidad de la raíz de la animación | Tick fijo (60 Hz), velocidad por segundo en `field/rules.yaml` | Cualquier frecuencia; las animaciones se escalan con `AnimationIntent::speedScale` |
| Rumbo de la cámara redondeado a 45°, ocho direcciones | Igual (`Field::cameraForward`), pero redondea al más cercano también con rumbos negativos; con mando, dirección analógica sobre ese rumbo | La asimetría es un efecto de la división entera; mando analógico |
| Giro instantáneo, animación a velocidad normal contra la pared | Igual (`turn_speed: 0`) | |
| Prueba de colisión del paso entero y anulación con `collisionGrace` | Misma sonda (radio adelante, medio radio a los lados), pero mueve X y luego Z y corta el paso justo antes de la celda bloqueada | A 60 Hz los pasos son la tercera parte; así nada atraviesa paredes a ningún tick |
| Desvío de hasta 0x50 hacia una dirección libre al chocar | No: se desliza por la componente libre | Con mando analógico basta el deslizamiento; el desvío depende de las ocho direcciones y del paso de 20 fps |
| El primer fotograma de una animación nueva no avanza | No | Es un efecto del impulso de la raíz, no una regla |
| Celdas truncando hacia cero | `floor` | Mismo resultado salvo en bordes exactos; sin depender del signo |
| Disparador cada fotograma sobre la celda | Al entrar en una celda con disparador (no mientras se está encima) | Evita warps repetidos aunque un dato cambie |
| Cambio de mapa: 20 + 20 fotogramas, el jugador anda sin colisión, el reloj y la cámara parados | Igual (`transition` en `field/rules.yaml`, `Field::fadeLevel`; la cámara guarda su foco mientras el jugador sale, en `FieldScene::update`) | |
| Fundido restando gris (tono de 7 en 7, texel blanco × tono / 128) y nombre del mapa del fotograma 10 al 20 | Igual (`transition` en `field/rules.yaml`, `Field::fadeLevel` y `Field::mapNameShown`): `render::ScreenFade` resta el gris sobre la escena, por fotogramas del original, y el HUD pone el nombre delante (`map_title_key`, en el idioma del jugador) | El HUD es RmlUi: se dibuja después del fundido para que el nombre quede delante |
| Nombre del mapa con la fuente de mapa de bits del juego, en gris claro (color 1 de su CLUT) | La fuente es un ajuste del jugador (`text_font`): la de mapa de bits del original, leída de `font/main.png` y `font/main_meta.yaml` del paquete base, o Noto Sans, la de por defecto; en los dos casos, en el gris del original (clase `.original-look`, ver [UI](../architecture/ui.md#aspecto-original)). Lo que la fuente del original no tiene (ñ, á, ¿, ¡...) lo dibuja Noto Sans en la misma línea base | Decisión del responsable de diseño, 2026-10-20 |
| Sin HUD fijo en el campo: lo que hay en pantalla (personajes, barras de estado de `partner_impl.c: renderStatusBars`, ventanas) va detrás del rectángulo del fundido (capa 1, `fade.c: renderFade`) y se oscurece restándole el mismo gris; solo el nombre va delante (capa 0) | El HUD del motor (minimapa, botón del menú) se desvanece con alfa al ritmo del fundido (`screen_fade`) en vez de restarle el gris | Desviación aprobada: decisión del responsable de diseño, 2026-10-20 |
| Nombre colocado en `x = 12 − (largo / 2) × 8` con espacios de relleno: centrado a unos píxeles | Centrado exacto, sin opción de quirk para imitar el desvío | Decisión del responsable de diseño, 2026-10-20 |
| Warps de script | `field/warps.yaml`: mapa + disparador → mapa + salida, con la prueba | No se leen scripts en tiempo de ejecución (fase 4) |
| Partner nuevo mirando hacia fuera un fotograma | Mirando al jugador desde el principio, y solo si cabe en la rejilla | Mismo resultado visible; no aparece dentro de una pared |
| Rastro: poda cada fotograma, de la más nueva a la más antigua | Igual | |
| El partner anda o se para solo por la distancia | También anda mientras sigue el rastro | Si no, se para detrás de una pared a menos de 2,5 R del jugador |
| Aparece en el rastro si está fuera de pantalla | Si no avanza durante `stuck_seconds` o se aleja más de `teleport_distance`, aparece detrás del jugador | `game/field` no sabe qué hay en pantalla; nunca se queda atascado |
| Paso según su estado (infeliz, andar despacio), termina el ciclo de andar, emociones parado | No | Fase 3 (cuidado): aún no hay estado del partner |
| Cámara: sigue solo con la cruceta; ventana de 320 × 240 | Sigue siempre al jugador (también con clic); la ventana es la de la pantalla, de 320 × 240 hacia arriba | Clic para ir; cualquier resolución y proporción |
| Reloj: un minuto cada 20 fotogramas, desde las 8:00 | Igual (`clock` en `field/rules.yaml`); `--time` lo pone a la hora de inicio de `clock` o a la del atardecer o la noche de `time_of_day` | |
| Velocidad del reloj de las opciones | No | Fase 3 (opciones de juego) |
| Fundido de la hora a escalones de 25 fotogramas, luces al final, parado fuera del campo; el menú de pausa del campo no lo para (`tickDaytimeTransition` solo espera a `GAME_STATE`, que el menú no cambia) | Igual (`render::timeOfDayFadeShown`; horas y luces en `time_of_day` de `field/rules.yaml`): con el menú abierto el reloj se para, pero un fundido ya empezado sigue | |
| Sin clic | Clic para ir: camino A* por puntos cada media celda (centros, bordes y esquinas), en 8 direcciones, solo por pasos que el jugador anda con la colisión del original aunque vaya 20 unidades desviado, y sin pisar warps que no son el destino | Juego de PC ([ADR 0013](../adr/0013-juego-nativo-de-pc.md)); el seguidor nunca se queda en un borde |

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
- `src/main/map.c`: `entityCheckCollision`, `checkMapCollision`, `collisionGrace`, `partnerTickCollision`,
  `isFiveTileWidePathBlocked`, `isTileOffScreen`, `loadMap`, `scriptTickChangeMap`; para la línea de render del mapa,
  `setupMap`, `initializeDrawingOffsets`, `tickCameraFollowPlayer`, `updateDrawingOffsets`, `cameraIsAtEdge`,
  `renderMap`, `updateTimeOfDay` y `tickDaytimeTransition`.
- `src/main/anim.c`: `startAnimation`, `tickAnimation`, `tickMomentum`, `applyRootMomentum`.
- `src/main/fade.c`: `fadeToBlack`, `fadeFromBlack`, `renderFade`.
- `src/main/clock.c`: `tickGameClock`, `renderGameClock`.
- `src/main/partner.c`: `partnerTickNormal`, `partnerTickWalking`, `getPartnerTamerCloseness`,
  `setPartnerSlowWalking`, `setPartnerIdle`, `updateConditionAnimation`.
- `src/main/partner_impl.c`: `setupPartnerOnWarp`.
- Scripts: `MAPHEAD` sección 218 (`set_script 178`) y el script 178, sección 0x33.
