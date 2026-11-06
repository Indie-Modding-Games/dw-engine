# fishing

La pesca (`dwe::game::fishing`, capa `game`): la caña, el corcho, el sedal y los peces del agua de un mapa. Lo que hacía
el original está en la [ficha de pesca](../../reference/fishing.md).

No dibuja nada ni conoce la bolsa ni al partner: depende de `core`, `assets` y `ruleset`. Los números salen de la
sección `fishing` del ruleset (`rules/<id>/fishing.yaml`) y las tablas de `data/fishing`, que extrae `dwx` y la sesión
resuelve a `FishingTables` y `FishingPlace` (objetos por su número, `ItemEntry::index`). El juego no pasa tablas a
`createGameSystems`, que lee `data/fishing` de los paquetes como lee las cartas; antes ningún mapa tenía agua y el menú
de pausa nunca ofrecía Pescar (`route-etapa7-fishing.yaml`).

## Uso

```cpp
auto rules = dwe::game::fishing::parseFishingRules(*ruleset.section("fishing"));
dwe::game::fishing::Fishing water{*rules, tables, place};
water.start(dwe::game::fishing::Rod::Old);
auto out = water.tick(input, view, random);   // un fotograma del original (20 por segundo)
```

- **Fotograma**: `tick` mueve la caña (`FISH_tickRod`) y luego el agua (`FISH_tickFishingWorld`). Se llama solo sin
  ningún aviso abierto; los avisos salen como eventos `Message`. Con la caña guardada (sin `start`) solo mueve el
  agua: la app lo llama en cada fotograma del mapa, no solo al pescar (ver «El agua sin la caña»).
- **Pantalla**: `FishingView` pasa del mundo a la pantalla del fondo (píxeles desde su centro óptico, y hacia abajo)
  y al revés; las zonas de los peces y el agua están en esa pantalla, como en el original.
- **Entrada**: `FishingInput` trae los botones del fotograma y dónde están la punta de la caña, la mano y el cebo, que
  pone el modelo del jugador.
- **Salida**: `FishingOutput` trae los eventos (sonidos, animaciones del jugador, avisos, el menú del cebo, el cebo
  comido, la captura, lo enganchado, la serpiente en tierra, el final) y el giro del jugador en unidades del original.
  La sesión aplica a la bolsa y al partner lo que toca (`session::FishingAdapter::apply`).

## El agua sin la caña

En el original el agua es un objeto más del campo desde que se carga el mapa (`checkFishingMap` → `FISH_init` →
`addObject(FISH_tickFishingWorld, FISH_renderFishingWorld)`), no algo del minijuego: los peces y la serpiente nadan y se
ven siempre en el lago. La app lo hace igual (`PlaySession::tickWater` → `FishingScreen::tickWater`), después del
campo y la sesión de cada tick, como `tickObjects` corre los objetos en el orden en que se añadieron (el jugador y el
partner antes que el agua); así las tiradas de los peces van tras las del campo en cada fotograma.

