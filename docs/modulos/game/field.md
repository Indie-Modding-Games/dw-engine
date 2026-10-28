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
  no tiene encuentros aleatorios: todos los enemigos del campo son NPC del mapa. Mientras un guion retiene el campo,
  los NPC no hacen su tick, pero su `Transform` empieza cada tick igual (`FieldNpcs::beginTick`): así un NPC que el
  guion lleva andando se dibuja donde está, no entre ese punto y donde estaba al empezar el guion.
- **Escenas de cuidado** (`holdForCare`): retienen al jugador sin parar el tiempo de juego, como el estado 6 del
  tamer en el original. No hay entrada ni casillas, y el compañero no sigue al jugador. `walkPartnerToRange(rango)`
  lleva al compañero hasta esa distancia del jugador: se acerca si está más lejos y se aleja si está más cerca
  (`partner.c: partnerTickFeedItem`, `checkEatDistance`). Al llegar, los dos se miran. Si se atasca, se para donde
  está. `runPartnerUp()` hace correr al compañero hacia el jugador hasta su distancia de correr, como antes de una caca
  en el campo (`partner.c: tickWildPoop`, `getPartnerTamerCloseness > 0`); si ya está, no se mueve.
- **Al acabar un guion o una charla** (`holdForScript(false)`, adaptación al PC del 2026-10-28): el original deja al
  compañero donde lo dejó el guion o donde estaba al hablar (`script_interp.c: scriptPauseGame`, `tickScript` no
  devuelve a nadie a su sitio; `partner.c:971`, la cercanía al jugador, lo tiene quieto más cerca de 2,5 radios), así
  que podía quedar pegado al jugador, metido en el NPC con el que hablaba o entre la cámara y el jugador, tapándolo.
  Aquí, mientras el jugador sigue quieto, el compañero se aparta andando (`Field::makeRoomForPlayer`): primero de
  lado si tapa al jugador desde la cámara, y luego hacia fuera hasta la mitad entre tocar al jugador (radio del
  jugador + el suyo) y su distancia de pararse; después vuelve a seguir como el original. Si el jugador echa a andar,
  lo sigue como siempre; si no tiene sitio, se queda. El NPC no se mueve en la charla y sigue mirando al jugador al
  acabar, como en el original (`overworld.c: tickWaypointWait` no lo gira). También, al seguir el rastro (propio del
  motor) el compañero ya no se mete en el jugador: el rastro acaba al tocarlo (como `partner.c:906` al acabar un
  paso).
- **Exploración libre** (`setExplore`, apagada por defecto): un warp de script lleva a cualquiera de los sitios de
  su casilla (`explorationWarps`). Con varios, el campo se para y `destinationChoice()` da el mapa, la casilla, los
  sitios y el de vainilla; `chooseDestination(índice)` toma uno y `chooseDestination(std::nullopt)` se queda.

## Posiciones

Dónde quedan los personajes (jugador, compañero y NPC) unos respecto a otros y a las paredes.

- **Vigilancia de solapes** (propia del motor, para depurar y para las rutas de prueba): en cada tick en que el
  jugador se mueve libre (sin guion, sin cuidado, sin cambio de mapa ni fundido que lo retenga), `Field` busca
  parejas de personajes cuyos cuadrados de colisión se pisan a fondo y personajes con el centro en una casilla
  bloqueada. Un solape que dura más de un segundo se registra una vez por episodio
  (`[positions] overlap a=player b=partner ticks=61 on mayo03`) y cuenta en `overlapEpisodes()`; la ruta lo lee con
  `expect_setting: {field.overlaps: 0}`. «A fondo» es pisarse más de la mitad de la suma de los medios lados en los
  dos ejes (`Field::overlaps`): los cuadrados del original (`map.c: entityCheckEntityCollision`, medio lado = radio de
  la especie) son anchos, y un compañero parado en diagonal a 2,5 radios del jugador (`partner.c:971` mide un
  círculo) ya los pisa por la esquina sin que se vea. El segundo de margen deja pasar al jugador por encima del
  compañero (lo cruza en medio segundo andando).
