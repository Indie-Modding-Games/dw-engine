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
