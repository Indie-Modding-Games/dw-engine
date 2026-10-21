# session

La partida (`dwe::session`): la librería de app que junta los sistemas del juego. Los módulos de `game/` no se usan
entre sí ([regla 4](../../docs/architecture/dependency-rules.md)), así que este es el único sitio que los une. El juego
(`apps/game`) y `dwe-sim` (`sim::HeadlessSessions`) la juegan igual, con y sin ventana.

## Qué tiene

`GameSession` es dueña de:

- el ruleset (`ruleset::RulesetStore`), que el panel de ajuste cambia en marcha: en el tick siguiente la partida
  vuelve a leer sus secciones (reloj, partner, `new_game`, `tamer`, autoguardado y las de cada adaptador);
- el reloj de juego (`ruleset::GameClock`), el único de la partida;
- el partner (`game::partner::Partner`), el domador (`TamerState`) y dónde está el jugador (`FieldPlace`);
- las ranuras de guardado (`game::save::SaveSlots`) y las peticiones de autoguardado (`AutosaveRequests`), si se le
  da un `SaveStorage`.

El campo no es suyo: la app lo crea (`FieldScene`) y se lo presta con `attachField`. La partida lee de él el mapa, la
posición del jugador, si hay un cambio de mapa en curso y si el último tick corrió tiempo de juego.

```cpp
auto session = session::GameSession::create({.files = &files, .rulesetId = "original", .storage = &storage,
                                             .gameVersion = "0.1.0", .wallClock = reloj});
(*session)->startNewGame();               // o load(slot) / loadFile(file)
(*session)->attachField(scene.field());
// En cada tick lógico, después del campo:
(*session)->tick(TickSeconds);
```

### El tick

1. Si el ruleset cambió (`revision`), vuelve a leer las reglas; un valor que no sirve se registra y no cambia nada.
2. Si el campo entró en un mapa nuevo: `Partner::enterMap` y autoguardado `MapChange`.
3. Avanza el reloj, teniendo en cuenta el avance rápido (`setSpeed`), pero solo si el campo corrió tiempo de juego
   (`Field::gameTimeRan`, falso durante los fundidos de un cambio de mapa: `tamer.c: stopGameTime`). Sin campo corre
   siempre (simulaciones, tests).
4. El cuidado del partner con lo que avanzó el reloj (`Partner::tick`), y después cada adaptador.
5. Reparte los eventos del partner: `Slept` → autoguardado `PartnerSleep`, `NewDay` → autoguardado `NewDay`,
   `TamerLevelChanged` → nivel de domador entre los límites de `tamer`, `Died` → renacer. Lo que provoca un evento
   (renacer da `Reborn`) se reparte también.
6. Si hay una petición de autoguardado que el ruleset deja pasar, escribe la ranura de autoguardado siguiente.

Las órdenes del jugador (`feed`, `cure`, `scold`, `praise`, `useToilet`, `sleep`, `train`) y el final de una batalla
(`BattleAdapter::finish`) reparten también sus eventos y su autoguardado. `takePartnerEvents` da los eventos del
último tick u orden para la UI, y `takeAutosaved` el autoguardado escrito (ranura y motivo).

### Lo que ve la UI

`partnerView()` da una copia de solo lectura (`PartnerView`): especie, nivel, edad, vidas, vida que le queda,
estadísticas, medidores con su rango (`CareMeter::fill()` de 0 a 1), condiciones, errores de cuidado y batallas. La
pestaña "Partner" del panel de ajuste (`apps/game/src/partner_tab.cpp`) la enseña.

## Partida nueva

Las secciones del ruleset de la partida (en `content/packs/game/rules/original/`):

- `new_game` ([esquema](../../content/schemas/ruleset_new_game.schema.json)): el partner de inicio, sus estadísticas y
  el nivel de domador.