| Regla | Decomp | Aquí | Test | Estado |
|---|---|---|---|---|
| El agua avanza 20 veces por segundo desde que se entra al mapa, con la caña guardada, mientras se anda y con el menú de pausa abierto | `FISH_init`, `FISH_tickFishingWorld`, `tickObjects` (`gameLoop`, `pauseFrame`) | `PlaySession::tickWater`, `FishingScreen::tickWater` | `fishing_water_tests`: rod away | Igual. Medido en el lago (`fish_mayo06_rod_ready`, caña guardada): 32 peces en la reserva; su contador de fotogramas sube 20 cada 60 vblanks andando y también con el menú abierto. Antes dwe solo movía y dibujaba los peces con la caña fuera |
| Los peces, la serpiente, las ondas y las marcas se dibujan siempre; la caña, el corcho, el sedal y el cebo solo al pescar | `FISH_renderFishingWorld` | `FishingScreen::showPrims` | captura `w20-fishC` | Igual. Peces como manchas azul oscuro junto a la orilla en los dos |
| Con la pesca apagada en el mapa no se mueve, no tira y no se dibuja | `FISH_tickFishingWorld`, `FISH_renderFishingWorld` (`fishingDisabled`) | `Fishing::tick`, `showPrims` | switched off | Igual |
| Mientras el partner evoluciona (estado 13) el agua ni avanza ni se ve | `FISH_tickFishingWorld`, `FISH_renderFishingWorld` (`PARTNER_STATE == 0xd`) | `PlaySession::tickWater` → `hideWater` | - | Igual en la regla; sin medir |
| En combate el agua sigue: avanza y se dibuja como en el campo, y tras el combate sigue igual (el mapa no se recarga) | `BTL_battleTickFrame` (`tickObjects`, `renderObjects`), `startBattle` | `PlaySession::tickWater` | captura `w21-fishD` | Igual en la regla. Ningún combate del juego ocurre en un mapa con agua (`mayo06` solo tiene a la serpiente amistosa, `mayo10` ningún NPC). Medido forzando uno (agua del lago puesta al empezar el combate de `species_bg` en `mayo02`): `FISH_tickFishingWorld` corre desde `tickObjects` mientras se carga el combate (`GAME_STATE` 3) y, en cuanto el código del combate (`BTL_REL`, en 0x80052EE0, 0x2349C bytes) pisa el de la pesca (`FISH_REL`, en 0x80070400), su llamada a `FISH_isTextBoxVisible` salta a mitad de `BTL_initializeItemParticleVelocities` y el juego se cuelga en la BIOS. dwe no tiene ese límite y sigue la regla del código. Antes el agua se paraba en combate |
| Cuando el compañero muere de viejo en un mapa con agua, el agua desaparece (peces, serpiente, ondas) hasta que se vuelve a cargar un mapa | `partnerTickDying` (`deinitializeFishing`) | `DeathScene::begin` → `FishingAdapter::leaveWater`, `FishingScreen::tickWater` | `fishing_adapter_tests`: natural death | Igual. Medido en el lago (`fish_mayo06_rod_ready`, vida 1 a las 9:59): en el primer fotograma de `partnerTickDying` `FISHING_DATA_PTR` pasa a NULL y las rayas de la serpiente dejan el lago; la muerte acaba en otro mapa (`room08`). La pérdida de una vida en combate o por enfermedad (`MURD_tick`) no la quita. Antes dwe solo la apagaba (`setFishingDisabled`) y la seguía dibujando quieta |

## Cámara, sedal y serpiente marina

La cámara de la pesca es la del original: `Fishing` guarda el centro de la vista en píxeles del fondo
(`cameraCentre`) y la app centra ahí la cámara del mapa (`FieldScene::setCameraPixel`). Las funciones puras están en
`view.hpp`; la app dibuja el sedal y la serpiente con `ScenePrim` (`FishingScreen::showPrims`).

