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
defecto `../decomp-index/functions.tsv`, o `--index`) y busca cada nombre en el código del motor (`packages/`, `apps/`,
`tests/`, `content/packs/`). Lo que solo nombran la extracción y las herramientas (`apps/dwx`, `packages/formats/`)
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

## Huecos que el jugador notará

Con la historia entera jugable (2026-11-06), lo que queda por portar es poco. Ordenado por lo pronto que aparece en
una partida normal:

| # | Hueco | Decomp | Motor |
|---:|---|---|---|
| 1 | Sin modo versus de dos jugadores ni la demo de combate del título (las 91 funciones de `vs/`); la máquina de guardar ya registra al compañero para él. Va a la fase 5 | `vs/` | — |

## Otros huecos que no son una función del índice

- **Deslizamientos de guion contra personajes:** `wouldTouchCharacter` (del motor, `apps/game/src/story_stage.cpp`)
  solo mira al jugador y al compañero, así que un deslizamiento del guion puede atravesar a los NPC
  ([field](../modulos/game/field.md#posiciones)).
- **Escenas del compañero sin escena propia:** la rutina 5 del compañero (la de la apertura, que solo llama el guion
  de la sala de depuración) acaba al momento; una pantalla de rutina que la app no tiene se cierra al momento y lo dice el log
  (`apps/game/src/story_scene_glue.cpp`).
- **Año de 31 días al entrenar:** el original avanza el día con un año de 31 días tras entrenar; el motor usa el
  reloj, de 30. Decidido así ([partner](../modulos/game/partner.md#paridad-con-el-original)).

<!-- port-coverage:begin (scripts/port_coverage.py writes this part) -->

## Resumen por sistema

Solo cuentan las funciones a portar (portado + parcial + falta); las atadas a la consola quedan fuera. Cobertura = portado + medio parcial, sobre las funciones a portar.

| Sistema | Ficheros del decomp | A portar | Portado | Parcial | Falta | Cobertura |
|---|---|---:|---:|---:|---:|---:|
| [Batalla](#batalla) | `btl/`, `main/` (9 ficheros) | 154 | 154 | 0 | 0 | 100 % |
| [Efectos de batalla](#efectos-de-batalla) | `btl/`, `main/` (6 ficheros) | 240 | 240 | 0 | 0 | 100 % |
| [Campo: movimiento, colisión y cámara](#campo-movimiento-colisión-y-cámara) | `main/` (5 ficheros) | 82 | 82 | 0 | 0 | 100 % |
| [NPC y objetos del mapa](#npc-y-objetos-del-mapa) | `main/` (8 ficheros) | 80 | 80 | 0 | 0 | 100 % |
| [Compañero: IA y cuidado](#compañero-ia-y-cuidado) | `main/partner.c`, `main/partner_impl.c` | 49 | 49 | 0 | 0 | 100 % |
| [Guion e intérprete](#guion-e-intérprete) | `main/script_common.c`, `main/script_interp.c`, `main/script_textbox.c`, `main/script_value.c` | 97 | 97 | 0 | 0 | 100 % |
| [Menús e interfaz](#menús-e-interfaz) | `main/` (8 ficheros) | 79 | 79 | 0 | 0 | 100 % |
| [Objetos y tiendas](#objetos-y-tiendas) | `main/item.c`, `shop/script_menu.c` | 96 | 96 | 0 | 0 | 100 % |
| [Evolución](#evolución) | `evl/evl.c`, `main/evl.c`, `main/evolution.c` | 29 | 29 | 0 | 0 | 100 % |
| [Entrenamiento](#entrenamiento) | `trn/`, `trn2/` (11 ficheros) | 38 | 37 | 1 | 0 | 99 % |
| [Pesca](#pesca) | `fish/fish.c`, `fish/fish_model.c`, `main/fish.c` | 95 | 95 | 0 | 0 | 100 % |
| [Muerte y renacer](#muerte-y-renacer) | `doo2/doo2.c`, `dooa/dooa.c`, `murd/murd.c` | 40 | 40 | 0 | 0 | 100 % |
| [Guardado y carga](#guardado-y-carga) | `main/main_menu.c` | 12 | 12 | 0 | 0 | 100 % |
| [Torneos](#torneos) | `dget/`, `main/`, `std/` (6 ficheros) | 113 | 113 | 0 | 0 | 100 % |
| [Versus y demo](#versus-y-demo) | `main/`, `vs/` (10 ficheros) | 93 | 29 | 4 | 60 | 33 % |
| [Minijuego de piedras](#minijuego-de-piedras) | `kar/kar.c`, `main/kar.c` | 44 | 44 | 0 | 0 | 100 % |
| [Audio](#audio) | `main/sound.c`, `main/sound_async.c` | 21 | 21 | 0 | 0 | 100 % |
| [Vídeos y final](#vídeos-y-final) | `eab/eab.c`, `endi/endi.c`, `mov/mov.c` | 30 | 30 | 0 | 0 | 100 % |
| [Núcleo y plataforma](#núcleo-y-plataforma) | `main/` (11 ficheros) | 51 | 51 | 0 | 0 | 100 % |
| **Total** | 1443 funciones a portar | 1443 | 1378 | 5 | 60 | 96 % |

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

Portado 82, parcial 0, falta 0.

## NPC y objetos del mapa

Los personajes del mapa y su IA de paseo, cofres, objetos del suelo y decorados con lógica.

Ficheros del decomp: `main/anim.c`, `main/bubble.c`, `main/butterfly.c`, `main/drop_shadow.c`, `main/map_object.c`, `main/model.c`, `main/overworld.c`, `main/world_object.c`.

Portado 80, parcial 0, falta 0.

## Compañero: IA y cuidado

Hambre, sueño, baño, enfermedad, felicidad y disciplina, caca, muerte natural, paseo y órdenes.

Ficheros del decomp: `main/partner.c`, `main/partner_impl.c`.

Portado 49, parcial 0, falta 0.

## Guion e intérprete

La máquina de scripts de eventos: órdenes, valores, cajas de texto y elecciones.

Ficheros del decomp: `main/script_common.c`, `main/script_interp.c`, `main/script_textbox.c`, `main/script_value.c`.

Portado 97, parcial 0, falta 0.

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
| `TRN_advanceTrainingTime` | `trn/trn_reward.c:721` | parcial | Avanza la hora y el día tras una sesión de entrenamiento. | `content/packs/game/rules/original/care.yaml:229` · el original usa un año de 31 días al entrenar; el motor, el del reloj (30). Decidido así (partner.md:188) |

## Pesca

El minijuego de pesca.

Ficheros del decomp: `fish/fish.c`, `fish/fish_model.c`, `main/fish.c`.

Portado 95, parcial 0, falta 0.

## Muerte y renacer

La escena de muerte, la de renacer y la caja de huevos.

Ficheros del decomp: `doo2/doo2.c`, `dooa/dooa.c`, `murd/murd.c`.

Portado 40, parcial 0, falta 0.

## Guardado y carga

La partida en la tarjeta de memoria, la máquina de guardar y el registro para el modo versus.

Ficheros del decomp: `main/main_menu.c`.

Portado 12, parcial 0, falta 0.

## Torneos

Inscripción, calendario y el overlay de copas, que trae su propio motor de combate.

Ficheros del decomp: `dget/dget.c`, `main/tournament.c`, `std/std_effect.c`, `std/std_hud.c`, `std/std_main.c`, `std/std_setup.c`.

Portado 113, parcial 0, falta 0.

## Versus y demo

El modo versus de dos jugadores y la demo de combate del título.

Ficheros del decomp: `main/sound.c`, `vs/vs_camera.c`, `vs/vs_combat.c`, `vs/vs_demo.c`, `vs/vs_effect.c`, `vs/vs_hud.c`, `vs/vs_intro.c`, `vs/vs_main.c`, `vs/vs_scene.c`, `vs/vs_select.c`.

Portado 29, parcial 4, falta 60.

| Función | Decomp | Estado | Qué hace | Motor / nota |
|---|---|---|---|---|
| `VSLoadSounds` | `main/sound.c:710` | falta | Carga los sonidos del modo versus. | `apps/game/src/versus_arena.cpp:150` |
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
| `VS__combatInit` | `vs/vs_combat.c:280` | parcial | Prepara el combate versus: daño, reloj, marcas y fichas de ambos luchadores. | `packages/game/battle/src/rules.cpp:1` · sin las marcas de jugador ni las fichas del HUD 3D; el versus en línea se enseña como panel |
| `VS_setupDemoCombat` | `vs/vs_combat.c:449` | falta | Prepara el combate automático de la demostración. | demostración del título: solo la llama el main de la versión de prueba; el de esta versión pone la película |
| `VS_checkDemoEndCondition` | `vs/vs_combat.c:552` | falta | Comprueba si la demostración termina por KO, tiempo o botón. | demostración del título: solo la llama el main de la versión de prueba; el de esta versión pone la película |
| `VS_cleanupDemoCombat` | `vs/vs_combat.c:750` | falta | Cierra el combate de demostración y libera sus objetos. | demostración del título: solo la llama el main de la versión de prueba; el de esta versión pone la película |
| `VS__handlePause` | `vs/vs_combat.c:1233` | falta | Gestiona la pausa del versus por cualquiera de los jugadores. | — |
| `VS__addTimeoutWindow` | `vs/vs_combat.c:1505` | falta | Muestra el aviso de tiempo agotado del versus. | — |
| `VS__tickDigimonStun` | `vs/vs_combat.c:1622` | falta | Mantiene al luchador aturdido con su animación. | — |
| `VS__tickDigimonSenile` | `vs/vs_combat.c:1684` | falta | Mantiene al luchador senil hasta que se agote su contador. | — |
| `VS__entityGetMoveWithHighestDistance` | `vs/vs_combat.c:2000` | falta | Busca la técnica de mayor alcance del luchador. | — |
| `VS__tickDigimonAttackingLogic` | `vs/vs_combat.c:2136` | falta | Lógica de ataque del luchador en el versus: colisión y uso de técnicas. | — |
| `VS__getBaseDistance` | `vs/vs_combat.c:2269` | falta | Calcula la distancia base de combate según el tamaño de ambos luchadores. | — |
| `VS__hasAffordableMoves2` | `vs/vs_combat.c:2292` | falta | Indica si el luchador tiene técnicas que pueda pagar con su MP. | — |
| `VS__initializePlayerMarker` | `vs/vs_combat.c:2337` | falta | Añade la marca que distingue a los luchadores de la misma especie. | — |
| `VS__clearFighterDataTables` | `vs/vs_combat.c:2343` | falta | Vacía las tablas internas de la ficha de un luchador. | — |
| `VS_runDemoCombat` | `vs/vs_combat.c:2429` | falta | Ejecuta el combate de demostración con música y sin menús. | demostración del título: solo la llama el main de la versión de prueba; el de esta versión pone la película |
| `VS_setupDemoBattle` | `vs/vs_demo.c:107` | falta | Prepara la demostración: arena, dos luchadores al azar y sus sonidos. | demostración del título: solo la llama el main de la versión de prueba; el de esta versión pone la película |
| `VS_cleanupDemoBattle` | `vs/vs_demo.c:133` | falta | Retira a los luchadores y la arena al acabar la demostración. | demostración del título: solo la llama el main de la versión de prueba; el de esta versión pone la película |
| `VS_loadDemoAssets` | `vs/vs_demo.c:155` | falta | Elige arena y música al azar para la demostración y prepara efectos. | demostración del título: solo la llama el main de la versión de prueba; el de esta versión pone la película |
| `VS_loadDemoFighters` | `vs/vs_demo.c:180` | falta | Elige al azar las dos criaturas de la demostración. | demostración del título: solo la llama el main de la versión de prueba; el de esta versión pone la película |
| `VS_randomizeDemoFighter` | `vs/vs_demo.c:195` | falta | Genera valores y técnicas al azar para un luchador de demostración. | demostración del título: solo la llama el main de la versión de prueba; el de esta versión pone la película |
| `VS_setupTrialBattle` | `vs/vs_demo.c:348` | falta | Prepara el versus de prueba con tres criaturas al azar por bando. | — |
| `VS_randomizeTrialDigimonList` | `vs/vs_demo.c:373` | falta | Rellena la lista de criaturas al azar de un bando del versus de prueba. | — |
| `VS_randomizeTrialDigimon` | `vs/vs_demo.c:383` | falta | Genera valores y técnicas al azar para una criatura del versus de prueba. | — |
| `VS_playDemo` | `vs/vs_demo.c:477` | falta (citada en docs) | Reproduce la demostración automática de combate de la pantalla de título. | demostración del título: solo la llama el main de la versión de prueba; el de esta versión pone la película |
| `VS_initializeTrialBattle` | `vs/vs_demo.c:486` | falta | Inicia el versus de prueba con las listas de ambos jugadores. | — |
| `VS_addCommandMenu` | `vs/vs_hud.c:449` | falta | Muestra el menú de órdenes de un jugador del versus. | — |
| `VS_tickTargetCursor` | `vs/vs_hud.c:652` | falta | Mueve el cursor de objetivo del jugador del versus. | — |
| `VS_addVersusModelScene` | `vs/vs_hud.c:1349` | falta | Inicia la animación del letrero de enfrentamiento del versus. | — |
| `VS_tickVersusModelScene` | `vs/vs_hud.c:1355` | falta | Anima el letrero de enfrentamiento del versus. | — |
| `VS_isVersusModelSceneFinished` | `vs/vs_hud.c:1401` | falta | Indica si terminó el letrero de enfrentamiento del versus. | — |
| `VS_loadStageModels` | `vs/vs_hud.c:1410` | falta | Calcula las curvas de los letreros de resultado y carga sus modelos. | — |
| `VS_addResultModelScene` | `vs/vs_hud.c:1445` | falta | Muestra el letrero de victoria del jugador ganador del versus. | — |
| `VS_setVersusModelSceneTimer` | `vs/vs_hud.c:1549` | falta | Fija el temporizador de la escena de letreros del versus. | — |
| `VS__setPostIntroPosition` | `vs/vs_intro.c:256` | falta | Deja al luchador en su posición final tras la presentación del versus. | — |
| `VS__tickIntroName` | `vs/vs_intro.c:540` | falta | Muestra el nombre del luchador durante la presentación del versus. | — |
| `VS_startAttackAnimation` | `vs/vs_main.c:553` | falta | Inicia la animación y partículas de una técnica. | — |
| `VS_resolveAttack` | `vs/vs_main.c:562` | falta | Resuelve los impactos de las técnicas en vuelo. | — |
| `VS_startFighterMove` | `vs/vs_main.c:797` | falta | Hace que el luchador empiece a ejecutar una técnica. | — |
| `VS_selectMoveTarget` | `vs/vs_main.c:856` | falta | Elige el objetivo de la técnica del luchador en el versus. | — |
| `VS_applyMoveResult` | `vs/vs_main.c:1151` | falta | Aplica el daño y efectos de una técnica que impacta en el versus. | — |
| `VS_queueRandomMove` | `vs/vs_main.c:1735` | falta | Elige al azar técnica y objetivo para la IA del luchador. | — |
| `VS_loadVSAssets` | `vs/vs_scene.c:1147` | parcial | Prepara la arena del versus: música por escenario, cámara, luces y efectos. | `apps/game/src/versus_arena.cpp:1` · la arena 0 con su cámara, sus luces y los efectos; sin la música por escenario |
| `VS_playVersusIntroSequence` | `vs/vs_scene.c:1558` | falta | Reproduce la presentación inicial del versus. | `apps/game/src/versus_flow.cpp:30` |
| `VS_tickVersusIntro` | `vs/vs_scene.c:1618` | falta | Avanza la presentación del versus por estados. | — |
| `VS_initializeCamera` | `vs/vs_scene.c:1851` | parcial | Coloca la cámara inicial de la arena del versus. | `apps/game/src/versus_arena.cpp:1` · la cámara de la copa (game::tournament::MatchCamera); sin medir contra la del versus |
| `VS__createPressStartToBeginBox` | `vs/vs_select.c:409` | falta | Muestra el aviso de pulsar Start para empezar. | — |
| `VS__initialize` | `vs/vs_select.c:442` | parcial | Flujo de preparación del versus: modo, mapa y selección de criaturas. | `apps/game/src/versus_flow.cpp:1` · sin elección de arena (VS__tickSelectMap) |
| `VS__tickSelectMap` | `vs/vs_select.c:1166` | falta (citada en docs) | Pantalla de elección del escenario del versus. | — |

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

Portado 30, parcial 0, falta 0.

## Núcleo y plataforma

Arranque, bucle principal, reloj, azar, matemáticas, ficheros, memoria y gráficos de bajo nivel.

Ficheros del decomp: `main/_psstart.c`, `main/clock.c`, `main/file.c`, `main/file_queue.c`, `main/graphics.c`, `main/line.c`, `main/main.c`, `main/math.c`, `main/rng.c`, `main/utils.c`, `main/utils2.c`.

Portado 51, parcial 0, falta 0.

## Fuera de las cuentas

995 funciones del decomp están atadas a la consola (dibujo, memoria, lectura del disco, sonido del hardware, tablas internas, depuración, copias del código de batalla en otros overlays) y no cuentan en ninguna cifra. `python3 scripts/port_coverage.py --list` las da con su motivo (estado `no-aplica`).

<!-- port-coverage:end -->