- `tamer` ([esquema](../../content/schemas/ruleset_tamer.schema.json)): los límites del nivel de domador y los
  niveles que da una evolución cuando sale su probabilidad (`evolution_level_gain`, `evl.c: EVL_applyEvolution`).

Una partida nueva con la misma semilla (`SessionDesc::seed`) es siempre la misma, aunque antes se haya jugado otra en
la misma `GameSession`: la semilla reinicia los números aleatorios del partner y los de los sistemas (`random()`, que empiezan en la semilla
más `SystemsSeedOffset`), y el reloj vuelve a la hora de inicio
de `clock` (con la velocidad que tenía).

## Guardado

| Sección | Qué guarda |
|---|---|
| `clock` | `elapsed_frames` y `pending_frames`: los fotogramas y la fracción que el siguiente tick completa |
| `partner` | El estado del partner (`partner::toYaml`, versión `PartnerStateVersion`); una partida de la versión 1 sube con `partner::addVirusBackupSpecies` (la copia de la forma virus sin forma) |
| `tamer` | `level` |
| `field` | `map`, `position` y `yaw` del jugador; vacío sin mapa |
| `random` | `partner` y `session`: el estado del generador del partner y el de los sistemas, para que una partida cargada siga igual que la guardada; una partida de la versión 1 (solo `partner`) sigue los de los sistemas desde el del partner |
| `inventory` | Los huecos, cuántos se usan y el dinero (`inventory::toYaml`, versión `SectionVersion`) |
| `evolution` | Las formas ya criadas (`evolution::toYaml`, versión `EvolutionStateVersion`); la marca de objeto de evolución usado no se guarda, como en el original |
| `moves` | Las técnicas del partner: `learned` (ids de `data/moves`) y `equipped` (cuatro posiciones de la lista de la especie o `null`; la cuarta es el remate) |

La cabecera lleva el ruleset, la versión del juego, el tiempo jugado y una etiqueta `<especie> <mapa>`. Cargar una
partida de otro ruleset lo selecciona antes de leer las secciones. Al cargar, `savedPlace()` dice dónde empezar: el
juego entra en ese mapa en esa posición (`FieldSceneDesc::startAt`).

## Adaptadores

Cada sistema que llega después (evolución, batalla, inventario) se enchufa con un fichero: una clase que implementa
`SessionSystem` (`session_system.hpp`) y se pasa en `SessionDesc::systems`. Da sus comprobaciones de sección
(`sectionChecks`), lee sus reglas (`readRules`, también tras un ajuste), registra sus secciones de guardado
(`addSaveSections`), empieza la partida nueva (`startNewGame`), avanza con el reloj (`tick`) y recibe los eventos del
partner (`routePartnerEvent`) y de las alabanzas y regañinas (`afterPraiseOrScold`, con la disciplina y la felicidad
de antes). Para sus órdenes la partida les da `careContext()`, `currentPlace()`, `random()` (los números aleatorios
de los sistemas, guardados con la partida), `addTamerLevel(probabilidad, niveles)` (`tamer.c: addTamerLevel`) y
`afterSystemCommand()`, que reparte los eventos y escribe el autoguardado como las órdenes de la partida.

### Evolución (`EvolutionAdapter`)

`EvolutionAdapter({.files = &files})` lee la sección `evolution` y sus tablas (las del manifiesto, una vez; los tests
le dan las suyas en `.tables`) y registra su comprobación de sección y la sección de guardado `evolution`.

- **Entradas**: llena `EvolutionInputs` desde el partner: forma, stats, peso, errores de cuidado, disciplina,
  felicidad, cansancio, batallas, días, contador de evolución, barra de virus, la forma virus
  (`care.toilet.virus_form`), la copia de la forma virus, el mapa
  (índice original de `data/maps`, `UnknownMapIndex` sin tabla de mapas), si el domador está libre
  (`careContext().tamerIdle`), si corre un guion (solo en las rutinas de guion) y las técnicas aprendidas: las del
  `BattleAdapter` (`learnedMoveIndices`), contadas con `countMasteredMoves` (solo las de índice menor que
  `evolution.requirements.mastered_moves_below`, 64: `evolution.c: getNumMasteredMoves`). El adaptador de batalla se
  crea después (necesita el inventario, que necesita la evolución), así que se le da con
  `setMoveSource(batalla)`; sin él, la evolución no ve técnicas aprendidas.
