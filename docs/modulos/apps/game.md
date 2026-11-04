# game

`dwe-game`, el juego: abre la ventana, monta los paquetes, empieza la partida (`apps/session`), dibuja el campo
(`FieldScene`) y las pantallas (`ui/*` del paquete `game`, con los modelos de vista de `apps/screens`) y corre los
autotests. Las opciones, los controles y cómo se recorren las rutas están en la
[guía de la prueba vertical](../../guides/prueba-vertical.md); la UI y sus comandos, en
[ui.md](../../architecture/ui.md).

## Semilla

`--seed <n>` da la semilla de una partida nueva (`SessionDesc::seed`): los números aleatorios del partner y los de los
sistemas salen de ella, así que una ruta con la misma semilla juega igual cada vez. Sin la opción, el juego toma una del
reloj del sistema y la escribe en el log (`game: new game, seed <n>`) para poder repetir la partida de un informe de
fallo. Con `--load` la opción no cambia nada: la partida cargada sigue con el estado de los números que guardó.

## El compañero en el campo

El campo dibuja al compañero con el modelo de su especie: `digimon/<especie>/model` del paquete base, con las
animaciones de `models/partner` del paquete `game`, que tienen los mismos ids en todos los compañeros
([modelos](../../architecture/models.md)). Al empezar, una partida cargada lo dibuja con la especie guardada; después,
el juego mira cada fotograma los eventos del compañero (`GameSession::takePartnerEvents`, en `partner_events.cpp`):
`Evolved` (una evolución, la forma virus o su vuelta) y `Reborn` cargan el modelo de la especie nueva
(`FieldScene::showPartnerSpecies`). El resto de eventos aún no se ven en pantalla (las reacciones del HUD llegan con
los guiones de los mapas) y solo se escriben en el log de depuración. Si el modelo de una especie no se puede cargar,
el compañero sigue con el que tenía y el log lo avisa.

`evolve("<especie>")`, en la consola y en los pasos `script` de las rutas, hace evolucionar al compañero como un guion
de mapa (`EvolutionAdapter::evolveByScript`); es una ayuda de depuración hasta que corran los guiones. La ruta
`tests/autotest/route-evolve.yaml` lo usa para comprobar que el modelo cambia (`wait: {partner_model: ...}`).

Hueco: el radio de colisión y las velocidades de andar y correr del compañero siguen siendo los de `field/rules`
(medidos en el compañero inicial), no los de la especie nueva (`DIGIMON_DATA[tipo].radius` y la raíz de sus
animaciones).

## Dar un objeto en el campo

Usar un objeto de la bolsa en el campo no lo aplica al momento: juega la escena del original (`game_menu.c:
startFeedingItem`, `partner.c: partnerTickFeedItem`, en `feed_scene.cpp`). El menú se cierra y el jugador queda quieto
mientras el tiempo corre (`Field::holdForCare`). El compañero anda hasta la distancia a la que toma objetos: se acerca si
está lejos y se aleja si está cerca (`Field::walkPartnerToRange`, `checkEatDistance`). Esa distancia va por especie en
`field_items.feed.take_distance` (`ITEM_TAKE_DISTANCE`). El jugador le tiende el objeto y suena el sonido. Pasado el
cuadro 7, el compañero lo come y el objeto se aplica al acabar, o lo rechaza y el objeto queda en la bolsa. Mientras
dura, la muerte y la evolución esperan, porque el jugador no está libre. Al abrir la escena se cierra el menú
(`startFeedingItem` pone `IS_IN_MENU = 0`). Las escenas de cuidado esperan a que un clip acabe su primera vuelta, también
si tiene bucle (`FieldScene::partnerClipFinished`, `playerClipPlayedThrough`); con un clip en bucle, el rechazo no
terminaba nunca. Sin el bloque `feed` del ruleset, la bolsa usa
el objeto al momento, como antes. `use_item` de la consola también lo usa al momento. Desde que el jugador lo tiende
hasta el cuadro 11 de la animación de comer (o hasta el final del rechazo), el icono del objeto sale de cara a la cámara
en el nodo 9 del modelo del jugador, sin sombra (`renderFeedingItem`, `addObject(0xfa7)`/`removeObject`;
`FieldScene::setHeldItem`, `feed_item.hpp`; captura `w11-missing/shots/feed-item-after.png`).

Diferencias con el original:

- El compañero decide si lo toma al elegir el objeto, no al tendérselo: es la misma tirada, un momento antes.
- Si el compañero se atasca contra una pared, se para donde está; el original sigue andando.

## Dormir

