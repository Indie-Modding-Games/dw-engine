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
| 2 | Sin cifras de daño ni de curación sobre los luchadores | `entity_text.c:71` `initializeEntityText`, `:147` `setCombatTextPosition` | — |
| 3 | Los efectos de las técnicas no dibujan partículas, nubes, cintas, destellos, sprites de pantalla, fundidos ni cambios de paleta, y la cámara no se mueve para lucirlas | `battle_effect.c` (`BTL_tickParticleEmitters`, `BTL_addCloudEffect`, `BTL_tickRibbonPoints`, `BTL_setupFixedCamera`, `BTL_renderScreenSprite`, `BTL_addClutLoadPrim`), `efe.c` (`EFECreateFlash`) | `game/battle/src/effect_runner_subops.cpp` salta esas subórdenes |
| 4 | Velocidad de proyectil única para todas las técnicas: los lentos se cortan antes de llegar y los rápidos pasan de largo | scripts de efecto de cada técnica | `battle.timing.projectile_speed` (docs/modulos/game/battle.md:217) |
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
| 15 | No hay partículas de curación al usar un objeto ni partículas de objeto en combate | `particle.c` `addHealingParticleEffect`, `tickHealingParticles`; `battle_effect.c` `BTL_addItemParticles` | — |
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
  (`apps/game/src/story_scene_glue.cpp:58-66`); la de olvidar técnicas (rutina 47) tampoco tiene pantalla.
- **Año de 31 días al entrenar:** el original avanza el día con un año de 31 días tras entrenar; el motor usa el
  reloj, de 30. Decidido así (docs/modulos/game/partner.md:187).

<!-- port-coverage:begin (scripts/port_coverage.py writes this part) -->

## Resumen por sistema

Cobertura = portado + medio parcial, sobre lo que hace falta portar (todo menos «no aplica»).