- **Cuándo mira**: en cada tick que cruza horas de juego, `checkNatural` con las horas cruzadas (el contador lo sube
  el partner; si no evoluciona, el adaptador deja el que devuelve), salvo si la barra de virus se llenó en ese tick:
  el virus gana (`partner_impl.c: tickConditions`) y la evolución natural espera. `VirusBarFull` → `fillVirusBar`;
  `Slept` → `wakeUp`; tras alabar o regañar → `praiseOrScold`; `Reborn` marca al bebé como criado
  (`evolution.c: reincarnatePartner`). Órdenes: `useItem(session, objeto)` (la llama el inventario),
  `returnFromVirus(session)` (rutina de guion 2) y `evolveByScript(session, forma)` (rutina 10).
- **Aplicar** (`evl.c: EVL_applyEvolution`): `Partner::changeSpecies` a la forma destino; `statChange` por
  `addStatGains` o, al volver de la forma virus, `restoredStats` con `setStats` y cada stat en su tope de
  `care.limits` (sin el tope por ganancia, decidido el 2026-10-20); HP y MP actuales dentro de los nuevos máximos;
  peso, errores de cuidado, batallas y contador; horas de vida extra (`addLifetime`); la probabilidad de nivel de
  domador con `tamer.evolution_level_gain`; la barra de virus a 0; la copia de la forma virus (forma y stats) en
  `PartnerState::virusBackup`; `recordEvolution` y autoguardado `Evolution`. `takeEvolutions()` da las aplicadas
  para la UI.

### Inventario (`InventoryAdapter`)

`InventoryAdapter({.files = &files, .evolution = &evolucion})` es dueño de la bolsa (`game::inventory::Inventory`) y
de los efectos (`ItemEffects`, con `data/items` y `data/item_effects`, leídos una vez; los tests le dan los suyos).
Registra las comprobaciones de las secciones `inventory` e `item_use` y la sección de guardado `inventory`
(`inventory::toYaml`, versión `SectionVersion`).

- **Partida nueva**: la bolsa de la sección `inventory` con los objetos de `bag.new_game_items` (en el original
  ninguno: el guion `dg 193` vacía la del arranque; ver `game/inventory`). Un objeto que no está en `data/items` es
  un error.
- **Usar** (`use(session, objeto)`, en el campo): el partner puede rechazarlo (`Partner::refuses` con la comprobación
  que dice `item_use.refusal`: nunca, tirada de disciplina o solo la comida favorita; los objetos de evolución se
  rechazan fuera de su nivel). Si lo toma: la comida va por `Partner::eat` (comida de verdad, que responde al hambre,
  si está en `item_use.meals`; el refuerzo de entrenamiento con los bits de `item_use.training_boost_bits`); el resto
  cura HP y MP dentro del máximo, revive, cura con la probabilidad de la medicina, sube stats por `addStatGains`, da
  horas de vida (`addLifetime`), cambia el nivel de domador (`addTamerLevel`), alivia (`usePortableToilet`) o pasa el
  objeto de evolución a `EvolutionAdapter::useItem`. Un objeto clave usado pide el autoguardado `KeyItem`.
- **Comprar** (`buy(session, objeto, cantidad, disparadores)`): con el descuento de bebé si el nivel del partner está
  en `item_use.shop_discount_levels`; un objeto clave pide el autoguardado `KeyItem`.
