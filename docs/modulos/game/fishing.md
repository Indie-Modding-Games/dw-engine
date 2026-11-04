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
  ningún aviso abierto; los avisos salen como eventos `Message`.
- **Pantalla**: `FishingView` pasa del mundo a la pantalla del fondo (píxeles desde su centro óptico, y hacia abajo)
  y al revés; las zonas de los peces y el agua están en esa pantalla, como en el original.
- **Entrada**: `FishingInput` trae los botones del fotograma y dónde están la punta de la caña, la mano y el cebo, que
  pone el modelo del jugador.
- **Salida**: `FishingOutput` trae los eventos (sonidos, animaciones del jugador, avisos, el menú del cebo, el cebo
  comido, la captura, lo enganchado, la serpiente en tierra, el final) y el giro del jugador en unidades del original.
  La sesión aplica a la bolsa y al partner lo que toca (`session::FishingAdapter::apply`).

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
| El sedal va de la punta de la caña al corcho, recortado al borde de la vista y en trozos de 64 píxeles, del color de la tensión (blanco en reposo) | `FISH_drawFishingLine`, `FISH_clipLineToRect`, `FISH_updateTension` | `fishingLine`, `clipLineToRect`, `tensionColor` | clip, pieces, colour | Igual. La punta sale de la mano del jugador (la caña aún no se dibuja) |
| Los peces aparecen en cualquier zona del fondo, no solo en la vista | `FISH_isScreenPointVisible`, `FISH_randomPointInSpot` | `CameraView::screenMin/screenMax` | ruta de pesca | Igual. Antes la vista solo cubría lo visible y cada zona fuera de ella dejaba «no water in sight» en el log; ahora ninguno |
| La serpiente: cinco piezas, cada una girada como la cabeza un fotograma después que la anterior, a 200, 175, 125 y 140 de distancia | `FISH_pushSeadramonSegment`, `FISH_drawSeadramon` | `pushSeaSerpentTurn`, `seaSerpentBody` | sea serpent | Igual en la forma. Suposición: sus caras leen un trozo de 8 x 8 de la página de textura que está vacío en la imagen extraída; se resta blanco, que da la silueta negra medida. Cada serpiente nueva empieza recta (en el original la historia es global) |
| Con la serpiente en tierra la pesca se apaga en el mapa y empieza su escena | `tamerTickFishing`, `triggerSeadramonCutscene` | evento `SeaSerpentLanded` (valor: `sea_serpent.scene_section`, 0x4e4) → `StoryAdapter::request` | - | Igual: la sección 0x4e4 del guion de cabecera salta a la 82 del guion 7; sale la serpiente y «What the... oh no! It's the Lake Guardian!», como en el original |
