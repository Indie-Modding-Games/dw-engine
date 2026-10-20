# field

La lógica del campo (`dwe::game::field`, capa `game`): el jugador se mueve por un mapa con intenciones, choca con la
rejilla de colisión del original, pisa disparadores y warps, y el partner le sigue. Lo que hacía el original y en qué
se separa el motor está en la [ficha del campo](../../docs/reference/campo.md).

No dibuja, no lee dispositivos de entrada ni ficheros: la app le da la entrada y los `MapDocument` (de
[`assets`](../../engine/assets/README.md#mapas)) y lee el estado. Jugador y partner son entidades del `scene::World`
([`scene`](../../engine/scene/README.md)) con los componentes que la app dibuja: `Transform`, `ModelRef`,
`AnimationIntent` y `PlayerTag` o `PartnerTag`.

## Uso

```cpp
dwe::scene::World world;
auto rules = dwe::game::field::loadFieldRules(vfs);   // field/rules
auto warps = dwe::game::field::loadFieldWarps(vfs);   // field/warps
dwe::game::field::Field field{world, *rules, warps->scriptedWarps};
field.enterMap(*dwe::assets::loadMapDocument(vfs, warps->startMap), warps->startExit);

// Cada tick fijo:
field.setInput({.stick = stick, .run = run});          // o .moveTarget = punto del suelo al hacer clic
field.tick(1.0F / 60.0F);
if (auto change = field.takeMapChange()) {
    field.enterMap(*dwe::assets::loadMapDocument(vfs, change->map), change->exit);
}
```

- **Entrada** (`FieldInput`): `stick` es una dirección relativa a la cámara del mapa (x a la derecha de la pantalla,
  y hacia el fondo), de longitud 0 a 1; `run` corre; `moveTarget` es el clic para ir, que sigue un camino A* por la
  rejilla hasta llegar, hasta que se use el stick o hasta `cancelTarget`.
- **Estado**: `mapId()`, `player()` y `partner()` (posición, yaw y movimiento), `collision()` para el minimapa,
  `path()` y `cameraForward()` para depurar. Cada tick, `AnimationIntent` de las dos entidades dice qué animación
  poner (quieto, andar, correr) y a qué velocidad, y `Transform` guarda la posición anterior para interpolar.
- **Cambios**: `takeMapChange()` da una vez el warp pisado (`MapChange`: mapa, salida y disparador); mientras está
  pendiente nada se mueve. `triggerSteps()` da los disparadores que no son warps pisados en el último tick, para los
  scripts de más adelante.

## Datos

- `field/rules` (`content/packs/game/field/rules.yaml`, [esquema](../../content/schemas/field_rules.schema.json)):
  radios, velocidades y giro del jugador y del partner, las distancias de seguimiento, el rastro y los límites para
  desatascarlo, y la zona muerta del stick.
- `field/warps` (`content/packs/game/field/warps.yaml`, [esquema](../../content/schemas/field_warps.schema.json)): el
  mapa y la salida de inicio, y los warps que en el original hacía un script, cada uno con su prueba.

Los dos se pueden cambiar desde un paquete de mod con el mismo id.

## Tests

`grid_tests.cpp` (colisión con trayectorias fijas, deslizamiento, sin atravesar paredes, caminos), `field_tests.cpp`
(movimiento, warps, seguimiento y desatasco del partner) y `rules_tests.cpp` (lectura de los YAML, también los del
paquete `game`) usan mapas de prueba. `route_tests.cpp` recorre la ruta de la fase 2 en el contenido extraído si
`DWE_CONTENT` apunta a él: File City (`twna01`) → casa de Jijimon (`room08`) → File City, con el stick y con clics.