- **Derrota** (`loseOnDefeat(session)`): las pilas perdidas, con los números aleatorios de la partida.
- Lo que solo vale en combate (curar estados, proteger, discos) es del adaptador de batalla.

La sección `item_use` ([esquema](../../content/schemas/ruleset_item_use.schema.json)) es de la partida: qué objetos
rechaza el partner (`partner_impl.c: partnerWillRefuseItem`), cuáles responden al hambre
(`partner_impl.c: partnerHandleFoodFeed`), qué niveles tienen el descuento de bebé (`script_common.c: isPartnerBaby`)
y el bit de cada stat en el refuerzo de entrenamiento (`trn_reward.c`).

### Batalla (`BattleAdapter`)

`BattleAdapter({.files = &files, .inventory = &inventario})` lee las secciones `battle` (`readBattleRules`, el
catálogo de técnicas con `data/moves` y las estrategias) y `partner_moves`, con `data/digimon` y `data/moves` leídas
una vez (los tests le dan las suyas). Registra las comprobaciones de las dos secciones y la sección de guardado
`moves`: es dueño de las técnicas del partner (`PartnerMoves`).

- **Técnicas**: como el original, las aprendidas por id (`utils2.c: learnMove`) y las equipadas por su posición en la
  lista de la especie (`stats.base.moves`, `0x2E` + posición), así que tras una evolución cada posición nombra la
  técnica de la nueva especie y una posición sin técnica queda vacía en combate (`btl.c`). En la partida nueva el
  partner aprende y equipa la técnica de `partner_moves.new_game_slot` y equipa como remate la última de su lista que
  es un remate (`partner_impl.c: initializePartner`); al renacer (`Reborn`) equipa solo `partner_moves.reborn_slot` y
  conserva las aprendidas (`evolution.c: reincarnatePartner`). `equip(session, lugar, posición)` solo equipa técnicas
  aprendidas (`utils2.c: hasMove`); el remate va con la especie.
- **Empezar** (`start(session, BattleRequest)`): el luchador del partner (`partnerCombatant`: la fila de
  `data/digimon`, sus stats, HP y MP y las técnicas equipadas), su estado (disciplina, cansancio, enfermo, herido y
  las técnicas que ya sabe), el minuto del reloj y una semilla de `random()`. La partida decide el recluta que se une
  si gana (`recruitOnVictory`).
- **Acabar** (`finish(session, BattleResult)`): HP y MP del final (`setStats`), las subidas por `addStatGains`, y el
  cuidado, la herida, las vidas y los minutos como los da la batalla por `Partner::applyBattle` (con la cuenta horaria
  del partner si la batalla acaba una hora: `tamer.c: advanceBattleTime`);
  las técnicas nuevas, los bits y los objetos ganados a la bolsa (los que no caben quedan en `dropsLeft`) y, en una
  derrota, las pilas perdidas por `InventoryAdapter::loseOnDefeat`. Pide los autoguardados `BattleEnd`, `RecruitJoin`
  (si se unió el recluta: `recruitJoined`) y `TournamentResult` (torneo o arena).

La sección `partner_moves` ([esquema](../../content/schemas/ruleset_partner_moves.schema.json)) es de la partida.

## Paridad con el original