«Dormir» del menú del campo juega la escena del original (`partner.c: partnerTickSleep`, en `sleep_screen.cpp` y
`sleep_steps.cpp`); el guardado y los fundidos están en [guardar y cargar](../../architecture/guardar-y-cargar.md#36-dormir).

Igual: el reloj se para y el jugador queda quieto (`Field::holdForCare`, estado 6 del jugador). Si el compañero no está
ya junto al jugador (más cerca de 2,5 radios, `getPartnerTamerCloseness` 2), anda recto hacia él
(`Field::walkPartnerToRange`) y los dos se miran. Entonces suena `playSound(0, 15)`, el jugador hace la animación 8
(le da una palmada) y el compañero la 0; al acabar la del jugador (31 fotogramas, 89 vblanks medidos) el compañero
bosteza con la 11 (51 fotogramas, 150 vblanks) y se acuesta con la 9; el fundido a negro empieza 5 fotogramas antes
del final de la 9 (57 vblanks después de empezarla). La 9 lleva al compañero hacia el jugador por su movimiento raíz
(`applyRootMomentum`: 70 unidades medidas en el original y en dwe; `FieldScene::playPartnerClip(clip, true)`, que
lo mueve por `Field::moveByClip` como las animaciones de los guiones). El campo
sigue (los NPC se mueven) mientras ellos juegan sus animaciones y durante los dos fundidos; solo espera con el menú de
dormir. El fundido de entrada enseña al compañero de pie (animación 0) y al jugador en la última pose de la 8, hasta
que recupera el control. Los números de las animaciones están en `care.yaml` (`sleep.scene`). Medido con un Agumon en
la ciudad (estado 3 del compañero escrito en RAM).

Igual: sin HUD durante la escena. El original para el tiempo de juego en el primer fotograma de la escena
(`partnerTickSleep` estado 0, `stopGameTime`) y no lo vuelve a poner en marcha hasta que el fundido de entrada pasa de
34 (`startGameTime` con `FADE_IN_CURRENT` 35), y `clock.c: renderGameClock` saca el reloj y las barras mientras el
tiempo está parado (50 píxeles por fotograma, de -135 a -220). Medido en twna01 de noche (07_field_night): el menú ya
había sacado el reloj (-220); al elegir «Dormir» el menú pone el tiempo en marcha un fotograma (-170) y la escena lo
para enseguida; el reloj vuelve a entrar con el fundido en 35. En dwe el campo sujeta al jugador con el tiempo parado
(`Field::holdForCare(true, CareClock::Stops)`), así que `gameTimeRan` es falso y el HUD (reloj, «Tab Menú» y barras)
se va desde que se elige «Dormir» hasta que el jugador recupera el control; el fotograma suelto del original no se
copia.

Igual: el compañero no se duerme solo. Con sueño hace su gesto (animación 0x11, `updateConditionAnimation`) y, si
llega la hora de despertar sin dormir, es un descuido (`tickSleepMechanics`); en el original no hay cama ni otro
sitio donde dormir que este menú.

Hueco: el original, si el compañero está fuera de la pantalla al empezar, lo pone en la casilla fuera de pantalla más
cercana al jugador (`tickPartnerWaypoints`); aquí anda desde donde está. Si se queda atascado contra algo, el original
sigue andando contra la pared y aquí se para donde está y sigue la escena.

## El baño

Si el jugador pisa la casilla del baño y el compañero tiene ganas, el compañero va al baño del mapa (`FieldToilet`, en
`field_toilet.cpp`). Es la sección 0x4e2 del original: `call_partner_routine(0)` y `partner.c: tickPartnerToilet`. El
jugador queda quieto mientras corre el tiempo. La cámara va al baño en 20 cuadros. El compañero corre a los dos puntos
del baño (`TOILET_DATA`, que el paquete base trae en `maps.yaml`, `toilets`) y hace allí la animación del baño. Al
acabarla, el compañero hace sus necesidades (`handleToilet`), la cámara vuelve y el compañero vuelve andando al primer
punto. Entonces suena el sonido del baño. Sin `field_items.toilet_walk`, o en un mapa sin puntos de baño, el compañero
va donde está, como antes.

Igual: los puntos de `TOILET_DATA` van en los ejes del original, con la z al revés que el mundo
(`MapToilet::firstPoint`/`secondPoint`, `core::fromOriginalAxes`), y el compañero corre a ellos sin chocar con el
mapa (`Field::walkPartnerTo`: en el estado 6 el clip lo lleva sin `partnerTickCollision`), así que entra en la caseta.
Antes la z no se giraba: en twna02 el compañero corría hacia (−2122, −1930) en vez de (−2122, 1930), se atascaba a unas
3800 unidades y la cámara se iba a un sitio vacío del mapa, así que la animación del baño no se veía. Medido en el
original (2026-10-31, `01_field_outside_jijimon_house`, twna01, el mismo baño 8, condición 8 y el jugador en la
casilla (32, 75)): Agumon corre desde (−1650, −2400) y pasa al segundo punto al pisar la casilla del primero, en
(−2115, −1939), unos 45 vblanks después; llega a (−2251, −1266), ya dentro de la caseta, y hace la animación 10 (51
fotogramas) con la cámara encima; luego vuelve andando (animación 2) al primer punto. dwe con la partida del usuario
(twna02, Agumon): la cámara va a la caseta, el compañero entra, hace la animación 10 sobre el retrete y vuelve, sin
atascos (`dwe_assets_tests`: "a toilet's points turn the original's z the other way into world space"; `field_tests`:
"a care walk to a point goes through walls and ends on the point's cell").

## Felicitar y regañar

«Felicitar» y «Regañar» del menú del campo (Tab) juegan la escena del original (`partner.c: partnerTickPraiseScold`,
`tamer.c: tamerTickPraiseScold`, en `praise_scold_call.cpp`). El menú se cierra, el jugador queda quieto mientras
corre el tiempo y el compañero se acerca hasta quedar a su lado; los dos se miran. Como al dormir, el campo sujeta al
jugador (`Field::holdForCare`) y, si el compañero no está ya a su lado, lo lleva andando en línea recta hasta menos de
2,5 radios (`walkPartnerToRange`; `partner.c: partnerTickPraiseScold` estados 0 y 1, `getPartnerTamerCloseness` igual
a 2). Antes el compañero solo seguía al jugador, y el seguimiento se para entre 2,5 y 3,5 radios: un compañero quieto
a esa distancia no llegaba nunca y la escena no acababa. Al felicitar suena `playSound(0,
14)`, el jugador hace la animación 38 (la 39 si el compañero es campeón o más, salvo Mamemon, MetalMamemon y
Digitamamon, que son bajos) y el compañero la 11. Al regañar suena `playSound(0, 13)`, el jugador hace la animación 7 y
el compañero la 12, o la 25 si el regaño llega justo después de un rechazo o de una caca en el suelo
(`ITEM_SCOLD_FLAG`). Al acabar la animación del jugador cuenta el elogio o el regaño (`handlePraiseScold`: disciplina y
felicidad), el jugador vuelve a estar de pie y la escena termina cuando la animación del compañero acaba su primera
vuelta. Los sonidos de esas animaciones suenan como los de cualquier clip del campo (`FieldScene::playClipSounds`). Las
animaciones están en `field_items.praise_scold`; sin ese bloque, el elogio o el regaño cuenta en cuanto el compañero
llega, sin animaciones, como antes.

Medido en el original (2026-10-30, Agumon junto al jugador en twna01, el campo a 20 fotogramas por segundo):

| Caso | Gesto del jugador | Respuesta del compañero | Cuenta en | Escena | Cambio |
|---|---|---|---|---|---|
| Felicitar | 38, 45 cuadros | 11, 51 cuadros | cuadro 46 del compañero | 50 cuadros | felicidad +7, disciplina -5 |
| Regañar | 7, 17 cuadros | 12, 41 cuadros | cuadro 18 | 41 cuadros | felicidad -10, disciplina +2 |
| Regañar tras un rechazo | 7, 17 cuadros | 25, 17 cuadros | cuadro 10 | 25 cuadros | disciplina +8, felicidad +3 |

En dwe las mismas animaciones con sus sonidos (la 11 suena en sus cuadros 12, 23 y 39; la 12, en el 10) y el mismo
orden: el gesto, el elogio o el regaño al acabarlo y el final con la respuesta.

Diferencias con el original: las animaciones empiezan en el mismo tick en que el compañero llega; en el original, el
del jugador empieza un fotograma después (el compañero pone al jugador en el estado 13 o 9 y el jugador lo atiende en
su siguiente tick). Una evolución especial tras el elogio o el regaño (`handleSpecialEvolutions(3)`) espera a que acabe
la escena, como en el original. El regaño tras un rechazo cambia la felicidad con un valor sin inicializar en el
original (+3 en las dos medidas); dwe usa 0 (`care.yaml`, `boosted_scold_happiness`).

## Las cacas en el suelo

`FieldPoop` (`field_poop.cpp`) coloca en el mapa los montones que el compañero deja (`partner_impl.c: createPoopPile`):
con el campo libre, cada montón pendiente de la sesión (`PoopAdapter`) va a la casilla delante del compañero según hacia
dónde mira, o a una vecina al azar si ya hay uno. Cada fotograma pasa a `FieldScene::setProps` los montones del mapa: el
modelo de `poop_piles.look.model` en el centro de su casilla, a la altura del compañero y a la escala de su tamaño entre
`model_size` (`renderPoop`). Sin la sección `poop_piles` los montones se guardan pero no se ven.

Color (Igual, medido en `twna01` con un montón puesto por RAM en `WORLD_POOP`): el montón es marrón y opaco
(texels 148,99,41 / 132,74,24 / 115,57,8 de la paleta (96, 501), sombreado gouraud cerca de 1.0) con una sombra que
oscurece el suelo. `ETCNA/UNTI.TMD` marca sus primitivas semitransparentes en modo resta, pero la consola solo mezcla
los texels con STP: el montón no tiene ninguno y la sombra sí. Lo resuelve el extractor (versión 9, ver
[semitransparencia por texel](../../formats/modelos.md)); con un `unti.glb` de antes el montón sale negro. La caca
gigante de `gomi02` (`ETCNA/TRY.TMD`) no es semitransparente y no cambia.

`FieldStoryProps` (`field_story_props.cpp`) dibuja lo que la historia pone en el campo además del mapa y sus personajes
(`session/story_props.hpp`): la vagoneta del Drill Tunnel delante del compañero, a su radio más 100 y girada como él
(`map_object.c: renderDirtCartModel`), el montón de tierra de tunn05 a la escala de su tamaño y la roca de tunn06, todos
con `FieldScene::setProps` (un juego de sitios por modelo); los quita al entrar en otro mapa (`unloadMapParts`).
Lleva el fundido a blanco de las rutinas 45 y 46 (`game::field::WhiteFade`) a 20 fotogramas por segundo y lo pasa a
`render::ScreenFade::setWhiteness`; el tono sale del de `Field` y vuelve a él, porque el original comparte `FADE_PROGRESS`
entre los fundidos, y un cambio de mapa quita el blanco (`fadeToBlack` borra el objeto 4005).
La carga del jefe final de la rutina 40 la pasa a `BossArrivalScene` (`boss_arrival_scene.cpp`), que lleva
`game::field::BossArrival` a 20 fotogramas por segundo: quita al jefe de lo dibujado (`FieldScene::setHiddenNpcs`, el
`isOnMap = 0` de `EAB_startBuildup`), dibuja los anillos de `ETCHI/BOSS_EFE.TMD` girados media vuelta donde está, a la
escala de cada fotograma y teñidos a 0x3c/0x80 (`EAB_setModelColor`, con el tinte de `FieldScene::PropPlace`), y pone
el zumbido retenido de la ranura 8 (`playSound2(8, 0)`); al entrar en otro mapa lo quita todo.
La rutina de compañero 8 (el estado 18 del jugador en el original) la juega la misma `BossArrivalScene` desde `StorySceneGlue`:
la llegada pone un `FieldStage` con la pantalla restada a negro (`EAB_renderFlash`, un cuadro sustractivo delante de
todo), el escenario, el jugador y el compañero ocultos y solo el jefe (`FieldStage::shownNpc`) con sus anillos
(`shownProps`), la cámara girando alrededor del hueso raíz del jefe (`EAB_calculateCameraOrbit`, con la proyección del
mapa) y las partículas como cuatro líneas lejanas; pone las animaciones 0x21 y 0x33 del jefe como un `play_animation`
y los sonidos 1, 2 y 3 de la ranura 8. Al acabar devuelve el escenario y la historia sigue
(`StoryScenes::partnerSceneFinished`). Prueba con la ruta `_work/routes-e2/boss-arrival/route-mgen99.yaml` (partida
nueva en mgen99 con todas las etapas): carga, llegada y combate contra el jefe.
Mueve el temblor de la rutina 34 (`screen_shake.hpp`: `ScreenShake`) a 20 fotogramas por segundo, lo pasa a la cámara
(`MapRenderer::setShake`) y dibuja su cuadrado rojo sustractivo sobre la escena con `FieldScene::setStoryScreenPrims`
(delante del mapa y de los personajes, detrás de la interfaz), y lleva un
sprite del mapa al nodo de un personaje (`spawnSpriteAtEntity`, con `FieldScene::entityNodePoint` y
`MapLayers::spriteSize`). La burbuja de humor de la rutina 28 (`mood_bubble.hpp`: `MoodBubble`) crece y sube a 20
fotogramas por segundo y se dibuja con `FieldScene::setStorySprites`: un cuadro aditivo de la imagen
`map/<mapa>/texture4/000` en el punto de condición de la especie (`conditionFx` de su fila y `entityNodePoint`); con
un contenido extraído sin `condition_fx` no sale.

Una caca en el campo tiene su escena (`partner.c: tickWildPoop`, estado 7), con `poop_piles.wild`: el jugador se para y
mira al compañero, que corre hacia él hasta su distancia de correr (`Field::runPartnerUp`), se agacha (`squat_clip`),
deja el montón delante de él y hace `after_clip`; luego el jugador queda libre. La escena empieza cuando el compañero
tiene una caca esperando (`Partner::wildPoopDue`); el montón y sus efectos (error de cuidado, ánimo, peso) llegan al
dejarlo (`GameSession::leaveWildPile`, `handleWildPoop`), y la posible bajada del nivel de domador al acabar
(`GameSession::finishWildPoop`, subestado 3), como en el original. Sin `poop_piles.wild` todo llega a la vez. La caca de
una noche de sueño sale sin escena (`handleSleeping`). Con `poop_piles.eat`, un compañero que come cacas (`care.toilet.poop_eaters`) busca con el
campo libre un montón del mapa con el jugador a menos de `small_reach` de su centro en x y en z (`large_reach` desde
el tamaño `large_from`; `tickPoopDetection`), va andando a él, hace `eat_clip` y se lo come (`handleEatingPoop`). Las
dos escenas dejan correr el tiempo y cierran el menú de pausa.

## La escena de evolución

`EvolutionScene` (`evolution_scene.cpp`) juega `evl.c: EVL_tickEvoSequence` cuadro a cuadro: el mapa, el jugador y los
demás se funden a negro en 0x20 cuadros (`EVL_fadeClutBank0/1`, `FieldStage::sceneryDarkness`, que oscurece el fondo,
las capas y los modelos), la cámara gira alrededor del compañero y en el cuadro 0x6A empieza el estallido. Los efectos
van en `EvolutionEffects` (`evolution_effects.cpp`, sin estado del motor, con pruebas) y se dibujan como primitivas del
original sobre la escena (`ScenePrimDrawer`, `scene_prims.cpp`):

- Cuadrados verdes en el suelo a los pies del compañero, nueve de cada 18 cuadros (`EVL_D_80063F28`, `EVL_spawnParticle`,
  `EVL_tickParticle`): crecen de 8 a 0x9C4, se apagan en 0x56 cuadros y van detrás de todo.
- Los huesos se rompen en el orden que se sortea al empezar (`EVL_initEvoSequence`, `rand() & 0xFFF` por hueso): los
  de la tabla `EVL_D_80063EFC` y luego uno por cuadro de 0x9B a 0x11B; los huesos sin objeto se saltan. Cada rotura
  suena (ranura 8, efecto 2 a 4), pasa el hueso a alambre, lanza sus caras como trozos 0x1F cuadros
  (`EVL_buildShardSet`, `EVL_tickShardSet`), un rayo desde el hueso 1 que dura hasta el cambio (`EVL_tickSpark`,
  `DEFEFF` paleta 1, aditivo) y un destello (`EFECreateFlash`, modo 1).
- El cuerpo se aclara de 0x70 a 0xCD con un factor al azar por canal (`ModelInstance::lift`).
- En los cuadros pares de 0xD7 a 0x11B, destellos grandes sobre el hueso 1, delante o detrás según el cuadro
  (`SPOT256`, `renderEFEFlash`; su seno es el de `math.c`, 512 por vuelta).
- En 0x11C cambia el modelo; la forma nueva sale en alambre y se rellena de 0x126 a 0x14E (`renderWireframed`,
  `WIREFRAME_RNG_TABLE`); en 0x162 se repone el campo.

Medido en el original (forzado con la hora en `twna01`) y comparado por fotogramas con dwe en las mismas fases.

Suposiciones:

- Los objetos avanzan el mismo cuadro en que nacen.
- Las caras con textura del alambre no llevan luz; el aclarado no se aplica a los trozos.
- El orden de las caras dentro de un hueso es el de los grupos del modelo exportado.
- Los números al azar de los efectos usan una semilla propia, no la del juego.
- Los cuadros de los efectos son constantes del código, no del ruleset.

## Efectos del campo

`FieldEffects` (`field_effects.cpp`, sin estado del motor, con pruebas) corre los efectos propios del campo un cuadro del
original a la vez (los ticks lógicos del campo, `transition.frameSeconds`) y `FieldScene` los dibuja con las mismas
primitivas que la evolución (`field_scene_fx.cpp`). Las texturas son las de `ETCDAT/ETCTIM.BIN` con su bit STP
(`stpSceneImage`): solo los téxeles con STP se suman (`ScenePrim::stpMask`); el resto sale opaco. Todo se borra al
cambiar de mapa.

- Nubes de humo y polvo (`efe.c: createCloudFX`, `tickCloudFX`, `renderCloudFX`): 14 cuadros, altura, tamaño, columna
  y tono de sus tablas; hasta 60. Las piden el gimnasio (`GymPorts::addCloud`, `TRN_createCloudFXLine`) y cada caca que
  sale (`createPoopPile`).
- Chispas del gimnasio (`createParticleFX(0, 0, …)`, `tickParticleFX`, `renderParticleFX`): 0x13 cuadros, el destello
  (`SPOT256`, dos) solo en el segundo y 25 partículas que se abren con el seno de 512 por vuelta; hasta 4.
- Partículas de curación (`particle.c`): 44 cuadros; una espiral que baja desde encima de la cabeza (una partícula por
  cuadro durante 20) y, con los objetos grandes, un anillo de 4 por cuadro. Qué objetos las lanzan y cuáles llevan
  anillo está en `inventory.yaml` (`effects.healing_particles`); la medicina solo cuando cura la enfermedad.
  `InventoryAdapter::takeHealingParticles` las pasa a `PlaySession`.
- Mariposa (`butterfly.c`): mientras la única condición del compañero es estar distraído y está en su estado normal;
  vuela en círculo alrededor del punto de `CONDITION_FX_OFFSETS` de su especie (tabla de especies: `condition_fx`, un nodo
  del modelo y un desplazamiento) y bate las alas cada 2 cuadros.
- Humo de un personaje que desaparece (rutina 38, `overworld.c: createNinjamonEffect`, `renderNinjamonEffect`):
  `ObjectSmoke` mueve los 41 primeros objetos del mapa a la derecha y abajo con pasos al azar, los devuelve a su sitio
  al pasar de x 160, los pone en la entrada 10 de la tabla de orden (`MapDepthLayers::setOrderValue`) y los esconde a
  los 60 cuadros.
- Niebla de Misty Trees y de dos pantallas de la cloaca (`overworld.c: renderMist`, que llama `renderMapOverlays` en
  las pantallas 115-119, 121, 0xa3 y 0xdc): `MistLayers` (`field_mist.cpp`) y `FieldScene::addMist`. Dos capas de la
  misma imagen, los 256 x 200 téxeles de la página de 4 bits en x 0x2c0 de la VRAM (la segunda imagen de 4 bits del
  mapa, o la única en la cloaca), estiradas sobre la pantalla de 320 x 240 y repetidas: la de atrás (paleta 0, tono
  0x96, B + F/4) se mueve con la cámara y deriva 1 px a la izquierda cada dos cuadros; la de delante, en espejo
  (tono 0x50, B + F), se mueve 12/10 de la cámara (truncado por cuadro, como el original) y deriva a la derecha; las dos
  filas siguen solo los movimientos verticales de la cámara. Va delante del mapa, los personajes y sus efectos (entrada
  20 de la tabla de orden) y detrás de la interfaz. La paleta de la capa de delante es la de `MIST_CLUT_Y` (columna /
  16: 1 en mist01-04 y 07, 5 en mist05) y la da la historia (`StoryScenes::mistOn`, `story_props.hpp:
  mistThisFrame`): el disparador 0x94 la deja en 8 / 12, sin color, y solo queda la capa de atrás; en la cloaca las dos
  usan la paleta 0 y el disparador 0x155 quita la niebla. Entre dos cuadros del original las capas siguen a la cámara
  para no temblar contra el fondo; en una ventana más ancha la niebla llega a sus bordes y no a las barras.

| Niebla | Original (medido 2026-11-03, mist01 y mist05) | dwe | Estado |
|---|---|---|---|
| Deriva parado | `MIST_X_OFFSETS[0]` -51 y `MAIN_D_80134228[0]` +51 en 100 cuadros de `PLAYTIME_FRAMES`; sigue con el menú abierto | -1 / +1 cada cuadro par (10 px/s) | Igual |
| Cámara | cámara +160 x, -47 y en 21 cuadros: capa de atrás -171 (-160 y la deriva), delante -177 con la deriva (12/10 truncado), filas +47 | misma cuenta (`field_mist_tests.cpp`) | Igual |
| Paletas | `MIST_CLUT_Y` 0x50 / 0x10 al llegar; 0xc0 / 0x80 unos cuadros después de poner el disparador 0x94 | 5 / 1 y 12 / 8 | Igual |
| Orden | cubre al jugador y al compañero; el reloj, las barras y el menú quedan encima | igual (contact sheet en `_work/tmp/w19-fog/`) | Igual |
| Al entrar en un mapa | la cámara anterior (`CAMERA_X_PREVIOUS`) da un salto de fase; se envuelve un paso por cuadro | igual, con la cámara de dwe | Suposición (no medido) |

- Reinicio de la animación de objetos del mapa (rutina 21, `overworld.c: resetMapObjectAnimation`):
  `restartObjectAnimations` (`story_scene_glue.cpp`) pone a cero el reloj y el cuadro de los objetos 2D del mapa desde
  la variable 247, tantos como dice la 248 (`MapDepthLayers::restartAnimation`). Las chispas de la descarga de
  Elecmon (`gias02`, objeto 41) salen así desde su primer cuadro.
- Cifras sobre el compañero en el campo (`entity_text.c: addEntityText`, `setCombatTextPosition`,
  `renderEntityText`): `FieldNumbers` (`field_numbers.cpp`) toma el daño que hizo `deal_damage`
  (`StoryGameOrders::takePartnerDamage`) y lo pone donde estaban los pies del compañero al añadirlo (en el campo no lo
  sigue); avanza una vez por cuadro del campo (1/20 s) con el mismo bote y la misma cola que las de batalla
  (`screens::DamageNumbers`), 31 cuadros. El documento `field_numbers` usa el estilo de las cifras de batalla de cada
  aspecto. Un combate que empieza las quita.
- `deal_damage` (`script_interp.c`, `scriptPauseGame(0xff)` antes del daño) congela el mundo en cuanto hace daño, no al
  girar los personajes: así la caída del jugador (animación 17) y la del compañero (40) llegan al final.

Medido en el original (2026-11-03, descarga de Elecmon en `gias02`, script 68, sección 5, tres veces): cada descarga
quita el 20 % de la vida máxima (800 → 640 → 480 → 320) y enseña "160" sobre el compañero desde unos 10 vblanks
después hasta los 94; a los 104 ya no está (31 cuadros del campo ≈ 93 vblanks). Las chispas salen pequeñas a los 10,
grandes a los 18 y se van antes de los 30. El jugador retrocede unas 400 unidades y el compañero unas 380, y los dos
caen; la cifra sigue a la vista con el cuadro de texto abierto. dwe: la cifra se ve hasta +94 y no a +106, el jugador
retrocede 396 y el compañero 383, y las tres descargas dan 640, 480 y 320. Igual.

Las chispas (objeto 41 de `gias02`, cinco cuadros de 1 fotograma) siguen el orden de `renderMapOverlays`: el script las
reinicia (rutina 21), ese fotograma se dibuja el primer cuadro y solo después se cuenta, así que dura un fotograma entero;
`MapDepthLayers::restartAnimation` reinicia también el reloj de 20 fotogramas por segundo, que antes podía pasar al segundo
cuadro en el mismo fotograma. Medido en dwe tras el arreglo (2026-11-04, vblank a vblank desde la tecla): la cifra y la
chispa pequeña a +6, la grande de +13 a +15, ya no están a +19; en el original la cifra y la chispa pequeña a +8..10, la
grande a +16..18 y ya no están a +28. De la cifra a la chispa grande, 7 frente a 8: Igual dentro de un fotograma del campo.
Suposición: toda la escena empieza unos 2 a 4 vblanks antes que en el original (la tecla llega antes al script, no las
chispas), y el cuadro de texto se abre antes (+30 frente a +44..64; depende del giro de Elecmon, no medido). Las cifras de curación con objetos
en el campo (`item.c`, `addEntityText`) aún no salen.

Medido en el original (2026-10-31): las nubes y chispas del gimnasio (`TRN_tickDefenseTraining`, estado
`trn_def_normal_bonus`) las crea una rutina de la lista de objetos y `tickObjects` las avanza el mismo cuadro, así que
se dibujan por primera vez en el estado 1 (la chispa enseña su destello en el segundo cuadro dibujado y dura uno menos);
la nube de una caca (`createPoopPile`) se dibuja primero en el estado 0. `FieldEffects::Birth` lo distingue. Igual: la
partícula 0 del anillo de curación divide 512 entre 0 (`512 / (i * 13 / 4)`); el código del original no comprueba el
divisor (`div` sin `break`) y la división del procesador da -1 con un dividendo positivo, que es lo que se toma. Igual: las
50 direcciones de las partículas (`initializeParticleFX`) son las que el original tira al arrancar, medidas en
`FX_PARTICLE_DATA` (las mismas en una partida del campo y en una de combate).

Suposiciones:

- Los números al azar de los efectos usan una semilla propia, no la del juego.
- Una especie nueva sin `condition_fx` no tiene mariposa.
- La mariposa no se ha medido en pantalla (el estado de solo enfurruñado no se pudo forzar por RAM); sus números son
  los de `renderButterfly`.

Objetos del suelo y cofres al cambiar de mapa (#176): `map.c: changeMap` borra los objetos sueltos y los cofres
(`clearDroppedItems`, `unloadMap`) al cargar el mapa nuevo, antes de su guion de cabecera, que pone los del mapa nuevo.
`PlaySession` llama a `FieldItems::enterMap` con el mapa nuevo justo tras `FieldScene::finishTick`, en el mismo punto.
Antes solo lo hacía `FieldItemsScreen::tick`, que no corre hasta que acaba el fundido de entrada: durante ese fundido
`FieldItemsScreen::show` seguía dibujando los objetos y cofres del mapa anterior en sus coordenadas, encima del mapa
nuevo. Prueba: `field_items_tests` "map.c changeMap: another map clears the last one's ground and chests, its own
stay"; en la captura cuadro a cuadro de room08 → twna01 con una carne soltada en room08, antes se veía sobre la hierba
de twna01 los 20 cuadros del fundido de entrada y desaparecía en el último; ahora no sale.

## Escena final

`EndingScene` juega la última escena (`call_partner_routine 6`, que pide la rutina 53; en el original, el tick final del
jugador con `endi.c`). La lógica es `EndingEffects`, a 20 fotogramas por segundo: el jugador
se dibuja con mezcla aditiva (`FieldStage::playerAdditive`) y sus colores bajan a negro del fotograma 1 al 60
(`ENDI_fadeClut`) mientras sale una chispa por fotograma sobre un hueso (80 en total); del 60 al 78 encoge a 0x999 y
crece a 0x1800 en el 80. Ahí suena el sonido 1 del banco de la rutina 52, las chispas dejan los huesos y suben juntas a
450 unidades por fotograma, 20 menos cada uno, hasta el 103 (5290 en total). En el 104 se para el sonido y desaparecen;
al fotograma siguiente la historia sigue (`SOME_SCRIPT_SYNC_BIT`), la rutina 53 acaba la partida y suena EDR. Las
chispas se dibujan como las partículas del campo (`FieldEffects::setGlows`, `renderFXParticle`), con color y tamaño
al azar en cada fotograma.

- Suposición: el orden del primer fotograma (el arranque y el primer avance van en fotogramas distintos) y las cifras
  están sacadas del decomp; falta medirlas en el original (`_work/tmp/measure-queue.md`).
- Falta: la sombra del jugador, que el motor aún no dibuja (`PLAYER_SHADOW_ENABLED = 0`).

## Muerte y renacer

`DeathScene` juega la vida perdida (`murd.c: MURD_tickScene`), la muerte (`dooa.c: DOOA_tickDissolve`) y el renacer
(`DOOA_tickRebirth`). El mapa donde despierta el tamer se elige al empezar la escena (`map.c: setDeathMap`) y se entra
en él a mitad (`changeToDeathMap`); la carga del mapa en segundo plano no existe aquí. Sin historia, o si la historia no tiene `story_scenes.death`, empieza con los eventos del compañero
y retiene el campo. En la vida perdida, a la vez que el compañero se queda solo en negro, sale sobre él la caja de vidas
(`MURD_createLivesBox`, documento `ui/lives_box`, modelo `screens::LivesBox`): crece desde el compañero, enseña un
corazón por vida con la que se empieza (`life.lives`), con la recién perdida llena, parpadeando y vacía, y se quita al
acabar el fundido a blanco (`MURD_removeLivesBox`). Si era la última, la caja vuelve a encogerse hacia el compañero
(`MURD_animateLivesBoxOut`) y la muerte sigue cuando ya no está. Con ella, la historia pide cada escena con `call_partner_routine` (9 y 7, vida perdida; 3, muerte;
4, renacer) y `StorySceneGlue` se la pasa: el campo sigue y lo retiene el guion. La muerte acaba con el compañero
ya ido, en negro. Entonces corre la sección del mapa de despertar: los textos, la caja de los huevos (`EggBoxScreen`,
documento `ui/egg_box`) y la rutina 4, que da al compañero su forma nueva y juega el renacer. Izquierda y derecha cambian
de huevo y confirmar elige uno; no se puede salir sin elegir. Lo que no está: la carga del overlay.

- Igual: con el aspecto original la caja crece en 5 cuadros a 20 por segundo desde el cuadrado de 16 píxeles del centro
  de la pantalla (`DOO2_openEggBox`: `createAnimatedUIBox(3, …)` desde `MAIN_D_80134B98` {-8, -8, 16, 16}) y, al elegir,
  encoge en otros 5 de vuelta a él (`removeAnimatedUIBox(3, MAIN_D_80134BA4)`), con el contorno gris 0x808080 de
  `renderUIBoxAnim` (`ui/common/original_box.lua`). Mientras crece no se mueve ni se elige huevo (`DOO2_tickEggInput`
  espera a que `DOO2_tickEggBox` ponga `sparkleIndex` a 0); al abrir no suena el sonido de caja sino el suyo
  (`stopSound`, `playSound(8, 4)`). La historia sigue cuando la caja ya encogió (`egg_box_closed`; el original devuelve 1
  con `UI_BOX_DATA[3].state` 0). Con otro aspecto la caja abre y cierra al momento.
- Igual, medido en el original (2026-11-03, estado `death_egg_box_open`): con el aspecto original la caja es la de
  `UI_BOX_DATA[3]`, -84, -70, 168 x 80 desde el centro de la pantalla (`MAIN_D_80134B90`; x 76 a 244, y 50 a 130 de
  320 x 240), semitransparente (`features` 2, color 0: `.original-box`), sin título ni textos: la pregunta es del
  diálogo de debajo. Dentro, los 4 huevos de `ETCDAT/TAMA.TIM` en fila (`DOO2_renderEggIcons`): 32 x 32 centrados en
  x -54, -18, 18 y 54 e y -30, el dibujo `EGG_ICONS` {0, 2, 3, 1} (u = 96 + 32 × dibujo) con la paleta de la fila
  492 + huevo, y el cursor de 40 x 40 (u 96, v 32, fila 496) sobre el huevo elegido
  (`ui/skins/original/egg_box/style.rcss`). Solo izquierda y derecha mueven el huevo (botones 0x8000 y 0x2000). El
  documento es `own-sounds`: sus teclas no suenan a menú; solo suenan los sonidos de la caja que manda el juego
  (abrir 8/4, mover 0/2, elegir 0/3) y el cierre de la caja (0/1).
- Diferencia de 4 píxeles: en la captura del original el borde superior de la caja sale en la línea 46, no en la 50
  del rectángulo; dwe la pone en la 50.

Los efectos de las dos escenas van en `DeathEffects` (`death_effects.cpp`, sin estado del motor, con pruebas) y se
dibujan como primitivas sobre la escena (`ScenePrimDrawer`), con las piezas de la evolución (`addFlashPrims`,
`addWiredFaces`, `SortedPrim`):

- El iris (`MURD_renderIris`, `DOOA_renderIrisWindow`): el anillo de `SPOT256` y cuatro rectángulos de su texel
  blanco, restados (`ScenePrimBlend::Subtractive`), cierran de 200 a 0 en 0x23 cuadros (0x21 en la muerte) sobre la
  mitad del compañero. Cuando el anillo ya cabe en la caja del compañero (`MURD_isDigimonLargerThanIris`), el
  compañero se dibuja encima.
- La cámara de la muerte (`DOOA_updateCutsceneCamera`, `DOOA_getOrbitPosition`): de 0x23 a 0x55 gira hacia el
  compañero desde la vista del mapa viejo; en 0x56 el compañero pasa al origen del mapa de despertar mirando al jugador y
  la cámara queda a altura × 5 + 1200, 400 + 2 × altura por encima, a 0x638 de su giro, con distancia de proyección
  1000; de 0x149 a 0x17B vuelve por el mismo camino a la vista del mapa. En esa segunda parte el centro de la imagen
  parte del centro de la pantalla (0xA0, 0x78) y no del desplazamiento del mapa; en el mapa de la historia (0xCD)
  parte de 0xBC, 28 píxeles a la derecha (`DOOA_tickDissolve` fase 0, `wake_map.story_map_start_x`).
- De 0x69 a 0x8C el cuerpo pasa a alambre en olas (`DOOA_SHARD_WAVE_SCHEDULE`); cada ola suelta como trozos las caras
  de los huesos desde el 2 que le tocan en `WIREFRAME_RNG_TABLE` y que `rand() & 3` deja (`DOOA_spawnShardWave`,
  `DOOA_spawnBoneShards`); los trozos caen al suelo (`DOOA_updateShards`).
- De 0xC0 a 0xD0 el compañero se aplasta y estira con `DOOA_DISSOLVE_SCALE_CURVE` y, pasado 300, salta altura + 100.
- De 0xD2 a 0x135, un destello por cuadro baja desde encima de la cabeza hasta el suelo meciéndose 10 a cada lado
  (modo 0, 8 cuadros, gris 60 + `rand() % 100`); en 0x135 los trozos empiezan a parpadear. Igual, medido en el
  original (2026-10-31, estado `death_dooa_light_start`): 7 destellos vivos a la vez (progreso 1 a 7), profundidad
  2512 y distancia de proyección 993 en el primero, escala 84, 167, 240, 298, 336, 358 (mitad de 6 a 23 píxeles);
  en pantalla, halo de 40 píxeles y centro blanco de 20 en 320 × 240. dwe da las mismas escalas y, a 1280 × 720,
  122 y 64 píxeles (41 y 21 del original). Antes salía tres veces más pequeño: con el campo oculto el mapa nuevo
  de `changeToDeathMap` no se preparaba y su cámara se quedaba en 320 × 240, así que las primitivas de la escena se
  dibujaban a escala 1 (`MapRenderer::setMap` conserva ahora el tamaño de la ventana).
- De 0x185 al final (`DOOA_tickDissolve` fase 0x66: `ENTITY_TABLE[2]->isOnMap = 1`) el primer personaje del mapa de
  despertar (el anciano en su casa) vuelve a verse sobre negro, y sigue durante los textos, la caja de los huevos y
  el renacer hasta que todo vuelve en el cuadro 152 (`life.yaml`: `death.host_frame`). Medido en el original
  (2026-11-03): con la caja abierta solo el anciano (entidad 2) está en el mapa; el jugador y los demás, no.
- En el renacer los trozos suben en espiral en el cuadro 10 y se cierran sobre el huevo en el 100 (`DOOA_setShardState`
  1 y 3). El huevo de `TAMA.TMD` (el del bebé, 6 piezas) sale en alambre desde el cuadro 1 y se rellena de 101 a 131;
  en 132 se quitan los trozos y hasta 152 hay un destello por cuadro sobre el huevo y rayos (`DOO2_renderSparkStreak`).
  En 152 sale el bebé, oculto, y el mapa vuelve de negro hasta 202 (`FieldStage::sceneryDarkness`). Desde 203 el huevo
  se rompe en los cuadros 207, 217, 227, 237, 247 y 248 (`DOOA_SPARKLE_BONE_IDS`); cada pieza suena (ranura 8,
  efecto 7) y sus caras vuelan 41 cuadros (`DOO2_buildShardSet`, `DOO2_tickShardSet`).
- Los sonidos de la escena se paran donde el original llama a `stopSound` (0x8C, 0xBF, el final y el cuadro 10 del
  renacer).

Suposiciones:

- Mientras el compañero se dibuja sobre el iris, el resto de la escena ya no se dibuja (el original solo vuelve a
  dibujar al compañero encima): así nada más cercano que su espalda tapa el anillo.
- Si la historia juega la muerte, el bebé y su huevo se toman del compañero ya renacido al empezar el renacer.
- Los números al azar de los efectos usan una semilla propia, no la del juego.

## La máquina de guardado

`SaveMachine` (`save_machine.cpp`) abre la máquina de la casa del anciano cuando el guion llama a la rutina 48
(`StorySceneGlue::takeSaveMachine`; el guion sigue al cerrarse) o con `save_machine("")`. `SaveMachineFlow`
(`save_machine_flow.cpp`) lleva los menús de `main_menu.c: tickMainMenu` 0x3C a 0x46 sin interfaz: registrar al
compañero en una de las 39 ranuras (`session::BattleRegistry`), «¿Quieres guardar?», el aviso sin ranura (se cierra
solo a los 0x79 cuadros del original) y el de por debajo de Novato. Guarda con `GameSaves`, como al dormir, y deshace el
registro si el guardado no se escribe. Si se abre mientras una escena de cuidados retiene al jugador
(`GameSession::savesWait`), la apertura espera y la máquina sale al acabar la escena (regla de seguridad de dwe,
[session](session.md#paridad-con-el-original)). El documento `ui/save_machine` dibuja las cajas del original donde el original
las dibuja; el cursor se para en los extremos y solo suena al moverse. Detalle en
[guardar-y-cargar.md, 3.7](../../architecture/guardar-y-cargar.md#37-máquina-de-guardado-y-fin-del-juego).

## Apertura de la partida nueva

Una partida nueva empieza como el original (`main.c: main`, estado 0): las preguntas de Jijimon, la película y el
primer mapa. `NewGameInterview` porta `newGameScene` (`main.c`), `loadNewGameScene` y `tickNewGameJijimon`
(`new_game.c`) y `newGameStateMachine` (`shop/script_menu.c`), con la sección `new_game_scene` del ruleset: Jijimon
entra andando sobre negro y, al girarse, pregunta si el jugador tiene Digivice, después día o noche (si dice que sí)
o si tiene amigos (si dice que no); la primera respuesta da Agumon y la segunda Gabumon. Luego pide los dos nombres
en la caja de nombres (`NamingBox`, documento `ui/naming_box`), con su confirmación, y se funde a negro. El
resultado va en `PlayStart::choice` hasta `GameSession::startNewGame`. Las frases son las de la tabla 1238 de
MAPHEAD y se ven en el documento `ui/story_textbox`, arriba de la pantalla como en el original
(`setupNewGameDialogueBox`, ajuste `story_textbox.top`). En las elecciones (sí/no, día/noche) la primera fila es el
nombre recién dado, en azul, como en el original: vacía antes de nombrar a nadie, la del jugador tras su nombre y
la del compañero tras el suyo, aunque la línea 0x1C sirva a las dos (capturas cp05-name-right-yesno,
cp06-partner-name-yesno). Mientras corren la película, la
entrevista o una caja de nombres, `menu` no abre el menú de pausa (allí salta la película o lleva el cursor a
Aceptar), como el original, que no tiene pausa en esas pantallas. La película también se salta con `confirm` o
`cancel` (Intro, Espacio, Esc): mejora para PC, el original solo lee Start; la pulsación que la arranca (la última
respuesta de la entrevista) no cuenta. La misma caja de nombres sirve a la rutina 32 de
los guiones (renombrar al jugador o al compañero, `StoryNaming`). No corre en un autotest, como las películas, salvo
que la ruta diga `new_game_scene: true`.

Tras el «sí» al nombre del jugador se pasa directo a «What's your Digimon's name?» (`newGameStateMachine`: estado
0x18, línea 0x1D); «Good Name.» (línea 0x1F) solo sale tras el nombre del compañero (estado 0x1C).

Antes de la entrevista, la ranura vacía pregunta «Record new adventure?» con el cursor en «Sí», como el original
(`SaveDialog::startOnYes`, ajuste `dialog.start_on_yes`); las demás preguntas de las ranuras empiezan en «No».

Con la historia y `story_scenes.new_game.chain: original`, el campo empieza en `twna01` salida 9: la cabecera del
mapa carga el guion 164 y la casilla de esa salida corre la apertura, que lleva a `topn01`, el despertar en casa de
Jijimon, y de ahí a File City (todo dentro de la etapa 1 del candado de historia). `seeded` sigue disponible: el
estado de `data/new_game_story` y el inicio de `field/warps`, sin la apertura. `--new-game-chain original|seeded`
elige la cadena de esta ejecución en vez de la del ruleset (`StoryScenes::setNewGameChain`). Un autotest, como deja
fuera las preguntas y las películas, empieza en `seeded` (el campo libre de File City) salvo que la ruta diga
`new_game: original` (hoy solo `route-etapa1`); `--new-game-chain` manda sobre los dos.

La caja de nombres es la americana: dos mitades de 9 filas y 5 columnas (mayúsculas; minúsculas y dígitos), con
«Name», Atrás y OK a su izquierda y el nombre en una caja aparte debajo, con el subrayado amarillo en la letra
siguiente (la piel original las coloca como el original: `ui/skins/original/naming_box`; el original solo dice
«Name», no de quién es el nombre, y esa línea se oculta), y el cursor se mueve como en `namingSelectionUp/Down/Left/Right` (arriba y abajo dan
la vuelta en cada mitad; a la derecha de la última columna se pasa a la otra mitad o a Atrás; desde un botón se entra
por la fila de la Z). Las direcciones repiten al mantenerlas (a los 6 fotogramas y luego cada 2). Confirmar escribe
la letra (un espacio no puede ser la primera); con el nombre lleno reescribe la última y el cursor va a Aceptar;
cancelar borra la última, y con el nombre lleno borra dos, como la versión americana; menú lleva a Aceptar, `alt`
(el círculo del original, que aquí es el botón norte del mando) a Atrás, y `extra` (su cuadrado, el oeste del mando)
borra todo el nombre. En el teclado `alt` y `extra` son letras del racimo `WASD` (`E` y `Q`) que al nombrar
**escriben** en lugar de disparar la acción (`takeTyping` corre antes que `pressButtons`, `naming_box.cpp:137`), así
que esos dos atajos solo valen en el mando; en teclado se llega a «Atrás» con las flechas del grid. A la vez se
puede teclear: las letras de la rejilla y el espacio se escriben (otra tecla suena rechazada) e Intro acepta el
nombre si se ha tecleado algo desde el último movimiento del cursor. El nombre del jugador empieza vacío, como en la
versión americana. Con la opción [nombres largos](#nombres-largos) (encendida por defecto) admite 12 letras en vez de
6.

Los sonidos son los del original (`playSound`, banco 0, sección `sounds`): cursor 2, confirmar 3 (una página, una
elección, una letra, borrar), rechazo 0xB (cancelar una elección, porque la escena tiene puesto el disparador 49; un
nombre vacío; un espacio primero). La escena no tiene música. El texto aparece por páginas enteras, como en el
original, que dibuja la página oculta una fila por fotograma y la muestra de golpe: no hay texto letra a letra en
ningún cuadro del juego. Mientras se dibuja, la escena no atiende pulsaciones.

El nombre propio del compañero sale de `PartnerView::nickname` (los nombres de la partida): en los diálogos, en la
página de estado del menú del compañero (con la especie en su propia línea, `game_menu.c: drawDigimonStatsStrings`;
la piel original la pone en la fila de debajo del nombre, como el original, con los estados a su derecha)
y en las ranuras de partida (`nickname`, junto a la especie, `main_menu.c: drawMainMenuStrings`). El combate, el HUD
y las escenas de evolución y de muerte muestran la especie o ningún nombre, como el original.

Con el ratón (mejora para PC que no cambia reglas, sin opción; el original no tiene puntero): un clic fuera de una
elección pasa la página, como confirmar. En las elecciones de Jijimon, un clic en una opción la elige y el puntero que
pasa por encima lleva el cursor a ella sin sonido; un clic fuera de las opciones no elige nada. En la caja de nombres,
un clic en una letra la escribe, en Atrás o en OK los pulsa, y en «Name» o en una fila vacía solo lleva el cursor; el
puntero que se mueve por la rejilla lleva el cursor con él. Lo mismo vale para los diálogos de la historia
(`docs/modulos/game/story.md`, «El puntero en los diálogos»). Un clic que se queda la UI (un cuadro de texto, una
elección, la caja de nombres) nunca es una orden de andar: `PlaySession::walksByPointer` solo deja que el clic lleve al
jugador cuando el campo lo mueve, está libre, no cambia de mapa y no corre ningún script ni cuadro de texto, y al
quitarle el control (un diálogo, un menú, el gimnasio, el combate, una escena, un cambio de mapa) se suelta el destino
que llevaba (`Field::dropMoveTarget`): antes, un clic para pasar una página de Koromon dejaba un destino que el jugador
seguía al cerrar el diálogo, y lo mismo al acabar un entrenamiento. Al revés, un clic en el suelo lejos del jugador
(a más de dos casillas, `MouseSteering::pressesInPlace`) solo lo lleva allí y no cuenta como confirmar en el campo:
si no, al salir de una casilla de confirmar (la máquina de guardado junto a la puerta de Jijimon) cada clic volvía a
hacer su pregunta. Cerca de él (sobre él, la máquina o quien tiene delante) el clic sigue siendo confirmar y habla o
usa lo que hay. Rutas: `route-mouse-opening.yaml` (la apertura
entera con el ratón) y `route-mouse-dialogue.yaml` (Koromon en el gimnasio, con `--start trai00`).

Diferencias con el original: en el mando, `alt` está en el botón norte y no en el del círculo, que aquí es cancelar;
las letras son de media anchura y no las de ancho fijo de la fuente del original; los registros de combate y de la
arena, que en el original nombran al compañero, aún no existen.

Cierre durante las preguntas: al acabar un autotest el juego se apaga a mano y en orden (la sesión, la UI, los
retratos, el renderizador y por último el dispositivo) antes de dar el veredicto. La escena de las preguntas
(`NewGameInterview`, con los modelos de Jijimon) no estaba en esa lista, así que una ruta que fallaba o acababa
durante la entrevista (o la caja de nombres) destruía el dispositivo con su memoria aún reservada y abortaba con la
aserción de VMA «Some allocations were not freed» (código 134, sin línea FAIL). Ahora se suelta antes que la sesión.
Comprobado con rutas que fallan a propósito: en la entrevista (antes 134, ahora 1 con su FAIL), en el título, en el
combate y con el menú de objetos abierto (1 con su FAIL, sin aserción); al cerrar la ventana o con `quit` del canal
de control ya salía limpio en esos puntos, porque los locales se destruyen en orden inverso.

## Apertura del combate

Todo combate (salvaje, de la historia o de la consola) empieza en `BattleScreen::start` con la fase `intro`: la
apertura del original (`battle_setup.c: BTL_handleBattleIntro`) que lleva `screens::BattleIntro` a 20 frames por
segundo, y que se publica con `battle.intro.*` ([screens](screens.md#apertura-del-combate)). Primero, 20 frames de la
marca de enfado sobre cada enemigo (`btl.c: handleBattleStart`, `setFleeBubble`), donde `PlaySession` lo proyecta:
el NPC del mapa de esa especie en esa casilla, a su altura más lo que sube la marca (`renderFleeBubble`). Después
el rótulo «BATTLE» (sonido 0x10), que vuela, se junta y parpadea, y su estallido (sonido 0x11); cuando la última
pieza sale de la pantalla, la fase pasa a `fight`. Durante la apertura el documento no atiende órdenes, ni la bolsa,
ni la carga del remate. La semilla de las piezas es el número de combates de la sesión.

Diferencias con el original: el rótulo espera 60 frames donde el original espera a que el domador llegue andando a
su sitio (el campo está quieto en el combate) y no salen las marcas de huida de los demás Digimon de la pantalla; la
marca de enfado no se inclina hacia su enemigo ni cambia de tamaño con la distancia. Un enemigo sin NPC en el mapa
(el de la consola, si no hay ninguno de su especie) no lleva marca.

El aviso del menú del HUD («Tab Menu») va con el reloj y las barras del campo (`hud.shown`): no sale en combates,
escenas ni menús, como en el original.

## Aviso de premio

`AwardScreen` (`award_screen.cpp`) es `tamer.c: checkPendingAwards` y `tamerTickAwardSomething`: mientras el jugador
anda libre (sin script, menú, cambio de mapa ni curso del gimnasio) comprueba las medallas (`MedalsAdapter::check`) y,
si hay una medalla o un nivel de domador pendiente, abre el cuadro de diálogo de la historia (`ui/award_notice`) con
las tres líneas de la medalla en el color 7 («Congratulations!», `main.tamer_messages.0005` a `0007`) o la del nivel
(subida en el color 7, bajada en el 3), suena `award_sound` (0, 7) y el campo y el reloj esperan hasta confirmar
(Intro / Espacio; cruz en el mando), que borra los dos avisos. Medido en el original: el cuadro sale con 999999 bits en
twna01, el HUD se oculta mientras está y vuelve al cerrarlo (igual en dwe). Las medallas son los disparadores 0x16c +
medalla que la página del jugador ya lee (`StoryAdapter::hasMedal`).

Igual en el aspecto original: el cuadro del aviso y el del aviso de objetos (`ui/field_item_notice`). Los dos son la
caja de la historia (borde cian del original) en x 30 a 292, y 162 a 221 (`tamer.c`, los estados del jugador del
premio y de coger un objeto: -130, 42, 262 x 59 desde el centro), con el relleno `UI_BOX_COLORS` 0, negro a la mitad
(`createAnimatedUIBox` color 0, rasgo 2; medido: dentro del cuadro el campo queda a 0,46 de su luz), las filas de 12
píxeles desde y 165 (`renderAwardSomethingTextbox`, `renderItemPickupTextbox`: `renderString` en y 45 desde el centro)
y el botón de confirmar de `script_textbox.c: renderUIBox`: 16 x 12 en (239, 43) desde la esquina del cuadro, tres
dibujos de SYSTEM_W (u 0x4C + dibujo x 16, v 0xB7, paleta 12) que cambian cada 4 cuadros del campo
(`TEXTBOX_OPEN_TIMER / 4 % 3`; medido en DuckStation: el contador sube 10 en 30 vblanks, 20 por segundo, y la cruz
del botón cambia de altura cada 12 vblanks): una vuelta cada 0,6 s. El botón es el glifo del dispositivo en uso
(`set_prompt("confirm", true)`, [docs/architecture/ui.md](../../architecture/ui.md), «Glifos de botones»): con un
mando de la familia de formas, el propio botón del original (la cruz de SYSTEM_W); con el teclado, la tecla de
confirmar (Espacio) dibujada al mismo estilo, 17 x 12, que crece a la izquierda desde el borde derecho del botón; con
otro mando, su botón con su letra o su posición; las tres con la misma animación. El de la caja de la historia mientras
espera (`renderScriptDialogueBox` lo dibuja con la misma función y en el mismo sitio) es el mismo. Estilo:
`ui/skins/original/story_textbox/style.rcss`. El aspecto moderno lo dibuja como una pequeña tecla o botón en la esquina
del cuadro que respira (opacidad de 1 a 0,55 en 0,9 s), con el nombre de la tecla o el dibujo del botón.

Igual: el aviso de enfermedad. Cuando el compañero cae enfermo (`partner_impl.c: tickSicknessMechanics`, que pone al
domador en el estado 0x14), el mismo cuadro sale con su nombre en el color 10 y «is Sick!» (`main.partner_impl_is_sick`)
en el color 1, suena `award_sound` y espera a confirmar (Intro / Espacio). `PlaySession` lo anota al ver `GotSick` en
los sucesos del compañero (`noteSickness`); si en el mismo cuadro hay una medalla o un nivel, sale ese, como en el
original, donde el texto de la medalla pisa al de la enfermedad. Medido en el original (DuckStation, la bandera 0x40
puesta a mano con 11 horas de herida): «AAis Sick!» en la primera línea del cuadro; en dwe «CODEX is Sick!». Diferencia
a propósito: el original pega el nombre a la frase sin espacio; dwe pone uno. Los sucesos del compañero (enfermar,
morir, evolucionar) esperan en la sesión hasta que el juego los recoge (`GameSession::takePartnerEvents`): antes se
borraban al empezar cada tick y se perdían en los cuadros que corren más de un tick.

## Exploración libre

`free_exploration.cpp` lleva la [exploración libre](../../architecture/warps-de-script.md#exploración-libre):
la enciende el ajuste `free_exploration` (menú de pausa, guardado en los ajustes del usuario) o `--explore` (solo en
esa ejecución). Cuando el campo para en una casilla con varios sitios, abre `ui/destination_choice` con los sitios
(`exploration_place_name.N`, `exploration_place_map.N`, `exploration_places`, `exploration_story`) y pasa al campo
lo que se elige (`choose_destination`) o quedarse (`stay_here`).

## Ritmo del bucle

El original corre un tick de lógica por imagen y espera tres refrescos (`main.c: gameLoop`, `VSync(3)`): 20 ticks por
segundo. El reloj suma un fotograma por tick y un minuto cada 20 (`clock.c: tickGameClock`), y las animaciones avanzan
un fotograma por tick (`anim.c: tickAnimation`). El juego corre la lógica a 60 ticks fijos por segundo
(`core::FixedStep`, tope de 0,25 s por imagen) y cada sistema cuenta sus fotogramas de 1/20 s; lo que se dibuja entre
ticks (los clips de los personajes, los fundidos, los efectos) avanza con el tiempo que tomó el paso
(`FixedStep::takenSeconds`), con el mismo tope que los ticks, nunca con el tiempo real de la imagen.

| Medida (30 s) | Original (DuckStation) | dwe a 45 fps (vsync) | dwe a 100 fps | dwe a 280-300 fps (sin vsync) | Estado |
|---|---|---|---|---|---|
| Reloj | 20 fotogramas/s, 1,00 min/s | 19,9-20,0 fotogramas/s | 20,0 | 20,0 | Igual |
| Ticks de lógica | 20/s | 60/s (3 por fotograma original) | 60/s | 60/s | Igual |
| Animación | 1 fotograma cada 3 refrescos (20/s) | 1,00 s de clip por s (clips de 20 ticks/s) | 1,00 | 1,00 | Igual |
| Carrera del jugador | 50 unidades por tick, 1000/s | ~995/s | igual (tick fijo) | igual | Igual |
| Combate | 20 fotogramas/s | 200 fotogramas en 10 s | 200 en 10 s | 200 en 10 s | Igual |
| Tras un parón de 2 s (carga, tirón) | lógica y animación juntas | antes: clips +2 s, lógica +0,25 s; ahora los dos +0,25 s | | | Igual |

Las pruebas `apps/game/tests/frame_pace_tests.cpp` corren el bucle a 30, 60, 144 y 240 imágenes por segundo, con
imágenes irregulares (0,5 a 40 ms) y con un parón de 2 s, y comprueban los mismos ticks, el mismo reloj y el mismo
tiempo de clip. Hueco: el clip de un personaje se muestrea por imagen, así que lo que la lógica lee de él (un clip que
ya terminó, en las escenas de cuidado) puede llegar un tick antes o después según el ritmo de la pantalla.

## Movimiento suave

Mejora opcional, encendida por defecto: el ajuste `smooth_motion` (fila «Movimiento suave» de las opciones del menú
de pausa, guardado en los ajustes del usuario) llega a `Field::setSmoothMotion` desde `PlaySession`. Encendido, los
personajes y la cámara se dibujan deslizándose entre los fotogramas de 1/20 s del original (`Field::drawnPose`, ver
`docs/modulos/game/field.md`, «Interpolación»); apagado, cada uno se dibuja donde lo deja su tick, a saltos de
fotograma como el original. La lógica es la misma en los dos casos.

## Estilo chibi

Mejora opcional y prototipo (el original no la tiene): dibuja un modelo con la cabeza grande, el torso corto y los
miembros cortos (`chibi_style.hpp`, `animation::buildChibiRig`, ver `docs/modulos/engine/animation.md`, «Estilo
chibi»), para que los modelos que no son del juego original (los de un mod o un `.glb` importado) se parezcan a los
suyos. El ajuste `chibi_style` tiene tres valores:

| Valor | Qué se dibuja chibi |
|---|---|
| `off` («No») | Nada |
| `external` («Modelos externos», por defecto) | Los modelos que vienen de un paquete que no es del motor |
| `all` («Todos») | Todos los modelos, también los del original |

Se cambia en la fila «Estilo chibi» de las opciones del título y de las opciones del menú de pausa (Tab, «Opciones»;
mando: Select), se aplica en el acto y se guarda en los ajustes del usuario; `--chibi` en la línea de órdenes fuerza
«Todos» para una ejecución. El origen sale del paquete que gana al resolver el modelo en el sistema de archivos
virtual (`isOriginalModel`): un modelo es del original si ese paquete es del motor y está en la capa base o remaster;
uno de la capa de mods o de usuario es externo. `FieldScene::setChibiMode` lo aplica al jugador, al compañero, a los
PNJ del campo y a los luchadores del combate, que son los mismos actores del campo.

Solo cambia el dibujo: las animaciones son las mismas, los pies siguen en el suelo en cada fotograma y las colisiones,
velocidades y tamaños que lee la lógica son los del original. Los puntos de los efectos que salen de un hueso
(`nodePoint`, `bonePoint`) y las caras del compañero y de los PNJ siguen la silueta chibi (`drawnJoints`). Cada
modelo calcula su forma al cargarse; un cuerpo que la detección no reconoce se queda como está, y `models/chibi.yaml`
del paquete del juego corrige una especie (apagarla, su forma, la parte de un hueso o sus factores).

`--model-viewer <especie>/model --chibi [--viewer-yaw <grados>]` enseña una especie quieta y andando, original y
chibi una al lado de otra, de frente (0) o de lado (90). Quedan sin seguir la silueta chibi las sombras y los
retratos. Pruebas aisladas en `tests/chibi_style_tests.cpp` (un humanoide alto de cajas en un paquete de mod se dibuja
chibi y uno del original no).

## Aviso de guardado

Mejora opcional, encendida por defecto (el original no autoguarda y no dice nada cuando su máquina de guardado escribe):
mientras el juego guarda, un Gabumon pequeño, de cuerpo entero y de perfil, va de un lado a otro, andando o corriendo,
junto a «Guardando» («Saving» en inglés, clave `save.saving`) en la esquina inferior derecha. El ajuste `save_notice`
(fila «Aviso de guardado» de las opciones del menú de pausa, guardado en los ajustes del usuario) llega a
`SaveIndicator::setEnabled` desde `PlaySession`; apagado, no se ve nada.

- **Cuándo.** `GameSession::savesWritten` cuenta cada guardado que la sesión escribe: el autoguardado, la máquina de
  guardado, `save("slot_NN")`, el del menú de dormir y el de la ranura de una partida nueva. `SaveIndicator` lo mira al
  final de `PlaySession::update`, cuando ya se escribió todo lo del frame, y con un guardado nuevo arranca
  `SaveNotice`: entero 1,5 s desde la escritura y después se desvanece en 0,5 s; otro guardado en ese tiempo lo empieza
  de nuevo. Un guardado que espera a que acabe una escena de cuidados (`savesWait`) no lo enseña hasta que se escribe.
  El aviso nunca retrasa la escritura: solo mira el contador.
- **Dónde.** El documento `ui/save_indicator` está mostrado toda la partida, transparente entre guardados (así está
  siempre maquetado y no le quita el foco a un menú). Mientras se ve, `SaveIndicator` toma los rectángulos de lo que
  dibujan los demás documentos (`Ui::drawnRects`: textos, cajas e imágenes; no las capas que tapan media ventana o más,
  como el velo de la máquina de guardado) y lo sube desde su esquina hasta el sitio más bajo libre con 6dp de margen
  (`liftClearOf`, ajuste `save_indicator.lift`, en píxeles), por ejemplo encima de las barras de felicidad y
  disciplina del aspecto original. Si ningún sitio de su lado está libre, no se ve. Solo se vuelve a colocar cuando
  algo le cae encima, para no seguir a una ventana que se desliza.
- **La figura.** `SaveFigure` carga el modelo de Gabumon (la especie `gabu`) del contenido extraído del
  jugador y sus clips de andar (el 2) y correr (el 4), los del compañero; si falta uno usa el otro y, sin ninguno, el 0.
  Los usa sin el movimiento de raíz. Lo dibuja de perfil, entero y algo más alto que las mayúsculas del texto, en una
  textura propia de 288x96 (`runtime/save-figure`) que el documento pinta como una tira de 72x24dp, solo los frames en
  que el aviso se ve. `LaneRunner` lo lleva por un carril a lo ancho de la tira y en cada punta se da la vuelta en 0,2 s
  mirando a la cámara. Cada vuelta anda o corre a la velocidad del compañero para ese paso (452 y 870 por segundo): casi
  siempre alterna, y de vez en cuando repite (nunca dos veces seguidas). Lo decide un hash del número de vuelta, así que
  es siempre igual y no toca el azar del juego. Mientras gira tiene las piernas quietas; si la vuelta nueva cambia de
  clip, el giro funde la zancada en la que acabó con el principio del clip nuevo, sin saltos. Sin el modelo
  (contenido incompleto) el ajuste `save_indicator.figure` es falso y el aviso enseña solo el texto. En git no hay nada
  del modelo ni capturas.
- **Aspecto.** En el aspecto original es una de las ventanas del original (`.original-box`) con el texto en la fuente
  del aspecto original a 22dp (dos veces su celda de 11 píxeles); en el moderno, una píldora oscura.

La ruta `route-save-indicator.yaml` lo comprueba con el guardado de la consola, el autoguardado de un cambio de mapa, la
máquina de guardado y el menú de pausa abierto a 1280x720, y a 1920x1080 y 1024x768; cada captura pasa
`expect_clear: save_indicator` (no pisa nada de otro documento) y `text_fit`. Al final apaga la opción y comprueba que
un guardado no lo enseña. Tests: `the save notice holds ...`, `the figure trots its lane back and forth ...`, `the
lane's laps mostly alternate ...`, `the indicator goes just above the windows ...`, `the indicator shows for each save
written ...` y `the indicator moves up clear of a window ...` (`apps/game/tests/save_indicator_tests.cpp`).

## Hora en dígitos

Mejora opcional, encendida por defecto (el original solo tiene el reloj de esfera): bajo el reloj del HUD del campo, una
caja pequeña dice la hora del juego en cifras, de 24 horas, «08:54». Es la hora y el minuto del día del juego, los
mismos que mueven la aguja y el punto de la esfera (`hud.clock.hour` y `hud.clock.minute`). El ajuste `digital_time`
(fila «Hora en dígitos», «Digital time» en inglés, de las opciones del menú de pausa, guardado en los ajustes del
usuario) llega como ajuste de la UI desde `PlaySession`, y el script del HUD (`ui/hud/logic.lua`) pone con él la
clase `digital` en `#field-clock`.

- **Cuándo cambia.** El texto solo se escribe cuando cambia el minuto (o el idioma): el HUD redibuja su estado con
  cada `hud.revision`, que el juego solo sube cuando algo cambia, y el script guarda la última hora escrita, así que un
  frame sin cambios no construye ningún texto.
- **Dónde.** Es parte del reloj (`#field-time` dentro de `#field-clock`), así que se ve y se esconde con él: el
  campo parado (menús, combates, escenas) lo quita a la vez que el reloj se desliza fuera, y se funde con el HUD en los
  cambios de mapa. En el aspecto original es una caja de 96dp de ancho con el marco de las ventanas del original
  (`.original-box`) y el fondo negro, para leerse sobre cualquier suelo, centrada bajo la esfera, con las cifras en
  blanco en la fuente del aspecto original a 22dp (el doble de su celda; la fuente de mapa de bits solo se dibuja a
  múltiplos enteros y el triple no cabe en la caja). Con escalas no enteras (1920x1080 es x1,5) las esquinas del marco
  no casan del todo con sus líneas, como en todas las cajas del aspecto original; en una caja tan baja se nota más. En
  el moderno, que no dibuja la esfera, es la hora grande de su panel, y apagada el panel solo dice el día.

La ruta `route-digital-time.yaml` hace capturas por la mañana a 1280x720, 1920x1080 y 1024x768, por la noche, con el
menú de pausa y con la opción apagada; cada captura con la caja pasa `expect_clear: hud#field-time` (no pisa nada del
HUD ni de otro documento, ver [UI](../engine/ui.md)) y `text_fit`. Tests: `the field clock tells the game time in
digits as HH:MM while the option is on` (`engine/ui/tests/ui_tests.cpp`), `one part of a document is checked against
the rest of it but the panel round it` (`engine/ui/tests/text_fit_tests.cpp`) y los ajustes del usuario
(`apps/game/tests/user_settings_tests.cpp`).

## Caminar con teclado o ratón

Opción de dwe (el original solo tiene el mando): «Caminar con» (`walk_control` en `settings.yaml`: `keys` o
`mouse`; `apps/game/src/walk_control.hpp`) dice qué mueve al jugador en el campo, y nunca son los dos a la vez. Por
defecto es `keys`, como el original: WASD, las flechas y el mando lo mueven, y un clic en el suelo no lo lleva a
ningún sitio ni lo arrastra al mantenerlo, así que un clic perdido nunca echa a andar al jugador. Con `mouse`, el clic
en el suelo lo lleva allí y mantenerlo lo arrastra, y las direcciones del teclado y del mando no lo mueven. Los menús y
los diálogos aceptan teclas y clics con las dos (un clic sigue siendo confirmar, y cerca del jugador habla o usa lo que
tiene delante). Está en las opciones del menú de pausa (fila `walk-control`). Cambiarla suelta el destino que llevara
el jugador (`WalkControlChanger`, `Field::dropMoveTarget`), así que no sigue andando hacia un clic de antes. Un
autotest no lee `settings.yaml`: usa `mouse` en las rutas de ratón y `keys` en las demás, o lo que diga
`walk_control` en la ruta. Tests: `apps/game/tests/walk_control_tests.cpp` (los dos modos y el cambio, que suelta el
destino); ruta `route-walk-control.yaml`.

## Minimapa bajo las ventanas

Regla de dwe (el original no tiene minimapa): el minimapa del HUD nunca se dibuja encima de una ventana, con cualquier
aspecto, tamaño de ventana y tamaño de minimapa. Cada frame, antes de maquetar la UI, `MinimapCover`
(`apps/game/src/minimap_setting.hpp`) mira la última maqueta: si algo que dibuja otro documento mostrado
(`Ui::drawnRects`: el menú de pausa, la página del compañero, la ventana de un comerciante o de la tienda de cartas, un
cuadro de resumen) pisa la caja de `#minimap`, publica `hud.minimap_covered` y el script del HUD pone la clase
`covered`, que lo deja en su sitio sin dibujarlo (`visibility: hidden` en `ui/hud/structure.rcss`, común a todos los
aspectos). Vuelve cuando nada lo ha pisado durante 0,25 s (`MinimapCover::HoldSeconds`), para que una caja que se
cierra con una transformación, que `drawnRects` no cuenta, no lo destape mientras encoge. Las ventanas no se mueven:
donde no llegan a su esquina (el menú de pausa moderno a 1280x720 o 1920x1080 con el minimapa grande) el minimapa se
sigue viendo.

La ruta `route-minimap-clear.yaml` pone el minimapa siempre visible y grande y comprueba con `expect_clear` el menú de
pausa, la página del compañero y el minimapa con el menú cerrado a 1280x720, 1920x1080 y 1024x768 (a 1024x768 el menú
moderno lo tapa y el minimapa se esconde); `route-bulk-cards.yaml` y `route-bulk-trade.yaml` (esta con el minimapa
grande a 4:3) comprueban igual la tienda de cartas y el trueque. Tests: `the minimap hides while a window lies over it
and shows a moment after it leaves` y `the HUD draws no minimap under a window, whatever the window`
(`apps/game/tests/minimap_setting_tests.cpp`).

## Cambios en lote

Mejora opcional, encendida por defecto: el ajuste `bulk_trades` (fila «Cambios en lote» de las opciones del menú de
pausa, guardado en los ajustes del usuario) llega desde `PlaySession::setBulkTrades` a `TradeScreen::setBulk` y a
`CardShop::setBulkPacks`. Encendido, el trueque de objetos y los sobres de la tienda de cartas preguntan cuántos de una
vez; apagado, van de uno en uno como el original. Las reglas y la prueba de igualdad están en
[screens.md](screens.md#cambios-en-lote).

## Requisitos de evolución

Mejora opcional, encendida por defecto (el gráfico de evolución del original solo dibuja las líneas): en la pestaña
«Árbol» («Chart») de la página del jugador, al abrir una forma (Intro) el cursor pasa por sus evoluciones (flechas
arriba y abajo; izquierda vuelve a la vista original con las formas de origen, derecha vuelve al cursor). Un panel a la
izquierda dice lo que pide la evolución bajo el cursor: el rango de errores de cuidado, el de peso, cada stat mínimo
(HP y MP en sus unidades, el original los compara divididos entre 10) o, para un destino de novato, que el stat más
alto del partner sea uno de los preferidos, y las condiciones extra (forma previa, disciplina, felicidad, batallas,
técnicas). Una evolución especial (sin errores ni peso) sale como «Evolución especial»; la de un bebé, «Siempre».

En el árbol del propio compañero cada línea lleva al lado su valor actual, verde si cumple y rojo si no, y una marca
(✓ o ✗) por grupo; abajo, «3 de 4 listos» y la hora de la revisión («Revisión 17/72 h»: el contador y las horas de su
nivel; el fallback del novato con las suyas). La regla es la del original (`evolution.c: calculateRequirementScore`):
un punto por grupo (errores de cuidado como mínimo, o como máximo con la marca 0x10; peso dentro de ±5; stats; un extra
cualquiera) y pasa con 3 de 4; entre las que pasan, la elección de la revisión (la puntuación más alta en bebé grande,
la media más alta de los stats pedidos en novato y campeón, con la preferencia por formas no criadas), y un novato sin
candidata pasa a Numemon desde las 96 horas. La línea de la forma que elegiría la revisión brilla en verde y dice «La
elige al revisar»; las demás se atenúan. Si la revisión elegiría otra forma, lo dice («Al revisar: …»).

Una forma que el jugador aún no ha descubierto (la casilla gris, sin imagen: no está entre las criadas,
`player.chart.raised`, la misma marca que usa el gráfico) sale como «???» y sin nada de su requisito, ni puntos ni
brillo, en el árbol del compañero y en los de las demás especies; descubierta, lo enseña todo. Si la revisión elegiría
una forma no descubierta, solo dice «Al revisar: ???» («Next check: ???»). `ChartRequirements` no publica los valores
del partner de esas formas y da su número como -2 en `evoreq.partner` (`encodeOutlook`, `shownTargets`).

Todo sale de `Evolution::outlook` y `Evolution::report` ([evolution.md](../game/evolution.md)), las mismas reglas que
deciden la evolución: `ChartRequirements` (`chart_requirements.cpp`), mientras el menú de pausa se ve, publica una vez
los requisitos de cada forma (`evoreq.req.<especie>`, «clave:rango|…») y las horas por nivel (`evoreq.hours`), y en
cada fotograma en que algo cambia los valores del partner (`evoreq.partner`, `evoreq.partner.<especie>`) y sube
`evoreq.version`; la lógica del menú (`pause_menu/logic.lua`) dibuja el panel y el cursor. El ajuste
`evolution_requirements` (fila «Requisitos de evolución» de las opciones del menú de pausa, guardado en los ajustes del
usuario) apagado deja el gráfico original. El gráfico solo existe en el aspecto original; la fila de la opción está en
los dos.

La ruta `route-evolution-requirements.yaml` abre el gráfico del compañero con el teclado (en una partida nueva todo
destino es «???»), mueve el cursor, pasa por la vista original, descubre Greymon y Tyrannomon (disparadores 0x200 + su
número), le da bayas grandes para mover el peso (con una Tyrannomon queda con 3 de 4; con tres la revisión elige
Monochromon, sin descubrir: «Al revisar: ???») y lo abre de nuevo a 1280x720, 1920x1080 y 1024x768 con `text_fit` y `expect_clear: pause_menu`; al final apaga y enciende la
opción. Tests: `apps/game/tests/chart_requirements_tests.cpp` (rangos, filas y formas no descubiertas), `game/evolution/tests/report_tests.cpp`
(los puntos del informe son los de la puntuación en cada mezcla de grupos; la forma que da `outlook` es la que elige
`checkNatural`) y `the chart's outlook names the form the next check evolves the partner into`
(`apps/session/tests/evolution_adapter_tests.cpp`).

## Carteles traducidos

Mejora opcional, encendida por defecto (la versión americana dejó muchos carteles de los mapas en japonés): los
carteles pintados en los fondos se escriben otra vez en el idioma del jugador (módulo [signs](../engine/signs.md)). El
ajuste `translated_signs` (fila «Carteles traducidos», «Translated signs» en inglés, de las opciones del menú de pausa,
guardado en los ajustes del usuario) llega a `FieldScene::setTranslatedSigns` desde `PlaySession`; apagado, el mapa
muestra las texturas originales.

- **Al entrar en un mapa.** `FieldScene::enterMap` guarda las imágenes del mapa tal como se leyeron y pinta los
  carteles del idioma antes de `MapRenderer::setMap`.
- **Al cambiar el idioma o la opción.** `finishTick` compara la clave de lo pintado (idioma, revisión del catálogo y
  opción) y, si cambió, sube otra vez el fondo y las texturas de los sprites (`MapRenderer::updateImages`) sin
  recargar el mapa.
- **`--dump-signs`** (`sign_dump.cpp`) escribe las imágenes de un mapa con rejilla para escribir overlays
  ([guía](../../guides/carteles.md)).

## Extras de la ficha

Mejora opcional, encendida por defecto: el ajuste `status_extras` (fila «Extras de la ficha», «Status extras» en
inglés, de las opciones del menú de pausa, guardado en los ajustes del usuario) llega como ajuste de la UI desde
`PlaySession` (`StatusExtrasSetting`). Encendido, la ficha del compañero enseña su barra de cansancio y sus errores de
cuidado, que el original esconde; apagado, la ficha es la del original. Dónde va cada uno en cada piel y la ruta:
[screens.md](screens.md#extras-de-la-ficha).

## Nombres largos

Mejora opcional, encendida por defecto (no cambia ninguna regla): el ajuste `long_names` (fila «Nombres largos»,
«Long names» en inglés, de las opciones del menú de pausa, guardado en los ajustes del usuario) llega como ajuste de la
UI desde `PlaySession` (`LongNamesSetting`). Encendido, la caja de nombres (la de la entrevista y la de la rutina 32)
admite hasta 12 letras (`LongNameLength`, `naming_box.hpp`); apagado, las 6 del original (`naming.length` de
`rules/original/new_game_scene.yaml`, `initializeNamingBuffer`). Doce es lo que cabe en la caja del nombre del
original (149 píxeles, una letra cada 12), así que la caja de nombres no cambia de tamaño en ninguna piel. El cambio
vale desde la siguiente caja que se abra; un nombre ya dado se queda como está.

Dónde se dibuja un nombre y qué hace uno largo:

| Lugar | Qué hace |
|---|---|
| Caja de nombres (`ui/naming_box`) | 12 casillas; caben en la caja del nombre de las dos pieles |
| Cuadros de diálogo (`ui/story_textbox`, también la charla del torneo) | Una fila que el nombre hace más ancha que la caja se estrecha desde su borde izquierdo (`ui/common/fit_text.lua`); las filas que caben no se tocan, así que con nombres de 6 letras todo queda como el original |
| Página del jugador (aspecto original) | Con más de 6 letras la caja del nombre crece 12 píxeles por letra (clase `long-player-name`) |
| Ficha del compañero | Su caja ya cabe 12 letras (crece para un nombre de especie más largo) |
| Lista de partidas (`ui/save_slots`) | La línea «nombre (especie)» se estrecha si no cabe |

Medido: ruta de prueba con el jugador «WWWWWWWWWWWW» y el compañero «Bartholomews» (la clave de ruta `names`), y la
caja de nombres en la entrevista (la clave `new_game_scene: true`); capturas antes/después
en las dos pieles.

## Barras de reclutamiento

Mejora opcional, encendida por defecto (no cambia ninguna regla; el original no enseña cuánto le falta al jugador):
mientras un trabajo o una partida decide si alguien se une a la ciudad, una barra enseña cuánto falta. Una sola opción
las enciende o apaga todas: el ajuste `shop_job_bar` (conserva la clave con la que entró; fila «Barra de reclutamiento»,
«Recruit bar» en inglés, de las opciones del menú de pausa, guardado en los ajustes del usuario), que llega como ajuste
de la UI desde `PlaySession` (`RecruitBarSetting`, `recruit_bar.hpp`).

- **La tienda del cañón**: mientras dura el trabajo (gcan11, guion 49 y los clientes de los guiones 199 a 202), una barra
  arriba en el centro enseña lo ganado frente a la cuenta con la que Monochromon se une a la ciudad, con una marca donde
  empieza la paga mejor, y los clientes atendidos («Clientes 7/20»).
- **El juego de las piedras**: durante la partida contra el primer rival (frzl17), dos líneas abajo, bajo la caja de
  diálogo y sin tapar el HUD del original: los puntos de cada uno («Tú 3 - 1 Penguinmon»), las tiradas que les quedan,
  cómo va («Vas ganando: Penguinmon se une a la ciudad», «Vas perdiendo por N», «Empate») y «Puntuación perfecta:
  medalla» mientras se pueda alcanzar.

| Qué | Original | dwe | Prueba | Estado |
|---|---|---|---|---|
| Barra del trabajo en la tienda | No existe: el final (sección 0x52) paga por la variable 243, el byte alto de la cuenta que deja la rutina 56: hasta 6, 500 bits; de 7 a 11, 1500; desde 12, se une (2000 bits y el disparador 247) | `ShopJobBar` (`shop_job_bar.hpp`) lee la regla `story_scenes.shop_job` (mapa, variable 123 de clientes, 20 clientes, `better_pay_from: 7`, `joins_from: 12`) y la cuenta (`StoryAdapter::shopTally`); la barra va de 0 a 3072 (12 × 256, `ShopJobRules::tallyFrom`) con la marca en 1792 (7 × 256). Sale desde el primer cliente (la variable 123 sube en el mapa de la tienda) hasta que el jugador sale del mapa (el salto del final); una cuenta de un trabajo anterior no la enseña hasta el primer cliente del siguiente. Publica `shop_job.*` y sube `shop_job.revision`; el HUD (`ui/hud/logic.lua`, `#shop-job`) dibuja el relleno, la marca y los textos (los clientes, que no pasan de 20 aunque algún cliente cuente dos al irse, y la próxima marca a la derecha), con las clases `better` y `joins` al pasar cada marca, en las dos pieles | `shop_job_bar_tests.cpp` («the shop job's bar shows from the first customer…», «a count left over…», «…hidden with the option off…»); `the game pack's shop job joins from a tally of 3072…` (`story_props_tests.cpp`); captura a media partida en las dos pieles | Mejora (opción) |
| Barra de la partida de las piedras | No existe: el guion 206 (sección 0x0a, al volver a frzl15) lee el resultado que deja `KAR_finishMatch` en la variable 121: 0 pierde o empata (sin unión), 1 gana, 2 gana con 10 puntos o más (además la medalla, disparador 372); con 1 o 2 Penguinmon se une a la ciudad (disparador 257, prosperidad +1). Solo el primer rival se une | `curlingRecruitOf` (`curling_recruit_bar.hpp`) lee la partida (`Match::teams`: puntos y tiradas hechas); la puntuación perfecta sigue al alcance mientras las piedras del jugador que siguen en juego (en el soporte, en la mano o en el hielo) puedan sumar los 10 en el anillo que más vale (2), es decir, mientras ninguna haya salido. `CurlingScreen::showRecruitBar` publica `curling_recruit.*` y sube `curling_recruit.revision`; el documento `curling_hud` (`#recruit`) la dibuja en y 222 a 240 del original, en las dos pieles (la original con su caja). Sale solo en la partida del primer rival y se quita al acabar | `curling_recruit_bar_tests.cpp` («…shows only during a match, and not with the option off», «…follows the score and the throws left», «the perfect score shows while…»); capturas a media partida en las dos pieles | Mejora (opción) |

## Burbuja con varias necesidades

Mejora opcional, encendida por defecto (no cambia ninguna regla): el ajuste `need_bubble_stack` (fila «Varias
necesidades», «Several needs» en inglés, de las opciones; guardado en los ajustes del usuario) llega como ajuste de la
UI desde `PlaySession` (`NeedBubbleStackSetting`) y el HUD junta hasta tres necesidades en un globo
([ui.md](../../architecture/ui.md#estado-del-campo)).

| Qué | Original | dwe | Prueba | Estado |
|---|---|---|---|---|
| Burbuja con dos o más necesidades | `partner_impl.c: handleConditionBubble`: una cada vez, por prioridad (sueño, cansancio, hambre, baño, herida, enfermedad); a los 50 frames cambia a la más alta de las demás, así que se turnan las dos más urgentes y una tercera no sale | Hasta tres en un globo ensanchado con las piezas del original, la más urgente a la izquierda; con una sola, la del original; apagada, la del original entera. Medido en dwe: cansancio + herida, 50 píxeles; cansancio + herida + enfermedad, 68 | `ui_tests`: «the need bubble shows several needs side by side ...»; `user_settings_tests`; `in_game_options_tests` | Mejora (opción) |

## Minimapa redondo

Mejora opcional, encendida por defecto con los dos aspectos (no cambia ninguna regla; el original no tiene
minimapa): el ajuste `minimap` (`on` u `off`; el `auto` de ajustes antiguos se lee como `on`) se cambia en la fila
«Mostrar minimapa» («Show minimap» en inglés) de las opciones en partida, en la página «Minimapa» del menú de pausa y
en las opciones del título; se aplica al momento, sin recargar el mapa, y se guarda en los ajustes del usuario
(`MinimapChanger`, `apps/game/src/minimap_setting.hpp`). Cómo se dibuja: [ui.md](../../architecture/ui.md#minimapa).

`PlaySession` publica `hud.minimap_allowed` (`minimapAllowed`): el minimapa solo sale mientras corre el tiempo del
campo (sin menú, escena, charla, fundido ni cambio de mapa, como el reloj y las barras del original:
`clock.c: renderGameClock`) y nunca en un combate, un entrenamiento, la pesca, las piedras, las pantallas del torneo,
una evolución, una muerte o el final. Va arriba a la derecha, lejos del reloj, las barras del compañero y la pista
del menú, y se esconde bajo cualquier ventana (más arriba, «Minimapa bajo las ventanas»).

| Qué | Original | dwe | Prueba | Estado |
|---|---|---|---|---|
| Minimapa redondo en el HUD | No existe | Disco del mapa alrededor del jugador, con el norte (−Z) arriba, el nombre del mapa encima y el día y la hora debajo; el jugador en el centro con su orientación, el compañero donde está (en el borde si queda fuera); con los dos aspectos y la misma disposición: el moderno con aro brillante y pastillas; el original con el nombre y la hora en sus ventanas (borde cian con esquinas, fondo negro y su letra), el aro de cuatro franjas de sus bordes y la N y el jugador en su amarillo; un nombre largo se parte en líneas centradas. Se ve con `on` y fuera de combates, escenas y minijuegos | `minimap_setting_tests.cpp` («the minimap shows only while the field runs…», «the options page's minimap row hides the HUD's minimap at once and saves the choice», modos y guardado); `in_game_options_tests.cpp` («the options page shows and hides the minimap with either look»); `ui_tests.cpp` («the HUD shows the minimap unless it is off…», «…round the player, north-up»); `minimap_tests.cpp` y `minimap_disc_tests.cpp` (cálculo del disco y del aro); ruta `route-minimap-clear.yaml` | Mejora (opción) |

## Enemigos en el minimapa

Mejora opcional, encendida por defecto (no cambia ninguna regla; el original no tiene minimapa): el ajuste
`minimap_enemies` (`true` o `false`) se cambia en la fila «Enemigos en el minimapa» («Enemies on minimap» en inglés)
de las opciones en partida, justo debajo de «Mostrar minimapa»; se aplica al momento y se guarda en los ajustes del
usuario (clave `minimap_enemies`). Apagado, el minimapa queda como antes: solo el jugador y el compañero.

Qué es un enemigo: el original no marca a ningún personaje del mapa como salvaje. Un personaje pelea porque su
sección del guion del mapa (la que tiene su id de guion y corre al tocarlo o hablarle) llega a la orden de empezar un
combate, que pelea con quien se habló. `fightingSections` (`apps/game/src/minimap_enemies.hpp`) recorre el listado
del guion activo (`script/dg/<n>`, el que da `StoryRunner::mapScript()`) desde el principio de cada sección siguiendo
saltos, condiciones, elecciones, `switch` y llamadas, y la sección pelea si llega a `start_battle`. El modo de
seguimiento no sirve: muchos salvajes solo se giran hacia el jugador y algunos que persiguen solo hablan (en todos los
mapas: modo 2, 102 que pelean y 73 que no; modo 12, 352 y 89). `MinimapEnemyFinder` lee el listado una vez por guion.
`PlaySession::minimapEnemyMarkers` pone un punto por cada personaje del mapa que está en el campo
(`NpcSet::positionOf`) y cuya sección pelea, una vez por id de guion; en cada frame lee dónde está, así que el punto
sigue al personaje y desaparece cuando se descarga, se va del mapa o cae en combate (el combate lo quita del campo).

Decisiones: un jefe de la historia o un reclutable cuya sección puede empezar un combate también lleva punto (el
punto dice que tocarlo o hablarle puede acabar en pelea); los amigos, el compañero y los personajes que solo hablan,
no. Un enemigo fuera de la vista del disco no se pinta (no se clava en el borde como el compañero). Sin guion del mapa
(la historia no corre) no se marca nada. Un salto a otro guion (`jump_to_file`) no se sigue. Cómo se dibuja:
[ui.md](../../architecture/ui.md#minimapa).

| Qué | Original | dwe | Prueba | Estado |
|---|---|---|---|---|
| Enemigos en el minimapa | No existe | Un punto rojo anaranjado con borde oscuro por cada personaje del mapa que puede pelear, donde está, bajo la flecha del jugador; con los dos aspectos. Medido en dwe: en el segundo mapa de la niebla salen los tres salvajes y se mueven con ellos, uno sale de la vista; en el túnel (dos que persiguen pero solo hablan) y en el pueblo, ninguno. Apagado, ninguno | `minimap_enemies_tests.cpp` (secciones que pelean, amigos fuera, posiciones vivas, apagado, lector del listado); `ui_tests.cpp` («the minimap marks the enemies in view round the player, under the player's arrow»); `minimap_tests.cpp` (el punto); `in_game_options_tests.cpp` («the options page turns the enemies on the minimap on and off…»); `user_settings_tests`; ruta `route-minimap-enemies.yaml` | Mejora (opción) |

## Mapa de la isla

Mejora opcional, encendida por defecto (no cambia ninguna regla; el original no tiene mapa de la isla): la tecla M
(L1 en el mando, la acción `world_map` de `defaultActionMap`) abre a pantalla completa un mapa de toda la isla, donde
el jugador anda libre (sin menú, guion, fundido ni ventana abierta, como el menú de pausa). M o Esc (cancelar) lo
cierran. Mientras está abierto el juego se para (`PlaySession::paused`). El ajuste `world_map` (fila «Mapa de la
isla», «Island map» en inglés, de las opciones; guardado en los ajustes del usuario) apaga la tecla; apagado, M sigue
cerrando un mapa abierto.

- **El dibujo**: el original no trae ninguna imagen de la isla, así que el motor la pinta al abrirla
  (`ui::paintWorldMap`, `engine/ui/world_map.hpp`) desde `ui/world_map/layout.yaml` del paquete del juego (esquema
  `world_map_layout`): cada zona con su centro, su radio y su color en un lienzo de 1000 × 640, y los caminos entre
  zonas. La tierra es la unión suave de las zonas con una costa ondulada y entintada; cada punto de tierra es de la zona
  más cercana (bordes punteados), con bajíos, ondas en el mar y caminos a trazos. Las zonas sin visitar se pintan
  desvaídas hacia el color del pergamino. La imagen se registra como `generated:world_map/<n>` (`Ui::setGeneratedImage`)
  y solo se repinta si cambian las zonas conocidas.
- **Las zonas**: una zona agrupa los nombres que el original enseña al entrar en un mapa (`MapEntry::loadingName`, el
  texto `main.map_names.<n>`), así que sus nombres salen traducidos y siguen al cambio de idioma. Su lugar en el lienzo
  es de dwe, a mano, coherente con las salidas entre mapas.
- **Dónde está el jugador**: la zona de su mapa, con un marcador que late sobre su nombre, y arriba «Estás en: lugar
  (zona)».
- **A dónde puede ir**: los lugares a los que llevan las salidas de su mapa: los saltos a pie cuya celda de disparo
  (110 + salida) tiene el mapa (`checkMapInteraction`) y los cambios de mapa de sus guiones al pisar o
  confirmar (`field/script_warps`), sin repetir y sin su propio lugar. Salen en una línea bajo el mapa y sus zonas
  resaltadas.
- **Visitado**: la sección de partida `map_visits` (`session::MapVisits`) guarda los mapas que ha pisado el jugador. Una
  partida anterior sin la sección carga sin visitas: solo se conoce la zona donde está.
- **Pieles**: la moderna, con su marco y sus colores; la original, con su caja (`.original-box`) y su fuente a 22dp en
  el título, el lugar, las salidas y la leyenda. Los nombres de las zonas usan la fuente moderna pequeña en las dos
  pieles, porque la del original a 22dp taparía la isla.

| Qué | Original | dwe | Prueba | Estado |
|---|---|---|---|---|
| Mapa de la isla (M) | No existe | `WorldMapScreen` (`world_map_screen.hpp`) publica `world_map.*` y sube `world_map.revision`; el documento `world_map` lo dibuja. La lógica pura (zonas, conocidas, marcador, salidas, tecla) está en `world_map.hpp` | `world_map_tests.cpp` de `apps/game` («a map lies in the area…», «the player knows the areas…», «the marker stands on the centre…», «the exits are the walk warps…», «M opens the island map…», «the game pack's island layout…»); `world_map_tests.cpp` de `engine/ui` (el pintor); `map_visits_tests.cpp` (partida nueva y antigua); `in_game_options_tests`; capturas en las dos pieles y dos lugares con `text_fit` | Mejora (opción) |

## Opciones en partida

Las opciones se cambian sin volver al título: la página «Opciones» del menú de pausa tiene las de pantalla (modo de
ventana, escala, sincronía vertical), la piel, el idioma y el texto con aspecto original (las mismas órdenes que el
menú del título: `language`, `text_font`) y, debajo, las opciones propias del motor.

| Cómo se abre | Teclado | Mando |
|---|---|---|
| Directamente, también desde la pausa de Start (la cierra) | — (no atajo) | Select (Back) |
| Desde el menú (aspecto moderno) | Tab, «Opciones» | Botón norte, «Opciones» |
| Desde el menú (aspecto original) | Tab, «Jugador», Tab, «Opciones» | Botón norte, «Jugador», botón norte, «Opciones» |

`settings` (`defaultActionMap`) abre el menú de pausa en su página de opciones (`pause_menu.start_page`, que lee su
`on_show`); Atrás desde ahí vuelve al juego, y con el menú abierto lo cierra. Solo tiene vinculación en el mando
(Select/Back): en el **teclado no hay atajo**, las opciones se abren desde el menú con `Tab`. Donde no hay menú de
pausa (título, películas, entrevista, caja de nombres, gimnasio) o con otro menú abierto no hace nada. Bajo el
recuadro de pausa se lee la tecla («Back Opciones») **solo en mando**; en teclado no se muestra porque `settings` no
tiene tecla (`pause_box/logic.lua`).

Las opciones propias del motor salen de una lista, `ui/common/game_options.lua` (`game_options`): id de la fila,
ajuste de la UI que publica el juego y que la fila envía como orden, texto, valor por defecto y, para las que no son
sí/no, sus valores (`values`) y el prefijo de sus textos (`texts`). La página llena con ellas, en orden, las filas
libres `option-slot-<n>` del documento (24) y les da el id de la opción, así que una opción nueva solo necesita su
entrada en esa lista y que el juego atienda su ajuste; ningún documento necesita una fila propia.

## Sombras, luz y colores vivos

Mejoras opcionales que no cambian ninguna regla, ni la colisión, ni la geometría, ni los números aleatorios; las tres
encendidas por defecto, como las demás mejoras que no tocan reglas. Se cambian en las opciones del menú de pausa
(filas «Sombras», «Luz mejorada», «Colores vivos») y en las del título con el aspecto original, se aplican al momento
y se guardan en `settings.yaml` (`shadows`, `enhanced_lighting`, `vivid_colors`; `GraphicsChanger`,
`apps/game/src/graphics_options.hpp`). `--graphics shadows=off|round|sun,lighting=on|off,colors=on|off` las cambia
solo para esa ejecución (capturas y rutas, que no leen los ajustes). `FieldScene` las aplica en
`field_scene_graphics.cpp`; cómo se dibujan: [render.md](../../architecture/render.md#paso-de-luz-y-color).

- Sombras: `round` es la sombra redonda del original bajo cada personaje (jugador, compañero y NPC del mapa;
  `drop_shadow.c: renderDropShadow`, que `model.c` llama para cada personaje que dibuja), del tamaño de su especie y nunca
  en las celdas de valor 0x80 o 0xFF (`getTileTrigger` da -1). Los objetos del suelo ya traían la suya. `sun` (por
  defecto) añade una sombra suave que proyecta el sol: corta a mediodía, larga y girando al amanecer y al atardecer,
  más débil con la paleta del atardecer y ninguna de noche ni en mapas sin hora (cuevas, casas). `off` no dibuja
  ninguna.
- Luz mejorada: un tinte según el reloj del juego (dorado por la mañana, neutro a mediodía, naranja al atardecer,
  azul de noche) solo en los mapas con hora, y las luces de cada mapa (`field/lights` del paquete del juego,
  `content/schemas/field_lights.schema.json`): lámparas, fuegos y cristales pintados en el fondo que iluminan lo que
  tienen cerca. Por ahora: twna01 (puertas de noche), leom01 (lámparas, botella y cristales), tunn01 y frzl01 (la
  estufa, con parpadeo).
- Colores vivos: más saturación en los colores apagados que en los vivos, un poco de contraste y un brillo suave
  alrededor de lo más claro. Solo en la escena del campo: el HUD, los menús y los fundidos no cambian.

| Qué | Original | dwe | Prueba | Estado |
|---|---|---|---|---|
| Sombra redonda de los personajes | `renderDropShadow`: cuadrado de 2 × radio de la especie en el suelo, SPOT256 (uv 0x40, 0x80, 63 × 63), resta con color 0x30, detrás de todo (OT 0xFFD); no en celdas con `getTileTrigger` -1 | Igual, con la opción en `round` o `sun`; se dibuja justo después del fondo y antes de los modelos (`MapRenderer::groundDrawer`) | `scene_light_tests.cpp` («the round shadow is the original's…»); `graphics_options_tests.cpp` | Portado |
| Sombra del sol | No existe | Mancha alargada desde los pies en la dirección contraria al sol, con la hora del reloj | `scene_light_tests.cpp` («the sun's shadow turns through the day…», «the sun's shadow stretches from the feet…») | Mejora (opción) |
| Luz por hora y luces del mapa | El original solo funde las paletas y sus tres luces de modelo con la hora (`map.c: updateTimeOfDay`) | Tinte suave sobre la escena y luces puntuales en píxeles del fondo, sin tocar la paleta ni el muestreo del mapa | `scene_light_tests.cpp` (tabla de luces, tintes, intensidades); `graphics_options_tests.cpp` («the game pack's light table…») | Mejora (opción) |
| Colores vivos | No existe | Vibrance, contraste suave y brillo en la escena del campo, nunca en la UI | `in_game_options_tests.cpp` («…cycles the shadows and switches the light and the colours») | Mejora (opción) |

## Contenido oculto

Mejora opcional, apagada por defecto: al empezar una partida nueva desde el menú principal (Jugar → ranura → «Sí»),
la pantalla «Reglas de la partida» (`ui/new_game_rules`, «Game rules» en inglés) deja elegir las reglas antes de que
la partida empiece. El cursor empieza en «Empezar»; «Contenido oculto: No/Sí» cambia entre el ruleset `original` y
`oculto` (Panjyamon, Gigadramon y MetalEtemon también por evolución: [evolution.md](../game/evolution.md#contenido-oculto)),
y cancelar vuelve a la lista de ranuras. La fila «Paso Ultra → Mega» solo sale con el contenido oculto encendido y
cuando los paquetes traen el ruleset `oculto_mega` (`TitleScreen` publica `new_game_rules.mega`).

No es un ajuste del usuario sino de la partida: `TitleScreen` guarda el id elegido en `PlayStart::rulesetId`, la
sesión arranca con él y la cabecera de la partida guardada lo apunta (`rulesetId`). Al cargar, `rulesetFor`
(`session_start.cpp`) lee esa cabecera antes de crear la sesión, para que las tablas que se montan una vez (especies
del compañero, tablas de evolución, `digimon` del combate) salgan ya con los parches del ruleset; una partida sin el
campo usa el de la línea de órdenes (`--ruleset`, `original` por defecto).

Con el ruleset, el árbol de evolución de la página del jugador (`player_chart.cpp`: `publishRulesetChart`) lee los
caminos de las tablas del ruleset y añade las casillas nuevas (`player.chart.count`: 61 en el original, 65 con el
contenido oculto y 66 con el paso Mega) y la lista de Megas del paso Mega (`player.chart.mega`, ids separados por
comas; vacía sin él). Una casilla que no cabe en la rejilla de su caja (las de la tercera columna de Ultra del
contenido oculto) pasa a una página siguiente del árbol ([etapas canónicas](#etapas-canónicas)).
Una forma nueva usa el icono de otra especie con otra paleta hasta que haya iconos propios.

Las rutas de prueba solo ven esta pantalla si lo piden (`new_game_rules: true`); las demás empiezan con las reglas
del original, como antes. `route-hidden-content.yaml` elige el contenido oculto y hace evolucionar al compañero a
cada forma nueva; `route-hidden-content-off.yaml` deja el original y comprueba que todo sigue igual
([prueba vertical](../../guides/prueba-vertical.md)).

## Etapas canónicas

Mejora opcional, apagada por defecto: el ajuste `canonical_stages` (fila «Etapas canónicas», «Canonical stages» en
inglés, de las opciones del menú de pausa, guardado en los ajustes del usuario) llega como ajuste de la UI desde
`PlaySession` (`CanonicalStagesSetting`). Solo cambia lo que el árbol de la página del jugador enseña; las reglas
siguen contando a esas especies como nivel 5.

| Etapa | Español | Inglés |
|---|---|---|
| huevo | Digihuevo | Digi-Egg |
| nivel 1 | Bebé I | Fresh |
| nivel 2 | Bebé II | In-Training |
| nivel 3 | Infantil | Rookie |
| nivel 4 | Maduro | Champion |
| nivel 5 | Perfecto | Ultimate |
| Mega | Supremo | Mega |
| Ultra | Súper Supremo | Ultra |

Huevo y Ultra quedan definidos (`pause_menu.chart_stage.*`) pero sin especies. Encendido, el detalle de una casilla
dice su etapa canónica («Supremo» para una Mega).

La caja Mega roja (`.stage-5`) sale cuando el árbol tiene alguna especie Mega: con la opción encendida, las que la
serie llama Mega (`MEGA_ORDER` en `ui/pause_menu/logic.lua`: Phoenixmon, HerculesKabuterimon, y MetalEtemon y
Machinedramon cuando el ruleset los trae), y con el paso Mega, aunque la opción esté apagada, las Megas del ruleset
(`player.chart.mega`). Va en la misma página, a la derecha de la de Ultra y de su misma altura: las seis cajas (Bebé I,
Bebé II, Infantil, Maduro, Perfecto/Ultra y Supremo) en un panel de 960dp. Para que quepan dentro del marco, con 30dp
de margen a cada lado como a la izquierda del original, las columnas de Maduro y Ultra quedan a 66dp en vez de 72 y
los lados de esas cajas a 21dp en vez de 24; la caja Mega mide 93dp con su columna centrada. Para saber qué especie
es cada casilla, `publishPlayerChart` publica `player.chart.<n>.id`.

Cada caja tiene una rejilla y una página guarda como mucho lo que cabe en ella:

| Caja | Columnas | Filas | Casillas por página |
|---|---|---|---|
| Bebé I | 1 | 4 (separadas 38 px) | 4 |
| Bebé II | 1 | 4 (separadas 38 px) | 4 |
| Infantil | 1 | 9 | 9 |
| Maduro | 4 | 8 | 32 |
| Ultra/Perfecto | 2 | 8 | 16 |
| Supremo | 1 | 8 | 8 |

Regla de reparto (`chart_places`): la caja de una casilla es la de su columna en el original (la de más a la derecha
que empieza en su x o antes; las Megas, la caja Mega). Una casilla en un hueco libre de la rejilla de su caja se queda
en él en la página 1, así que las del original quedan donde las pone el original. Las demás (una Mega, una del ruleset
fuera de la rejilla o en un hueco ya ocupado) toman en orden los huecos libres de su caja, columna a columna de arriba
abajo, primero en la página 1 y luego en la 2, la 3...; las Megas en el orden de `MEGA_ORDER` y luego por casilla. Así,
con la opción encendida Phoenixmon y HerculesKabuterimon dejan sus huecos de Ultra y Myotismon del contenido oculto
ocupa uno. Con el contenido oculto y todo apagado la caja de Ultra (15 del original y Gigadramon) está llena y
MetalEtemon y Myotismon pasan a la página 2; con el paso Mega y la opción apagada MetalEtemon y Machinedramon van a la
caja Mega y Myotismon a la página 2.

Todas las páginas tienen las mismas cajas en el mismo sitio; en las siguientes solo hay casillas donde hay especies.
Con más de una página, bajo las cajas de Bebé I y Bebé II y en el mismo sitio en todas, van la flecha hacia arriba
(`#chart-less`, la de los sprites de combate, `battle-up-arrow`) si hay página anterior, el número de página
(`#chart-page`, «1/2») y la flecha volteada hacia abajo (`#chart-more`, como en la lista de partidas) si hay página
siguiente. Para pasar de página:

| Cómo | A la siguiente | A la anterior |
|---|---|---|
| Cursor en el árbol | Abajo (flecha ↓, mando abajo) en la última casilla de una columna | Arriba en la primera casilla de una columna |
| Sin cursor (en la pestaña) | Abajo | Arriba |
| Ratón | Rueda hacia abajo sobre el árbol, o clic en ▼ | Rueda hacia arriba, o clic en ▲ |

El cursor sigue en su columna: va a la casilla de arriba de esa columna al bajar y a la de abajo al subir, a la columna
más cercana con casillas si en la otra página no tiene, y vuelve a la casilla que dejó si pasa de página otra vez sin
moverse. El cambio es un deslizamiento de 0,2 s (`#chart-pages` sube 564dp por página dentro de `#chart-clip`, que
recorta al interior del marco; las cajas de la página que se deja son `#chart-stage-other-<n>`): a 20 pasos por
segundo como las cajas del original, o continuo con «Movimiento suave». Mientras dura no se aceptan teclas, y al acabar
las casillas de las páginas que no se ven quedan ocultas (`.off-page`) para que el puntero no las toque. El cursor y
el detalle funcionan igual en todas las páginas. La rueda llega a Lua con `event.wheel`
([editar menús](../../guides/editar-menus.md)). Sin especies Mega y con todo en su rejilla (las reglas del original)
no hay caja Mega, ni flechas, ni página 2, y el árbol es el del original píxel a píxel.

Apagado, el árbol es el del original y sus nombres los de la [decisión 4 del glosario](../../guides/glosario-es.md);
el nivel 5 se llama «Ultra» también en el árbol (antes «Definitivo»).

La lista de opciones ya no cabe entera a 1280x720 en ninguna piel: se desplaza (`#page-options`, `overflow-y: auto`),
la navegación lleva la fila enfocada a la vista y la rueda del ratón la mueve. La comprobación de textos no cuenta las
filas que la lista deja fuera de su caja (`text_fit.cpp`); se comprueban al llegar a ellas.

`route-canonical-stages.yaml` (en/es, piel original y moderna) enciende la opción con el contenido oculto, captura el
árbol de una página con la caja Mega, abre el detalle de Phoenixmon («Supremo») y el de MetalGreymon, la apaga y
captura el árbol original con flechas, el detalle de MetalGreymon, la página 2 (Abajo desde Etemon, la última de su
columna: el cursor cae en MetalEtemon, en la misma columna) y la vuelta a la página 1; `route-mega-step.yaml` captura
el árbol con la caja Mega y la página 2 (Myotismon, al que evoluciona antes con `evolve` para que su casilla se abra,
y su detalle) con la opción apagada, y con ella encendida los cuatro Megas en una página y el detalle de Machinedramon
([prueba vertical](../../guides/prueba-vertical.md)).
