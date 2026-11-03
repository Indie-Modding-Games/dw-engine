# curling

El juego de las piedras de la pista de hielo (`dwe::game::curling`, capa `game`), una especie de curling: el
jugador y el rival lanzan cinco piedras cada uno hacia la diana, entre obstáculos, y gana quien suma más puntos. No
dibuja nada: depende de `core`, `assets` y `ruleset`. Los números salen de la sección `curling` del ruleset
(`rules/<id>/curling.yaml`): los tipos y pesos de las piedras, el rozamiento, la fuerza, los anillos y sus puntos.

## Uso

```cpp
auto rules = dwe::game::curling::parseCurlingRules(*ruleset.section("curling"));
dwe::game::curling::Match match{*rules, dwe::game::curling::Opponent::Penguinmon, random};
auto events = match.tick(buttons, random);   // un fotograma del original (20 por segundo)
if (match.finished()) { /* match.result(): 2, 1 o 0 */ }
```

- **Inicio**: el guion lo abre con `call_partner_routine(1)` al pisar la casilla de salida de la pista (frzl17,
  sección 0x33) después de hablar con el rival (variable 113 a 1, variable 122 con el rival: 0 el primero, 1 el
  segundo). El rival lleva al jugador a frzl17 por la salida 1, que lo deja encima de esa casilla: la partida
  empieza al llegar, sin moverse. `session::CurlingAdapter` crea la partida y `apps/game/src/curling_screen.cpp` la juega en lugar del
  campo.
- **Controles** (teclado primero): Arriba/Abajo (W/S o flechas) eligen piedra y opción, Izquierda/Derecha (A/D o
  flechas) giran el tiro (Arriba/Abajo desplazan la vista mientras se apunta), Intro o Espacio confirman (piedra, dirección y fuerza cuando el indicador está donde se quiere).
  En mando, la cruceta y el botón de confirmar.
- **Final**: el resultado va a la variable 121 (`KAR_finishMatch`): 2 si el jugador gana con 10 puntos o más, 1 si
  gana, 0 si pierde o empata. El guion hace el resto: con 2 da una medalla (una sola vez) y, si el jugador
  gana, el primer rival se une a la ciudad (disparador 257).

## Paridad con el original

Estado: **Igual** (hace lo mismo que el decomp), **Suposición** (el decomp no lo deja claro; se eligió lo más
probable y está marcado en el código), **Hueco** (falta).

