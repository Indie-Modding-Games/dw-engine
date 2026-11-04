# Cobertura del port

Qué funciones del decomp tiene ya el motor, sistema por sistema, para cerrar los huecos antes de que el jugador los
encuentre jugando. Cada función del índice del decomp está en uno de cuatro estados:

- **Portado:** el código del motor la hace. Casi siempre la nombra en un comentario o en un log (`[port:...]`); si no,
  una entrada manual dice dónde está.
- **Parcial:** el motor hace una parte; la nota dice qué falta.
- **Falta:** el motor no la hace.
- **No aplica:** el motor la sustituye por lo suyo y no hay que portarla: gráficos, sonido, disco y memoria del
  hardware original, cuadros y cursores de su interfaz, lectura de operandos de los scripts (que se pasan a Lua) y
  las copias del código de batalla que llevan los overlays de torneos, versus y el segundo gimnasio.

## Cómo se hace

`scripts/port_coverage.py` lee el índice de funciones del decomp (`functions.tsv`, fuera del repositorio; por
defecto `../decomp-index/functions.tsv`, o `--index`) y busca cada nombre en el código del motor (`game/`, `apps/`,
`engine/`, `tests/`, `content/packs/`). Lo que solo nombran la extracción y las herramientas (`apps/dwx`, `formats/`)
cuenta como falta: nombran las funciones del original para encontrar sus tablas. Una cita que dice que algo aún no se
hace («not drawn yet», «not ported», `TODO`) deja la función en parcial. Las citas solo por línea (`partner.c:906`) no
se usan: el índice no tiene las funciones estáticas y la línea cae a menudo en la vecina.

Después aplica `docs/reference/port-coverage.toml`: el sistema de cada fichero del decomp, las reglas de «no
aplica» y las decisiones a mano por función (estado, nota, sitio en el motor y qué hace). Escribe la parte de esta
página que va entre las dos marcas; el resto se edita a mano.

```sh
python3 scripts/port_coverage.py            # reescribe la parte generada
python3 scripts/port_coverage.py --check    # sale con 1 si la página no está al día
python3 scripts/port_coverage.py --list     # una fila por función, para revisar
```

Límites: que el motor nombre una función no prueba que la haga entera, y una función que hace sin nombrarla sale como
falta hasta que una entrada manual la corrige. Las entradas manuales de pesca, muerte y renacer, evolución, piedras,
vídeos, torneos y versus salen de leer el cuerpo de cada función; conviene repasarlas al portar esos sistemas.

## Top 20 huecos que el jugador notará

Ordenados por lo pronto que aparecen en una partida normal: combates, cuidado del compañero, entrenamiento, la ciudad
de inicio y los mapas cercanos, y reclutar.