| Regla | Decomp | Nuestro | Test | Estado |
|---|---|---|---|---|
| Partida nueva con el partner de id 3 | `main.c: main` (`initializePartner(3)`) | `new_game.partner: agum` | `a new game starts the partner, ...` | Igual |
| Estadísticas de inicio del partner | No encontrado | `new_game.stats` | idem | **Hueco**: valores provisionales |
| Nivel de domador 0 y hora 8:00 al empezar | `main_menu.c: initializeDefaultSavegame` | `new_game.tamer_level`, `clock.start` | idem | Igual |
| Nivel de domador entre 0 y 10 | `tamer.c: addTamerLevel` | `tamer.level_min/level_max`, `changeTamerLevel` | `the original's tamer level ...` | Igual |
| El reloj corre en el campo y se para en los cambios de mapa | `clock.c: tickGameClock`, `tamer.c: stopGameTime/startGameTime` | `advanceGameTime` con `Field::gameTimeRan` | `field_clock_tests.cpp` | Igual |
| Un minuto de juego por segundo | `clock.c: tickGameClock` | sección `clock` | `on the field the game clock keeps ...` | Igual |
| Los efectos del área vuelven a empezar en cada mapa | `partner.c: setupPartnerOnWarp` | `followMap` → `Partner::enterMap` | `a map change autosaves, ...` | Igual |
| Autoguardado al dormir, al cambiar de mapa y al acabar una batalla | El original no autoguarda | sección `save` | `the partner going to sleep autosaves`, `a map change autosaves, ...` | Desviación aprobada (2026-10-20) |
| Autoguardado al empezar un día | El original no autoguarda | `save.autosave.on_new_day: true` | `a new day autosaves only when ...` | Desviación aprobada (2026-10-20) |
| Al morir, renace enseguida como bebé | `partner.c` (muerte y renacer tras la escena de muerte) | `routePartnerEvent` → `Partner::rebirth` | `a partner that dies is reborn as a baby` | **Hueco**: sin la escena ni la espera |
| Respuesta del partner al área (le gusta, no le gusta) | `map.c: MAP_TILE_DATA.partnerAreaResponse` | `CareContext::area = Neutral` | — | **Hueco** |
| Montones de caca en el suelo | `partner.c` | `PoopedOnGround` no se reparte | — | **Hueco** |
| El primer partner y cada bebé renacido quedan como criados | `main.c: main`, `evolution.c: reincarnatePartner` (`setDigimonRaised`) | `EvolutionAdapter::startNewGame`, `Reborn` | `the evolution adapter marks the first partner as raised ...` | Igual |
| La evolución se mira cada hora de juego, aunque se adelante el reloj | `partner_impl.c: tickConditions` | `EvolutionAdapter::tick` → `checkNatural` con `advance.hours` | `a baby evolves when the clock's hour ...` | Igual |
| Lo que cambia al evolucionar | `evl.c: EVL_applyEvolution`, `EVL_clampBaseStats` | `EvolutionAdapter::apply` | `an evolution item turns ...`, `back from the virus form ...` | Igual (vuelta del virus: decisión del 2026-10-20) |
| Nivel de domador al evolucionar a campeón o definitivo | `evl.c: EVL_applyEvolution` (`addTamerLevel(20/100, 1)` si no corre un guion) | `tamer.evolution_level_gain`, `GameSession::addTamerLevel` | `a tamer level chance of 100 ...` | Igual |
| Barra de virus llena y evolución natural en la misma hora | `partner_impl.c: tickConditions` (el virus se mira después y su destino gana) | `EvolutionAdapter::tick` no mira la evolución natural si el partner tiene pendiente `VirusBarFull` (`Partner::pendingEvents`); el evento la convierte en la forma virus | `the virus bar filling in the hour the counter reaches ...` | Igual (decidido el 2026-10-20): se decide por tick de la partida, no por fotograma |
| Qué objetos rechaza el partner | `partner_impl.c: partnerWillRefuseItem` | `item_use.refusal`, `InventoryAdapter` (`RefusalCheck`) | `the partner never takes some items ...`, `a fixed evolution item is refused ...` | Igual |
| Solo la comida responde al hambre | `partner_impl.c: partnerHandleFoodFeed` | `item_use.meals` → `FoodEffect::meal` | `food feeds the partner ...` | Igual |
| Descuento de bebé en las tiendas | `script_common.c: isPartnerBaby` (`BABY_IDS`) | `item_use.shop_discount_levels` | `buying a key item asks ...` | Igual |
| Probabilidad de las medicinas | `item.c: handleMedicineHealing` (`randomLimit(roll) < cure`) | `CureEffect` con `cure × 100 / roll` | — | Igual con los valores del original (3/3, 2/10, 10/10) |
| Sección de guion de un chip | `item.c: handleChips` (0x16) | — | — | **Hueco** (fase 4) |
| Técnicas aprendidas en los requisitos | `evolution.c: getNumMasteredMoves` | `EvolutionAdapter::setMoveSource`, `BattleAdapter::learnedMoveIndices`, `evolution.requirements.mastered_moves_below` | `the evolution requirements count the learned moves below ...` | Igual |
| Técnicas del primer partner: aprende y equipa una, y su remate | `partner_impl.c: initializePartner` | `partner_moves.new_game_slot`, `BattleAdapter::startNewGame` | `a new game's partner learns ...`, `the user's disc, extracted: the first partner ...` | Igual |
| Las técnicas equipadas son posiciones de la lista de la especie | `stats.base.moves`, `btl.c`, `utils2.c: entityGetTechFromAnim` | `PartnerMoves::equipped`, `partnerCombatant` | `the partner fights with its stats, ...` | Igual |
| El renacido equipa una técnica y conserva las aprendidas | `evolution.c: reincarnatePartner` | `partner_moves.reborn_slot`, `Reborn` | `a reborn partner equips one move again ...` | Igual |
| Lo que deja una batalla al partner (stats, cuidado, herida, vidas, 20 minutos y la hora) | `battle_ui.c: battleStatsGainsAndDrops`, `tamer.c: startBattle/advanceBattleTime` | `BattleAdapter::finish` → `Partner::applyBattle` | `a won battle gives ...`, `a lost battle costs ...` | Igual |
| Autoguardado al unirse un recluta y tras un torneo | El original no autoguarda | `save.autosave.on_recruit_join/on_tournament_result` | `a recruit that joins ...`, `a tournament's battle ...` | Desviación aprobada (2026-10-20) |

