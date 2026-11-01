# tournament

Los torneos de la arena (`dwe::game::tournament`, capa `game`): los rivales que saca una copa, el cuadro de ocho con
los combates que el jugador no juega decididos por nivel, los valores y técnicas de cada rival y cuántas victorias
ganan la copa. No dibuja nada ni juega los combates: depende de `core`, `assets` y `ruleset`. Los números salen de la
sección `tournament` del ruleset (`rules/<id>/tournament.yaml`) y de `data/stadium` (las tablas del original: valor
base del rival por copa y probabilidad de victoria por nivel), que extrae `dwx`.

## Uso

```cpp
auto rules = dwe::game::tournament::parseTournamentRules(*ruleset.section("tournament"), stadium);
auto cup = dwe::game::tournament::cupDataOf(listing, cupNumber);      // sección 10 del guion del mapa
auto rivals = dwe::game::tournament::drawRivals(*rules, cupNumber, *cup, resident, partnerSpecies, random);
auto bracket = dwe::game::tournament::drawBracket(*rules, *rivals, partnerSpecies, levelOf, random);
auto rival = dwe::game::tournament::makeRival(*rules, cupNumber, species, random);
```

- **Inicio**: la rutina 6 del guion (`call_routine(6)`) abre la pantalla de torneo; `session::TournamentAdapter`
  lee la copa (variable 3), las listas de la copa en el guion del mapa activo y los residentes de la ciudad
  (disparadores 200 + especie), y saca rivales y cuadro.
- **Inscripción**: la rutina 18 del guion (`call_routine(18)`, recepcionista del vestíbulo) abre
  `apps/game/src/tournament_schedule_screen.cpp`: calendario de cinco días con las copas abiertas, texto de la copa
  con P y, al elegir una, la pregunta y la elección de entrar. `TournamentAdapter::openSchedule` arma el calendario
  (`buildSchedule`, `cupOpen`) y `TournamentAdapter::enter` deja la inscripción (disparadores y variables 2 a 4).
- **Combates**: `apps/game/src/story_tournament.cpp` juega los tres combates como batallas de tipo `Tournament` con
  reloj (`BattleSetup::timeLimit`): sin huida, sin bolsa, sin premios ni cuidados al acabar. Se juegan en la arena
  (sección `arena` del ruleset): `FieldScene` dibuja su modelo con una cámara propia (`FieldStage::view`,
  `render::MapRenderer::setStageCamera`) y la batalla pisa su rejilla (`FieldStage::ground`).
- **Cuadro**: `game::tournament::BracketAnimation` es el cuadro del original fotograma a fotograma (20 por segundo):
  la presentación con la tarjeta del grado, el tablero antes de cada combate y la subida del campeón. Lo dibuja
  `apps/game/src/tournament_bracket_screen.cpp` en el documento `ui/tournament_bracket` con las imágenes extraídas
  del jugador (sección `bracket` del ruleset y `data/stadium: bracket`). Sin esas tablas la copa se juega sin cuadro.
- **Final**: `TournamentAdapter::finish` quita la inscripción (disparador 37), suma al historial (copas ganadas,
  perdidas, victorias), pone el disparador de la copa ganada (15 + copa), da los premios de más del ruleset
  (`extra_prizes`) y deja las victorias en la variable 255, que lee el guion (sus premios son del guion 214).
- **Medalla**: la rutina 22 (`TournamentAdapter::awardMedal`, sección `medal`) da la medalla de los torneos y su
  frase.

## Paridad con el original

Estado: **Igual** (hace lo mismo que el decomp), **Suposición** (el decomp no lo deja claro; se eligió lo más
probable y está marcado en el código), **Hueco** (falta).

