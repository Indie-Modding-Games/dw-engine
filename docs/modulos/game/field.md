# field

La lógica del campo (`dwe::game::field`, capa `game`): el jugador se mueve por un mapa con intenciones, choca con la
rejilla de colisión del original, pisa disparadores y warps, y el partner le sigue. Lo que hacía el original y en qué
se separa el motor está en la [ficha del campo](../../reference/campo.md).

No dibuja, no lee dispositivos de entrada ni ficheros: la app le da la entrada y los `MapDocument` (de
[`assets`](../engine/assets.md#mapas)) y lee el estado. Jugador y partner son entidades del `scene::World`
([`scene`](../engine/scene.md)) con los componentes que la app dibuja: `Transform`, `ModelRef`,
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
- **Filtro de salidas**: `setExitFilter(filtro)` decide si un cambio de mapa del jugador (warp pisado, warp de script
  de una casilla, elección de la exploración libre) puede ocurrir; se pregunta antes del fundido y antes de escribir
  disparadores. Uno cerrado deja al jugador en su sitio y `takeClosedExit()` lo da una vez. Lo usa el candado de
  historia ([session](../apps/session.md)); sin filtro todo pasa.
- **Fundido y nombre**: el cambio de mapa cuenta en fotogramas del original. `fadeLevel()` dice cuánto quita el
  fundido de la pantalla (0 nada, 1 negro), con la curva de `fade.c` (`fadeOutShade`, `fadeInShade` y
  `shadeDarkness` en `rules.hpp`): la app lo resta sobre la escena (`render::ScreenFade`), no lo mezcla con negro.
  `mapNameShown()` da el mapa cuyo nombre se ve, del fotograma 10 del fundido de salida hasta entrar en el mapa
  nuevo; la app lo busca en la tabla de mapas y el HUD lo pone delante del fundido.
- **Warps de script** (`ScriptWarpSetup`): los de `field/script_warps` con `GuardFacts` (los lectores de
  disparadores, bolsa, dinero, estadísticas y condiciones del partner) y un `TriggerWriter` que recibe los
  `sets_flags` del warp tomado; después el campo vuelve a decidir las casillas del mapa (`refreshScriptWarps`).
  `keepsOff(trigger)` dice qué casillas evita el clic para ir (no la del warp en la que está el jugador).
  Ver [warps de script](../../architecture/warps-de-script.md).
- **NPC que ven al jugador** (`FieldNpcs`, con `npc.tracking` en `field/rules`): según el `followMode` de su
  registro, un NPC mira al jugador, corre adonde lo vio y vuelve, o lo sigue mientras está en su radio
  (`overworld.c: NPCEntityTickOverworld`). Mientras reacciona no sigue su ruta. Si un NPC con `autotalk` choca con
  el jugador, el contacto (`contacts()`) lanza su sección del guion, que es la que empieza el combate. El original
  no tiene encuentros aleatorios: todos los enemigos del campo son NPC del mapa.
- **Exploración libre** (`setExplore`, apagada por defecto): un warp de script lleva a cualquiera de los sitios de
  su casilla (`explorationWarps`). Con varios, el campo se para y `destinationChoice()` da el mapa, la casilla, los
  sitios y el de vainilla; `chooseDestination(índice)` toma uno y `chooseDestination(std::nullopt)` se queda.

## Datos

- `field/rules` (`content/packs/game/field/rules.yaml`, [esquema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/field_rules.schema.json)):
  radios, velocidades y giro del jugador y del partner, las distancias de seguimiento, el rastro y los límites para
  desatascarlo, la zona muerta del stick, el cambio de mapa (fotogramas, tonos del fundido y fotograma del nombre),
  las horas del día, y cómo se mueven los NPC y cómo reaccionan al jugador (`npc`).

El campo no tiene reloj propio: `gameTimeRan()` dice si el último tick corrió tiempo de juego (no lo corre durante
un cambio de mapa, `tamer.c: stopGameTime`/`startGameTime`), y quien lleva el bucle avanza `ruleset::GameClock` con
él. En el juego lo hace la partida ([apps/session](../apps/session.md)).
- `field/warps` (`content/packs/game/field/warps.yaml`, [esquema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/field_warps.schema.json)): el
  mapa y la salida de inicio, y los warps que en el original hacía un script, cada uno con su prueba.

Los dos se pueden cambiar desde un paquete de mod con el mismo id.

## Tests

`grid_tests.cpp` (colisión con trayectorias fijas, deslizamiento, sin atravesar paredes, caminos), `field_tests.cpp`
(movimiento, warps con su fundido y su nombre fotograma a fotograma, seguimiento y desatasco del partner) y
`rules_tests.cpp` (lectura de los YAML, también los del paquete `game`, y la curva del fundido) usan mapas de prueba. `route_tests.cpp` recorre la ruta de la fase 2 en el contenido extraído si
`DWE_CONTENT` apunta a él: File City (`twna01`) → casa de Jijimon (`room08`) → File City, con el stick y con clics.