| Regla | Decomp | Aquí | Test | Estado |
|---|---|---|---|---|
| Al sacar la caña la vista sigue donde la dejó el campo; al acabar vuelve a centrar al jugador | `FISH_enableCameraControl`, `FISH_applySavedCameraScroll`, `FISH_endFishing` | `Fishing::tick`, `endFishing` | ruta de pesca | Igual. Medido en el lago (fondo 1280 x 256): centro (480, 8) en el original y en dwe |
| La vista sigue al corcho 16 píxeles por fotograma con zona muerta 40 x 30; al jugador, sin zona muerta; a la captura, 32 | `FISH_panCameraTo` y su llamada en cada estado de `FISH_tickRod` | `panCameraTo`, `Fishing::panTo` | `fishing_view_tests`: dead zone, radius | Igual. Corcho colgando: el original sube la vista de y 8 a 4, dwe a 6 (la mano del modelo no es la del original). En vuelo la vista lo sigue un fotograma tras otro en los dos (original 480, 478, 473...; dwe 480, 476, 453... con otro lanzamiento) |
| Con la tecla de cámara (E; mando: Norte; el círculo del original) las flechas mueven la vista 5 píxeles por fotograma | `FISH_panCameraWithDPad` | `panCameraWithDPad`, `Fishing::panWithDPad` | d-pad | Igual. Medido: 45 píxeles en 9 fotogramas en el original; 50 en 10 en dwe; sin vuelta si el corcho queda en la zona muerta |
| Con un aviso abierto la vista va al corcho (o a la captura) 16 píxeles por fotograma | `FISH_scrollViewToTarget`, `FISH_showTextBox` | `Fishing::scrollToNotice` | - | Igual |
| El sedal va de la punta de la caña al corcho, recortado al borde de la vista y en trozos de 64 píxeles, del color de la tensión (blanco en reposo) | `FISH_drawFishingLine`, `FISH_clipLineToRect`, `FISH_updateTension` | `fishingLine`, `clipLineToRect`, `tensionColor` | clip, pieces, colour | Igual. La punta es la de la caña (`FISH_updateRodPose`) |
| Los peces aparecen en cualquier zona del fondo, no solo en la vista | `FISH_isScreenPointVisible`, `FISH_randomPointInSpot` | `CameraView::screenMin/screenMax` | ruta de pesca | Igual. Antes la vista solo cubría lo visible y cada zona fuera de ella dejaba «no water in sight» en el log; ahora ninguno |
| La serpiente: cinco piezas, cada una girada como la cabeza un fotograma después que la anterior, a 200, 175, 125 y 140 de distancia | `FISH_pushSeadramonSegment`, `FISH_drawSeadramon` | `pushSeaSerpentTurn`, `seaSerpentBody` | sea serpent | Igual en la forma. Suposición: sus caras leen un trozo de 8 x 8 de la página de textura que está vacío en la imagen extraída; se resta blanco, que da la silueta negra medida. Cada serpiente nueva empieza recta (en el original la historia es global). Las «rayas negras finas» que se ven sobre el agua a las 8 y a las 17 son esta serpiente vista desde la orilla: el original las muestra igual (medido en `fish_mayo06_rod_ready` a las 8: rayas negras de 1 a 2 píxeles y unos 70 de largo a 320 x 240; quitándola, desaparecen en los dos). Como dwe empieza a las 8:00, salía en todas las pruebas |
| Con la serpiente en tierra la pesca se apaga en el mapa y empieza su escena | `tamerTickFishing`, `triggerSeadramonCutscene` | evento `SeaSerpentLanded` (valor: `sea_serpent.scene_section`, 0x4e4) → `StoryAdapter::request` | - | Igual: la sección 0x4e4 del guion de cabecera salta a la 82 del guion 7; sale la serpiente y «What the... oh no! It's the Lake Guardian!», como en el original |

## Marcas, ondas, cebo y aviso

Las marcas de picada y las ondas son dos reservas de 16 (`effects.hpp`: `MarkerPool`, `RipplePool`) que `Fishing`
avanza en su fotograma de agua, antes de los peces; con un aviso abierto se quedan quietas. La app las dibuja con la
imagen de pesca del mapa (`sprite_file` de `data/fishing`: `texture/etcdat/fishing2` en el lago) y su escala
(`FishingMapEntry::scale`), y el cebo con el icono de su objeto (`FishingScreen::showPrims`). Medido en el lago
(`fish_mayo06_waiting`, escribiendo las reservas del original).