| # | Hueco | Decomp | Motor |
|---:|---|---|---|
| 1 | La máquina de guardar del mapa no se abre: la rutina 48 se cierra al momento; solo se guarda al dormir y con los autoguardados | `script_interp.c` rutina 48, `main_menu.c:5288` `tickSaveMachine` | `apps/game/src/story_scene_glue.cpp:48`; la máquina existe en `save_machine.cpp` |
| 2 | Sin cifras de curación ni de mejoras (con icono) sobre los luchadores; las de daño ya salen | `entity_text.c:71` `initializeEntityText`, `utils2.c` `drawEntityTextIcon`, `item.c` `handleHPHealingItem` | `apps/screens/src/damage_numbers.cpp` |
| 3 | Los efectos de las técnicas no dibujan cintas, sprites de pantalla, fundidos ni cambios de paleta, y la cámara no se mueve para lucirlas (los emisores de partículas, las nubes y los destellos ya salen) | `battle_effect.c` (`BTL_tickRibbonPoints`, `BTL_setupFixedCamera`, `BTL_renderScreenSprite`, `BTL_addClutLoadPrim`) | `game/battle/src/effect_runner_subops.cpp` salta esas subórdenes |
| 4 | Cerrado: cada técnica llega cuando su script toca al defensor (`ScriptedFlights`, fila «Viaje del proyectil» de battle.md); `battle.timing.projectile_speed` solo vale sin scripts (simulación, pruebas) o si el script nunca toca | scripts de efecto de cada técnica | `game/battle/src/attack_hits.cpp` (`effectLanding`) |
| 5 | Los estados no se ven sobre el luchador: ni burbujas de veneno, ni confusión, ni estrellas de aturdimiento (solo el texto del HUD) | `battle_main.c:2698` `BTL_addStatusEffectVisual`, `battle_effect.c` `BTL_addPoisonBubble`, `BTL_tickConfusionEffect`, `BTL_tickStunEffect` | `apps/screens/src/battle_view.cpp:45` |
| 6 | La escena de evolución no dibuja partículas, huesos que se rompen ni destellos | `evl/evl.c` `EVL_tickParticle`, `EVL_tickShardSet`, `EVL_tickSpark`, `EVL_initEvoSequence` | `apps/game/src/evolution_scene.cpp:231` |
| 7 | Al dar de comer no se ve el objeto en la mano del jugador | `game_menu.c:2245` `renderFeedingItem` | `apps/game/src/feed_scene.cpp:81` |
| 8 | Tras un combate de historia el compañero no vuelve andando: la vuelta acaba al momento, y no se guarda su rastro durante el combate | `script_interp.c:1950`; `battle_main.c:702` `BTL_tickPartnerWaypointTrail` | `apps/session/src/story_scenes.cpp:75-79` (esbozo) |
| 9 | Los personajes del mapa que no luchan no huyen al empezar el combate (se van andando tras ganar) | `battle_setup.c` `BTL_handleBattleIntro`, `battle_tick.c:308` `NPCEntityTickBattle`, `btl.c:128` `isScreenConcave` | docs/modulos/game/battle.md:126 |
| 10 | El cuadro final del combate no dice la orden aprendida ni el bonus de MP | `battle_ui.c:297` `handleBattleEndBox`, `battle_hud.c` `BTL_appendCommandLearnedText`, `BTL_appendMPBonusText` | `apps/screens/include/dwe/screens/battle_view.hpp:116` |
| 11 | Con la bolsa llena, recibir un objeto no pregunta cuál tirar: la rutina 0 se cierra al momento | `script_common.c:1815` `openDiscardItem` | `apps/session/src/story_routines/routine_discard_item.cpp` |
| 12 | Sin medallas ni la escena de premio (medalla o nivel del jugador) | `script_common.c:3026` `unlockMedal`, `tamer.c:721` `checkMedalConditions`, `:793` `checkPendingAwards`, `:1751` `tamerTickAwardSomething` | — |
| 13 | Las pantallas de servicio de la ciudad se cierran al momento: guardarropía de objetos, gramola, intercambio por méritos, trueque, cartas, el cuadro del dinero y el humo de un personaje que desaparece | `shop/script_menu.c` `tickItemKeeper`, `openJukebox`, `openMeritShop`, `openMojyamonShop`, `openBuyCardMenu`, `rollCardPack`; `script_common.c` `createShopBitsBox`; `overworld.c:777` `createNinjamonEffect` | `apps/game/src/story_scene_glue.cpp:48`; rutinas 1, 2, 9, 11, 14, 15 y 38 |
| 14 | El compañero infeliz se distrae, pero no se ve la mariposa que persigue | `butterfly.c` `setButterfly`, `tickButterfly` | `game/partner/src/partner_tick.cpp:313` |
| 15 | Cerrado: las partículas de curación salen al usar un objeto en el campo y al lanzarlo en combate, y las de objeto en combate también | `particle.c` `addHealingParticleEffect`, `tickHealingParticles`; `battle_effect.c` `BTL_addItemParticles` | `apps/game/src/field_effects.cpp`, `apps/game/src/battle_screen.cpp` (`stepFieldEffects`), `game/battle/src/status_visuals_effects.cpp` |
| 16 | La pesca: la cámara no va al corcho ni se mueve con la cruceta, el sedal y las ondas no se dibujan, y el cebo es un marcador | `fish/fish.c` `FISH_panCameraTo`, `FISH_panCameraWithDPad`, `FISH_clipLineToRect`, `FISH_tickRipple`, `FISH_showBaitSprite` | `apps/game/src/fishing_screen.cpp:50`, `:231` |
| 17 | Las escenas de muerte y renacer no dibujan el iris ni los fragmentos, destellos y chispas del huevo | `murd/murd.c`, `dooa/dooa.c`, `doo2/doo2.c` | `apps/game/src/death_scene.cpp:265`, `:394` |
| 18 | Objetos de mapa con lógica sin portar: tapa de los cofres, montones de tierra y vagoneta de la mina, roca, cajas de la ciudad de juguete, engranajes, pedestal, cristales de teletransporte, niebla; y las rutinas de guion que los ponen (19, 20, 26, 27, 41–44, 49) siguen sin hacer nada | `map_object.c` (`tickOpenChestTray`, `decreaseDirtPileSize`, `spawnBoulder`, `spawnToyTownBoxes`, `spawnGearbox`, `spawnAngemonPedestal`, `loadWarpCrystals`), `overworld.c:1012` `tickRemoveMist` | `game/story/src/story_routines.cpp:27` (rutina sin manejador: sigue) |
| 19 | Sin puntos de prosperidad: no se recalculan al cargar ni al empezar, y la ciudad no elige su mapa según lo que ha crecido | `main.c:3902` `recalculatePPandArena`, `map.c:5592` `getFileCityTopMap` | — |
| 20 | Sin torneos (el guion pide la copa y sigue sin jugarla) ni el minijuego de piedras; más tarde, la aparición del jefe final, los créditos y el modo versus | `tournament.c:128` `scriptStartTournament`, `utils2.c:577` `startTournament`, `std/`; `kar/kar.c:778` `KAR_start`; `eab/`, `endi/`, `vs/` | `apps/session/src/story_routines/routine_tournament.cpp`; `apps/game/src/story_scene_glue.cpp:58` |

