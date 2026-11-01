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

Medido en el original (2026-10-31): las nubes y chispas del gimnasio (`TRN_tickDefenseTraining`, estado
`trn_def_normal_bonus`) las crea una rutina de la lista de objetos y `tickObjects` las avanza el mismo cuadro, así que
se dibujan por primera vez en el estado 1 (la chispa enseña su destello en el segundo cuadro dibujado y dura uno menos);
la nube de una caca (`createPoopPile`) se dibuja primero en el estado 0. `FieldEffects::Birth` lo distingue. Igual: la
partícula 0 del anillo de curación divide 512 entre 0 (`512 / (i * 13 / 4)`); el código del original no comprueba el
divisor (`div` sin `break`) y la división del procesador da -1 con un dividendo positivo, que es lo que se toma.

Suposiciones:

- Las 50 direcciones de las partículas y los números al azar usan una semilla propia, no la del juego.
- Una especie nueva sin `condition_fx` no tiene mariposa.
- La mariposa no se ha medido en pantalla (el estado de solo enfurruñado no se pudo forzar por RAM); sus números son
  los de `renderButterfly`.

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
de huevo y confirmar elige uno; no se puede salir sin elegir. Lo que no está: los sprites de los huevos en VRAM
(`EGG_ICONS`) y la carga del overlay; el documento enumera los huevos.

- Igual: con el aspecto original la caja crece en 5 cuadros a 20 por segundo desde el cuadrado de 16 píxeles del centro
  de la pantalla (`DOO2_openEggBox`: `createAnimatedUIBox(3, …)` desde `MAIN_D_80134B98` {-8, -8, 16, 16}) y, al elegir,
  encoge en otros 5 de vuelta a él (`removeAnimatedUIBox(3, MAIN_D_80134BA4)`), con el contorno gris 0x808080 de
  `renderUIBoxAnim` (`ui/common/original_box.lua`). Mientras crece no se mueve ni se elige huevo (`DOO2_tickEggInput`
  espera a que `DOO2_tickEggBox` ponga `sparkleIndex` a 0); al abrir no suena el sonido de caja sino el suyo
  (`stopSound`, `playSound(8, 4)`). La historia sigue cuando la caja ya encogió (`egg_box_closed`; el original devuelve 1
  con `UI_BOX_DATA[3].state` 0). Con otro aspecto la caja abre y cierra al momento.
- Hueco: la caja llega al rectángulo del panel del documento, no al de 168 x 80 píxeles (`MAIN_D_80134B90`
  {-84, -70}) con los 4 iconos en fila (`DOO2_renderEggIcons`); la confirmación y el movimiento durante el crecimiento
  aún suenan con los avisos genéricos de la UI.

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
  1000; de 0x149 a 0x17B vuelve por el mismo camino a la vista del mapa.
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
- Los desplazamientos de pantalla de la cámara de la muerte en el mapa 0xCD (0xBC en vez de 0xA0) no se aplican.
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
los guiones (renombrar al jugador o al compañero, `StoryNaming`). No corre en un autotest, como las películas.

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
(el círculo del original) a Atrás, y `extra` (su cuadrado) borra todo el nombre. A la vez se
puede teclear: las letras de la rejilla y el espacio se escriben (otra tecla suena rechazada) e Intro acepta el
nombre si se ha tecleado algo desde el último movimiento del cursor. El nombre del jugador empieza vacío, como en la
versión americana.

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

Diferencias con el original: en el mando, `alt` está en el botón norte y no en el del círculo, que aquí es cancelar;
las letras son de media anchura y no las de ancho fijo de la fuente del original; el mensaje de
enfermedad y los registros de combate y de la arena, que en el original nombran al compañero, aún no existen.

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
twna01, el HUD se oculta mientras está y vuelve al cerrarlo (igual en dwe). Hueco: el icono de confirmar de la
esquina del cuadro, como en el aviso de objetos. Las medallas son los disparadores 0x16c + medalla que la
página del jugador ya lee (`StoryAdapter::hasMedal`).

## Exploración libre