| Regla | Decomp | Aquí | Test | Estado |
|---|---|---|---|---|
| Al tirar la picada sobre un pez sale una marca: corazón si puede picar, cruz si no; vive 10 y se ve 9 fotogramas | `FISH_spawnMarker`, `FISH_tickMarker`, `FISH_tickMarkers`, `FISH_allocMarker` | `MarkerPool`, `Fishing::findFishToBite` | `fishing_effects_tests`: 9 frames, 16 at most | Igual. Medido: vida 10 en la tirada, 9 al acabar ese fotograma y 8 el siguiente (la caña va antes que el agua en el mismo fotograma). 16 x 16 píxeles de pantalla, con su punto abajo en el centro, delante de todo |
| Al engancharse un pez se borran las marcas | `FISH_clearMarkers` | `Fishing::hookFish` | 16 at most | Igual |
| Las ondas y salpicaduras pasan por su tabla y se quitan al acabar; el susto a los peces dura lo que su onda | `FISH_tickRipple`, `FISH_tickRipples`, `FISH_spawnRipple` | `stepRipple`, `RipplePool` | each kind, scaring ripple | Igual. La salpicadura dura 10 fotogramas; sin hueco libre no hay onda ni susto |
| Una onda es una tarjeta en pie sobre el agua a lo largo de x, sumando, de escala del mapa por la suya | `FISH_drawRipple` | `addWaterSprites` | rectangles | Igual. En el lago (escala 0x3800) un anillo mide 3,5 unidades por téxel: unos pocos píxeles, tenues |
| El cebo y la captura se ven con el icono de su objeto, un cuadrado de cara a la pantalla de 35 unidades a cada lado en el lago, solo sobre el agua | `FISH_showBaitSprite`, `FISH_drawBaitSprite` | `addBait` | ruta de pesca | Igual. Medido: la carne a unos 11 píxeles cerca del corcho |
| El aviso se cierra con Confirmar tras 10 fotogramas abierto, o con Cancelar al momento, y suena como confirmar | `FISH_tickTextBox`, `FISH_closeTextBox` | `ui/fishing_notice/logic.lua` | `fishing_notice_tests` | Igual. Un Confirmar antes de medio segundo no hace nada ni suena |
| El menú de cebo lista una fila por casilla de la bolsa con comida, en el orden de la bolsa y con la cantidad de esa casilla; dos montones del mismo objeto son dos filas | `FISH_openBaitMenu` | `session::baitRows`, `FishingScreen::openBaitMenu` | `fishing_adapter_tests` | Igual. Medido: Solomillo 3, un objeto que no es comida, Carne 5, Solomillo 2 y Carne gigante 4 dan Solomillo 3, Carne 5, Solomillo 2, Carne gigante 4. Antes dwe daba una fila por objeto en el orden de la tabla |
| Cada fila del menú de cebo pone la cantidad tras una x en dos huecos alineados a la derecha: «x 3», «x12» | `FISH_renderBaitMenu` (`renderItemAmount(0, 2, ...)`) | `ui/fishing_bait/logic.lua`, `fishing.bait.entry` | `fishing_bait_tests` | Igual. El original dibuja una x de 8 píxeles y la cantidad con sus cifras («Meat × 5» en `fish_mayo06_bait_menu`). En la fuente del original el código del × (0x817E, glifo 78) dibuja un triángulo (y el del △, el glifo 77, una cruz), así que con el aspecto original salía un triángulo; los dos aspectos usan ahora la x, que la fuente tiene |
| Elegir cebo, cancelar y los sonidos del menú | `FISH_tickBaitMenu`, `FISH_closeBaitMenu`, `FISH_isBaitMenuVisible` | `ui/fishing_bait/logic.lua`, `Fishing::chooseBait` | - | Mejora: una columna en páginas de ocho y los sonidos de mover, confirmar y cancelar de la interfaz común (el original: dos columnas de nueve filas y un solo sonido) |
| Al guardar la caña o dejar el mapa se cierran el menú de cebo y el aviso | `FISH_endFishing`, `FISH_closeBaitMenuIfOpen`, `FISH_shutdownTextBox`, `FISH_deinitialize` | `Fishing::endFishing`, `FishingScreen::enterMap` | - | Igual |