## Tests

`tests/session_tests.cpp` (partida nueva, misma semilla, criar días a avance rápido → guardar → cargar en otra
partida → seguir igual, ranuras, autoguardado al dormir y por día nuevo según el ruleset, ajuste en marcha, muerte y
renacer, la vista del partner, nombres de ranura), `tests/field_clock_tests.cpp` (con un campo de prueba: el ritmo del
reloj de antes, el reloj parado en los fundidos a cualquier velocidad, autoguardado al cambiar de mapa y el sitio
guardado), `tests/save_migration_tests.cpp` (partidas con secciones de versiones anteriores),
`tests/evolution_adapter_tests.cpp` (con las tablas inventadas de `game/evolution/tests/fixture.hpp`: evolución por
horas y autoguardado, barra de virus, vuelta con los stats topados, objeto, guion, guardar y cargar, comprobación de
sección), `tests/inventory_adapter_tests.cpp` (con los objetos inventados de `game/inventory/tests/fixture.hpp`: bolsa de
partida nueva y su lista, curar, comer con refuerzo, chip, rechazos, objeto de evolución por nivel, compra con objeto
clave y descuento, guardar y cargar, derrota con la misma semilla, comprobaciones de sección),
`tests/battle_adapter_tests.cpp` (con las técnicas inventadas de `game/battle/tests/fixture.hpp`: técnicas de partida
nueva, luchador, equipar, victoria y derrota, autoguardados, una batalla de principio a fin, renacer, guardar y cargar,
comprobaciones de sección; con `DWE_CONTENT`, el primer partner del disco), `tests/system_hook_tests.cpp` (alabanza y regañina a los sistemas, nivel de domador con probabilidad) y `tests/rules_tests.cpp`
(esquemas de `new_game`, `tamer`, `item_use` y `partner_moves`, valores del original y comprobaciones). Usan el
paquete `game` del árbol de fuentes sobre un paquete base de sustitución con las tablas vacías, especies inventadas
(no las del original) y `MemorySaveStorage`.