## Otros huecos que no son una función del índice

- **Deslizamientos de guion contra personajes:** `wouldTouchCharacter` (del motor) ignora a los NPC, así que un
  deslizamiento del guion puede atravesarlos (docs/modulos/game/field.md:150).
- **Rutinas de guion sin manejador:** el guion sigue sin hacer nada en las rutinas 19, 20, 26, 27, 28 (burbuja de
  humor), 34 (temblor de la escena del fuego), 40 (jefe final), 41–46 (engranajes, ciudad de juguete, fundidos a
  blanco), 49 (pedestal), 51 y 52 (final) y 54 (guardado de partida terminada) (`script_interp.c:1655-1960`).
- **Escenas del compañero que acaban al momento:** retrete del guion, piedras, apertura, final y aparición del jefe
  (`apps/game/src/story_scene_glue.cpp:58-66`).
- **Año de 31 días al entrenar:** el original avanza el día con un año de 31 días tras entrenar; el motor usa el
  reloj, de 30. Decidido así (docs/modulos/game/partner.md:187).

<!-- port-coverage:begin (scripts/port_coverage.py writes this part) -->

## Resumen por sistema

Solo cuentan las funciones a portar (portado + parcial + falta); las atadas a la consola quedan fuera. Cobertura = portado + medio parcial, sobre las funciones a portar.