## Caña, corcho y peces

La app dibuja la caña, el corcho y los peces como el original (`fishing_look.cpp`); las cuentas puras están en
`look.hpp` y la animación del corcho en `view.hpp`. Medido en el lago (`fish_mayo06_aiming`, `fish_mayo06_waiting`).

| Regla | Decomp | Aquí | Test | Estado |
|---|---|---|---|---|
| La caña es el modelo `ETCDAT/SAO.TMD` (4 piezas) colgado del hueso 9 del jugador; sus juntas se doblan hacia el corcho según la tensión (128 en reposo, de 0 a 512 con la tensión); la punta es el inicio del sedal | `FISH_updateRodPose`, `FISH_drawRodSegments` | `rodPose`, `rodBend`, `fishingRodPose` | rod: four segments | Igual. Punta al apuntar: (-90, 798, 124) del jugador en el original, (-90 a -101, 795, 110 a 122) en dwe. La caña asombrosa copia las filas 88 a 95 de su textura sobre las 80 a 87 (`MoveImage`); dwe desplaza la coordenada v 8 filas |
| Al soltar el lanzamiento la caña toma la pose del lanzamiento antes de que el corcho salga de la punta | `FISH_tickRod` (release) | `Fishing::release`, `throwFromTip_` | ruta de pesca | Igual con un fotograma de retraso (dwe aplica la animación al fotograma siguiente). Punta al soltar: 36 detrás y 814 arriba en el original, 96 detrás y 797 arriba en dwe; antes salía 424 detrás y el corcho caía en la orilla |
| El corcho es un sprite de la imagen de pesca, de pie, a la escala del mapa (3,5 en el lago), uno por caña (17 a 23 la vieja, 24 a 30 la asombrosa) | `FISH_bobberCommand` (orden 1), `FISH_drawSprite3D` | `bobberSprite`, `spriteCorners` | each rod has its own sprites | Igual: punta naranja y cuerpo oscuro, unos 3 x 3 píxeles a 320 x 240 en los dos |
| El corcho cuelga (1), se hunde (1 a 5 y vuelve a 3), flota (3) y se mece (4, 3) una vez cada 60 fotogramas con una onda; al picar baja (4, 5, 4) y al morder se hunde (4 a 7 y desaparece); al perder el cebo vuelve (7 a 3) | `FISH_bobberCommand` (órdenes 2, 7, 8, 9, 0xb, 0xc, 0xe) | `BobberAnimation`, `Fishing::tickBobberAnimation` | bobber: sequences | Igual. Flotando se mueve un punto al azar en x y z la mitad de los fotogramas |
| Cada pez es un sprite plano sobre el agua con la cabeza hacia donde nada: pequeño (tamaño < 100) con 4 fotogramas cada 4 y escala `fish_scale_small`; grande con 8 cada 3 y `fish_scale_large`; restado con sombra transparencia × 30 / 128; el pez enganchado que lucha usa sus propios 6 fotogramas | `FISH_drawFishSprite`, `FISH_drawFish`, `FISH_drawHookedFish` | `fishLook`, `hookedStruggles`, `addFishingFish` | fish: frames, scale, shade | Igual. Con la cámara rasante del lago se ven como rayas finas azul oscuro: en el original un pez de tamaño 206 junto al corcho ocupa 37 x 3 píxeles (prim leído del original); en dwe los pequeños salen igual de finos |
| La pesca se apaga mientras el compañero evoluciona o muere y se vuelve a encender al acabar la evolución, al acabar la luz de la muerte y al acabar el renacer | `setFishingDisabled`, `setFishingEnabled`; llamadas en `partnerTickEvolving`, `partnerTickDying`, `partnerTickDying2` | `FishingAdapter::setFishingDisabled/Enabled`, `EvolutionScene`, `DeathScene` | - | Igual |
