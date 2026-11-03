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
  segundo). `session::CurlingAdapter` crea la partida y `apps/game/src/curling_screen.cpp` la juega en lugar del
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
| Piedras de cada equipo (tipo por tirada sobre la tabla, peso por tipo) y de 0 a 2 obstáculos | `kar.c: KAR_setupMatch` | `Match::setupMatch` | setupMatch draws | Igual (mismo orden de tiradas, `rand() % n`) |
| Deslizamiento: avanza velocidad/peso, conserva el 98 %, se para bajo 2; las piedras con pinchos se clavan al pararse | `KAR_tickStones` | `tickStones` | tickStones | Igual |
| Una piedra parada cerca de los lanzadores sale del juego | `KAR_checkStonesStopped` | `checkStonesStopped` | stopped stone goes out | Igual |
| Paredes laterales, diagonales, fondo y frente | `KAR_bounceOffWall`, `KAR_getWallZone`, `KAR_findWallContact`, `KAR_placeAtContact` | `physics.cpp` | wall bounce | Igual |
| Choques en 11 subpasos (con el reintento del último), contra piedra clavada, parada o en marcha | `KAR_updateCollisions`, `KAR_resolveStoneCollision`, `KAR_collide*`, `KAR_computeImpactShare`, `KAR_computeSeparation` | `physics.cpp` | collision | Igual. `ratan2` (`core::original::ratan2`) toma el escalón de la tabla como el original (el lado menor entre el mayor en 1024 avos, redondeado hacia abajo); la tabla se calcula y coincide con la del disco en 1017 de 1025 escalones (los otros 8, a una unidad). Medido: con `atan2` redondeado salía una unidad distinta en el 13,8 % de los ángulos; ahora en el 0,8 % |
| Anillos y puntos (+2 centro, +1 anillo, −2 y +2 en las dianas laterales), recontados tras cada tiro | `KAR_classifyStoneRings`, `KAR_tickScoreTally` | `classifyStoneRings`, `tickScoreTally` | rings and tally | Igual |
| Partida: colocar, pregunta jugar/reglas, "Preparados", elegir, apuntar, fuerza, lanzar, recuento, turno del que menos ha tirado | `KAR_tickMatchState`, `KAR_tickYesNoPrompt`, `KAR_beginAiming`, `KAR_select*Stone`, `KAR_beginThrow` | `Match::tickMatchState` | whole match | Igual |
| Rival: tiro directo si hay paso, si no rebote, piedra en anillo o al azar; fuerza inversa al rozamiento menos 600 o 500 | `KAR_chooseOpponentShot`, `KAR_findClearShotAngle`, `KAR_aimBankShot`, `KAR_aimAtStoneInRing`, `KAR_aimAtRandomStone`, `KAR_computeThrowPower` | `match.cpp` | whole match | Igual |
| Resultado 2/1/0 en la variable 121 | `KAR_finishMatch` | `Match::result`, `CurlingAdapter::finish` | finish result | Igual |
| Piedras y lanzadores dibujados | `KAR_renderScene` | `CurlingScreen::showMarkers` | - | Hueco: las piedras son marcadores de color sobre el mapa; sin modelos ni giro |
| Anillos de la diana que se iluminan: los que pisa una piedra en marcha (cada piedra enciende el primero libre que la contiene) y, en el recuento, el anillo nuevo de la piedra contada mientras parpadea | `KAR_updateRingMarkers` | `Match::updateRingMarkers`, `CurlingScreen::showRings`; los objetos del mapa de cada anillo en `rules/original/curling.yaml` | updateRingMarkers (dos) | Igual |
| Marcador, indicador de fuerza, pregunta, letrero de "Preparados" | `KAR_renderScores`, `KAR_renderPowerMeter`, `KAR_tickYesNoPrompt`, `KAR_renderReadyPrompt` | documento `curling_hud` | - | Suposición: textos en un panel (las opciones de la pregunta con las palabras del original), no los sprites del original |
| Páginas de ayuda de las reglas: el nombre del rival y tres líneas por página, del primer o el segundo rival | `KAR_tickHintBox`, `KAR_drawHintPage*` | `CurlingScreen::showHud` (textos `kar.*_dialogue`) | - | Suposición: el texto del original en la caja de diálogo de la historia, en su sitio (la del original está en el mismo); las cuatro líneas salen a la vez (el original escribe una por cuadro); las líneas nombran los botones del mando, como el original |
| Al apuntar, Arriba/Abajo desplazan la vista 10 píxeles por cuadro e Izquierda/Derecha 8 cuando el giro ya va rápido (10 cuadros pulsado) o hay ayuda abierta, sin salir del fondo | `KAR_handleAimScroll`, `moveCameraByOffset` | `Match::handleAimScroll`, `CurlingScreen::scroll` | handleAimScroll | Igual |
| Mientras la piedra lanzada se desliza, la cámara la sigue: cada fotograma se mueve lo que la piedra avanzó (el estado corre antes que las piedras, así que va un fotograma por detrás), sin el tope del fondo | `KAR_tickMatchState` (estados 9, 0xA y 0xB, `KAR_D_800638F4`), `map.c: moveCameraByDiff` | `Match::tickMatchState` (evento `Follow`), `CurlingScreen::follow` | the camera follows the thrown stone ...; en vivo en frzl17 con teclado: la piedra queda en pantalla hasta la diana | Igual (#181). Antes la cámara se quedaba en los lanzadores, la piedra salía de la pantalla y no se veía dónde paraba. Suposición: sin medir en el original (no hay estado guardado en frzl17; en la cola de medidas) |
| Flecha de puntería | `KAR_renderAimArrow` | marcador delante de la piedra | - | Hueco: un marcador, no la flecha del original |
| Animaciones de los lanzadores y del compañero | `KAR_tickMatchState`, `KAR_finishMatch` (`startAnimation`) | eventos `Animation` | - | Hueco: los personajes no se mueven |

## Tests

`dwe_curling_tests`: la sección del ruleset, las tiradas del montaje, el deslizamiento y la parada, la piedra que
sale, la piedra clavada, el rebote, un choque, los anillos y el recuento, el resultado y una partida entera con
semilla fija contra los dos rivales, con y sin las reglas.