| Sistema | Ficheros del decomp | Portado | Parcial | Falta | No aplica | Cobertura |
|---|---|---:|---:|---:|---:|---:|
| [Batalla](#batalla) | `btl/`, `main/` (9 ficheros) | 128 | 4 | 13 | 26 | 90 % |
| [Efectos de batalla](#efectos-de-batalla) | `btl/`, `main/` (6 ficheros) | 134 | 4 | 74 | 73 | 64 % |
| [Campo: movimiento, colisión y cámara](#campo-movimiento-colision-y-camara) | `main/` (5 ficheros) | 67 | 2 | 11 | 16 | 85 % |
| [NPC y objetos del mapa](#npc-y-objetos-del-mapa) | `main/` (8 ficheros) | 40 | 1 | 21 | 40 | 65 % |
| [Compañero: IA y cuidado](#companero-ia-y-cuidado) | `main/partner.c`, `main/partner_impl.c` | 49 | 0 | 0 | 2 | 100 % |
| [Guion e intérprete](#guion-e-interprete) | `main/script_common.c`, `main/script_interp.c`, `main/script_textbox.c`, `main/script_value.c` | 73 | 3 | 12 | 57 | 85 % |
| [Menús e interfaz](#menus-e-interfaz) | `main/` (8 ficheros) | 65 | 1 | 2 | 56 | 96 % |
| [Objetos y tiendas](#objetos-y-tiendas) | `main/item.c`, `shop/script_menu.c` | 63 | 8 | 21 | 6 | 73 % |
| [Evolución](#evolucion) | `evl/evl.c`, `main/evl.c`, `main/evolution.c` | 14 | 1 | 9 | 14 | 60 % |
| [Entrenamiento](#entrenamiento) | `trn/`, `trn2/` (11 ficheros) | 34 | 1 | 2 | 13 | 93 % |
| [Pesca](#pesca) | `fish/fish.c`, `fish/fish_model.c`, `main/fish.c` | 64 | 9 | 8 | 39 | 85 % |
| [Muerte y renacer](#muerte-y-renacer) | `doo2/doo2.c`, `dooa/dooa.c`, `murd/murd.c` | 13 | 0 | 16 | 27 | 45 % |
| [Guardado y carga](#guardado-y-carga) | `main/main_menu.c` | 7 | 1 | 3 | 24 | 68 % |
| [Torneos](#torneos) | `dget/`, `main/`, `std/` (6 ficheros) | 0 | 4 | 81 | 364 | 2 % |
| [Versus y demo](#versus-y-demo) | `main/`, `vs/` (10 ficheros) | 0 | 0 | 91 | 315 | 0 % |
| [Minijuego de piedras](#minijuego-de-piedras) | `kar/kar.c`, `main/kar.c` | 0 | 0 | 35 | 13 | 0 % |
| [Audio](#audio) | `main/sound.c`, `main/sound_async.c` | 19 | 0 | 0 | 15 | 100 % |
| [Vídeos y final](#videos-y-final) | `eab/eab.c`, `endi/endi.c`, `mov/mov.c` | 3 | 0 | 23 | 18 | 12 % |
| [Núcleo y plataforma](#nucleo-y-plataforma) | `main/` (11 ficheros) | 40 | 1 | 2 | 43 | 94 % |
| **Total** | 2438 funciones | 813 | 40 | 424 | 1161 | 65 % |

## Batalla

El combate normal: órdenes, IA, daño, estados, HUD, inicio y final, aprendizaje de técnicas.

Ficheros del decomp: `btl/battle_hud.c`, `btl/battle_main.c`, `btl/battle_setup.c`, `btl/command_menu.c`, `btl/command_shout.c`, `main/battle_tick.c`, `main/battle_ui.c`, `main/btl.c`, `main/entity_text.c`.

Portado 128, parcial 4, falta 13, no aplica 26.

| Función | Decomp | Estado | Qué hace | Motor / nota |
|---|---|---|---|---|
| `BTL_appendCommandLearnedText` | `btl/battle_hud.c:280` | falta | Añade al cuadro final la orden de combate que el compañero aprendió. | docs/modulos/game/battle.md: Hueco |
| `BTL_appendMPBonusText` | `btl/battle_hud.c:324` | falta | Añade al cuadro final el bonus de MP ganado. | docs/modulos/game/battle.md: Hueco |
| `BTL_tickEnemyAI` | `btl/battle_main.c:515` | parcial | IA de los rivales: elige técnica, se acerca y ataca. | `content/packs/game/rules/original/battle.yaml:10` · el rival fuera de pantalla no es prudente (docs/modulos/game/battle.md:119) |
| `BTL_tickPartnerWaypointTrail` | `btl/battle_main.c:702` | falta | Durante el combate, guarda el rastro de casillas del compañero para que luego vuelva junto al jugador sin atascarse. | relacionado con la vuelta tras un combate de historia |
| `BTL_applyFlattenScale` | `btl/battle_main.c:2673` | falta | Aplasta el modelo del luchador aplastado. | el estado existe; falta comprobar la escala del modelo en pantalla |
| `BTL_addStatusEffectVisual` | `btl/battle_main.c:2698` | falta | Pone sobre el luchador el efecto visual de su estado (veneno, confusión, aturdimiento). | el HUD solo enseña el nombre del estado |
| `BTL_applyStretchScale` | `btl/battle_main.c:2715` | falta | Estira el modelo al recuperarse del aplastado. | — |
| `BTL_applySquashScale` | `btl/battle_main.c:2721` | falta | Encoge el modelo en el aplastado. | — |
| `BTL_addPoisonStatusVisual` | `btl/battle_main.c:2898` | falta | Pone las burbujas de veneno sobre el luchador. | — |
| `BTL_addConfusionStatusVisual` | `btl/battle_main.c:2905` | falta | Pone el efecto de confusión sobre el luchador. | — |
| `BTL_addStunStatusVisual` | `btl/battle_main.c:2915` | falta | Pone las estrellas de aturdimiento sobre el luchador. | — |
| `BTL_handleBattleIntro` | `btl/battle_setup.c:207` | parcial | Entrada al combate: quién se une, quién huye y el rótulo de inicio. | `apps/game/src/battle_screen.cpp:29` · los personajes que no luchan no huyen al empezar (docs/modulos/game/battle.md:126) |
| `NPCEntityTickBattle` | `main/battle_tick.c:308` | parcial | Mueve a los personajes del mapa que no luchan durante el combate (huyen). | los personajes que no luchan no huyen al empezar; se van andando tras ganar (battle.md: Hueco) |
| `handleBattleEndBox` | `main/battle_ui.c:297` | parcial | Cuadro final del combate: objetos, lesión, técnicas, stats y dinero. | `apps/game/src/battle_screen.cpp:275` · faltan la orden aprendida y el bonus de MP (docs/modulos/game/battle.md:109) |
| `isScreenConcave` | `main/btl.c:128` | falta | Dice si el mapa es de los cóncavos, donde los personajes que no luchan no se unen al combate. | va con la huida de los que no luchan |
| `initializeEntityText` | `main/entity_text.c:71` | falta | Prepara las cifras flotantes (daño, curación) de cada luchador. | el motor no enseña cifras sobre los luchadores |
| `setCombatTextPosition` | `main/entity_text.c:147` | falta | Coloca sobre el luchador las cifras de daño y curación que suben. | el motor no enseña cifras sobre los luchadores |

No aplica: limpieza de objetos y memoria del original: el motor libera lo suyo (11); dibujo del original: el motor dibuja con su propio renderizador; el hueco visual, si lo hay, va en la función de lógica (8); preparación de tablas, objetos o recursos del original: el motor monta los suyos (2); índice interno (2); estado interno del rastro del compañero (1); cálculo auxiliar (1); carga asíncrona del original (1).

## Efectos de batalla

La máquina de efectos de las técnicas (scripts EFE): proyectiles, golpes, cámara, partículas, destellos.

Ficheros del decomp: `btl/battle_effect.c`, `main/buff_model.c`, `main/efe.c`, `main/efe_table.c`, `main/effect_common.c`, `main/particle.c`.

Portado 134, parcial 4, falta 74, no aplica 73.

| Función | Decomp | Estado | Qué hace | Motor / nota |
|---|---|---|---|---|
| `BTL_initializeParticleEmitters` | `btl/battle_effect.c:1449` | falta | Prepara los emisores de partículas de las técnicas. | battle.md: las partículas no se dibujan |
| `BTL_tickParticleEmitters` | `btl/battle_effect.c:1898` | falta | Mueve las partículas que sueltan las técnicas. | battle.md: Hueco |
| `BTL_markEFEFinished` | `btl/battle_effect.c:2221` | falta | Marca el efecto como terminado para que la batalla siga. | mark_finished se salta (battle.md: Hueco) |
| `BTL_disableMapLayer` | `btl/battle_effect.c:2234` | falta | Oculta una capa del mapa durante la técnica (fondos que cambian). | solo lo cita la extracción del disco |
| `BTL_renderScreenFade` | `btl/battle_effect.c:2239` | falta | Funde la pantalla a un color durante una técnica. | dibujo del original: el motor dibuja con su propio renderizador; el hueco visual, si lo hay, va en la función de lógica |
| `BTL_renderScreenOverlay` | `btl/battle_effect.c:2434` | falta | Pinta un velo de color sobre la pantalla durante una técnica. | dibujo del original: el motor dibuja con su propio renderizador; el hueco visual, si lo hay, va en la función de lógica |
| `BTL_renderRingTube` | `btl/battle_effect.c:2461` | falta | Dibuja un tubo de anillos (rayos, torbellinos) de una técnica. | dibujo del original: el motor dibuja con su propio renderizador; el hueco visual, si lo hay, va en la función de lógica |
| `BTL_tickRibbonPoints` | `btl/battle_effect.c:2562` | falta | Mueve los puntos de una cinta de luz de una técnica. | battle.md: las cintas no se dibujan |
| `BTL_initializeRibbonPoints` | `btl/battle_effect.c:2586` | falta | Prepara una cinta de luz de una técnica. | battle.md: Hueco |
| `BTL_renderRadialWaves` | `btl/battle_effect.c:2600` | falta | Dibuja ondas que se abren en círculo de una técnica. | dibujo del original: el motor dibuja con su propio renderizador; el hueco visual, si lo hay, va en la función de lógica |
| `BTL_addClutLoadPrim` | `btl/battle_effect.c:2700` | falta (citada en docs) | Cambia la paleta de un efecto mientras corre (destellos de color). | battle.md: las cargas de CLUT se saltan |
| `BTL_drawTMDScreenSpace` | `btl/battle_effect.c:2722` | falta | Dibuja un modelo del efecto pegado a la pantalla. | dibujo del original: el motor dibuja con su propio renderizador; el hueco visual, si lo hay, va en la función de lógica |
| `BTL_loadClutColors` | `btl/battle_effect.c:2744` | falta (citada en docs) | Copia colores a la paleta de un efecto. | battle.md: Hueco |
| `BTL_getCameraRotation` | `btl/battle_effect.c:2803` | falta | Lee el giro de la cámara para orientar un efecto. | battle.md: las órdenes de cámara se saltan |
| `BTL_convertToViewSpace` | `btl/battle_effect.c:2843` | falta | Pasa una posición del efecto al espacio de la cámara. | battle.md: Hueco |
| `BTL_render3DTexturedQuad` | `btl/battle_effect.c:2902` | falta | Dibuja un cuadro con textura en el mundo (llamas, rayos). | solo lo cita la extracción del disco |
| `BTL_renderWireframeBox` | `btl/battle_effect.c:2978` | falta | Dibuja una caja de líneas de un efecto. | dibujo del original: el motor dibuja con su propio renderizador; el hueco visual, si lo hay, va en la función de lógica |
| `BTL_renderWireframeGrid` | `btl/battle_effect.c:3063` | falta | Dibuja una rejilla de líneas de un efecto. | dibujo del original: el motor dibuja con su propio renderizador; el hueco visual, si lo hay, va en la función de lógica |
| `BTL_render2DTexturedQuad` | `btl/battle_effect.c:3130` | falta | Dibuja un cuadro con textura en pantalla de un efecto. | solo lo cita la extracción del disco |
| `BTL_restoreCameraView` | `btl/battle_effect.c:3183` | falta | Devuelve la cámara del combate a su sitio tras una técnica. | battle.md: Hueco |
| `BTL_setupFixedCamera` | `btl/battle_effect.c:3199` | falta | Fija la cámara en un punto para lucir una técnica. | battle.md: Hueco |
| `BTL_renderEFELine` | `btl/battle_effect.c:3269` | falta | Dibuja una línea de un efecto. | dibujo del original: el motor dibuja con su propio renderizador; el hueco visual, si lo hay, va en la función de lógica |
| `BTL_renderParallaxSprites` | `btl/battle_effect.c:3869` | falta | Dibuja capas de sprites con paralaje de un efecto. | dibujo del original: el motor dibuja con su propio renderizador; el hueco visual, si lo hay, va en la función de lógica |
| `BTL_renderScrollingBackground` | `btl/battle_effect.c:3917` | falta | Dibuja un fondo que se desplaza durante una técnica. | dibujo del original: el motor dibuja con su propio renderizador; el hueco visual, si lo hay, va en la función de lógica |
| `BTL_addSourceEntityParticleFX` | `btl/battle_effect.c:4034` | falta (citada en docs) | Lanza partículas desde el atacante. | battle.md: Hueco |
| `BTL_renderParticleFlashSprite` | `btl/battle_effect.c:4142` | falta | Dibuja el destello de una partícula. | dibujo del original: el motor dibuja con su propio renderizador; el hueco visual, si lo hay, va en la función de lógica |
| `BTL_projectPositionToScreen` | `btl/battle_effect.c:4172` | falta | Proyecta una posición del efecto a la pantalla para sus sprites. | solo lo cita la extracción del disco |
| `BTL_renderScreenSprite` | `btl/battle_effect.c:4192` | falta | Dibuja un sprite en pantalla de un efecto. | battle.md: Hueco |
| `BTL_addCloudEffect` | `btl/battle_effect.c:4239` | falta | Añade una nube de humo o polvo de una técnica. | battle.md: Hueco |
| `BTL_addParticleEmitter` | `btl/battle_effect.c:4288` | falta | Añade un emisor de partículas. | battle.md: Hueco |
| `BTL_calculateAttackHitPosition` | `btl/battle_effect.c:5145` | parcial | Calcula en qué punto del cuerpo del objetivo cae un golpe. | el motor registra el golpe; sin comprobar que use este punto |
| `BTL_interpolateClamped` | `btl/battle_effect.c:5207` | falta | Interpola un valor entre dos límites (color de cintas, radio de ondas). | solo lo usan dibujos que faltan |
| `BTL_resetPoisonBubbles` | `btl/battle_effect.c:5359` | falta | Reinicia las burbujas del veneno sobre un luchador. | — |
| `BTL_addPoisonBubble` | `btl/battle_effect.c:5364` | falta | Suelta una burbuja de veneno sobre el luchador envenenado. | — |
| `BTL_tickPoisonBubble` | `btl/battle_effect.c:5389` | falta | Hace subir una burbuja de veneno. | — |
| `BTL_tickPoisonEffect` | `btl/battle_effect.c:5427` | falta | Mantiene el efecto visual del veneno sobre el luchador. | — |
| `BTL_initializePoisonBubble` | `btl/battle_effect.c:5443` | falta | Prepara una burbuja de veneno. | — |
| `BTL_addPoisonEffect` | `btl/battle_effect.c:5454` | falta | Pone el efecto visual del veneno. | — |
| `BTL_tickConfusionEffect` | `btl/battle_effect.c:5501` | falta | Anima el efecto visual de la confusión. | — |
| `BTL_initializeConfusionEffect` | `btl/battle_effect.c:5534` | falta | Prepara el efecto visual de la confusión. | — |
| `BTL_addConfusionEffect` | `btl/battle_effect.c:5541` | falta | Pone el efecto visual de la confusión. | — |
| `BTL_resetStunSubEffects` | `btl/battle_effect.c:5597` | falta | Reinicia las estrellas del aturdimiento. | — |
| `BTL_tickStunEffect` | `btl/battle_effect.c:5606` | falta | Anima el efecto visual del aturdimiento. | — |
| `BTL_addStunSubEffect` | `btl/battle_effect.c:5717` | falta | Añade una estrella del aturdimiento. | — |
| `BTL_tickStunSubEffect` | `btl/battle_effect.c:5740` | falta | Hace girar una estrella del aturdimiento. | — |
| `BTL_setTMDObjectColor` | `btl/battle_effect.c:5829` | falta | Tiñe un objeto de un modelo (auras, brillos de estado). | — |
| `BTL_tickFinisherAura` | `btl/battle_effect.c:5855` | falta | Anima el aura del remate cargado alrededor del compañero. | — |
| `BTL_addFinisherAura` | `btl/battle_effect.c:6012` | falta | Pone el aura del remate cargado. | — |
| `BTL_tickAuraProjectile` | `btl/battle_effect.c:6051` | falta | Mueve un proyectil de aura. | — |
| `BTL_initializeAuraProjectiles` | `btl/battle_effect.c:6123` | falta | Prepara los proyectiles de aura. | — |
| `BTL_addAuraProjectile` | `btl/battle_effect.c:6137` | falta | Lanza un proyectil de aura. | — |
| `BTL_initializeItemParticleVelocities` | `btl/battle_effect.c:6193` | falta | Da velocidades a las partículas de un objeto usado en combate. | — |
| `BTL_tickItemParticles` | `btl/battle_effect.c:6226` | falta | Mueve las partículas de un objeto usado en combate. | — |
| `BTL_initializeBattleItemParticles` | `btl/battle_effect.c:6269` | falta | Prepara las partículas de los objetos en combate. | — |
| `BTL_addItemParticles` | `btl/battle_effect.c:6275` | falta | Lanza las partículas al usar un objeto en combate. | — |
| `BTL_initializeBuffTrails` | `btl/battle_effect.c:6340` | falta | Prepara las estelas de una mejora de stats. | — |
| `BTL_tickBuffDisk` | `btl/battle_effect.c:6394` | falta | Anima el disco de una mejora de stats. | — |
| `BTL_addBuffTrails` | `btl/battle_effect.c:6433` | falta | Pone las estelas de una mejora de stats. | — |
| `BTL_tickBuffTrails` | `btl/battle_effect.c:6457` | falta | Mueve las estelas de una mejora de stats. | — |
| `BTL_initializeUnk3` | `btl/battle_effect.c:6517` | falta | Prepara el disco de mejora de stats. | — |
| `BTL_addBuffDiskEffect` | `btl/battle_effect.c:6523` | falta | Pone el disco de una mejora de stats. | — |
| `BTL_tickBuffRings` | `btl/battle_effect.c:6581` | falta | Anima los anillos de una mejora de stats. | — |
| `BTL_initializeUnk2` | `btl/battle_effect.c:6676` | falta | Prepara los anillos de mejora de stats. | — |
| `tickBuffModel` | `main/buff_model.c:38` | falta | Anima el modelo que acompaña a una mejora de stats. | — |
| `morphBuffModel` | `main/buff_model.c:48` | falta (citada en docs) | Alterna la paleta del modelo de mejora. | — |
| `initializeParticleFX` | `main/efe.c:350` | falta | Prepara las partículas de los efectos. | — |
| `tickParticleFX` | `main/efe.c:463` | parcial | Mueve las partículas de los efectos. | `game/battle/src/particle_rolls.cpp` · solo sus tiradas, con --original-rolls (battle.md) |
| `addEntityParticleFX` | `main/efe.c:611` | parcial | Lanza partículas pegadas a un luchador. | `game/battle/src/particle_rolls.cpp` · solo sus tiradas, con --original-rolls (battle.md) |
| `initializeEntityParticleFX` | `main/efe.c:633` | falta | Prepara las partículas pegadas a luchadores. | — |
| `tickEntityParticleFX` | `main/efe.c:638` | parcial | Mueve las partículas pegadas a un luchador. | `game/battle/src/particle_rolls.cpp` · solo sus tiradas, con --original-rolls (battle.md) |
| `tickCloudFX` | `main/efe.c:733` | falta | Mueve las nubes de humo y polvo de los efectos. | — |
| `EFECreateFlash` | `main/efe.c:808` | falta (citada en docs) | Crea un destello de luz de una técnica. | battle.md: los destellos se saltan |
| `tickEFEFlash` | `main/efe.c:886` | falta | Anima un destello de luz. | — |
| `setEFEFlashOffset` | `main/efe.c:965` | falta | Mueve un destello de luz. | — |
| `translateConditionFXToEntity` | `main/effect_common.c:307` | falta | Pega un efecto de estado a la posición de un personaje. | — |
| `tickHealingParticles` | `main/particle.c:82` | falta | Mueve las partículas de curación al usar un objeto. | — |
| `initializeHealingParticles` | `main/particle.c:201` | falta | Prepara las partículas de curación. | — |
| `addHealingParticleEffect` | `main/particle.c:209` | falta | Lanza las partículas de curación sobre el compañero. | — |

No aplica: dibujo del original: el motor dibuja con su propio renderizador; el hueco visual, si lo hay, va en la función de lógica (24); limpieza de objetos y memoria del original: el motor libera lo suyo (21); gráficos, memoria o ficheros del hardware original: los sustituye el motor (11); preparación de tablas, objetos o recursos del original: el motor monta los suyos (5); lectura del fichero de efecto; la hace la extracción (2); tabla de saltos del original (2); lectura del disco (2); dato auxiliar de la escena de renacer (2); tabla de ficheros del disco (1); carga asíncrona del original (1); depuración del original (1); preparación del original (1).

## Campo: movimiento, colisión y cámara

El jugador en el mapa, la rejilla de colisión, los warps, el seguimiento del compañero y la cámara.

Ficheros del decomp: `main/aabb.c`, `main/map.c`, `main/map_collision.c`, `main/tamer.c`, `main/vecmath.c`.

Portado 67, parcial 2, falta 11, no aplica 16.

| Función | Decomp | Estado | Qué hace | Motor / nota |
|---|---|---|---|---|
| `isInDaytimeTransition` | `main/map.c:4607` | falta | Dice si el mapa está cambiando entre día y noche. | sin búsqueda de la luz del día en el motor; comprobar |
| `reinitializeAfterTournament` | `main/map.c:5445` | falta | Vuelve al mapa tras un torneo. | no hay torneos |
| `createMeramonShake` | `main/map.c:5472` | falta | Empieza el temblor de pantalla de la escena del fuego de la montaña. | — |
| `tickMeramonShake` | `main/map.c:5482` | falta | Hace temblar la pantalla en esa escena. | — |
| `getFileCityTopMap` | `main/map.c:5592` | falta | Elige qué versión del mapa de la ciudad cargar según cuánto ha crecido. | comprobar si las ediciones de mapa del guion lo cubren |
| `checkMedalConditions` | `main/tamer.c:721` | falta | Da la medalla de aprender todas las técnicas y la anuncia. | — |
| `checkPendingAwards` | `main/tamer.c:793` | falta | Lanza el anuncio pendiente de un premio (medalla o nivel del jugador). | — |
| `tamerTickOpening` | `main/tamer.c:1668` | falta | Escena de apertura del jugador. | `game/story/include/dwe/game/story/partner_routines.hpp:21` · la escena del compañero 5 acaba al momento |
| `tamerTickEnding` | `main/tamer.c:1682` | falta | Escena final del jugador antes del vídeo. | `game/story/include/dwe/game/story/partner_routines.hpp:22` · la escena 6 acaba al momento; solo se ve el vídeo final |
| `tamerTickSicknessLostLife` | `main/tamer.c:1699` | parcial | Escena de perder una vida por enfermedad. | `apps/game/src/death_scene.cpp:70` · la restauración tras la vida perdida por enfermedad es un hueco (docs/modulos/game/battle.md:107) |
| `tamerTickMachinedramonSpawn` | `main/tamer.c:1718` | falta | El jugador asiste a la aparición del jefe final. | la escena acaba al momento (story_scene_glue.cpp:58) |
| `tamerTickBattleLostLife` | `main/tamer.c:1734` | parcial | Escena de perder una vida en combate. | `apps/game/src/death_scene.cpp:70` · la vida y el MP tras perder salen de una regla hasta los guiones (docs/modulos/game/battle.md:107) |
| `tamerTickAwardSomething` | `main/tamer.c:1751` | falta | Escena de premio: para el tiempo, enseña la medalla o el nivel ganado y espera. | — |

No aplica: gráficos, memoria o ficheros del hardware original: los sustituye el motor (6); dibujo del original: el motor dibuja con su propio renderizador; el hueco visual, si lo hay, va en la función de lógica (2); prueba de cajas del original (1); cálculo de pantalla del original (1); lectura del disco; la hace la extracción (1); limpieza de objetos y memoria del original: el motor libera lo suyo (1); carga de trozos del fondo del original (1); limpieza del original (1); lectura del disco; la colisión la extrae dwx (1); cálculo de matrices (1).

## NPC y objetos del mapa

Los personajes del mapa y su IA de paseo, cofres, objetos del suelo y decorados con lógica.

Ficheros del decomp: `main/anim.c`, `main/bubble.c`, `main/butterfly.c`, `main/drop_shadow.c`, `main/map_object.c`, `main/model.c`, `main/overworld.c`, `main/world_object.c`.

Portado 40, parcial 1, falta 21, no aplica 40.

| Función | Decomp | Estado | Qué hace | Motor / nota |
|---|---|---|---|---|
| `animateEntityTexture` | `main/anim.c:76` | falta (citada en docs) | Anima por código la textura de algunos personajes (ojos, brillos). | docs/formats/modelos.md: no se extrae |
| `initializeButterfly` | `main/butterfly.c:60` | falta | Prepara la mariposa que distrae al compañero infeliz. | — |
| `setButterfly` | `main/butterfly.c:71` | falta | Hace salir la mariposa que persigue el compañero infeliz (condición de distraído). | la condición sí está (partner_tick.cpp:313); la mariposa no se ve |
| `tickButterfly` | `main/butterfly.c:94` | falta | Hace volar la mariposa. | — |
| `unsetButterfly` | `main/butterfly.c:134` | falta | Quita la mariposa. | — |
| `loadDirtCartModel` | `main/map_object.c:423` | falta | Carga la vagoneta de tierra de la mina. | solo lo cita la extracción del disco |
| `loadWarpCrystals` | `main/map_object.c:500` | falta | Carga los cristales de teletransporte de un mapa. | solo lo cita la extracción del disco |
| `spawnBoulder` | `main/map_object.c:616` | falta | Pone la roca que bloquea un paso. | solo lo cita la extracción del disco |
| `initializeMedalModel` | `main/map_object.c:656` | falta | Carga el modelo de una medalla. | preparación de tablas, objetos o recursos del original: el motor monta los suyos |
| `spawnToyTownBoxes` | `main/map_object.c:662` | falta | Pone las cajas de la ciudad de juguete. | solo lo cita la extracción del disco |
| `spawnGearbox` | `main/map_object.c:749` | falta | Pone la caja de engranajes de la fábrica. | solo lo cita la extracción del disco |
| `spawnAngemonPedestal` | `main/map_object.c:775` | falta | Pone el pedestal que se mueve en el santuario. | solo lo cita la extracción del disco |
| `initializeTrainingPoop` | `main/map_object.c:809` | falta | Pone la caca gigante del gimnasio. | solo lo cita la extracción del disco |
| `tickOpenChestTray` | `main/map_object.c:1083` | falta | Anima la tapa del cofre al abrirse. | — |
| `tickCloseChestTray` | `main/map_object.c:1096` | falta | Anima la tapa del cofre al cerrarse. | — |
| `decreaseDirtPileSize` | `main/map_object.c:1109` | falta | Encoge un montón de tierra al cavar. | — |
| `moveAngemonPedestal` | `main/map_object.c:1167` | falta | Mueve el pedestal del santuario. | — |
| `activateMedalTexture` | `main/map_object.c:1193` | falta (citada en docs) | Pone la textura de la medalla ganada. | — |
| `loadDirtPileModel` | `main/map_object.c:1205` | falta | Carga el montón de tierra que se cava. | solo lo cita la extracción del disco |
| `createNinjamonEffect` | `main/overworld.c:777` | parcial | Nube de humo de un personaje que desaparece. | `apps/session/include/dwe/session/story_scenes.hpp:112` · la rutina 38 se cierra sin el humo |
| `spawnSpriteAtEntity` | `main/overworld.c:992` | falta | Pone un sprite (humo, signo) sobre un personaje. | — |
| `tickRemoveMist` | `main/overworld.c:1012` | falta | Disipa la niebla del mapa. | — |

No aplica: dibujo del original: el motor dibuja con su propio renderizador; el hueco visual, si lo hay, va en la función de lógica (19); gráficos, memoria o ficheros del hardware original: los sustituye el motor (5); limpieza de objetos y memoria del original: el motor libera lo suyo (3); consulta interna (2); animación del original; el motor anima sus clips (1); recorte de dibujo (1); preparación de tablas, objetos o recursos del original: el motor monta los suyos (1); estado de dibujo (1); modelo vacío del original (1); preparación de modelos (1); lectura del disco; los modelos los extrae dwx (1); subida de textura del original (1); limpieza del original (1); tabla de objetos del original (1); bucle de objetos del original; el motor tiene su bucle (1).

## Compañero: IA y cuidado

Hambre, sueño, baño, enfermedad, felicidad y disciplina, caca, muerte natural, paseo y órdenes.

Ficheros del decomp: `main/partner.c`, `main/partner_impl.c`.

Portado 49, parcial 0, falta 0, no aplica 2.

No aplica: consulta interna (1); preparación de tablas, objetos o recursos del original: el motor monta los suyos (1).

## Guion e intérprete

La máquina de scripts de eventos: órdenes, valores, cajas de texto y elecciones.

Ficheros del decomp: `main/script_common.c`, `main/script_interp.c`, `main/script_textbox.c`, `main/script_value.c`.

Portado 73, parcial 3, falta 12, no aplica 57.

| Función | Decomp | Estado | Qué hace | Motor / nota |
|---|---|---|---|---|
| `readSelectedItemMerit` | `main/script_common.c:877` | parcial | Lee los méritos del objeto elegido. | no hay pantalla del intercambio por méritos |
| `tickSingleCardShop` | `main/script_common.c:1070` | falta | Tienda de una sola carta. | las pantallas de cartas no existen |
| `tickShopBitsBox` | `main/script_common.c:1154` | falta | Cuadro con el dinero del jugador que abre el guion. | la rutina 15 se cierra sin enseñarlo |
| `createShopBitsBox` | `main/script_common.c:1757` | parcial | Enseña el dinero del jugador durante un diálogo. | `apps/session/include/dwe/session/story_scenes.hpp:111` · la rutina 15 se cierra sin enseñar el cuadro |
| `openDiscardItem` | `main/script_common.c:1815` | parcial | Pide tirar un objeto cuando la bolsa está llena al recibir otro. | `apps/session/include/dwe/session/story_scenes.hpp:97` · la rutina 0 se registra pero la app no tiene pantalla: se cierra al momento |
| `createSingleCardShopMenu` | `main/script_common.c:2047` | falta | Abre la tienda de una sola carta. | no hay pantallas de cartas |
| `playShopSoundOnlyInSavannah` | `main/script_common.c:2371` | falta | Toca el sonido de la tienda, solo en el mapa de la sabana. | comprobar |
| `calculateCardMenuStrings` | `main/script_common.c:2666` | falta | Monta la lista del menú de cartas. | no hay pantallas de cartas |
| `calculateMusicMenuStrings` | `main/script_common.c:2727` | falta | Monta la lista de canciones de la gramola. | no hay pantalla de gramola |
| `calculateBirdramonMenuStrings` | `main/script_common.c:2759` | falta | Monta la lista de destinos del pájaro transportista. | comprobar la pantalla de destinos |
| `unlockMedal` | `main/script_common.c:3026` | falta (citada en docs) | Da una medalla al jugador. | no hay medallas en el motor |
| `hasMedal` | `main/script_common.c:3033` | falta | Dice si el jugador tiene una medalla. | no hay medallas en el motor |
| `createMonochromonMoodBubble` | `main/script_common.c:3038` | falta (citada en docs) | Burbuja de humor del rival de la tienda de cambio. | scripts.md: más tarde |
| `triggerSeadramonCutscene` | `main/script_common.c:3057` | falta (citada en docs) | Lanza la escena de la serpiente marina al pescarla. | — |
| `tickBackgroundDialogue` | `main/script_textbox.c:861` | falta | Diálogo que corre sin parar el juego (comentarios de fondo). | comprobar |

No aplica: dibujo del original: el motor dibuja con su propio renderizador; el hueco visual, si lo hay, va en la función de lógica (17); lectura de operandos; los scripts se pasan a Lua (12); texto de interfaz (5); cursor de interfaz; lo hace la pantalla (4); gráficos, memoria o ficheros del hardware original: los sustituye el motor (4); cuadros de interfaz del original (3); entrada del original (2); cursor de interfaz (2); cadenas del original (2); entrada del original; el motor tiene la suya (1); texto de interfaz; lo monta la pantalla (1); consulta interna (1); limpieza de objetos y memoria del original: el motor libera lo suyo (1); tabla de modelos del original (1); codificación de texto del original (1).

## Menús e interfaz

Título, menú de juego, ficha del compañero, bolsa, cajas de interfaz, fundidos y texto.

Ficheros del decomp: `main/fade.c`, `main/font.c`, `main/game_menu.c`, `main/inventory.c`, `main/main_menu.c`, `main/new_game.c`, `main/sjis.c`, `main/ui.c`.

Portado 65, parcial 1, falta 2, no aplica 56.

| Función | Decomp | Estado | Qué hace | Motor / nota |
|---|---|---|---|---|
| `fadeToWhite` | `main/fade.c:170` | falta | Funde la pantalla a blanco. | rutina 45 del guion, sin registrar: sigue sin fundir |
| `fadeFromWhite` | `main/fade.c:193` | falta | Vuelve del blanco. | rutina 46 del guion, sin registrar |
| `renderFeedingItem` | `main/game_menu.c:2245` | parcial | Dibuja el objeto en la mano al dar de comer. | `apps/game/src/feed_scene.cpp:81` · el objeto que se da de comer no se dibuja |

No aplica: dibujo del original: el motor dibuja con su propio renderizador; el hueco visual, si lo hay, va en la función de lógica (31); gráficos, memoria o ficheros del hardware original: los sustituye el motor (4); limpieza de objetos y memoria del original: el motor libera lo suyo (4); cuadros de interfaz del original (3); texto en memoria de vídeo (2); utilidad (2); texto de interfaz (2); arranque, cola de lectura del disco, codificación de texto o carga de sonido del original: lo hace el motor (2); preparación del original (1); texto del original; el motor usa sus estilos (1); repetición del mando; la hace el motor (1); entrada del original (1); preparación de tablas, objetos o recursos del original: el motor monta los suyos (1); cuadros de interfaz del original; el motor usa su interfaz (1).

## Objetos y tiendas

Uso de objetos, comida y chips, tiendas, cartas, guardarropía y menús de guion del overlay de tiendas.

Ficheros del decomp: `main/item.c`, `shop/script_menu.c`.

Portado 63, parcial 8, falta 21, no aplica 6.

| Función | Decomp | Estado | Qué hace | Motor / nota |
|---|---|---|---|---|
| `handleChips` | `main/item.c:2248` | parcial | Usa un chip: sube un stat y lanza su sección de guion. | `content/packs/game/rules/original/inventory.yaml:59` · la sección de guion de un chip no corre (docs/modulos/apps/session.md:276) |
| `rollCardPack` | `shop/script_menu.c:679` | parcial | Saca las cartas de un sobre. | `game/inventory/include/dwe/game/inventory/cards.hpp` · la tirada existe; la rutina 14 se cierra sin pantalla |
| `openBuyCardMenu` | `shop/script_menu.c:711` | parcial | Abre la compra de cartas. | `apps/session/include/dwe/session/story_scenes.hpp:106` · la rutina 14 se cierra sin pantalla |
| `openSellCardMenu` | `shop/script_menu.c:753` | parcial | Abre la venta de cartas. | `apps/session/include/dwe/session/story_scenes.hpp:107` · la rutina 14 se cierra sin pantalla |
| `openMeritShop` | `shop/script_menu.c:788` | parcial | Abre el intercambio de objetos por puntos de mérito. | `apps/session/include/dwe/session/story_scenes.hpp:98` · la rutina 1 se cierra sin pantalla |
| `tickItemKeeper` | `shop/script_menu.c:892` | parcial | Guardarropía de objetos. | `apps/session/include/dwe/session/story_scenes.hpp:104` · la rutina 11 se cierra sin pantalla |
| `openJukebox` | `shop/script_menu.c:947` | parcial | Abre la gramola. | `apps/session/include/dwe/session/story_scenes.hpp:103` · la rutina 9 se cierra sin pantalla |
| `openMojyamonShop` | `shop/script_menu.c:1051` | parcial | Abre el trueque de objetos. | `apps/session/include/dwe/session/story_scenes.hpp:99` · la rutina 2 se cierra sin pantalla |
| `hasAnyCardToBuy` | `shop/script_menu.c:1296` | falta | Dice si queda alguna carta por comprar. | no hay pantallas de cartas |
| `createCardMenu` | `shop/script_menu.c:1452` | falta | Abre el menú de compra o venta de cartas. | la rutina 14 se cierra sin pantalla |
| `shopFillMeritItemList` | `shop/script_menu.c:1479` | falta | Rellena la lista del intercambio por méritos. | la rutina 1 se cierra sin pantalla |
| `itemKeeperFillItemList` | `shop/script_menu.c:1536` | falta | Rellena la lista del guardarropía de objetos. | la rutina 11 se cierra sin pantalla |
| `createItemKeepWindow` | `shop/script_menu.c:1605` | falta | Abre el guardarropía de objetos. | — |
| `jukeboxFillTitleList` | `shop/script_menu.c:1636` | falta (citada en docs) | Rellena la lista de la gramola. | la rutina 9 se cierra sin pantalla |
| `createJukeboxMenu` | `shop/script_menu.c:1672` | falta | Abre la gramola. | — |
| `mojyaTradeFillItemList` | `shop/script_menu.c:1758` | falta (citada en docs) | Rellena la lista del trueque de objetos. | la rutina 2 se cierra sin pantalla |
| `createMojyaTradeMenu` | `shop/script_menu.c:1786` | falta | Abre el trueque de objetos. | — |
| `setMojyaItemTradedTrigger` | `shop/script_menu.c:1801` | falta (citada en docs) | Marca el objeto ya cambiado en el trueque. | — |
| `tickCardMenu` | `shop/script_menu.c:1816` | falta | Compra o venta de cartas: cursor, precio y confirmación. | — |
| `tickItemKeeperWindow` | `shop/script_menu.c:1941` | falta | Guardarropía: pasa objetos entre la bolsa y el almacén. | — |
| `updateKeeperTextbox` | `shop/script_menu.c:2035` | falta | Texto del guardarropía. | — |
| `keeperMoveOne` | `shop/script_menu.c:2061` | falta | Guardarropía: pasa un objeto. | — |
| `keeperMoveTen` | `shop/script_menu.c:2114` | falta | Guardarropía: pasa diez objetos. | — |
| `keeperMoveStack` | `shop/script_menu.c:2180` | falta | Guardarropía: pasa la pila entera. | — |
| `keeperScrollLeftToItem` | `shop/script_menu.c:2238` | falta | Guardarropía: salta al objeto anterior. | — |
| `keeperScrollRightToItem` | `shop/script_menu.c:2296` | falta | Guardarropía: salta al objeto siguiente. | — |
| `tickJukeboxMenu` | `shop/script_menu.c:2396` | falta (citada en docs) | Gramola: elige y toca una canción. | — |
| `tickMojyaTradeMenu` | `shop/script_menu.c:2547` | falta | Trueque: elige el objeto a cambiar. | — |
| `updateMojyaTradeStrings` | `shop/script_menu.c:2647` | falta | Textos del trueque. | — |

No aplica: dibujo del original: el motor dibuja con su propio renderizador; el hueco visual, si lo hay, va en la función de lógica (6).

## Evolución

Elección del destino y la escena de evolución.

Ficheros del decomp: `evl/evl.c`, `main/evl.c`, `main/evolution.c`.

Portado 14, parcial 1, falta 9, no aplica 14.

| Función | Decomp | Estado | Qué hace | Motor / nota |
|---|---|---|---|---|
| `EVL_initShardSets` | `evl/evl.c:301` | falta | Prepara los grupos de trozos de la evolución. | la escena no dibuja trozos |
| `EVL_resetParticles` | `evl/evl.c:327` | falta | Vacía las partículas de luz de la evolución. | la escena no dibuja partículas |
| `EVL_resetSparks` | `evl/evl.c:336` | falta | Vacía las chispas de la evolución. | la escena no dibuja chispas |
| `EVL_spawnParticle` | `evl/evl.c:778` | falta | Crea una partícula de luz de la evolución. | la escena no dibuja partículas |
| `EVL_releaseAllParticles` | `evl/evl.c:854` | falta | Quita todas las partículas de la evolución. | la escena no dibuja partículas |
| `EVL_tickShardSet` | `evl/evl.c:869` | falta | Mueve un grupo de trozos que se desprenden del compañero al evolucionar. | la escena no dibuja trozos |
| `EVL_buildShardSet` | `evl/evl.c:1001` | falta | Parte un hueso del compañero en trozos que salen despedidos. | la escena no dibuja trozos |
| `EVL_tickParticle` | `evl/evl.c:1163` | falta | Mueve una partícula de luz de la evolución. | la escena no dibuja partículas |
| `EVL_tickSpark` | `evl/evl.c:1264` | falta | Consume la vida de una chispa de la evolución y la quita. | la escena no dibuja chispas |
| `EVL_initEvoSequence` | `evl/evl.c:1277` | parcial | Arranca la escena de evolución y sortea el orden en que se rompen los huesos. | `apps/game/src/evolution_scene.cpp:88` · falta el orden aleatorio de huesos y los efectos que prepara |

No aplica: dibujo del original: el motor dibuja con su propio renderizador; el hueco visual, si lo hay, va en la función de lógica (7); gráficos, memoria o ficheros del hardware original: los sustituye el motor (6); gestión de memoria del original (1).

## Entrenamiento

El gimnasio: tragaperras, cursos y recompensas.

Ficheros del decomp: `trn/trn_brain.c`, `trn/trn_hud.c`, `trn/trn_off.c`, `trn/trn_reward.c`, `trn/trn_slots.c`, `trn/trn_speed.c`, `trn2/trn2_def_map108.c`, `trn2/trn2_hp_map107.c`, `trn2/trn2_hud.c`, `trn2/trn2_reward.c`, `trn2/trn2_slots.c`.

Portado 34, parcial 1, falta 2, no aplica 13.

| Función | Decomp | Estado | Qué hace | Motor / nota |
|---|---|---|---|---|
| `TRN_func_800888A0` | `trn/trn_reward.c:116` | falta | Función sin nombre del cálculo de recompensas del gimnasio. | sin identificar |
| `TRN_advanceTrainingTime` | `trn/trn_reward.c:721` | parcial | Avanza la hora y el día tras una sesión de entrenamiento. | `content/packs/game/rules/original/care.yaml:225` · el original usa un año de 31 días al entrenar; el motor, el del reloj (30). Decidido así (partner.md:187) |
| `TRN2_func_8008AA84` | `trn2/trn2_reward.c:158` | falta | Función sin nombre del cálculo de recompensas del segundo gimnasio. | sin identificar |

No aplica: copias de otra función, que cuenta por las dos (10); dibujo del original: el motor dibuja con su propio renderizador; el hueco visual, si lo hay, va en la función de lógica (2); posición en pantalla para dibujar (1).

## Pesca

El minijuego de pesca.

Ficheros del decomp: `fish/fish.c`, `fish/fish_model.c`, `main/fish.c`.

Portado 64, parcial 9, falta 8, no aplica 39.

| Función | Decomp | Estado | Qué hace | Motor / nota |
|---|---|---|---|---|
| `FISH_panCameraTo` | `fish/fish.c:969` | falta | Desplaza la cámara hacia un punto (el corcho) con un límite por fotograma. | La app ignora el foco de cámara de la pesca; la cámara sigue siempre al jugador. |
| `FISH_panCameraToTamer` | `fish/fish.c:1013` | parcial | Devuelve la cámara al jugador antes del menú de cebo o al recoger. | `apps/game/src/fishing_screen.cpp:237` · La cámara ya está en el jugador; no hay paneo suave de vuelta. |
| `FISH_panCameraWithDPad` | `fish/fish.c:1018` | falta | Deja al jugador mover la vista con la cruceta mientras pesca. | La vista libre no existe (fishing_screen.cpp:231). |
| `FISH_scrollViewToTarget` | `fish/fish.c:1406` | falta | Lleva la cámara al punto del aviso (el pez o el corcho) mientras se lee. | Los avisos no mueven la cámara. |
| `FISH_tickTextBox` | `fish/fish.c:1452` | parcial | Cierra el aviso con Confirmar tras 10 fotogramas o con Cancelar. | `content/packs/game/ui/fishing_notice/logic.lua:47` · Solo cierra con Confirmar (Cancelar se ignora), sin espera de 10 fotogramas ni paneo. |
| `FISH_tickMarker` | `fish/fish.c:1716` | parcial | Mantiene unos fotogramas la marca sobre un pez que tira la picada. | `game/fishing/src/pond.cpp:401` · El motor emite la marca, pero la app no la dibuja ni la mantiene. |
| `FISH_spawnMarker` | `fish/fish.c:1780` | parcial | Pone una marca sobre el pez que tira la picada (distinta si puede picar). | `game/fishing/src/pond.cpp:401` · Se emite el evento, pero la app no dibuja la marca. |
| `FISH_tickMarkers` | `fish/fish.c:1795` | parcial | Avanza la vida de todas las marcas. | `game/fishing/src/pond.cpp:401` · Las marcas no se dibujan en la app. |
| `FISH_clearMarkers` | `fish/fish.c:1819` | parcial | Borra las marcas al engancharse un pez. | No hay marcas en pantalla que borrar; la app no las dibuja. |
| `FISH_tickRipple` | `fish/fish.c:1841` | parcial | Anima una onda o salpicadura del agua y la quita al acabar. | `game/fishing/src/pond.cpp:163` · El susto a los peces está; la animación de la onda no se dibuja. |
| `FISH_showBaitSprite` | `fish/fish.c:2136` | parcial | Muestra el cebo o la captura con el icono del objeto. | `game/fishing/src/rod.cpp:373` · Se ve como un marcador amarillo, no con el icono del objeto. |
| `FISH_pushSeadramonSegment` | `fish/fish.c:2557` | falta | Guarda el giro de la cabeza para que el cuerpo de la serpiente marina la siga. | La serpiente marina se ve como marcador; no hay cuerpo por segmentos. |
| `FISH_clipLineToRect` | `fish/fish.c:3573` | falta | Recorta el sedal al borde de la pantalla para dibujarlo. | El sedal no se dibuja (lineShown no lo usa la app). |
| `FISH_scrollCameraTo` | `fish/fish.c:4673` | falta | Desplaza el fondo del mapa hasta un punto de la pantalla. | La pesca no mueve la cámara; sigue al jugador. |
| `FISH_applySavedCameraScroll` | `fish/fish.c:4695` | falta | Recupera el desplazamiento de cámara guardado al volver a la pesca. | va con la cámara de la pesca, que falta |
| `FISH_enableCameraControl` | `fish/fish.c:4703` | falta | Guarda la vista al sacar la caña para restaurarla al acabar. | No hay control de cámara propio de la pesca. |
| `setFishingEnabled` | `main/fish.c:52` | parcial | Vuelve a permitir la pesca tras la escena de muerte o renacer del compañero. | `game/fishing/include/dwe/game/fishing/fishing.hpp:197` · Existe setDisabled, pero nada lo llama al acabar esas escenas. |

No aplica: dibujo del original: el motor dibuja con su propio renderizador; el hueco visual, si lo hay, va en la función de lógica (21); limpieza de objetos y memoria del original: el motor libera lo suyo (4); Vacía en el original. (3); Reserva fija de memoria; el motor usa eventos. (2); Reserva fija de memoria; el motor emite eventos de onda. (2); Estado de caja de UI; lo sustituye el documento del menú de cebo. (1); Estado de caja de UI; lo sustituye el documento del aviso. (1); Carga de texturas y modelos; la hace el motor de render. (1); Función vacía de carga de modelo. (1); preparación de tablas, objetos o recursos del original: el motor monta los suyos (1); Carga de modelos; la hace el motor de assets. (1); Acceso a modelos cargados; lo hace el motor de render. (1).

## Muerte y renacer

La escena de muerte, la de renacer y la caja de huevos.

Ficheros del decomp: `doo2/doo2.c`, `dooa/dooa.c`, `murd/murd.c`.

Portado 13, parcial 0, falta 16, no aplica 27.

| Función | Decomp | Estado | Qué hace | Motor / nota |
|---|---|---|---|---|
| `DOO2_tickShardSet` | `doo2/doo2.c:208` | falta | Avanza un grupo de trozos del huevo que se rompe y lo quita al acabar. | los trozos del huevo no se dibujan |
| `DOO2_releaseShardSet` | `doo2/doo2.c:219` | falta | Quita un grupo de trozos del huevo. | los trozos del huevo no se dibujan |
| `DOO2_tickEggBox` | `doo2/doo2.c:327` | falta | Reinicia cada cuadro el contador de destellos del huevo. | los destellos del huevo no se dibujan |
| `DOO2_resetShardSets` | `doo2/doo2.c:587` | falta | Vacía los grupos de trozos del huevo antes del renacimiento. | los trozos del huevo no se dibujan |
| `DOO2_buildShardSet` | `doo2/doo2.c:597` | falta | Parte el modelo del huevo en trozos que salen volando al eclosionar. | los trozos del huevo no se dibujan |
| `DOO2_releaseAllShardSets` | `doo2/doo2.c:712` | falta | Quita todos los grupos de trozos del huevo. | los trozos del huevo no se dibujan |
| `DOOA_hasIrisClosed` | `dooa/dooa.c:602` | falta | Dice si el iris que se cierra ya tapa al compañero, para ocultarlo. | el iris de la muerte no se dibuja |
| `DOOA_initShardEffect` | `dooa/dooa.c:637` | falta | Prepara el efecto de trozos de alambre del compañero al morir. | — |
| `DOOA_updateCutsceneCamera` | `dooa/dooa.c:728` | falta | Mueve la cámara de la muerte hacia el compañero y gira a su alrededor. | las cámaras de la escena no se aplican |
| `DOOA_spawnShardWave` | `dooa/dooa.c:791` | falta | Suelta una oleada de trozos de alambre desde cada hueso del compañero. | — |
| `DOOA_toggleShardFlicker` | `dooa/dooa.c:805` | falta | Hace parpadear los trozos de alambre de la muerte. | — |
| `DOOA_setShardState` | `dooa/dooa.c:1210` | falta | Cambia la fase del efecto de trozos (subir, huevo, renacer). | — |
| `DOOA_getOrbitPosition` | `dooa/dooa.c:1305` | falta | Calcula el punto de cámara que orbita al compañero en la muerte. | las cámaras de la escena no se aplican |
| `DOOA_updateShards` | `dooa/dooa.c:1323` | falta | Mueve, encoge y apaga los trozos de alambre y la luz que sube. | — |
| `DOOA_spawnBoneShards` | `dooa/dooa.c:1559` | falta | Crea los trozos de alambre de un hueso del compañero. | — |
| `MURD_isDigimonLargerThanIris` | `murd/murd.c:359` | falta | Dice si el iris que se cierra ya tapa al compañero en una vida perdida. | el iris no se dibuja ni oculta al compañero |

No aplica: dibujo del original: el motor dibuja con su propio renderizador; el hueco visual, si lo hay, va en la función de lógica (14); gráficos, memoria o ficheros del hardware original: los sustituye el motor (8); preparación de tablas, objetos o recursos del original: el motor monta los suyos (2); gestión de memoria del original (1); limpieza de objetos y memoria del original: el motor libera lo suyo (1); copias de otra función, que cuenta por las dos (1).

## Guardado y carga

La partida en la tarjeta de memoria, la máquina de guardar y el registro para el modo versus.

Ficheros del decomp: `main/main_menu.c`.

Portado 7, parcial 1, falta 3, no aplica 24.

| Función | Decomp | Estado | Qué hace | Motor / nota |
|---|---|---|---|---|
| `countRegisteredDigimon` | `main/main_menu.c:5136` | falta | Cuenta las criaturas registradas para el modo versus. | llega con el modo versus |
| `registerBattleData` | `main/main_menu.c:5230` | falta (citada en docs) | Registra al compañero para el modo versus. | save_machine.hpp: llega con el modo versus |
| `tickSaveMachine` | `main/main_menu.c:5288` | parcial | La máquina de guardar que abre el guion. | `apps/game/src/save_machine.hpp` · la máquina de guardar existe, pero la rutina 48 del guion se cierra sin abrirla |
| `awardMachinedramonData` | `main/main_menu.c:5299` | falta (citada en docs) | Al acabar el juego, registra al jefe final para el modo versus. | — |

No aplica: dibujo del original: el motor dibuja con su propio renderizador; el hueco visual, si lo hay, va en la función de lógica (16); tarjeta de memoria (4); tarjeta de memoria; el motor guarda en ficheros (1); gráficos, memoria o ficheros del hardware original: los sustituye el motor (1); suma de control de la tarjeta (1); suma de control de la tarjeta; el motor valida su fichero (1).

## Torneos

Inscripción, calendario y el overlay de copas, que trae su propio motor de combate.

Ficheros del decomp: `dget/dget.c`, `main/tournament.c`, `std/std_effect.c`, `std/std_hud.c`, `std/std_main.c`, `std/std_setup.c`.

Portado 0, parcial 4, falta 81, no aplica 364.

| Función | Decomp | Estado | Qué hace | Motor / nota |
|---|---|---|---|---|
| `fillEnabledTournamentTable` | `dget/dget.c:101` | falta | Arma la tabla de copas de los próximos 5 días y marca las que el compañero puede inscribir. | — |
| `buildScheduleLabels` | `dget/dget.c:147` | falta | Crea la cabecera con los días del calendario de copas en el tablón. | — |
| `buildScheduleEntries` | `dget/dget.c:201` | falta | Crea la cuadrícula del calendario con cada copa y su rango por día. | — |
| `initTournamentInfo` | `dget/dget.c:285` | falta | Abre el recuadro con la información de la copa seleccionada (requisitos y premio). | — |
| `tournamentCheckFair` | `dget/dget.c:331` | falta | Indica si la especie del compañero está entre las admitidas por la copa. | — |
| `isTournamentEnabled` | `dget/dget.c:350` | falta (citada en docs) | Indica si una copa está disponible según el avance de la historia. | — |
| `tournamentCheckEligible` | `dget/dget.c:429` | falta (citada en docs) | Comprueba si el compañero cumple los requisitos de inscripción de la copa. | — |
| `tickTournamentSchedule` | `dget/dget.c:468` | falta | Mueve el cursor del calendario y confirma o cancela la elección de copa. | — |
| `tickTournamentInfo` | `dget/dget.c:631` | falta | Cierra el recuadro de información de la copa al pulsar un botón. | — |
| `initTournamentSchedule` | `dget/dget.c:664` | parcial | Enseña el calendario de torneos. | `apps/session/include/dwe/session/story_scenes.hpp:108` · la rutina 18 se cierra sin pantalla |
| `updateTournamentRegistration` | `main/tournament.c:42` | falta (citada en docs) | Valida la inscripción pendiente y avisa cuando llega el día y la hora de la copa. | — |
| `scriptStartTournament` | `main/tournament.c:128` | parcial | Empieza un torneo. | `apps/session/include/dwe/session/story_scenes.hpp:100` · la rutina 6 se cierra sin torneo |
| `getCupDataJumpTable` | `main/tournament.c:146` | falta (citada en docs) | Busca en el guion del mapa los datos de una copa por sección e id. | — |
| `checkTournamentMedalConditions` | `main/tournament.c:168` | parcial | Mira si toca dar la medalla de los torneos. | `apps/session/src/story_routines/routine_tournament_medal.cpp:15` · la rutina 22 sigue al momento sin copas ganadas; no hay torneos |
| `scriptCheckTournamentMedal` | `main/tournament.c:242` | parcial | Comprueba y da la medalla de los torneos. | `apps/session/include/dwe/session/story_scenes.hpp:109` · la rutina 22 se cierra sin pantalla |
| `STD_addChampionScene` | `std/std_effect.c:1251` | falta | Inicia la escena de celebración del campeón de la copa. | — |
| `STD_tickChampionScene` | `std/std_effect.c:1259` | falta | Anima la escena del campeón: letras, giro de cámara y podio. | — |
| `STD_addPodiumRenderer` | `std/std_effect.c:1380` | falta | Muestra el podio en la escena de premiación. | — |
| `STD_addLoseScene` | `std/std_effect.c:1401` | falta | Inicia la escena de derrota con barrido aleatorio de la pantalla. | — |
| `STD_tickLoseScene` | `std/std_effect.c:1423` | falta | Anima la escena de derrota del combate de copa. | — |
| `STD_addWinScene` | `std/std_effect.c:1516` | falta | Inicia la escena de victoria del combate de copa. | — |
| `STD_tickWinScene` | `std/std_effect.c:1546` | falta | Anima el letrero de victoria del combate de copa. | — |
| `STD_handleCommands` | `std/std_hud.c:504` | falta | Navega y confirma las órdenes al compañero en el combate de copa, incluido el remate. | — |
| `STD_tickTamerTournament` | `std/std_hud.c:540` | falta | Lee la entrada del jugador en la copa: alterna barras de vida y da órdenes. | — |
| `STD_addVersusModelScene` | `std/std_hud.c:1168` | falta | Inicia la animación del letrero de enfrentamiento antes del combate. | — |
| `STD_tickVersusModelScene` | `std/std_hud.c:1173` | falta | Anima el letrero de enfrentamiento entre ambos luchadores. | — |
| `STD_isVersusModelSceneFinished` | `std/std_hud.c:1213` | falta | Indica si terminó la animación del letrero de enfrentamiento. | — |
| `STD_loadWinLoseModel` | `std/std_hud.c:1227` | falta | Calcula las curvas de entrada de los letreros de victoria/derrota y carga sus modelos. | — |
| `STD_loadChampionModels` | `std/std_hud.c:1273` | falta | Prepara la órbita de cámara y coloca las letras del letrero de campeón. | solo lo cita la extracción del disco |
| `STD_initializeChampionScene` | `std/std_hud.c:1371` | falta | Coloca al compañero en el centro con la pose de victoria y fija la cámara del campeón. | — |
| `STD_placePlayer1` | `std/std_main.c:1568` | falta | Coloca al compañero en su lado de la arena para el combate. | — |
| `STD_placePlayer2` | `std/std_main.c:1593` | falta | Coloca al rival en su lado de la arena para el combate. | — |
| `STD_setPostIntroPosition` | `std/std_main.c:1682` | falta | Deja al luchador en su posición final al terminar la presentación. | — |
| `STD_runIntro` | `std/std_main.c:1885` | falta | Ejecuta la presentación del combate de copa con cámara y nombres. | — |
| `STD_tickIntroName` | `std/std_main.c:2063` | falta | Muestra el nombre del luchador durante la presentación. | — |
| `STD_setCameraOrbit` | `std/std_main.c:2224` | falta | Cámara que orbita según la distancia entre ambos luchadores. | — |
| `STD_setCameraYXZ` | `std/std_main.c:2266` | falta | Cámara que encuadra al compañero y al rival a la vez. | — |
| `STD_setCameraToEntity` | `std/std_main.c:2286` | falta | Cámara fija relativa al luchador enfocado según su orientación. | — |
| `STD_tickVSPhase` | `std/std_main.c:2306` | falta | Elige cada cuadro el modo de cámara activo del combate de copa. | — |
| `STD_setViewpointRotationFromEntity` | `std/std_main.c:2347` | falta | Cámara que gira junto con el luchador enfocado. | — |
| `STD_setCameraLookAtEntity` | `std/std_main.c:2358` | falta | Cámara que mira hacia el luchador enfocado. | — |
| `STD_applyRotatingCamera` | `std/std_main.c:2394` | falta | Cámara que gira lento alrededor de la arena tras el combate. | — |
| `STD_setVSPhase` | `std/std_main.c:2445` | falta | Activa el control de cámara de la copa con el modo indicado. | — |
| `STD_selectRandomCamera` | `std/std_main.c:2457` | falta | Elige un ángulo de cámara al azar en técnicas y lanza victoria o derrota al final. | — |
| `STD_setRandomViewpoint` | `std/std_main.c:2487` | falta | Elige al azar un punto de vista preestablecido sobre un luchador. | — |
| `STD_setChampionCamera` | `std/std_main.c:2526` | falta | Encuadra al compañero según su tamaño para la escena de campeón. | — |
| `STD_setViewpointFromBone` | `std/std_main.c:2551` | falta | Sitúa la cámara relativa a un hueso del luchador. | — |
| `STD_updateCameraLerp` | `std/std_main.c:2580` | falta | Interpola la cámara de presentación entre sus etapas. | — |
| `STD_tickCameraChase` | `std/std_main.c:2652` | falta | Cámara que persigue al luchador que corre durante la presentación. | — |
| `STD_tickCameraIntro` | `std/std_main.c:2753` | falta | Avanza la secuencia de cámara de la presentación. | — |
| `STD_startCameraIntro` | `std/std_main.c:2782` | falta | Inicia el recorrido de cámara entre los dos luchadores. | — |
| `STD_applyEntityViewpoint` | `std/std_main.c:2854` | falta | Cámara anclada a la vista del luchador enfocado. | — |
| `STD_addFighterCounter` | `std/std_main.c:2911` | falta | Inicia el reloj de tiempo del combate de copa. | — |
| `STD_tickFighterCounter` | `std/std_main.c:2920` | falta | Descuenta el reloj del combate salvo cuando un luchador está caído. | — |
| `STD_updateBracket` | `std/std_main.c:2949` | falta | Avanza el cuadro de eliminatorias al siguiente cruce y destaca al jugador. | — |
| `STD_addBracketProjectile` | `std/std_main.c:3314` | falta | Lanza la marca animada que sube por el cuadro hacia el ganador. | — |
| `STD_addBracketHitFlash` | `std/std_main.c:3336` | falta | Inicia el destello en una casilla del cuadro de eliminatorias. | — |
| `STD_initializeBracket` | `std/std_main.c:3444` | falta | Reinicia el estado del cuadro de eliminatorias. | — |
| `STD_setupParticipants` | `std/std_main.c:3464` | falta | Reparte los ocho participantes de la copa en el cuadro. | solo lo cita la extracción del disco |
| `STD_addBracket` | `std/std_main.c:3544` | falta | Muestra el cuadro de eliminatorias de la copa. | — |
| `STD_tickBracket` | `std/std_main.c:3556` | falta | Anima el cuadro de eliminatorias entre combates. | — |
| `STD_isBracketFinished` | `std/std_main.c:3577` | falta | Indica si terminó la animación del cuadro de eliminatorias. | — |
| `STD_tickBracketHitFlash` | `std/std_main.c:3582` | falta | Anima el destello de una casilla del cuadro. | — |
| `STD_addBracketIntro` | `std/std_main.c:3672` | falta | Inicia la presentación del cuadro al empezar la copa. | — |
| `STD_tickBracketIntro` | `std/std_main.c:3686` | falta | Anima la presentación del cuadro de eliminatorias. | — |
| `STD_combatSetup` | `std/std_main.c:3939` | falta | Prepara los datos de combate del compañero y del rival en la copa. | — |
| `STD_checkEndCondition` | `std/std_main.c:4038` | falta | Comprueba fin de combate por KO o tiempo y decide ganador. | — |
| `STD_tickBattle` | `std/std_main.c:4431` | falta | Avanza un cuadro del combate de copa. | — |
| `STD_areAllEnemyDigimonDead` | `std/std_main.c:4626` | falta | Indica si todos los rivales del combate han caído. | — |
| `STD_tickBattleResultScreen` | `std/std_main.c:4641` | falta | Gestiona la pantalla de resultado tras el combate de copa. | — |
| `STD_addTimeoutWindow` | `std/std_main.c:4698` | falta | Muestra el aviso de tiempo agotado del combate. | — |
| `STD_tickDigimonSenile` | `std/std_main.c:4860` | falta | Mantiene al luchador en estado senil hasta que se agote su contador. | — |
| `STD_initializePlayerMarker` | `std/std_main.c:5476` | falta | Añade la marca que distingue a cada luchador en pantalla. | — |
| `STD_combatMain` | `std/std_main.c:5582` | falta | Bucle principal de un combate de copa hasta el resultado. | — |
| `STD_startWalkingAnimation2` | `std/std_main.c:5652` | falta | Elige animación de caminar normal o cansada según la vida. | — |
| `STD_applyPartnerStatsToFighter` | `std/std_main.c:5913` | falta | Copia los valores del compañero a su ficha de luchador. | — |
| `STD_selectMoveTarget` | `std/std_main.c:6660` | falta | Elige el objetivo de la técnica del luchador. | — |
| `STD_applyMoveResult` | `std/std_main.c:6839` | falta | Aplica el daño y efectos de una técnica que impacta. | — |
| `STD_updateFighterStatusVisuals` | `std/std_main.c:6954` | falta | Actualiza los efectos visibles de estados alterados del luchador. | — |
| `STD_addCommandMenu` | `std/std_main.c:7502` | falta | Muestra el menú de órdenes del jugador en la copa. | — |
| `STD_initializeTournament` | `std/std_setup.c:243` | falta | Prepara la arena de la copa: música, cámara, luces, escenario y efectos. | solo lo cita la extracción del disco |
| `STD_initializeOpponent` | `std/std_setup.c:279` | falta | Genera un rival de la copa con valores y técnicas según su nivel. | solo lo cita la extracción del disco |
| `STD_runMatch` | `std/std_setup.c:401` | falta | Juega un combate de copa: presentación, combate y cámara final. | — |
| `STD_initializeCamera` | `std/std_setup.c:415` | falta | Coloca la cámara inicial de la arena de copa. | — |
| `STD_tournamentMain` | `std/std_setup.c:521` | falta (citada en docs) | Flujo completo de la copa: cuadro, combates, resultados y premio. | — |

No aplica: copias de otra función, que cuenta por las dos (303); dibujo del original: el motor dibuja con su propio renderizador; el hueco visual, si lo hay, va en la función de lógica (28); limpieza de objetos y memoria del original: el motor libera lo suyo (16); búsqueda genérica en la tabla de entidades (2); gráficos, memoria o ficheros del hardware original: los sustituye el motor (2); utilidad genérica de reloj (1); lectura de punteros del guion del original (1); ajuste de UV de modelos del original (1); carga de modelos del original (1); cámara libre de depuración con el segundo mando (1); aplicación de proyección y punto de vista del original (1); asignación directa de valores de cámara (1); cálculo auxiliar de distancias (1); comprobación geométrica auxiliar (1); interpolación lineal genérica (1); cuenta genérica de dígitos (1); bucle de cuadros del original (1); configuración de luces del original (1).

## Versus y demo

El modo versus de dos jugadores y la demo de combate del título.

Ficheros del decomp: `main/sound.c`, `vs/vs_camera.c`, `vs/vs_combat.c`, `vs/vs_demo.c`, `vs/vs_effect.c`, `vs/vs_hud.c`, `vs/vs_intro.c`, `vs/vs_main.c`, `vs/vs_scene.c`, `vs/vs_select.c`.

Portado 0, parcial 0, falta 91, no aplica 315.

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

No aplica: copias de otra función, que cuenta por las dos (260); dibujo del original: el motor dibuja con su propio renderizador; el hueco visual, si lo hay, va en la función de lógica (22); limpieza de objetos y memoria del original: el motor libera lo suyo (12); gráficos, memoria o ficheros del hardware original: los sustituye el motor (3); lectura de mandos del original (2); preparación de tablas, objetos o recursos del original: el motor monta los suyos (2); aplicación de matrices de cámara del original (1); aplicación de proyección y punto de vista del original (1); asignación directa de valores de cámara (1); cálculo auxiliar de distancias (1); comprobación geométrica auxiliar (1); interpolación lineal genérica (1); cuenta genérica de dígitos (1); función vacía en el original (1); carga de modelos del original (1); bucle de cuadros del original (1); configuración de luces del original (1); contador genérico de cuadros (1); carga de modelos, texturas y colisión del original (1); registro de dibujo del original (1).

## Minijuego de piedras

El minijuego de piedras sobre hielo.

Ficheros del decomp: `kar/kar.c`, `main/kar.c`.

Portado 0, parcial 0, falta 35, no aplica 13.

| Función | Decomp | Estado | Qué hace | Motor / nota |
|---|---|---|---|---|
| `KAR_setupMatch` | `kar/kar.c:686` | falta | Prepara la partida de curling: piedras de cada equipo, obstáculos y rival. | el motor no tiene el minijuego; la escena termina al instante |
| `KAR_start` | `kar/kar.c:778` | falta | Empieza el minijuego de piedras. | `game/story/include/dwe/game/story/partner_routines.hpp:17` · la escena 1 acaba al momento: no hay minijuego de piedras |
| `KAR_tickStones` | `kar/kar.c:797` | falta | Mueve las piedras sobre el hielo con su rozamiento y sus choques. | — |
| `KAR_finishMatch` | `kar/kar.c:945` | falta | Cierra la partida: poses finales y guarda el resultado (gana, gana por mucho o pierde). | — |
| `KAR_tick` | `kar/kar.c:973` | falta | Bucle principal del minijuego de curling por cuadro. | — |
| `KAR_handleAimScroll` | `kar/kar.c:1215` | falta | Desplaza la cámara con la cruceta mientras el jugador apunta. | — |
| `KAR_updateRingMarkers` | `kar/kar.c:1248` | falta | Marca en qué anillo de la diana está cada piedra. | — |
| `KAR_checkStonesStopped` | `kar/kar.c:1319` | falta | Comprueba si todas las piedras se han detenido para pasar el turno. | — |
| `KAR_updateCollisions` | `kar/kar.c:1362` | falta | Detecta los choques entre piedras y contra las paredes de la pista. | — |
| `KAR_bounceOffWall` | `kar/kar.c:1468` | falta | Hace rebotar una piedra contra la pared de la pista. | — |
| `KAR_distance` | `kar/kar.c:1529` | falta | Calcula la distancia entre dos puntos de la pista. | — |
| `KAR_resolveStoneCollision` | `kar/kar.c:1541` | falta | Decide cómo se reparte un choque entre dos piedras. | — |
| `KAR_collidePeggedStone` | `kar/kar.c:1569` | falta | Choque de una piedra en marcha contra una piedra fija. | — |
| `KAR_collideRestingStone` | `kar/kar.c:1607` | falta | Choque de una piedra en marcha contra una piedra parada, que sale despedida. | — |
| `KAR_collideMovingStones` | `kar/kar.c:1665` | falta | Choque entre dos piedras que se mueven a la vez. | — |
| `KAR_computeImpactShare` | `kar/kar.c:1762` | falta | Calcula cuánta fuerza recibe cada piedra en un choque. | — |
| `KAR_computeSeparation` | `kar/kar.c:1784` | falta | Separa dos piedras que se solapan tras chocar. | — |
| `KAR_getWallZone` | `kar/kar.c:1859` | falta | Dice en qué zona de pared de la pista está un punto. | — |
| `KAR_placeAtContact` | `kar/kar.c:1934` | falta | Coloca la piedra justo en el punto de contacto del choque. | — |
| `KAR_beginAiming` | `kar/kar.c:1944` | falta | Empieza la fase de apuntar del turno del jugador. | — |
| `KAR_selectPreviousStone` | `kar/kar.c:1968` | falta | Elige la piedra anterior para lanzar. | — |
| `KAR_selectNextStone` | `kar/kar.c:1982` | falta | Elige la piedra siguiente para lanzar. | — |
| `KAR_beginThrow` | `kar/kar.c:2020` | falta | Lanza la piedra elegida hacia el punto apuntado. | — |
| `KAR_tickYesNoPrompt` | `kar/kar.c:2032` | falta | Pregunta sí/no del minijuego (confirmar tiro o jugar otra vez). | — |
| `KAR_tickHintBox` | `kar/kar.c:2085` | falta | Anima el cuadro de ayuda con las instrucciones del curling. | — |
| `KAR_classifyStoneRings` | `kar/kar.c:2140` | falta | Clasifica las piedras por anillo para puntuar. | — |
| `KAR_tickScoreTally` | `kar/kar.c:2181` | falta | Cuenta y muestra los puntos de cada equipo al final de la ronda. | — |
| `KAR_chooseOpponentShot` | `kar/kar.c:2306` | falta | Decide el tiro del rival según las piedras en la pista. | — |
| `KAR_findClearShotAngle` | `kar/kar.c:2525` | falta | Busca un ángulo de tiro libre de obstáculos para el rival. | — |
| `KAR_setOpponentShot` | `kar/kar.c:2600` | falta | Fija el destino y la fuerza del tiro del rival. | — |
| `KAR_aimAtRandomStone` | `kar/kar.c:2624` | falta | El rival apunta a una piedra al azar. | — |
| `KAR_registerThrownStone` | `kar/kar.c:2732` | falta | Apunta la piedra recién lanzada en la lista de lanzadas. | — |
| `KAR_rotatePoint` | `kar/kar.c:2740` | falta | Gira un punto sobre la pista para posicionar piedras y marcas. | — |
| `KAR_tickMatchState` | `kar/kar.c:2765` | falta | Máquina de estados de la partida: turnos, tiros, rondas y final. | — |
| `checkCurlingMap` | `main/kar.c:8` | falta | Al entrar en el mapa de curling carga el minijuego. | el motor no tiene el minijuego |

No aplica: dibujo del original: el motor dibuja con su propio renderizador; el hueco visual, si lo hay, va en la función de lógica (10); sale al empezar en el original: código muerto (2); preparación de tablas, objetos o recursos del original: el motor monta los suyos (1).

## Audio

Música, efectos de sonido y su carga.

Ficheros del decomp: `main/sound.c`, `main/sound_async.c`.

Portado 19, parcial 0, falta 0, no aplica 15.

No aplica: arranque, cola de lectura del disco, codificación de texto o carga de sonido del original: lo hace el motor (9); gráficos, memoria o ficheros del hardware original: los sustituye el motor (3); lectura del disco; la hace la extracción (1); secuenciador del original (1); voces del chip de sonido; el motor tiene su mezclador (1).

## Vídeos y final

El reproductor de vídeo, los créditos y la animación del final.

Ficheros del decomp: `eab/eab.c`, `endi/endi.c`, `mov/mov.c`.

Portado 3, parcial 0, falta 23, no aplica 18.

| Función | Decomp | Estado | Qué hace | Motor / nota |
|---|---|---|---|---|
| `EAB_setModelColor` | `eab/eab.c:159` | falta (citada en docs) | Tiñe el modelo del efecto de aparición del jefe final. | la aparición del jefe termina al instante |
| `EAB_tickBuildup` | `eab/eab.c:187` | falta | Lanza anillos de energía periódicos antes de que aparezca el jefe final. | — |
| `EAB_initializeRings` | `eab/eab.c:210` | falta | Vacía la lista de anillos de energía de la aparición. | — |
| `EAB_addBuildupRing` | `eab/eab.c:219` | falta | Añade un anillo de energía de la carga previa. | — |
| `EAB_tickBuildupRing` | `eab/eab.c:241` | falta | Mueve y desvanece un anillo de la carga previa. | — |
| `EAB_addSpawnRing` | `eab/eab.c:276` | falta | Añade un anillo de la aparición del jefe final. | — |
| `EAB_tickSpawnRing` | `eab/eab.c:298` | falta | Mueve y desvanece un anillo de la aparición. | — |
| `EAB_tickSpawn` | `eab/eab.c:349` | falta | Escena de aparición del jefe final: destello, anillos, partículas, cámara y sonidos. | — |
| `EAB_initializeParticles` | `eab/eab.c:462` | falta | Vacía la lista de partículas de la aparición. | — |
| `EAB_setEntitiesVisible` | `eab/eab.c:490` | falta | Oculta a todos menos al jefe durante su aparición y luego los repone. | — |
| `EAB_addParticle` | `eab/eab.c:513` | falta | Añade una partícula de luz a la aparición del jefe. | — |
| `EAB_tickParticle` | `eab/eab.c:595` | falta | Mueve y apaga una partícula de la aparición. | — |
| `EAB_startBuildup` | `eab/eab.c:649` | falta | Empieza la carga de energía previa a la aparición del jefe final. | solo la herramienta de extracción usa su modelo; el juego no la reproduce |
| `ENDI_setModelSemiTrans` | `endi/endi.c:115` | falta | Vuelve translúcido el modelo del compañero en el final. | el motor no tiene la escena del final, solo el vídeo |
| `ENDI_updateEnding` | `endi/endi.c:159` | falta | Escena final: el compañero se desvanece, encoge, crece y sube convertido en luz. | — |
| `ENDI_startParticles` | `endi/endi.c:227` | falta | Arranca las partículas de luz de la escena final. | — |
| `ENDI_spawnParticle` | `endi/endi.c:280` | falta | Suelta una partícula de luz desde un hueso del compañero. | — |
| `ENDI_releaseParticles` | `endi/endi.c:302` | falta | Libera las partículas para que suban solas. | — |
| `ENDI_getParticleBase` | `endi/endi.c:340` | falta | Lee el punto de origen de las partículas del final. | — |
| `ENDI_setParticleBase` | `endi/endi.c:345` | falta | Mueve el punto de origen de las partículas del final. | — |
| `ENDI_stopParticles` | `endi/endi.c:350` | falta | Detiene las partículas de la escena final. | — |
| `ENDI_clearParticles` | `endi/endi.c:355` | falta | Vacía la lista de partículas del final. | — |
| `ENDI_tickEndingParticles` | `endi/endi.c:364` | falta | Mueve y apaga las partículas de luz del final. | — |

No aplica: dibujo del original: el motor dibuja con su propio renderizador; el hueco visual, si lo hay, va en la función de lógica (9); limpieza de objetos y memoria del original: el motor libera lo suyo (2); gráficos, memoria o ficheros del hardware original: los sustituye el motor (2); descodificación de vídeo; el motor usa su propio reproductor (2); lectura en flujo del disco; el motor lee el vídeo de archivo (2); sincronía de hardware; el motor tiene su propio bucle (1).

## Núcleo y plataforma

Arranque, bucle principal, reloj, azar, matemáticas, ficheros, memoria y gráficos de bajo nivel.

Ficheros del decomp: `main/_psstart.c`, `main/clock.c`, `main/file.c`, `main/file_queue.c`, `main/graphics.c`, `main/line.c`, `main/main.c`, `main/math.c`, `main/rng.c`, `main/utils.c`, `main/utils2.c`.

Portado 40, parcial 1, falta 2, no aplica 43.

| Función | Decomp | Estado | Qué hace | Motor / nota |
|---|---|---|---|---|
| `recalculatePPandArena` | `main/main.c:3902` | falta | Recalcula los puntos de prosperidad de la ciudad según quién se ha unido. | no se encontró en el motor |
| `unlearnMove` | `main/utils2.c:519` | parcial | Hace olvidar una técnica al compañero. | `apps/session/src/story_routines/routine_forget_moves.cpp` · la rutina de olvidar técnicas se cierra sin pantalla |
| `startTournament` | `main/utils2.c:577` | falta (citada en docs) | Carga y juega un torneo; suma los ganados. | no hay torneos |

No aplica: gráficos, memoria o ficheros del hardware original: los sustituye el motor (21); arranque, cola de lectura del disco, codificación de texto o carga de sonido del original: lo hace el motor (5); dibujo del original: el motor dibuja con su propio renderizador; el hueco visual, si lo hay, va en la función de lógica (5); preparación de tablas, objetos o recursos del original: el motor monta los suyos (3); entrada del original (2); consulta de dibujo (2); tabla de ficheros del disco (1); texturas del original (1); solo la demo de prueba (1); preparación del original (1); limpieza de objetos y memoria del original: el motor libera lo suyo (1).

<!-- port-coverage:end -->
