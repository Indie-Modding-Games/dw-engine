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
  reloj (`BattleSetup::timeLimit`): sin huida, sin bolsa, sin premios ni cuidados al acabar.
- **Final**: `TournamentAdapter::finish` quita la inscripción (disparador 37), suma al historial (copas ganadas,
  perdidas, victorias), pone el disparador de la copa ganada (15 + copa) y deja las victorias en la variable 255,
  que lee el guion.

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
| Modo de carga del rival sorteado en cada decisión (2 con un 10 %, si no 0 o 1) | `std_setup.c: STD_tickEnemyAI` | - | - | Hueco: el rival usa la IA de enemigo normal |
| Reloj de 99 s: un segundo cada 20 fotogramas, parado con un luchador caído | `std_main.c: STD_addFighterCounter`, `STD_tickFighterCounter` | `MatchClock`, `Battle::step` | timer second per 20 frames | Igual |
| Fin del combate: caído el partner pierde; caídos los rivales gana; los dos, empate; al acabar el tiempo gana quien perdió menos vida desde el inicio, igual es empate; el empate cuenta como derrota | `std_main.c: STD_checkEndCondition`, `STD_deinitializeCombat` | `Battle::endMatch` | fallen partner loses; time-up judgement | Igual |
| Sin premios, sin cuidados ni vidas tras el combate de copa | `std_setup.c: STD_deinitializeCombat` | `settleBattle` (tipo `Tournament`) | no rewards | Igual |
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
| Arena, cámara de copa, presentación, cuadro animado, letreros de enfrentamiento/victoria/derrota, escena del campeón | `std_*.c` (`STD_runIntro`, `STD_addBracket`, `STD_addChampionScene`...) | - | - | Hueco: los combates se juegan en el mapa actual con la cámara de combate normal |
| Rival dibujado en el campo | `std_setup.c: STD_placePlayer2` | - | - | Hueco: la pantalla de combate solo dibuja personajes del mapa; el rival pelea pero no se ve |
| Reloj en pantalla y aviso de tiempo agotado | `STD_tickFighterCounter`, `STD_addTimeoutWindow` | - | - | Hueco |
| Menú de órdenes de la copa | `STD_addCommandMenu`, `STD_handleCommands` | menú de combate normal | - | Suposición: mismas órdenes que el combate normal |

## Tests

`dwe_tournament_tests`: las listas de una copa en un guion, el calendario de inscripción (copas abiertas, cinco días,
corte de las 10:00, día de entrada), el sorteo de rivales, el cuadro, los sustitutos, los
valores y técnicas de un rival y las victorias que ganan cada copa. `dwe_battle_tests` (`tournament_match_tests.cpp`):
el combate de copa sin premios, la derrota al caer el partner, el reloj y el juicio al acabar el tiempo.