- **Regla de suelta** (Mejora documentada: el original no tiene nada parecido; deja a cada uno donde lo dejó quien lo
  retenía). Todo lo que retiene a los personajes y los suelta llama a `Field::releaseHandoff(motivo)`: el final de un
  guion o de una charla (`holdForScript(false)`, también el del gimnasio), el de una escena de cuidado
  (`holdForCare(false)`), el final de un combate (`FieldScene::releaseFighters`, también el de un combate fijo y
  después de que los que huyen salgan andando), la entrada en un mapa por un warp o al cargar (`enterMapAt`). Un NPC
  puesto en el mapa (`FieldNpcs::spawn`) o soltado por un guion (`hold(id, false)`, `holdAll(false)`) lo pide por su
  cuenta. La suelta:
  - olvida el rastro del compañero, su tiempo atascado y la última casilla del jugador (`trail_`,
    `partnerStuckSeconds_`, `lastPlayerCell_`): lo que el compañero seguía antes ya no vale;
  - en el siguiente tick sin retención, `separateActors` mira a todos: quien tiene el centro en una casilla bloqueada
    o pisa a fondo a otro (`Field::overlaps`) va al sitio libre más cercano (anillos cada 25 unidades hasta seis
    casillas, primero en la dirección contraria a lo que pisa) donde cabe en la rejilla (`fitsOnGrid`), no toca a
    nadie, no es un warp y se llega en línea recta sin pared (salvo si sale de una pared), y se coloca de golpe
    (`scene::place`, sin interpolar el salto). Se mueve primero el compañero (cede ante todos), luego los NPC en orden
    de ranura (ceden ante el jugador, el compañero y los NPC anteriores); el jugador solo sale de una pared. Un NPC
    movido empieza su ruta otra vez (`restartPath`, como `clearMapAITable`); lo mismo los NPC que lucharon. Si no
    hay sitio, se queda y se registra (`[positions] release ... no free spot`).
  - Tras un guion o una charla espera a que el compañero termine de apartarse andando (`makeRoomForPlayer`, la
    salida suave); el salto solo llega si después sigue pisando a alguien.
- **Interpolación** (del motor: el original dibuja cada personaje en su sitio de cada fotograma, sin nada en medio).
  La lógica corre a 60 ticks por segundo y el dibujo interpola entre el sitio anterior y el actual de cada
  `Transform`. Cada tick de la partida empieza con `Field::settle` (`FieldScene::settleActors`, al principio de
  `PlaySession::tick`), que hace anterior = actual en jugador, compañero y NPC; también `Field::tick` lo hace siempre,
  aunque solo corra el fundido de un cambio de mapa o la espera del mapa nuevo. Así, en los ticks en que el campo no
  corre (combate para los que no luchan, evolución, sueño, pesca, menús, aviso de objeto) nadie se dibuja deslizándose
  desde donde lo dejó un tick viejo. Un salto de guion se dibuja de golpe: `placeByScript` a más de media casilla y
  `turnByScript` de más de un octavo de vuelta igualan también el anterior; los pasos cortos (el gimnasio, el final de
  un deslizamiento) se siguen interpolando en su tick. `playerFacePartner` y el mirarse al acabar un paseo de cuidado
  (`entityLookAtLocation`) giran de golpe, como el original.
- **Sitios de aparición**: el compañero al entrar por un warp (a `spawnDistance` del jugador) y al ponerse junto al
  jugador cuando se atasca o se aleja (`placePartnerNearPlayer`) prueba la rejilla y además que no pisa a fondo a
  nadie; si no, prueba el siguiente sitio. Un NPC aparece donde dice su registro y, si pisa a alguien, la regla de
  suelta lo aparta en el tick siguiente.

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
(movimiento, warps con su fundido y su nombre fotograma a fotograma, seguimiento, desatasco y sitio tras un guion del
partner) y
`rules_tests.cpp` (lectura de los YAML, también los del paquete `game`, y la curva del fundido) usan mapas de prueba. `route_tests.cpp` recorre la ruta de la fase 2 en el contenido extraído si
`DWE_CONTENT` apunta a él: File City (`twna01`) → casa de Jijimon (`room08`) → File City, con el stick y con clics.
