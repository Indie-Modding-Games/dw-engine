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
  rejilla hasta llegar, hasta que se use el stick o hasta `cancelTarget`. Un clic mientras algo sujeta al jugador (un
  script, el cuidado, un elegidor, el cambio de mapa o su fundido) no planifica nada, y el destino que hubiera se
  suelta (`dropMoveTarget`): al soltarlo, el jugador no echa a andar solo. `PlaySession` lo suelta también en cada tick
  en que el campo no lleva al jugador (una pantalla como el gimnasio, un menú, el combate). El camino sale también de un punto más
  cerca de una pared que la sonda del personaje (la llegada de mgen12 desde mgen11): si ningún punto de alrededor se
  alcanza en línea recta, valen los que se pueden pisar.
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
  `shadeNow()` da el tono que guarda la pantalla (`FADE_PROGRESS`) y `setShade()` lo cambia cuando lo dibuja otro
  fundido: `WhiteFade` (`white_fade.hpp`) es el fundido a blanco de las rutinas 45 y 46 (`fade.c: fadeToWhite`,
  `fadeFromWhite`), que sube `160 / (fotogramas + 1)` por fotograma desde ese tono hasta 160 y vuelve desde 160 menos
  un paso, dibuja sus fotogramas y se quita al siguiente (con 40 acaba en 40, no en 0).
- **Llegada del jefe final** (`boss_arrival.hpp`, `BossArrival`, overlay EAB en fotogramas del original): la rutina 40
  (`EAB_startBuildup`) quita al jefe del mapa y empieza la carga, que añade un anillo cada 6 fotogramas (`EAB_tickBuildup`;
  vive 18, así que hay 3 a la vez) y nunca acaba sola; `bossRingScale` da su tamaño (de 0x10b8 a 0x614 de ancho y de
  0x15c a 0xd98 de alto, con `_sin` de 512 por vuelta). `effectLerp` es el `lerp` del original con su vuelta pasado
  `t1`. Suposición sin medir (no hay estado guardado cerca del jefe): el anillo nuevo cuenta un fotograma en el mismo en
  que nace, como si su objeto fuera detrás del de la carga en la tabla de objetos.
  La llegada (rutina de compañero 8, `EAB_tick` y `EAB_tickSpawn`) cuenta desde el fotograma siguiente: del 45 la
  pantalla se resta a negro (`blackout`); en el 50 el jefe vuelve con la animación 0x21, se para el zumbido y suena el 2,
  el jefe pasa a `vabId` 5 (`bossSoundSlot`: el banco 4 de sus clips suena de la ranura 5, la que cargó la rutina 51;
  `FieldScene::setNpcSoundSlot`),
  la carga se quita y solo se ve el jefe (`othersHidden`, hasta el 319) con la cámara girando (`orbit`, del 50 al 318:
  de 0x71c a 0x171c de ángulo hasta el 290, de 0xdac a 0xbb8 de distancia y de 0x258 a 0x32 de altura hasta el 320);
  el negro vuelve a 0 en el 82; cada fotograma pone un anillo de llegada (vive 9) y 9 de cada 18 una partícula verde
  (vive 86, crece de 8 a 0x9c4); en el 210 la animación 0x33; en el 320 vuelve todo, se quitan las partículas y suena el
  3; los anillos siguen hasta el 329 y en el 340 la llegada acaba (`arrivalOver`). `solidShares` da cuánto del jefe se
  dibuja sólido (de 0 a 16 hasta el 290), que el motor aún no usa. Suposición: sin medir en el original.
  `mapNameShown()` da el mapa cuyo nombre se ve, del fotograma 10 del fundido de salida hasta entrar en el mapa
  nuevo; la app lo busca en la tabla de mapas y el HUD lo pone delante del fundido. El fundido de salida sigue
  desde el tono que dejó el último (`FADE_PROGRESS`), también si corta un fundido de entrada a medias.
  `startPlay()` es el arranque de la partida en su primer mapa (`main.c: main: fadeFromBlack(0x28)`): 40
  fotogramas (`start_fade_in_frames`) sin retener a nadie, así que las casillas corren desde el primero.
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
  parejas de personajes cuyos cuadrados de colisión se pisan a fondo y al jugador o al compañero con el centro en una
  casilla bloqueada (un NPC en una pared está donde el original lo tiene: ver «Regla de suelta»). Un solape que dura más de un segundo se registra una vez por episodio
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
  - olvida el rastro del compañero, su tiempo atascado y la última casilla del jugador
    (`PartnerFollow::forgetTrail`, `partnerStuckSeconds_`, `lastPlayerCell_`): lo que el compañero seguía antes ya
    no vale;
  - en el siguiente tick sin retención, `separateActors` mira al compañero y al jugador: el compañero con el centro en
    una casilla bloqueada o que pisa a fondo a otro (`Field::overlaps`), y el jugador solo si está en una pared, van
    al sitio libre más cercano (anillos cada 25 unidades hasta seis casillas, primero en la dirección contraria a lo
    que pisa) donde cabe en la rejilla (`fitsOnGrid`), no toca a nadie, no es un warp y se llega en línea recta sin
    pared (salvo si sale de una pared), y se coloca de golpe (`scene::place`, sin interpolar el salto). Si no hay
    sitio, se queda y se registra (`[positions] release ... no free spot`). Los NPC que lucharon empiezan su ruta
    otra vez (`restartPath`, como `clearMapAITable`).
  - **Los NPC no se mueven nunca** (Igual: el original no mueve a un NPC que no camina por su ruta, su reacción o un
    guion). Se quedan donde los puso el mapa, su ruta o un guion aunque estén en una casilla bloqueada o pisen a
    alguien; los demás los rodean. Medido en DuckStation: Kunemon en su cama de mayo02 (sección 6 de su guion, la
    casilla es pared) sigue en (143, -1000) con la orientación 1024 al abrir la charla, durante ella (pose 29) y 300
    fotogramas después de cerrarla (pose 28 que pone el guion). Antes la suelta lo sacaba a (-64, -819) al entrar al
    mapa y a (22, -725) tras cada charla; 81 NPC de 989 nacen en una pared (camas, mostradores de tiendas como el
    Agumon del banco en room02, que salía delante del mostrador). Un NPC sobre el jugador se queda ahí; el jugador se
    aparta andando.
  - Tras un guion o una charla espera a que el compañero termine de apartarse andando (`makeRoomForPlayer`, la
    salida suave); el salto solo llega si después sigue pisando a alguien.