| Regla | Decomp | Aquí | Test | Estado |
|---|---|---|---|---|
| La partida empieza al llegar a la pista | `script 131` sección 0x33, `checkMapInteraction`, `map_collision.c: getTileTrigger` | `Field::checkTriggers` lee la celda del original (`originalCellAt`) | `field_tests`: "a step cell is the one the original reads ..."; ruta `etapa5-curling` | Igual. Medido en el original (estado `curling_start` del MCP): al entrar por la salida 1 sale la pregunta sin moverse. Antes el jugador quedaba en la pista sin que pasara nada hasta salir de la celda y volver |
| Piedras de cada equipo (tipo por tirada sobre la tabla, peso por tipo) y de 0 a 2 obstáculos | `kar.c: KAR_setupMatch` | `Match::setupMatch` | setupMatch draws | Igual (mismo orden de tiradas, `rand() % n`) |
| Deslizamiento: avanza velocidad/peso, conserva el 98 %, se para bajo 2; las piedras con pinchos se clavan al pararse | `KAR_tickStones` | `tickStones` | tickStones | Igual |
| Una piedra parada cerca de los lanzadores sale del juego | `KAR_checkStonesStopped` | `checkStonesStopped` | stopped stone goes out | Igual |
| Paredes laterales, diagonales, fondo y frente | `KAR_bounceOffWall`, `KAR_getWallZone`, `KAR_findWallContact`, `KAR_placeAtContact` | `physics.cpp` | wall bounce | Igual |
| Choques en 11 subpasos (con el reintento del último), contra piedra clavada, parada o en marcha | `KAR_updateCollisions`, `KAR_resolveStoneCollision`, `KAR_collide*`, `KAR_computeImpactShare`, `KAR_computeSeparation` | `physics.cpp` | collision | Igual. `ratan2` (`core::original::ratan2`) toma el escalón de la tabla como el original (el lado menor entre el mayor en 1024 avos, redondeado hacia abajo); la tabla se calcula y coincide con la del disco en 1017 de 1025 escalones (los otros 8, a una unidad). Medido: con `atan2` redondeado salía una unidad distinta en el 13,8 % de los ángulos; ahora en el 0,8 % |
| Anillos y puntos (+2 centro, +1 anillo, −2 y +2 en las dianas laterales), recontados tras cada tiro | `KAR_classifyStoneRings`, `KAR_tickScoreTally` | `classifyStoneRings`, `tickScoreTally` | rings and tally | Igual |
| Partida: colocar, pregunta jugar/reglas, "Preparados", elegir, apuntar, fuerza, lanzar, recuento, turno del que menos ha tirado | `KAR_tickMatchState`, `KAR_tickYesNoPrompt`, `KAR_beginAiming`, `KAR_select*Stone`, `KAR_beginThrow` | `Match::tickMatchState` | whole match | Igual |
| Rival: tiro directo si hay paso, si no rebote, piedra en anillo o al azar; fuerza inversa al rozamiento menos 600 o 500 | `KAR_chooseOpponentShot`, `KAR_findClearShotAngle`, `KAR_aimBankShot`, `KAR_aimAtStoneInRing`, `KAR_aimAtRandomStone`, `KAR_computeThrowPower` | `match.cpp` | whole match | Igual |
| Resultado 2/1/0 en la variable 121 | `KAR_finishMatch` | `Match::result`, `CurlingAdapter::finish` | finish result | Igual |
| Piedras dibujadas: el objeto de `KARRING.TMD` por equipo y tipo (las de pinchos lisas en la mano), en la pista con su giro, en la mano del lanzador (nodo 9 del jugador, nodo 4 del segundo rival, 0x78 abajo) y en los soportes a los lados de la pantalla; los soportes se llenan de una en una cada diez cuadros tras empezar, y la fila del rival espera a que salga su ayuda | `KAR_start`, `KAR_renderScene`, `KAR_setupMatch`, `KAR_beginAiming` | `stoneModelObject`, `Match::stoneDrawn`, `CurlingScreen::showStones`, `CurlingScreen::placeHands` | stoneModelObject ...; stoneDrawn ... | Igual en qué piedra y cuándo. Medido en el original (estado `curling_start`): las columnas de los soportes en x≈52 y x≈268 de 320. Suposición: los soportes se dibujan en el original con su propia vista encima de todo; aquí van en el rayo de ese punto de la pantalla, a medio camino entre la cámara y el suelo, con el tamaño de esa vista y mirando a la cámara como miran a esa vista, inclinadas 0x96 (en 16:9 quedan donde el original los pone en 4:3, más cerca del centro) |
| Anillos de la diana que se iluminan: los que pisa una piedra en marcha (cada piedra enciende el primero libre que la contiene) y, en el recuento, el anillo nuevo de la piedra contada mientras parpadea | `KAR_updateRingMarkers` | `Match::updateRingMarkers`, `CurlingScreen::showRings`; los objetos del mapa de cada anillo en `rules/original/curling.yaml` | updateRingMarkers (dos) | Igual |
| Marcador, indicador de fuerza, pregunta, letrero de "Preparados" | `KAR_renderScores`, `KAR_renderPowerMeter`, `KAR_tickYesNoPrompt`, `KAR_renderReadyPrompt` | documento `curling_hud` | - | Suposición: textos en un panel (las opciones de la pregunta con las palabras del original), no los sprites del original |
| Páginas de ayuda de las reglas: el nombre del rival y tres líneas por página, del primer o el segundo rival | `KAR_tickHintBox`, `KAR_drawHintPage*` | `CurlingScreen::showHud` (textos `kar.*_dialogue`) | - | Suposición: el texto del original en la caja de diálogo de la historia, en su sitio (la del original está en el mismo); las cuatro líneas salen a la vez (el original escribe una por cuadro); las líneas nombran los botones del mando, como el original |
| Al apuntar, Arriba/Abajo desplazan la vista 10 píxeles por cuadro e Izquierda/Derecha 8 cuando el giro ya va rápido (10 cuadros pulsado) o hay ayuda abierta, sin salir del fondo | `KAR_handleAimScroll`, `moveCameraByOffset` | `Match::handleAimScroll`, `CurlingScreen::scroll` | handleAimScroll | Igual |
| Mientras la piedra lanzada se desliza, la cámara la sigue: cada fotograma se mueve lo que la piedra avanzó (el estado corre antes que las piedras, así que va un fotograma por detrás), sin el tope del fondo | `KAR_tickMatchState` (estados 9, 0xA y 0xB, `KAR_D_800638F4`), `map.c: moveCameraByDiff` | `Match::tickMatchState` (evento `Follow`), `CurlingScreen::follow` | the camera follows the thrown stone ...; en vivo en frzl17 con teclado: la piedra queda en pantalla hasta la diana | Igual (#181). Antes la cámara se quedaba en los lanzadores, la piedra salía de la pantalla y no se veía dónde paraba. Medido en el original (estado `curling_throw` del MCP): la cámara sigue a la piedra hasta que para |
| Flecha de puntería: 15 puntos a 200 por delante de la piedra en la dirección del lanzador, rebotados en las paredes (x = ±0x2EE), con una onda de gris que baja por la línea, un punto que falta y avanza cada 3 cuadros, y la punta girada como la línea; solo al apuntar (estados 6 y 7) | `KAR_renderAimArrow`, `KAR_rotatePoint` | `Match::renderAimArrow`, `Match::aimArrow`, `CurlingScreen::showAimArrow`, `FieldScene::setPointQuads` | aimArrow ... | Igual. La imagen es la del original: la primera imagen de 4 bits de la pista con su novena paleta (VRAM (640, 0), CLUT (0x80, 0x1E6)) |
| Sonidos: el roce de la piedra lanzada suena desde que se suelta hasta que se leen los anillos; choques, paredes, piedra clavada, anillos, victoria y derrota del banco del minijuego; cursor y página del banco del menú | `KAR_tickMatchState` (estados 9 y 0xC: `playSound2(8, 0)`, `stopSoundMask`), `KAR_tickStones`, `KAR_resolveStoneCollision`, `KAR_tickScoreTally` | eventos `Sound`, `RollingSound`, `SoundStop`; `CurlingScreen::follow` | the rolling sound starts at the release ... | Igual. Antes el roce seguía sonando hasta acabar solo |
| Animaciones y orientación de los lanzadores y del compañero: quietos al empezar; el que lanza mira al frente al apuntar y gira al revés que la puntería; agacharse, impulso y lanzamiento; al final los tres miran a la cámara con su pose; la piedra lanzada por el primer rival se desliza girada como él la soltó | `KAR_tickMatchState`, `KAR_beginAiming`, `KAR_turnAimLeft`, `KAR_turnAimRight`, `KAR_beginThrow`, `KAR_finishMatch` (`startAnimation`, `rotation.vy`) | `Match::facing`, eventos `Animation`, `CurlingScreen::animate`, `CurlingScreen::faceCharacters` | facing ...; finishMatch ... | Igual. Los clips quedan en manos del guion de la rutina como los de la historia: el jugador y el compañero hasta que el guion los suelta, el rival hasta que vuelve a 0 |

## Tests

`dwe_curling_tests`: la sección del ruleset, las tiradas del montaje, el deslizamiento y la parada, la piedra que
sale, la piedra clavada, el rebote, un choque, los anillos y el recuento, el resultado y una partida entera con
semilla fija contra los dos rivales, con y sin las reglas.