`free_exploration.cpp` lleva la [exploración libre](../../architecture/warps-de-script.md#exploración-libre):
la enciende el ajuste `free_exploration` (menú de pausa, guardado en los ajustes del usuario) o `--explore` (solo en
esa ejecución). Cuando el campo para en una casilla con varios sitios, abre `ui/destination_choice` con los sitios
(`exploration_place_name.N`, `exploration_place_map.N`, `exploration_places`, `exploration_story`) y pasa al campo
lo que se elige (`choose_destination`) o quedarse (`stay_here`).

## Movimiento suave

Mejora opcional, encendida por defecto: el ajuste `smooth_motion` (fila «Movimiento suave» de las opciones del menú
de pausa, guardado en los ajustes del usuario) llega a `Field::setSmoothMotion` desde `PlaySession`. Encendido, los
personajes y la cámara se dibujan deslizándose entre los fotogramas de 1/20 s del original (`Field::drawnPose`, ver
`docs/modulos/game/field.md`, «Interpolación»); apagado, cada uno se dibuja donde lo deja su tick, a saltos de
fotograma como el original. La lógica es la misma en los dos casos.

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
contenido oculto y 66 con el paso Mega); la caja de Ultra gana una tercera columna (y el panel se ensancha a 960dp) cuando una casilla cae a la derecha de la
última columna del original (`player.chart.wide`).
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
dice su etapa canónica, y las especies que la serie llama Mega (`MEGA_CELLS` en `ui/pause_menu/logic.lua`: Phoenixmon,
HerculesKabuterimon, y MetalEtemon y Machinedramon cuando el ruleset los trae) salen de la caja de Ultra a una segunda
página del árbol. Las casillas que dejan libres las ocupan, en orden, las de la tercera columna de Ultra que trae el
ruleset, así que la caja de Ultra vuelve a sus dos columnas. Para saber qué especie es cada casilla,
`publishPlayerChart` publica `player.chart.<n>.id`.

Página 1 es el árbol del original tal cual, con una flecha hacia abajo (la flecha hacia arriba de los sprites de
combate, `battle-up-arrow`, volteada como en la lista de partidas) bajo las cajas de Bebé I y Bebé II. Página 2 es la
caja Mega roja (`#chart-stage-5`) a toda la altura, en el centro y del ancho de la de Bebé I, con todas sus casillas
grises (una por especie Mega, separadas como las cuatro de Bebé I mientras quepan cuatro) y la flecha hacia arriba en
el mismo sitio. Para pasar de página:

| Cómo | De la 1 a la 2 | De la 2 a la 1 |
|---|---|---|
| Cursor en el árbol | Abajo (flecha ↓, mando abajo) en la última casilla de una columna; el cursor va a la primera Mega | Arriba en la primera Mega; el cursor vuelve a la casilla que dejó |
| Sin cursor (en la pestaña) | Abajo | Arriba |
| Ratón | Rueda hacia abajo sobre el árbol, o clic en la flecha | Rueda hacia arriba, o clic en la flecha |

El cambio es un deslizamiento de 0,2 s (`#chart-pages` sube 564dp dentro de `#chart-clip`, que recorta al interior del
marco): a 20 pasos por segundo como las cajas del original, o continuo con «Movimiento suave». Mientras dura no se
aceptan teclas, y al acabar la página que no se ve queda oculta (`.off-page`) para que el puntero no la toque. El
cursor y el detalle funcionan igual en las dos páginas: elegir una Mega abre su árbol con la etapa «Supremo». La rueda
llega a Lua con `event.wheel` ([editar menús](../../guides/editar-menus.md)). Sin especies Mega en el árbol (opción
apagada) no hay flecha ni página 2, y el árbol es el del original píxel a píxel.

Apagado, el árbol es el del original y sus nombres los de la [decisión 4 del glosario](../../guides/glosario-es.md);
el nivel 5 se llama «Ultra» también en el árbol (antes «Definitivo»).

La lista de opciones ya no cabe entera a 1280x720 en ninguna piel: se desplaza (`#page-options`, `overflow-y: auto`),
la navegación lleva la fila enfocada a la vista y la rueda del ratón la mueve. La comprobación de textos no cuenta las
filas que la lista deja fuera de su caja (`text_fit.cpp`); se comprueban al llegar a ellas.

`route-canonical-stages.yaml` (en/es, piel original y moderna) enciende la opción con el contenido oculto, captura el
árbol (página 1 con la flecha), baja a la página 2 con la caja Mega y abre el detalle de Phoenixmon («Supremo»), vuelve
a la página 1 y abre el de MetalGreymon, la apaga y captura el árbol original; `route-mega-step.yaml` captura la página 2
con los cuatro Megas y el detalle de Machinedramon
([prueba vertical](../../guides/prueba-vertical.md)).