- **Fotogramas del original** (Igual, `tamer.c: tamerTickWalkingState`, `partner.c: partnerTickWalking`,
  `anim.c: startAnimation`): la lógica del original corre a 20 fotogramas por segundo y los personajes avanzan con el
  impulso de la raíz de su animación. `takeFrames` cuenta los fotogramas de 1/20 s que caen en cada tick y
  `movePlayer`/`movePartnerFrames` dan un paso por fotograma: el jugador uno entero (50 corriendo, 20 andando), el
  compañero el de su animación (ver «Seguimiento del compañero»). Cambiar de animación (arrancar, pararse, pasar de andar a correr) gasta un fotograma sin avanzar, como
  `startAnimation` al poner el impulso a cero; la dirección cambia en el acto (`setTamerDirection`). Al soltar el
  stick el jugador se para en el fotograma siguiente, sin el paso de más. Pulsar estando parado empieza un fotograma
  en ese mismo tick (Mejora documentada: el original espera al siguiente fotograma de su reloj, hasta 3 vblanks). El
  compañero decide andar, correr o pararse solo por la distancia (`getPartnerTamerCloseness`: corre a más de 3,5 R,
  anda a 2,5 R o más), también mientras sigue el rastro.
- **Seguimiento del compañero** (Igual, `map.c: partnerTickCollision`, `collisionGrace`, `entityCheckCollision`,
  `partner.c: partnerTickWalking`, `updateConditionAnimation`, `anim.c: tickAnimation`, `applyRootMomentum`):
  `game::field::PartnerFollow` lo porta con los enteros del original. Cada fotograma, después del jugador: el rastro
  (anillo de 30 casillas, `isFiveTileWidePathBlocked`; al empezar uno, `initializePartnerWaypoint` toma la casilla
  actual del jugador como la última vista), mira al jugador o al centro de la casilla más vieja del rastro
  (`_atan` con su tabla de 512, `core::original::atan4096`), prueba el paso hacia delante de su animación
  (`entityCheckCollision`: primero el mapa, que devuelve 10 y **no** cuenta, luego el jugador y los NPC con los
  bordes delanteros de su cuadrado); contra el jugador o un NPC recupera el rumbo que tenía y pasa por
  `collisionGrace`. Luego elige la animación por la distancia (al pasar de andar a correr, o de quieto a andar, acaba
  la vuelta en curso; de correr a andar cambia en el acto) y avanza con el impulso de la raíz de esa animación
  girado por su rumbo (`rsin`/`rcos`), con la fracción de unidad en 1/32768 (`locX >> 15`). Las animaciones salen del
  modelo del compañero (`battleClipsOf`, lo mismo que usa el combate); sin ellas (pruebas) anda y corre a las
  velocidades de `field/rules`. Al pararse espera de 1 a 5 vueltas de su animación de quieto (`randomLimit(5) + 1`)
  y hace su gesto. El `Transform` toma la posición y el rumbo enteros tras cada fotograma; si otro lo movió (guion,
  cuidado, `place`, apartarse), el seguimiento empieza desde ahí.