| Sistema | Ficheros del decomp | A portar | Portado | Parcial | Falta | Cobertura |
|---|---|---:|---:|---:|---:|---:|
| [Batalla](#batalla) | `btl/`, `main/` (9 ficheros) | 154 | 154 | 0 | 0 | 100 % |
| [Efectos de batalla](#efectos-de-batalla) | `btl/`, `main/` (6 ficheros) | 240 | 240 | 0 | 0 | 100 % |
| [Campo: movimiento, colisión y cámara](#campo-movimiento-colision-y-camara) | `main/` (5 ficheros) | 82 | 80 | 2 | 0 | 99 % |
| [NPC y objetos del mapa](#npc-y-objetos-del-mapa) | `main/` (8 ficheros) | 79 | 79 | 0 | 0 | 100 % |
| [Compañero: IA y cuidado](#companero-ia-y-cuidado) | `main/partner.c`, `main/partner_impl.c` | 49 | 49 | 0 | 0 | 100 % |
| [Guion e intérprete](#guion-e-interprete) | `main/script_common.c`, `main/script_interp.c`, `main/script_textbox.c`, `main/script_value.c` | 98 | 98 | 0 | 0 | 100 % |
| [Menús e interfaz](#menus-e-interfaz) | `main/` (8 ficheros) | 79 | 79 | 0 | 0 | 100 % |
| [Objetos y tiendas](#objetos-y-tiendas) | `main/item.c`, `shop/script_menu.c` | 96 | 96 | 0 | 0 | 100 % |
| [Evolución](#evolucion) | `evl/evl.c`, `main/evl.c`, `main/evolution.c` | 29 | 29 | 0 | 0 | 100 % |
| [Entrenamiento](#entrenamiento) | `trn/`, `trn2/` (11 ficheros) | 38 | 37 | 1 | 0 | 99 % |
| [Pesca](#pesca) | `fish/fish.c`, `fish/fish_model.c`, `main/fish.c` | 96 | 96 | 0 | 0 | 100 % |
| [Muerte y renacer](#muerte-y-renacer) | `doo2/doo2.c`, `dooa/dooa.c`, `murd/murd.c` | 40 | 40 | 0 | 0 | 100 % |
| [Guardado y carga](#guardado-y-carga) | `main/main_menu.c` | 11 | 10 | 0 | 1 | 91 % |
| [Torneos](#torneos) | `dget/`, `main/`, `std/` (6 ficheros) | 112 | 107 | 5 | 0 | 98 % |
| [Versus y demo](#versus-y-demo) | `main/`, `vs/` (10 ficheros) | 91 | 0 | 0 | 91 | 0 % |
| [Minijuego de piedras](#minijuego-de-piedras) | `kar/kar.c`, `main/kar.c` | 44 | 44 | 0 | 0 | 100 % |
| [Audio](#audio) | `main/sound.c`, `main/sound_async.c` | 21 | 21 | 0 | 0 | 100 % |
| [Vídeos y final](#videos-y-final) | `eab/eab.c`, `endi/endi.c`, `mov/mov.c` | 31 | 31 | 0 | 0 | 100 % |
| [Núcleo y plataforma](#nucleo-y-plataforma) | `main/` (11 ficheros) | 51 | 51 | 0 | 0 | 100 % |
| **Total** | 1441 funciones a portar | 1441 | 1341 | 8 | 92 | 93 % |

## Batalla

El combate normal: órdenes, IA, daño, estados, HUD, inicio y final, aprendizaje de técnicas.

Ficheros del decomp: `btl/battle_hud.c`, `btl/battle_main.c`, `btl/battle_setup.c`, `btl/command_menu.c`, `btl/command_shout.c`, `main/battle_tick.c`, `main/battle_ui.c`, `main/btl.c`, `main/entity_text.c`.

Portado 154, parcial 0, falta 0.

## Efectos de batalla

La máquina de efectos de las técnicas (scripts EFE): proyectiles, golpes, cámara, partículas, destellos.

Ficheros del decomp: `btl/battle_effect.c`, `main/buff_model.c`, `main/efe.c`, `main/efe_table.c`, `main/effect_common.c`, `main/particle.c`.

Portado 240, parcial 0, falta 0.

## Campo: movimiento, colisión y cámara

El jugador en el mapa, la rejilla de colisión, los warps, el seguimiento del compañero y la cámara.

Ficheros del decomp: `main/aabb.c`, `main/map.c`, `main/map_collision.c`, `main/tamer.c`, `main/vecmath.c`.

Portado 80, parcial 2, falta 0.

| Función | Decomp | Estado | Qué hace | Motor / nota |
|---|---|---|---|---|
| `tamerTickSicknessLostLife` | `main/tamer.c:1699` | parcial | Escena de perder una vida por enfermedad. | `apps/game/src/death_scene.cpp:126` · la restauración tras la vida perdida por enfermedad es un hueco (docs/modulos/game/battle.md:107) |
| `tamerTickBattleLostLife` | `main/tamer.c:1734` | parcial | Escena de perder una vida en combate. | `apps/game/src/death_scene.cpp:126` · la vida y el MP tras perder salen de una regla hasta los guiones (docs/modulos/game/battle.md:107) |

## NPC y objetos del mapa

Los personajes del mapa y su IA de paseo, cofres, objetos del suelo y decorados con lógica.

Ficheros del decomp: `main/anim.c`, `main/bubble.c`, `main/butterfly.c`, `main/drop_shadow.c`, `main/map_object.c`, `main/model.c`, `main/overworld.c`, `main/world_object.c`.

Portado 79, parcial 0, falta 0.

## Compañero: IA y cuidado

Hambre, sueño, baño, enfermedad, felicidad y disciplina, caca, muerte natural, paseo y órdenes.

Ficheros del decomp: `main/partner.c`, `main/partner_impl.c`.

Portado 49, parcial 0, falta 0.

## Guion e intérprete

La máquina de scripts de eventos: órdenes, valores, cajas de texto y elecciones.

Ficheros del decomp: `main/script_common.c`, `main/script_interp.c`, `main/script_textbox.c`, `main/script_value.c`.

Portado 98, parcial 0, falta 0.

## Menús e interfaz

Título, menú de juego, ficha del compañero, bolsa, cajas de interfaz, fundidos y texto.

Ficheros del decomp: `main/fade.c`, `main/font.c`, `main/game_menu.c`, `main/inventory.c`, `main/main_menu.c`, `main/new_game.c`, `main/sjis.c`, `main/ui.c`.

Portado 79, parcial 0, falta 0.

## Objetos y tiendas

Uso de objetos, comida y chips, tiendas, cartas, guardarropía y menús de guion del overlay de tiendas.

Ficheros del decomp: `main/item.c`, `shop/script_menu.c`.

Portado 96, parcial 0, falta 0.

## Evolución

Elección del destino y la escena de evolución.

Ficheros del decomp: `evl/evl.c`, `main/evl.c`, `main/evolution.c`.

Portado 29, parcial 0, falta 0.

## Entrenamiento

El gimnasio: tragaperras, cursos y recompensas.

Ficheros del decomp: `trn/trn_brain.c`, `trn/trn_hud.c`, `trn/trn_off.c`, `trn/trn_reward.c`, `trn/trn_slots.c`, `trn/trn_speed.c`, `trn2/trn2_def_map108.c`, `trn2/trn2_hp_map107.c`, `trn2/trn2_hud.c`, `trn2/trn2_reward.c`, `trn2/trn2_slots.c`.

Portado 37, parcial 1, falta 0.

| Función | Decomp | Estado | Qué hace | Motor / nota |
|---|---|---|---|---|
| `TRN_advanceTrainingTime` | `trn/trn_reward.c:721` | parcial | Avanza la hora y el día tras una sesión de entrenamiento. | `content/packs/game/rules/original/care.yaml:229` · el original usa un año de 31 días al entrenar; el motor, el del reloj (30). Decidido así (partner.md:187) |

## Pesca

El minijuego de pesca.

Ficheros del decomp: `fish/fish.c`, `fish/fish_model.c`, `main/fish.c`.

Portado 96, parcial 0, falta 0.

## Muerte y renacer

La escena de muerte, la de renacer y la caja de huevos.

Ficheros del decomp: `doo2/doo2.c`, `dooa/dooa.c`, `murd/murd.c`.

Portado 40, parcial 0, falta 0.

## Guardado y carga

La partida en la tarjeta de memoria, la máquina de guardar y el registro para el modo versus.

Ficheros del decomp: `main/main_menu.c`.

Portado 10, parcial 0, falta 1.

| Función | Decomp | Estado | Qué hace | Motor / nota |
|---|---|---|---|---|
| `countRegisteredDigimon` | `main/main_menu.c:5136` | falta | Cuenta las criaturas registradas para el modo versus. | llega con el modo versus |

## Torneos

Inscripción, calendario y el overlay de copas, que trae su propio motor de combate.

Ficheros del decomp: `dget/dget.c`, `main/tournament.c`, `std/std_effect.c`, `std/std_hud.c`, `std/std_main.c`, `std/std_setup.c`.

Portado 107, parcial 5, falta 0.

| Función | Decomp | Estado | Qué hace | Motor / nota |
|---|---|---|---|---|
| `STD_tickChampionScene` | `std/std_effect.c:1259` | parcial | Anima la escena del campeón: letras, giro de cámara y podio. | `game/tournament/src/champion_show.cpp:1` · falta el brillo que recorre cada letra (copia dentro de la memoria de vídeo) |
| `STD_tickTamerTournament` | `std/std_hud.c:540` | parcial | Lee la entrada del jugador en la copa: alterna barras de vida y da órdenes. | `game/battle/src/battle.cpp:1` · las órdenes sí; select no oculta las barras de vida |
| `STD_tickBattleResultScreen` | `std/std_main.c:4641` | parcial | Gestiona la pantalla de resultado tras el combate de copa. | `apps/game/src/story_tournament.cpp:1` · TIME UP, la caja de daño y el empate; el que pierde no se desploma tras la caja |
| `STD_startQueuedMove` | `std/std_main.c:6599` | parcial | — | `apps/game/src/story_tournament.cpp:627` · The flattened fighter's shot has no technique camera (STD_startQueuedMove skips |
| `STD_initializeTournament` | `std/std_setup.c:243` | parcial | Prepara la arena de la copa: música, cámara, luces, escenario y efectos. | `apps/game/src/story_tournament.cpp:1` · modelo, suelo y cámara de la arena; sin música propia, luces ni efectos de la copa |

## Versus y demo

El modo versus de dos jugadores y la demo de combate del título.

Ficheros del decomp: `main/sound.c`, `vs/vs_camera.c`, `vs/vs_combat.c`, `vs/vs_demo.c`, `vs/vs_effect.c`, `vs/vs_hud.c`, `vs/vs_intro.c`, `vs/vs_main.c`, `vs/vs_scene.c`, `vs/vs_select.c`.

Portado 0, parcial 0, falta 91.

| Función | Decomp | Estado | Qué hace | Motor / nota |
|---|---|---|---|---|
| `VSLoadSounds` | `main/sound.c:710` | falta (citada en docs) | Carga los sonidos del modo versus. | — |
| `VS_setCameraOrbit` | `vs/vs_camera.c:179` | falta | Cámara del versus que orbita según la distancia entre luchadores. | — |
| `VS_setCameraYXZ` | `vs/vs_camera.c:221` | falta | Cámara del versus que encuadra a ambos luchadores. | — |
| `VS_setViewpointRotationFromEntity` | `vs/vs_camera.c:241` | falta | Cámara del versus que gira con el luchador enfocado. | — |
| `VS_setCameraLookAtEntity` | `vs/vs_camera.c:252` | falta | Cámara del versus que mira al luchador enfocado. | — |
| `VS_setCameraSimple` | `vs/vs_camera.c:288` | falta | Cámara del versus que gira lento alrededor de la arena. | — |
| `VS_setVSPhase` | `vs/vs_camera.c:309` | falta | Activa el control de cámara del versus con el modo indicado. | — |
| `VS_tickVSPhase` | `vs/vs_camera.c:316` | falta | Elige cada cuadro el modo de cámara activo del versus. | — |
| `VS_setRandomViewpoint` | `vs/vs_camera.c:386` | falta | Elige al azar un punto de vista preestablecido en el versus. | — |
| `VS_setViewpointFromBone` | `vs/vs_camera.c:425` | falta | Sitúa la cámara del versus relativa a un hueso del luchador. | — |
| `VS_setCameraToEntity` | `vs/vs_camera.c:454` | falta | Cámara del versus fija relativa al luchador enfocado. | — |
| `VS_updateCameraLerp` | `vs/vs_camera.c:485` | falta | Interpola la cámara de presentación del versus. | — |
| `VS_tickCameraChase` | `vs/vs_camera.c:557` | falta | Cámara que persigue al luchador en la presentación del versus. | — |
| `VS_tickCameraIntro` | `vs/vs_camera.c:658` | falta | Avanza la secuencia de cámara de la presentación del versus. | — |
| `VS_startCameraIntro` | `vs/vs_camera.c:683` | falta | Inicia el recorrido de cámara entre los luchadores del versus. | — |
| `VS_applyEntityViewpoint` | `vs/vs_camera.c:755` | falta | Cámara del versus anclada a la vista del luchador. | — |
| `VS_addFighterCounter` | `vs/vs_camera.c:819` | falta | Inicia el reloj de tiempo del combate versus. | — |
| `VS_tickFighterCounter` | `vs/vs_camera.c:828` | falta | Descuenta el reloj del combate versus. | — |
| `VS__combatInit` | `vs/vs_combat.c:280` | falta | Prepara el combate versus: daño, reloj, marcas y fichas de ambos luchadores. | — |
| `VS_setupDemoCombat` | `vs/vs_combat.c:449` | falta | Prepara el combate automático de la demostración. | — |
| `VS_checkDemoEndCondition` | `vs/vs_combat.c:552` | falta | Comprueba si la demostración termina por KO, tiempo o botón. | — |
| `VS_cleanupDemoCombat` | `vs/vs_combat.c:750` | falta | Cierra el combate de demostración y libera sus objetos. | — |
| `VS__combatSetup` | `vs/vs_combat.c:780` | falta | Prepara los datos de combate de los dos jugadores del versus. | — |
| `VS__checkEndCondition` | `vs/vs_combat.c:871` | falta | Comprueba fin del combate versus por KO o tiempo y decide ganador. | — |
| `VS__tickBattle` | `vs/vs_combat.c:1143` | falta | Avanza un cuadro del combate versus. | — |
| `VS__handlePause` | `vs/vs_combat.c:1233` | falta | Gestiona la pausa del versus por cualquiera de los jugadores. | — |
| `VS__areAllEnemyDigimonDead` | `vs/vs_combat.c:1414` | falta | Indica si todos los rivales del versus han caído. | — |
| `VS__addTimeoutWindow` | `vs/vs_combat.c:1505` | falta | Muestra el aviso de tiempo agotado del versus. | — |
| `VS__tickDigimonStun` | `vs/vs_combat.c:1622` | falta | Mantiene al luchador aturdido con su animación. | — |
| `VS__tickDigimonSenile` | `vs/vs_combat.c:1684` | falta | Mantiene al luchador senil hasta que se agote su contador. | — |
| `VS__entityGetMoveWithHighestDistance` | `vs/vs_combat.c:2000` | falta | Busca la técnica de mayor alcance del luchador. | — |
| `VS__tickDigimonAttackingLogic` | `vs/vs_combat.c:2136` | falta | Lógica de ataque del luchador en el versus: colisión y uso de técnicas. | — |
| `VS__getBaseDistance` | `vs/vs_combat.c:2269` | falta | Calcula la distancia base de combate según el tamaño de ambos luchadores. | — |
| `VS__hasAffordableMoves2` | `vs/vs_combat.c:2292` | falta | Indica si el luchador tiene técnicas que pueda pagar con su MP. | — |
| `VS__initializePlayerMarker` | `vs/vs_combat.c:2337` | falta | Añade la marca que distingue a los luchadores de la misma especie. | — |
| `VS__clearFighterDataTables` | `vs/vs_combat.c:2343` | falta | Vacía las tablas internas de la ficha de un luchador. | — |
| `VS_runDemoCombat` | `vs/vs_combat.c:2429` | falta | Ejecuta el combate de demostración con música y sin menús. | — |
| `VS__combatMain` | `vs/vs_combat.c:2474` | falta | Bucle principal de un combate versus hasta el resultado. | — |
| `VS__tickVSInput` | `vs/vs_combat.c:2592` | falta | Lee las órdenes de los jugadores durante el combate versus. | — |
| `VS___tickVSInput` | `vs/vs_combat.c:2599` | falta | — | — |
| `VS__tickDigimonP1` | `vs/vs_combat.c:2679` | falta | Actualiza cada cuadro al luchador del jugador 1. | — |
| `VS__tickDigimonP2` | `vs/vs_combat.c:2684` | falta | Actualiza cada cuadro al luchador del jugador 2. | — |
| `VS_setupDemoBattle` | `vs/vs_demo.c:107` | falta | Prepara la demostración: arena, dos luchadores al azar y sus sonidos. | — |
| `VS_cleanupDemoBattle` | `vs/vs_demo.c:133` | falta | Retira a los luchadores y la arena al acabar la demostración. | — |
| `VS_loadDemoAssets` | `vs/vs_demo.c:155` | falta | Elige arena y música al azar para la demostración y prepara efectos. | — |
| `VS_loadDemoFighters` | `vs/vs_demo.c:180` | falta | Elige al azar las dos criaturas de la demostración. | — |
| `VS_randomizeDemoFighter` | `vs/vs_demo.c:195` | falta | Genera valores y técnicas al azar para un luchador de demostración. | — |
| `VS_setupTrialBattle` | `vs/vs_demo.c:348` | falta | Prepara el versus de prueba con tres criaturas al azar por bando. | — |
| `VS_randomizeTrialDigimonList` | `vs/vs_demo.c:373` | falta | Rellena la lista de criaturas al azar de un bando del versus de prueba. | — |
| `VS_randomizeTrialDigimon` | `vs/vs_demo.c:383` | falta | Genera valores y técnicas al azar para una criatura del versus de prueba. | — |
| `VS_playDemo` | `vs/vs_demo.c:477` | falta | Reproduce la demostración automática de combate de la pantalla de título. | — |
| `VS_initializeTrialBattle` | `vs/vs_demo.c:486` | falta | Inicia el versus de prueba con las listas de ambos jugadores. | — |
| `VS_addCommandMenu` | `vs/vs_hud.c:449` | falta | Muestra el menú de órdenes de un jugador del versus. | — |
| `VS_tickTargetCursor` | `vs/vs_hud.c:652` | falta | Mueve el cursor de objetivo del jugador del versus. | — |
| `VS___tickVSInput` | `vs/vs_hud.c:688` | falta | Procesa las órdenes de un jugador del versus con su propio mando. | — |
| `VS_addVersusModelScene` | `vs/vs_hud.c:1349` | falta | Inicia la animación del letrero de enfrentamiento del versus. | — |
| `VS_tickVersusModelScene` | `vs/vs_hud.c:1355` | falta | Anima el letrero de enfrentamiento del versus. | — |
| `VS_isVersusModelSceneFinished` | `vs/vs_hud.c:1401` | falta | Indica si terminó el letrero de enfrentamiento del versus. | — |
| `VS_loadStageModels` | `vs/vs_hud.c:1410` | falta | Calcula las curvas de los letreros de resultado y carga sus modelos. | — |
| `VS_addResultModelScene` | `vs/vs_hud.c:1445` | falta | Muestra el letrero de victoria del jugador ganador del versus. | — |
| `VS_setVersusModelSceneTimer` | `vs/vs_hud.c:1549` | falta | Fija el temporizador de la escena de letreros del versus. | — |
| `VS__setPostIntroPosition` | `vs/vs_intro.c:256` | falta | Deja al luchador en su posición final tras la presentación del versus. | — |
| `VS__tickIntroName` | `vs/vs_intro.c:540` | falta | Muestra el nombre del luchador durante la presentación del versus. | — |
| `VS_applyPartnerStatsToFighter` | `vs/vs_main.c:292` | falta | Copia los valores de la criatura a su ficha de luchador del versus. | — |
| `VS_rollAttackOutcome` | `vs/vs_main.c:346` | falta | Tira el resultado de un ataque: acierto, daño y efectos. | — |
| `VS_startAttackAnimation` | `vs/vs_main.c:553` | falta | Inicia la animación y partículas de una técnica. | — |
| `VS_resolveAttack` | `vs/vs_main.c:562` | falta | Resuelve los impactos de las técnicas en vuelo. | — |
| `VS_startFighterMove` | `vs/vs_main.c:797` | falta | Hace que el luchador empiece a ejecutar una técnica. | — |
| `VS_selectMoveTarget` | `vs/vs_main.c:856` | falta | Elige el objetivo de la técnica del luchador en el versus. | — |
| `VS_playMoveEffect` | `vs/vs_main.c:895` | falta | Lanza el efecto visual y sonoro de una técnica. | — |
| `VS_updateFighterStatusVisuals` | `vs/vs_main.c:1035` | falta | Actualiza los efectos visibles de estados alterados en el versus. | — |
| `VS_applyMoveResult` | `vs/vs_main.c:1151` | falta | Aplica el daño y efectos de una técnica que impacta en el versus. | — |
| `VS_queueRandomMove` | `vs/vs_main.c:1735` | falta | Elige al azar técnica y objetivo para la IA del luchador. | — |
| `VS_initializeVS` | `vs/vs_scene.c:1069` | falta | Flujo del versus: presentación y serie de combates hasta acabar. | — |
| `VS_resetMatchState` | `vs/vs_scene.c:1128` | falta | Reinicia victorias, derrotas y ronda del versus. | — |
| `VS_loadVSAssets` | `vs/vs_scene.c:1147` | falta | Prepara la arena del versus: música por escenario, cámara, luces y efectos. | solo lo cita la extracción del disco |
| `VS_addInputObjects` | `vs/vs_scene.c:1184` | falta | Registra la lectura de órdenes de ambos jugadores del versus. | — |
| `VS_loadFighterEntities` | `vs/vs_scene.c:1194` | falta | Crea a los dos luchadores del combate actual del versus. | — |
| `VS_getFirstSpecialMove` | `vs/vs_scene.c:1545` | falta | Busca la última técnica especial que conoce una especie. | — |
| `VS_playVersusIntroSequence` | `vs/vs_scene.c:1558` | falta | Reproduce la presentación inicial del versus. | — |
| `VS_tickVersusIntro` | `vs/vs_scene.c:1618` | falta | Avanza la presentación del versus por estados. | — |
| `VS_initializeCamera` | `vs/vs_scene.c:1851` | falta | Coloca la cámara inicial de la arena del versus. | — |
| `VS__loadTextures` | `vs/vs_select.c:271` | falta | Prepara la pantalla de selección: listas de criaturas de cada jugador. | — |
| `VS__tickSelectDigimon` | `vs/vs_select.c:330` | falta | Bucle de selección de criaturas de los dos jugadores. | — |
| `VS__isAlreadySelected` | `vs/vs_select.c:363` | falta | Indica si una criatura ya fue elegida para el equipo. | — |
| `VS__handleDigimonSelected` | `vs/vs_select.c:384` | falta | Añade la criatura elegida al equipo del jugador. | — |
| `VS__createPressStartToBeginBox` | `vs/vs_select.c:409` | falta | Muestra el aviso de pulsar Start para empezar. | — |
| `VS__initialize` | `vs/vs_select.c:442` | falta | Flujo de preparación del versus: modo, mapa y selección de criaturas. | — |
| `VS__tickSelectMode` | `vs/vs_select.c:989` | falta | Pantalla de elección del modo del versus (número de combates). | — |
| `VS__tickSelectBox` | `vs/vs_select.c:1053` | falta | Mueve y confirma la opción del cuadro de selección del versus. | — |
| `VS__tickSelectMap` | `vs/vs_select.c:1166` | falta | Pantalla de elección del escenario del versus. | — |

## Minijuego de piedras

El minijuego de piedras sobre hielo.

Ficheros del decomp: `kar/kar.c`, `main/kar.c`.

Portado 44, parcial 0, falta 0.

## Audio

Música, efectos de sonido y su carga.

Ficheros del decomp: `main/sound.c`, `main/sound_async.c`.

Portado 21, parcial 0, falta 0.

## Vídeos y final

El reproductor de vídeo, los créditos y la animación del final.

Ficheros del decomp: `eab/eab.c`, `endi/endi.c`, `mov/mov.c`.

Portado 31, parcial 0, falta 0.

## Núcleo y plataforma

Arranque, bucle principal, reloj, azar, matemáticas, ficheros, memoria y gráficos de bajo nivel.

Ficheros del decomp: `main/_psstart.c`, `main/clock.c`, `main/file.c`, `main/file_queue.c`, `main/graphics.c`, `main/line.c`, `main/main.c`, `main/math.c`, `main/rng.c`, `main/utils.c`, `main/utils2.c`.

Portado 51, parcial 0, falta 0.

## Fuera de las cuentas

997 funciones del decomp están atadas a la consola (dibujo, memoria, lectura del disco, sonido del hardware, tablas internas, depuración, copias del código de batalla en otros overlays) y no cuentan en ninguna cifra. `python3 scripts/port_coverage.py --list` las da con su motivo (estado `no-aplica`).

<!-- port-coverage:end -->