| Regla | Decomp | Aquí | Test | Estado |
|---|---|---|---|---|
| Listas de la copa en el guion | `tournament.c: getCupDataJumpTable` (sección 10, entradas 1 a 4) | `cupDataOf`, `cupList` | cup data read from a listing | Igual |
| Rivales: residentes, lista entera en la copa 22, todos si la lista está vacía, relleno con repetidos o siete distintos sin la especie del partner | `utils2.c: startTournament` | `drawRivals` | rivals drawn | Igual (mismo orden de tiradas) |
| Sustitutos de especies sin luchador (64→54, 63→48, 65→42) | `std_setup.c: STD_tournamentMain` | `standIn`, `drawBracket` | stand-ins | Igual |
| Cuadro: hueco del partner, rivales barajados, primera y segunda ronda por nivel (`STD_WIN_CHANCES`) | `std_main.c: STD_setupParticipants` | `drawBracket` | bracket | Igual (la tirada del cruce del partner se gasta aunque no cuente) |
| Valores del rival: base de la copa ±30 %, PV y PM ×10, escala por nivel (×1, ×2, ×2,5), topes 999 y 9999 | `std_setup.c: STD_initializeOpponent` | `makeRival` | rival stats | Igual |
| Técnicas del rival: de los 15 primeros huecos, sin remates y pagables (PM/10 ≥ coste×3), barajadas; remate en el cuarto hueco | `std_setup.c: STD_initializeOpponent` | `makeRival` | rival moves | Igual (la tirada de 1 entre 10 del remate amplio se gasta y se pisa, como en el original) |
| Prioridad de los huecos del rival 30/30/30/0 | `std_setup.c: STD_tickEnemyAI` | `movePriorities` en `tournament.yaml` | - | Igual |
| Modo de carga del rival sorteado en cada decisión (2 con un 10 %, si no 0 o 1) | `std_main.c: STD_tickEnemyAI` | `tickEnemy` (con reloj de copa), `enemy_ai.match_cautious_roll` | STD_tickEnemyAI: cautious rival | Igual |
| Reloj de 99 s: un segundo cada 20 fotogramas, parado con un luchador caído | `std_main.c: STD_addFighterCounter`, `STD_tickFighterCounter` | `MatchClock`, `Battle::step` | timer second per 20 frames | Igual |
| Fin del combate: caído el partner pierde; caídos los rivales gana; los dos, empate; al acabar el tiempo gana quien perdió menos vida desde el inicio, igual es empate; el empate cuenta como derrota | `std_main.c: STD_checkEndCondition`, `STD_deinitializeCombat` | `Battle::endMatch` | fallen partner loses; time-up judgement | Igual |
| Sin premios, sin cuidados ni vidas tras el combate de copa | `std_setup.c: STD_deinitializeCombat` | `settleBattle` (tipo `Tournament`) | no rewards | Igual |
| Sin caja de fin entre combates: el ganador repite su pose (animación 0x2a) 100 fotogramas si ganó el partner, 120 si ganó el rival; 20 fotogramas más con el reloj, la tira y las barras, y el combate se cierra | `std_main.c: STD_deinitializeCombat` | `BattleScreen::finish`, `tickMatchEnd` | - (ruta `match-end`) | Igual en tiempos y sin caja; Hueco: la pose del rival cuando gana, la música de victoria o derrota (3 y 4 de la arena) y la escena de empate (`STD_addVersusModelScene`) |
| Entre combates, el partner recupera un quinto de PV y PM máximos | `std_setup.c: STD_deinitializeCombat` | `TournamentAdapter::matchOver` | - | Igual |
| Tres victorias ganan la copa (dos en la copa 5, rareza del original) | `utils2.c: startTournament` | `winsNeeded` | wins needed | Igual |
| Final: inscripción fuera, copas ganadas/perdidas, victorias sumadas con tope, disparador 15 + copa, victorias en la variable 255 | `utils2.c: startTournament` | `TournamentAdapter::finish` | - | Igual |
| Reloj del juego tras el torneo | guion 213, sección 0x36 (`add_minutes_to_date_at` +360) | el guion | - | Igual (comprobado en el decomp, sin jugar una copa en el original): `STD_removeOverworldObjects` quita el objeto del reloj (0xfa2) y la copa pone `GAME_STATE` 5, así que `tickGameClock` no corre; tras la rutina 6 el guion guarda la hora (`store_date` 105), le suma 360 minutos y avanza hasta ella. En dwe los combates de copa corren sin el campo y el reloj no avanza |
| Inscripción: sale si ya hay inscripción (disparador 37); si no, variable 3 a 0xFF y calendario | `dget.c: initTournamentSchedule` | `TournamentScheduleScreen::take`, `TournamentAdapter::openSchedule` | - (ruta `registration`) | Igual |
| Calendario de 5 días desde hoy (tabla de 30 días, hasta 6 copas por día, solo las abiertas) | `dget.c: fillEnabledTournamentTable` | `buildSchedule` | fillEnabledTournamentTable: five days | Igual |
| Copa abierta: residentes de los rivales (o todos del 200 al 311), luego tipo 0 libre, 1 con la copa A ganada (disparador 18), 2 con residentes de la sección 11 del guion, otro con rivales hasta esa especie | `dget.c: isTournamentEnabled` | `cupOpen` | isTournamentEnabled: residents | Igual |
| Solo hoy (hasta las 10:00) y mañana se pueden inscribir; marca de especie admitida (lista 3) o demasiado fuerte (lista 2), en amarillo; cerradas en rojo | `dget.c: fillEnabledTournamentTable`, `tournamentCheckEligible`, `tournamentCheckFair` | `buildSchedule`, `openSchedule` | fillEnabledTournamentTable: five days | Igual |
| Cursor con vuelta en 5 × 6; confirmar en una copa marcada la elige, en otra suena el 11; cancelar cierra; pausa (P, el Start del original) enseña el texto de la copa | `dget.c: tickTournamentSchedule`, `initTournamentInfo`, `tickTournamentInfo` | `TournamentScheduleScreen::actInSchedule`, `press` | - (ruta `registration`) | Igual |
| Copa elegida: su texto arriba, pregunta del recepcionista (línea 3 y disparador 40 si es demasiado fuerte, si no línea 2) y elección del jugador «Entrar / No entrar» | `dget.c: initTournamentSchedule` (estados 3 y 4) | `TournamentScheduleScreen::pick`, `talkRows` | - (ruta `registration`) | Igual |
| Entrar: disparadores 37, 38 y 39; variable 2 = día, o (día + 1) con 0x80 si es mañana; variable 4 = especie del partner | `dget.c: initTournamentSchedule` (estado 5) | `TournamentAdapter::enter`, `entryDay` | initTournamentSchedule: registration values | Igual |
| Cancelar en la elección Entrar / No entrar | `showMapheadSelection` | `TournamentScheduleScreen::act` | - | Suposición: cuenta como «No entrar» |
| Caja del título más ancha que la del original (108 píxeles) para que quepa el texto con esta fuente | `dget.c: buildScheduleLabels` | `tournament_schedule/structure.rcss` | - | Suposición: mismo centro y alto |
| Arena: la 1 (B_TOGI) en las copas de betl02, si no la 0 (T_TOGI); su modelo en lugar del mapa, sin los personajes del mapa ni el jugador | `std_setup.c: STD_tournamentMain`, `STD_initializeTournament`, `STD_loadArenaAssets`, `STD_renderArena0/1` | `arenaFor`, `StoryTournament::enterArena`, `FieldStage::view`/`shownProps` | the arena, where the fighters stand, the orbit camera | Igual |
| Suelo del combate: la rejilla de colisión de la arena (B_TOGI.ATR en las dos) | `std_setup.c: STD_loadArenaAssets` | `FieldStage::ground`, `FieldBattleGround::blocksStep` | - | Igual |
| Luchadores al empezar: el partner en +500 y el rival en -500 (salen de ±2500 o ±1800 y la presentación los acerca 2000 o 1300) | `std_main.c: STD_placePlayer1/2`, `STD_runIntro`, `STD_setPostIntroPosition` | `arenaStart` | the arena, where the fighters stand, the orbit camera | Igual (como si se saltara la presentación) |
| Cámara del combate: a un lado de los luchadores, su distancia × 500 / 200 entre 1000 y 4200 hacia atrás, 1000 de alto, mirando al punto medio 300 sobre el suelo; proyección 500 | `std_main.c: STD_setCameraOrbit`, `std_setup.c: STD_initializeCamera` | `arenaView`, `StoryTournament::showArena` | the arena, where the fighters stand, the orbit camera | Igual |
| Al acabar la copa, el jugador y el partner vuelven a donde estaban en el mapa | `reinitializeAfterTournament` | `StoryTournament::leaveArena` | - | Suposición: el original recarga el mapa y el guion sigue desde ahí |
| Presentación con cámara que persigue y nombres, acercamiento de la cámara con el letrero de inicio (distancia mínima de 200 a 4200 y vuelta a 1000), escenas de victoria/derrota, escena del campeón en el podio y luces de la arena | `STD_runIntro`, `STD_startCameraChase`, `STD_addChampionScene`, `STD_initializeLighting` | - | - | Hueco: la copa usa la presentación y la música del combate normal; tras la subida del campeón no hay podio |
| Tablas del cuadro (filas, salto del perdedor, camino de cada hueco, paleta de cada especie, icono y paleta de su disparo) leídas del ejecutable del jugador | `std_main.c: STD_BRACKET_ROW_Y`, `STD_BRACKET_HOP_Y`, `STD_BRACKET_PATHS`, `STD_DIGIMON_SPRITE_CLUT`, `STD_BRACKET_PROJECTILE_ICONS/CLUTS` | `dwx` (`data/stadium: bracket`), `parseBracketLayout` | data/stadium: the bracket's tables | Igual |
| Presentación de la copa: tarjeta del grado (`STD_GRADES[copa]`) sobre el tablero oscurecido 100 fotogramas y fundido de 20 (120 en total), música 29 pista 0 | `std_setup.c: STD_tournamentMain`, `std_main.c: STD_addBracketIntro`, `STD_tickBracketIntro` | `BracketAnimation::startIntro`, `TournamentBracketScreen::open` | STD_addBracketIntro: the card shows 100 frames and fades for 20 | Igual (sacado del decomp; sin medir) |
| Cuadro antes de cada combate: los ocho suben por el árbol, los combates que el partner no juega se resuelven con disparos (doble el del ganador), destello del golpe, el perdedor salta abajo; llega el combate del partner y el tablero se oscurece 3 por fotograma; música 29 pista 1, sonidos 5, 1, 3 y 4 del banco 16; a 20 fotogramas por segundo; el partner no está en el campo | `std_main.c: STD_addBracket`, `STD_updateBracket`, `STD_tickBracketProjectile`, `STD_tickBracketHitFlash`, `STD_removeBracket` | `BracketAnimation`, `TournamentBracketScreen`, `StoryTournament::update` | STD_updateBracket (tres casos) | Igual (sacado del decomp; sin medir) |
| Dibujo del cuadro: caja gris 0x20 a media transparencia en toda la pantalla, tablero (stdtim 005) en (-128, -97), sprites de 16 × 16 de stdtim 000/001 con la paleta de su especie, espejo según el camino | `std_main.c: STD_drawBracket`, `STD_renderBracketDigimon`, `STD_renderBracketProjectile` | `ui/tournament_bracket` | - (ruta `tournaments`) | Suposición: el brillo (modos semitransparentes del original) se hace con opacidad; la sombra de la presentación (resta de un téxel desconocido) es un negro al 50 % de su brillo |
| Subida del campeón tras la final (61 fotogramas, música 29 pista 2), fundido a negro de 10 y 11 fotogramas | `std_setup.c: STD_tournamentMain` (`STD_addBracket(2)`) | `BracketAnimation::show(Champion)`, `TournamentBracketScreen` | after a win the champion climbs to the top | Igual (sacado del decomp; sin medir) |
| Nombres en el cuadro | `STD_drawBracket` (`renderBracketGlyph`, sin dibujo en la versión US) | apariencia moderna: nombre bajo cada hueco y la ronda (`localization/<idioma>/tournament.yaml`) | - | Igual en la apariencia original (sin nombres); la moderna es una mejora |
| Premios de la copa: los del guion de la arena (dinero y objeto del ganador, del segundo, Giant Meat de participación); un ruleset puede añadir más por copa ganada | guion 214 (sección 0x33) | `extra_prizes` en `tournament.yaml` y en `species_extra` (los del contenido oculto), `TournamentAdapter::giveExtraPrizes` | extra_prizes: a ruleset's prizes by cup | Igual (la lista del original está vacía) |
| Copa ganada: disparador 15 + copa, guardado con los disparadores | `utils2.c: startTournament` | `TournamentAdapter::finish`, `cupWon` | - (ruta `tournaments`) | Igual |
| Medalla de los torneos (rutina 22): grupos de copas (15-20, 21-25, 26-32, 33-36), 100 victorias, las 66 cartas con el juego terminado (50); sólo cuenta la primera condición que se cumple; da la medalla y su frase (sección 0x4db, sin nombre) y espera a que se cierre | `tournament.c: checkTournamentMedalConditions`, `scriptCheckTournamentMedal` | `tournamentMedal`, `TournamentAdapter::awardMedal`, rutina 22 | checkTournamentMedalConditions: the first group won, the wins, the cards | Igual |
| Rival dibujado en la arena: su especie con las animaciones de los personajes, en -500 mirando al partner (0xC00), quitado al acabar cada combate | `std_setup.c: STD_initializeOpponent`, `std_main.c: STD_placePlayer2`, `removeMapEntities` | `FieldScene::addStageFighter`, `StoryTournament::update` | - (ruta `arena`) | Igual |
| Reloj en pantalla todo el combate: el panel de TIME.TIM arriba en el centro, «1P» y «COM» a los lados y los segundos en dos cifras, sin el cero de las decenas por debajo de 10 | `std_main.c: STD_renderFighterCounter`, `STD_renderCounterDigits`, `STD_removeFighterCounter` | `BattleView::matchSeconds`, `ui/battle` (`match-clock`), aspecto original | battle view: STD_renderFighterCounter (ruta `clock`) | Igual |
| Aviso de tiempo agotado: el texto de STDTIM y la caja con el daño hecho por cada luchador | `STD_addTimeoutWindow`, `STD_renderTimeoutText` | - | - | Hueco: el combate acaba con el juicio del tiempo sin el aviso |
| Menú de órdenes de la copa | `STD_addCommandMenu`, `STD_handleCommands` | menú de combate normal | - | Suposición: mismas órdenes que el combate normal |

## Tests

`dwe_tournament_tests`: el cuadro (tablas, celdas, presentación, combates con disparos, subida del campeón), las
secciones `bracket`, `extra_prizes` y `medal` con la medalla de los torneos, las listas de una copa en un guion, el calendario de inscripción (copas abiertas, cinco días,
corte de las 10:00, día de entrada), la arena (cuál, dónde empiezan los luchadores, la cámara), el sorteo de rivales, el cuadro, los sustitutos, los
valores y técnicas de un rival y las victorias que ganan cada copa. `dwe_battle_tests` (`tournament_match_tests.cpp`):
el combate de copa sin premios, la derrota al caer el partner, el reloj y el juicio al acabar el tiempo.

Ruta: `tests/autotest/route-etapa3-tournaments.yaml` (en y es) entra en la copa C y pierde, y en la D al día
siguiente y la gana (`docs/guides/prueba-vertical.md`).