- **Choque con desvío** (Igual, `tamer.c: checkMapInteraction`, `map.c: entityCheckCollision`, `collisionGrace`):
  antes de avanzar se prueba el paso entero en la posición adelantada (`stepCollides`, la sonda de
  `checkMapCollision`). Si choca, `collisionGrace` recorre las cuatro direcciones de `COLLISION_GRACE_ROTATION`
  según el rumbo: la primera libre gira al personaje hasta 0x50 hacia ella y el paso pierde la componente que empuja
  contra las bloqueadas de antes; si ninguna está libre, gira 32 y no avanza. Para el jugador cuentan la rejilla, los
  NPC y el compañero (en el campo el original lo cuenta aquí aunque lo atraviese en la prueba de antes); el compañero
  lo hace en `PartnerFollow` (arriba). Contra una pared de frente
  el jugador gira 0x50 y se desliza.
- **Colocar** (`placeAt`, el comando de script `place`): pone al personaje exactamente donde se pide, sin pedir la
  regla de suelta.
- **Interpolación** (del motor: el original dibuja cada personaje en su sitio de cada fotograma, sin nada en medio).
  La lógica corre a 60 ticks por segundo y el dibujo interpola entre el sitio anterior y el actual de cada
  `Transform`. El jugador y el compañero, que avanzan a saltos de fotograma, se dibujan deslizándose por todo el
  fotograma (`Field::drawnPose`, que usan `FieldScene` y la cámara): del sitio de antes del paso al de después en
  1/20 s, sin tirones aunque el paso caiga en un solo tick. Cada tick de la partida empieza con `Field::settle` (`FieldScene::settleActors`, al principio de
  `PlaySession::tick`), que hace anterior = actual en jugador, compañero y NPC; también `Field::tick` lo hace siempre,
  aunque solo corra el fundido de un cambio de mapa o la espera del mapa nuevo. Así, en los ticks en que el campo no
  corre (combate para los que no luchan, evolución, sueño, pesca, menús, aviso de objeto) nadie se dibuja deslizándose
  desde donde lo dejó un tick viejo, y el deslizamiento del fotograma acaba. Si tras `settle` corre el tick del campo,
  el deslizamiento sigue: antes `settle` lo apagaba en cada tick y el jugador y el compañero se dibujaban un tercio
  del paso deslizándose, luego el resto de golpe y dos ticks quietos (el «movimiento a tirones» que vio el usuario el
  2026-10-31). Su reloj avanza una vez por tick del campo. Es una mejora opcional (`setSmoothMotion`, ajuste
  `smooth_motion`, encendida por defecto): apagada, todos se dibujan donde los deja su tick, como el original. Un salto de guion se dibuja de golpe: `placeByScript` a más de media casilla y
  `turnByScript` de más de un octavo de vuelta igualan también el anterior; los pasos cortos (el gimnasio, el final de
  un deslizamiento) se siguen interpolando en su tick. `playerFacePartner` y el mirarse al acabar un paseo de cuidado
  (`entityLookAtLocation`) giran de golpe, como el original.
- **Tamaño del compañero** (Igual): el campo lo mide con el radio de su especie en la tabla de especies, como el
  original (`map.c: entityCheckEntityCollision`, `partner.c:971`), no con el fijo de `field/rules` (que queda para las
  pruebas y para cuando no hay tabla). `FieldScene::showPartnerSpecies` se lo pasa (`Field::setPartnerRadius`) al
  cargar, al evolucionar, al renacer y al cambiar de compañero; con él cambian la colisión con la rejilla, la que ven
  los NPC y las distancias de seguir (correr más allá de 3,5 radios, andar más allá de 2,5). Si crece, pide la regla
  de suelta, por si ahora pisa a alguien.
- **NPC tras un guion** (Igual): al soltar el guion (`holdForScript(false)`) los NPC que movió dejan de deslizarse
  (su `ScriptedActor` pierde la velocidad, que si no volvía a moverlos en la siguiente retención), y el que ya no
  tiene animación de guion (`setActiveAnim` a 0) vuelve a sus propias animaciones; el que la tiene la conserva, como
  `NPC_ACTIVE_ANIM` en el original hasta que un guion la quita.
- **Camino con más tramos que puntos** (Igual, `overworld.c: tickWaypointWait`, `NPCEntityTickWaypointWalk`,
  `map.c: loadMap`): un mapa puede dar menos puntos de paso que tramos tiene el camino de un NPC (dgha02, id de
  script 7). El original lee los huecos que `loadMap` dejó a cero: el tramo espera 0 fotogramas o anda al origen del
  mapa. Aquí igual (`waypointOf`); antes el NPC se quedaba quieto con un error en el log.
- **NPC fuera de pantalla** (Igual, `overworld.c: NPCEntityTickOverworld`, `map.c: entityIsOffScreen`,
  `utils2.c: isInvisible`, `map.c: entityCheckCollision`): el original calcula al final del tick de cada NPC si se ve
  (`isOnScreen`, ninguna esquina de su caja de radio y altura en la ventana) y el choque salta a los que no se ven.
  Aquí `FieldScene::followNpcs` lo calcula al final de cada tick con la cámara (`FieldScene::onScreen`) y
  `FieldNpcs::setOnScreen` los saca de todos los choques y toques (`collides`) hasta que se vuelven a ver; la traza
  los sigue listando. Caso medido: Etemon espera 1000 unidades arriba de la puerta de su árbol en mayo04a (296, -1000,
  1865) hasta que la historia lo deja caer. En el original su `isOnScreen` vale 0 y el jugador sube con arriba desde
  (288, 1548) hasta la puerta (306, 2198, rumbo 2128); antes el motor lo paraba en z = 1600 contra el cuadrado de
  Etemon, ahora llega a (331, 2200, rumbo 2128). Suposición: la ventana del motor es más ancha que los 320 x 240 del
  original, así que un NPC en el borde puede verse (y chocar) aquí y no allí.
- **Sitios de aparición**: el compañero al entrar por un warp (a `spawnDistance` del jugador) y al ponerse junto al
  jugador cuando se atasca o se aleja (`placePartnerNearPlayer`) prueba la rejilla y además que no pisa a fondo a
  nadie; si no, prueba el siguiente sitio. Un NPC aparece donde dice su registro y, si pisa a alguien, la regla de
  suelta lo aparta en el tick siguiente.

Estado: **Igual** (hace lo mismo que el decomp), **Mejora documentada** (del motor, a propósito), **Suposición** (el
decomp no lo deja claro) o **Hueco** (falta).

| Qué | Original | Motor | Prueba | Estado |
|---|---|---|---|---|
| Separar al compañero que queda pisando a otro o en una pared, y al jugador en una pared, al soltarlos (los NPC no se mueven: Igual, medido con Kunemon en mayo02) | nada: cada uno queda donde lo dejó quien lo retenía | `releaseHandoff`, `separateActors` | `field_tests`: "the release rule ..."; rutas de `_work/routes-e2/positions` | Mejora documentada |
| Vigilancia de solapes | — | `watchOverlaps`, `field.overlaps` | `field_tests`: "the overlap watch ..." | Mejora documentada (depuración) |
| Al cargar, un NPC sobre el jugador | `main.c:3821`: lleva al jugador a la entrada del mapa si pisa a ciertos NPC | la regla de suelta aparta al NPC; el jugador no se mueve | `field_tests`: "an NPC put on the player ..." | Suposición (se prefiere no mover al jugador) |
| Objeto que suelta un enemigo vencido | `item.c: spawnDroppedItems`: cae en (x, 0, z) del enemigo al cerrarse la caja final, sin mirar la colisión (la colisión del combate, `map.c: entityCheckCollision`, lo mantiene fuera de las paredes) | `FieldItems::drop` y `dropCell` con la colisión del mapa (`FieldItemsAdapter::dropItem`, desde `BattleScreen::close`): en una casilla libre queda donde estaba el enemigo; si su casilla está cerrada (un enemigo empujado o un paso que se coló en la pared) pasa al centro de la casilla libre más cercana, anillo a anillo hasta 8 casillas; los objetos de guion (`spawnItem`) siguen en la casilla que da el guion | `field_items_tests`: "item.c spawnDroppedItems: a drop on a wall lies on the nearest free cell" | Mejora documentada: el original no la necesita si el enemigo nunca entra en la pared; no medido en el original (#164) |
| Tamaño del compañero en el campo | radio de la especie | `setPartnerRadius` | `field_tests`: "the partner's size follows its species ..." | Igual |
| NPC tras un guion | `NPC_ACTIVE_ANIM` sigue hasta que un guion lo quita | `holdForScript(false)` quita la velocidad y el `ScriptedActor` sin animación | `field_tests`: "a script let go ..." | Igual |
| Paso por fotograma (20 por segundo) y fotograma de arranque sin avance | `tamer.c: tamerTickWalkingState`, `anim.c: startAnimation`/`applyRootMomentum` | `takeFrames`, `movePlayer`, `stepPlayer`, `movePartnerFrames` | `field_tests`: "the player steps on the original's frames ..."; oráculo `field_walk_seen` | Igual |
| Pulsar estando parado | espera al siguiente fotograma de su reloj (0 a 3 vblanks) | el fotograma empieza en ese tick | `field_tests`: "the player steps on the original's frames ..." | Mejora documentada (menos latencia) |
| Velocidad por fotograma del jugador | el impulso de la raíz cambia de un fotograma a otro | la media de `field/rules` | oráculo `field_walk_seen` | Suposición (en diagonal se desvía 1 unidad) |
| Seguimiento del compañero: rastro, mirada, choque, animación y paso por el impulso de la raíz, en enteros | `map.c: partnerTickCollision`, `partner.c: partnerTickWalking`, `anim.c: tickAnimation` | `PartnerFollow`, `Field::movePartner` | `partner_follow_tests`: "the partner follows the player frame by frame ..." (60 fotogramas de un modelo entero que reproduce la traza del original); oráculo `field_walk_seen`: antes hasta 147 unidades de posición y 308 de rumbo, ahora 2 a 5 unidades (el desfase del gesto inicial, que el lado dwe coloca a ±1 fotograma) y el mismo fotograma en cada cambio de animación | Igual |
| Movimiento de raíz de las animaciones que pone un guion al jugador, al compañero o a un NPC: cada fotograma de 1/20 s el clip lo mueve por el paso de su raíz, girado a donde mira, sin prueba de colisión, hasta que acaba o empieza otro. Durante un paseo con guion (`entity_walk_to`) el deslizamiento sustituye a la raíz; durante un movimiento con guion (`entity_move_to`, `entity_move_to_axis`) se suma a ella. Al soltar el guion, el jugador y el compañero dejan de moverse por el clip | `script_interp.c: scriptStartAnimation` (0xFD `startAnimationTamer`, 0xFC el compañero, si no `scriptNPCStartAnimation`) → `anim.c: startAnimation` (`animFlag` = 1); `tamer.c: tamerTickOverworld`, `partner.c: partnerTickOverworld` y `overworld.c: NPCEntityTickOverworld` → `anim.c: tickAnimation`, `applyRootMomentum`; `tickEntityWalkTo` frente a `tickEntityMoveTo`/`tickEntityMoveToAxis` | `Field::moveByClip` y `applyClipRootMotion`, `clipRootStep` (`field_script.cpp`), `CharacterClipMotions` (al jugador y a los NPC se lo pone `FieldScene` con `battleClipsOf`, al compañero `setPartnerClips`), `ScriptGlide` y `story::Glide`; la escena de dormir usa el mismo camino (`FieldScene::playPartnerClip(clip, true)`) | `field_tests`: "an NPC playing a script's clip moves by the clip's root motion", "the player and the partner playing a script's clip move by the clip's root motion", "a care scene's moving clip carries the partner until it stops" | Igual. Medido en el original (2026-10-30): NPC, `e1-cp28`, Kunemon en su cama de mayo02: las animaciones 30 y 8 lo sacan de (143, −1000) a (−284, −1000); dwe (−281, −1000). Compañero, `mayo04a_etemon_tree_door` con la variable 1 a 60 (prosperidad) y la casilla 0x33 del script 4 (Etemon cae del árbol): la animación 40 lleva a Agumon de (208, 1974) a (208, 1593), −381 en z mirando a 2048, antes del combate; dwe antes no lo movía, ahora de (243, 1978) a (243, 1595), −383. Jugador, la misma escena con la animación 15 del jugador cambiada a 17 en la RAM del guion: de (306, 1848) a (306, 2245) al empezar el combate (fotograma 33 de 49); dwe (con el Lua cambiado igual) de (306, 1898) a (306, 2282). Etemon cae con su animación 29 mientras `entity_move_to_axis` lo baja: el original lo deja en (49, 1618) desde (296, 1865); dwe, con el paso sumado, en (79, 1648) (sin sumarlo lo dejaba en (256, 1825)). Dormir: la animación 9 lleva al compañero 70 unidades, como antes |
| El compañero mientras un guion retiene el campo (una charla, una escena) | `script_interp.c: scriptPauseGame` → `overworld.c: setMovementEnabled(1, 1)` → estado 11 del compañero; `partner.c: partnerTickIdle` arranca una vez su animación 0 y no corre ni el seguimiento ni los gestos de ánimo hasta que el guion lo suelta o le pone otra | `Field::holdForScript` llama a `PartnerFollow::standHeld` y `partnerFollowClip` da esa animación 0 mientras dura la retención; `FieldScene` la dibuja como la del seguimiento (la animación que pone el guion sigue mandando) | `field_tests`: "a script's hold stands the partner on its idle clip until it lets go" | Igual. Medido (2026-11-01, `04_jijimon_house_interior`, hablando con Tokomon mientras Agumon llegaba corriendo): estado 11 y animación 0 en el acto, igual y en el mismo sitio 400 fotogramas después con una dirección pulsada. Antes dwe seguía con el temporizador de gestos durante la charla (7 gestos «happy» en la charla de apertura con el anciano, 1 en una charla corta); ahora ninguno |
| El compañero y las paredes al seguir | `partnerTickCollision` no hace nada si `entityCheckCollision` da 10 (el mapa): solo el rastro lo aparta | igual: un compañero encerrado atraviesa la pared hacia el jugador | `field_tests`: "a partner in a closed pocket walks through its wall ..." | Igual |
| Compañero fuera de pantalla | `isTileOffScreen`: lo pone en la casilla del rastro si no se ve | lo pone junto al jugador si lleva un segundo sin avanzar | `field_tests`: "a partner that cannot step for a second ..." | Hueco (el rastro fuera de pantalla aún no se usa) |
| Compañero muy lejos (enfadado, con la mariposa, anda mientras el jugador corre: `partner.c: partnerTickWalking`) | `getClosestTileOffScreen`: lo trae a la primera casilla fuera de pantalla en la recta del jugador hacia él; nunca se le ve saltar | a más de `teleport_distance` y fuera de la vista (`FieldScene::offScreen`: la pantalla de 320 x 240 con 40 píxeles de margen, en fracciones de la ventana), a la primera casilla fuera de la vista y más cerca en esa recta; si aún se ve, sigue andando. Antes lo ponía detrás del jugador, a la vista (tarjeta #170). Medido en dwe: corriendo con el compañero enfadado la distancia pasa de 2000 y vuelve desde 2002 unidades, fuera de la vista | `field_tests`: "a partner left too far comes back from the first cell out of view toward it" | Igual (sin cámara, en las pruebas, sigue detrás del jugador) |
| Espera antes del gesto | `randomLimit(5) + 1` vueltas de la animación de quieto, con el generador del original | igual, con el generador del motor (`gestureRandom_`) | oráculo `field_walk_seen` (el original no hizo el gesto en los 46 fotogramas parado) | Suposición (otra secuencia aleatoria) |
| NPC fuera de pantalla fuera del choque | `isInvisible` (`isOnScreen` 0) en `entityCheckCollision`; Etemon en mayo04a: 0, el jugador llega a la puerta (306, 2198) | `FieldNpcs::setOnScreen`, `collides`, `FieldScene::followNpcs` | `field_tests`: "an NPC off screen is out of the collision ..."; antes el jugador se paraba en z = 1600, ahora llega a (331, 2200) | Igual |
| Choque del compañero con un NPC | `entityCheckEntityCollision` con los cuadrados de los NPC | `FieldNpcs::blocks` en `npcBlocks` | — | Suposición |
| Choque con desvío de hasta 0x50 | `map.c: collisionGrace`, `COLLISION_GRACE_ROTATION` | `collisionGrace`, `walkForward`, `stepCollides` | `field_tests`: "against a wall straight ahead ..."; oráculo `field_walk` | Igual |
| Compañero contra un NPC | `map.c: partnerTickCollision` | `PartnerFollow::collisionGrace` | oráculo `field_walk` | Igual |
| Andar o pararse del compañero | `partner.c: getPartnerTamerCloseness`, solo la distancia | igual, también siguiendo el rastro | `field_tests`: seguimiento | Igual |
| Rumbo del compañero con el jugador delante | recupera el que tenía (`partnerTickCollision`) | igual | oráculo `field_walk_seen` | Igual |
| `place` de script | pone el personaje en su sitio | igual, sin regla de suelta | `field_tests`: "a placed partner stays ..." | Igual |
| Dibujo del paso de fotograma | sin interpolación | `drawnPose` desliza el paso por 1/20 s, también con `settle` al principio de cada tick de la partida | `field_tests`: "... drawn gliding over a frame's ticks", "the game's ticks, each starting with settle, keep the frame's glide going". Medido en dwe (release, lavapipe a 320x180, corriendo en mayo01_2): antes el 36 % de los dibujos con el jugador en marcha lo dejaban quieto (coef. de variación de la velocidad dibujada 1,40); después el 4 % (los de arrancar y cambiar de mapa) y 0,32 | Mejora opcional (`smooth_motion`, encendida por defecto) |
| Movimiento suave apagado | dibuja cada fotograma en su sitio | `drawnPose` sin deslizar ni interpolar | `field_tests`: "with smooth motion off the characters are drawn where their tick left them" | Igual (con la mejora apagada) |
| Interpolación sin tick de campo y saltos de guion | sin interpolación | `settle`, `placeByScript`/`turnByScript` con salto | `field_tests`: "settling ...", "a script's jump ..." | Mejora documentada |
| El jugador atraviesa al compañero | `tamer.c: checkMapInteraction` pasa al compañero como `source` a `entityCheckCollision`, que lo salta (`other != source`) | el jugador lo atraviesa; la vigilancia lo registra si se queda encima | ruta `control` | Igual |
| Celda de disparo bajo el jugador en una arista | `map_collision.c: getTileTrigger` (`getModelTile`): trunca hacia 0 y resta una columna con x < 0 y una fila con z > 0, así que en la arista de una celda lee la vecina lejos del 0. La salida 1 de frzl17 deja al jugador en (-400, 1400), la esquina de cuatro celdas: lee la (45, 35), la del curling, y la partida empieza al llegar (medido en el original: al entrar por esa salida sale la pregunta "Start Game" sin moverse) | `Field::checkTriggers`, `Field::checkConfirm` y el disparador recordado al entrar leen `assets::originalCellAt`; antes `collisionCellAt` (suelo) leía la (46, 36) y había que salir de la celda y volver | `field_tests`: "a step cell is the one the original reads, the neighbour away from 0 on an edge" | Igual |
| Deslizamientos de guion contra paredes y NPC; `wouldTouchCharacter` sin NPC | — | sin cambiar; la regla de suelta arregla el resultado | — | Hueco |
| Camino de vuelta del jugador: la celda por la que entró al mapa y, mientras la línea recta hasta ella está tapada, las celdas donde perdió de vista la anterior (hasta 30); lo usa la carrera del domador al empezar un combate | `map.c: initializeTamerWaypoints` (en `loadMap`), `tickTamerWaypoints` (al principio de `tamerTickWalkingState`), `isLinearPathBlocked`, `addTamerWaypoint`, `clearTamerWaypoints` | `TamerTrail` (`Field::tamerTrail`) | `field_tests`: "a line of cells is blocked ...", "the way back keeps the cell ..." | Igual; el original lee el anillo pasado de 30 celdas, aquí se para al final |
| Pasos y demás sonidos de los clips del campo | `anim.c: tickAnimation` (0x4000): `playSound(banco, sonido)` en el tick de cada instrucción si la entidad se ve (`isOnScreen`: el jugador siempre; compañero y NPC por `entityIsOffScreen(320, 240)`); banco 4 = el de la especie. Medido corriendo en twna01/twnb01: jugador `playSound(1, 27)` en los ticks 2 y 8 de la carrera (anim 3), uno cada 6 fotogramas (18 vblanks); compañero `playSound(1, 12)` en los 3 y 9 de la suya (anim 4); un NPC que anda, `playSound(1, 10)`; al pararse dejan de sonar. No depende del suelo | `clipSounds`/`AnimationPlayer::takeSounds` (motor), `FieldScene::playClipSounds` y `onScreen` (app) con el banco `sound/sl` del hueco 1; antes los clips del campo no tocaban ningún sonido | `animation_tests`: "a clip plays its sounds on the ticks it leaves, ...", "the player hands over the sounds ..."; `assets_tests`: "a clip keeps the sounds its stream plays ..."; ruta a mano: carrera 2/8, andar 3/13, compañero 3/9 (sin audio en el banco de pruebas: por las llamadas) | Igual |
| Sonidos de clip con los bancos de la especie, en combate y en las escenas de cuidado | `anim.c: tickAnimation` (0x4000): el banco 4 del clip es `stats.current.vabId` de la entidad (compañero 4, enemigo i del combate 4 + i, `btl.c: loadBattleData`; un NPC que nunca luchó tiene 0, y uno que luchó guarda su hueco); el 3 es siempre el hueco 3. `sound.c: loadPartnerSounds`: hueco 3 = entrada de `VLALL` del tipo del compañero (15 fuera de 1–65), hueco 4 = su `VBALL`; `readVBALLSection(i + 4, tipo)` carga los de los enemigos. En combate `isOnScreen` no se recalcula (`battle_tick.c`): cada luchador conserva el de su último tick de campo; en las escenas de cuidado (`partner.c: partnerTickOverworld`, estados 3–9 y 15) sí. Medido en `11_battle_agumon`: compañero `playSound(4, 0x37)` y enemigo (Agumon, `vabId` 5) `playSound(5, 0x37)` desde `tickAnimation`, más pasos `playSound(1, 12)`/`(1, 2)`; en `01_field_outside_jijimon_house`: compañero `vabId` 4, NPC `vabId` 0 | `SessionSound` (huecos 3–7 por `used_by` de cada `bank_meta`, `attachPartner`, `useEnemyBanks`), `BattleScreen` (carga los enemigos y da a cada figura su hueco), `FieldScene::playClipSounds` (hueco propio del actor, visibilidad guardada al sujetarlo el combate) y `animatePartnerClip` ya no descarta los sonidos | `dwe_session_tests`: "slots 3 and 4 hold the partner's species banks, 5 to 7 the last battle's enemies'"; ruta a mano en :91 (llamadas en el log) | Igual; Suposición: un NPC recién creado al entrar en un mapa empieza con hueco 0 (el original guarda el `vabId` de la ranura de NPC entre mapas) |
| Arranque de la partida nueva: twna01 no se ve | `main.c: main`: `fadeFromBlack(0x28)` con el jugador libre; en el mismo fotograma (vblank 32224, `FADE_IN_CURRENT` 1, `FADE_PROGRESS` 137) la casilla 54 llama a la sección 0x36, `warp_to` hace `fadeToBlack(20)` desde ese tono y la pantalla sigue negra hasta el rótulo «File City» y topn01, donde el jugador ya está tumbado (anim 18) al verse | `Field::startPlay` (40 fotogramas, sin retener), `shadeNow`/`leavingFrom_`: la salida sigue desde el tono del fundido a medias; antes el fundido de 20 retenía al jugador, la casilla saltaba en el tick 59 con twna01 ya iluminado y se veía a todos de pie (topn01 en el tick 113; ahora en el 59) | `field_tests`: "play starts with a long fade in ...", "a fade out started during a fade in ..."; capturas en `_work/routes-e2/npc-and-opening/` | Igual (el tono de salida es 160 en vez de 144: los dos negros); al cargar partida sigue sin fundido (Hueco) |
| El fundido de entrada sigue durante un combate | `fade.c: renderFadeIn` es un objeto de dibujo (`addObject(4005)`) que avanza un paso por fotograma dibujado, también dentro de `BTL_battleMain`: un combate que empieza antes de que acabe el fundido (consola, o un guion justo tras un cambio de mapa) ve volver la pantalla igual que en el campo | `Field::tickFadeIn`, que `PlaySession` llama en cada tick con el combate activo; antes el campo no avanzaba y el combate se quedaba casi negro (brillo medio 9 de 255 en twna01 frente a 114 del mismo combate sin fundido; no depende de Xvfb ni de la hora del día) | `field_tests`: "the fade in runs on while a battle holds the field"; capturas antes/después en `_work/tmp/w15-arena/dark-compare.png` | Igual |
| Paseo de cuidado a un punto (el baño, comerse una caca) | `tickEntityWalkTo` acaba al pisar la casilla del punto (`getModelTile`), no sobre el punto; en los estados 6 y 9 del compañero (`tickPartnerToilet`, `partnerTickEatShit`) el clip lo lleva sin `partnerTickCollision` ni choque con NPC | `tickCareWalk`/`stepCareWalk`: acaba en la casilla del punto (`originalCellAt`) o a 1 unidad de él, sin chocar con el mapa ni con los NPC (el paseo a una distancia sí choca) | `field_tests`: "a care walk to a point goes through walls and ends on the point's cell" | Igual. Medido (2026-10-31, baño de twna01): Agumon pasa al segundo punto en (−2115, −1939), ya en la casilla de (−2122, −1930), y entra en la caseta hasta (−2251, −1266) |
| El compañero al salir del baño (animación del paseo de cuidado) | `partner.c: tickPartnerToilet`: el subestado 0 arranca la carrera (4) y los subestados 1 y 2 siguen con ella; el 3 arranca la caminata (2) al acabar la animación 10 y `handleToilet`; el 4 llega al punto y suelta el guion con la caminata puesta, y `partnerTickWalking` (estado 1) sigue desde esa animación sin pararse | `Field::walkPartnerTo` pone el movimiento en el acto; al llegar a un punto el paso conserva el movimiento; `holdForCare(false)` con el compañero caminando o corriendo sigue en el seguimiento con la animación 2 o 4 en el fotograma al que llegó (`playPartnerFrom`); `FieldScene` no reinicia la animación que ya dibuja cuando el seguimiento la toma | `field_tests`: "a care walk walks from its first frame, and let go on its point the partner walks on" | Igual. Medido (2026-11-01, baño de twna01): en el original, animación 10 → 2 sin pasar por la 0, sigue en 2 al volver al estado 1 en (−2120, −2024) y solo pasa a la 0 al pararse en (−2133, −2178). Antes dwe intercalaba la animación 0 un fotograma tras la 10, otra vez al soltarlo, y después corría (4) 0,25 s antes de caminar; ahora 10 → 2 → 0, parada en (−2123, −2179) |
| Vuelta andando tras un combate de historia | `story_scenes.cpp:75-79` es un esbozo | sin cambiar | — | Hueco |

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
partner; la regla de suelta, la vigilancia de solapes, la interpolación, el tamaño del compañero y los NPC tras un
guion) y
`rules_tests.cpp` (lectura de los YAML, también los del paquete `game`, y la curva del fundido) usan mapas de prueba. `route_tests.cpp` recorre la ruta de la fase 2 en el contenido extraído si
`DWE_CONTENT` apunta a él: File City (`twna01`) → casa de Jijimon (`room08`) → File City, con el stick y con clics.
