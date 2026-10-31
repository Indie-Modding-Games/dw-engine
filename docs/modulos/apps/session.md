# session

La partida (`dwe::session`): la librería de app que junta los sistemas del juego. Los módulos de `game/` no se usan
entre sí ([regla 4](../../architecture/dependency-rules.md)), así que este es el único sitio que los une. El juego
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
   `TamerLevelChanged` → nivel de domador entre los límites de `tamer`, `Died` → renacer (salvo si la historia
   juega la muerte: ver abajo). Lo que provoca un evento (renacer da `Reborn`) se reparte también.
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

- `new_game` ([esquema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/ruleset_new_game.schema.json)): el partner de inicio, sus estadísticas y
  el nivel de domador. Las estadísticas de la sección son la reserva: si el paquete base trae `data/new_game_stats`
  (lo que pone el guion 193 tras la introducción, extraído por `dwx`;
  [scripts](../../formats/scripts.md#estadísticas-de-la-partida-nueva)), el partner empieza con las de su
  especie en ese documento, o con las de `others`, con el HP y el MP actuales que da el guion.
- `new_game_scene` ([esquema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/ruleset_new_game_scene.schema.json)):
  las preguntas de Jijimon antes del primer mapa ([apps/game](game.md#apertura-de-la-partida-nueva)). Lo que
  responde el jugador llega a `startNewGame(NewGameChoice)`: el partner que eligen las respuestas sustituye al de
  `new_game` y los nombres que escribe quedan en `GameSession::names()`, que la partida guarda en el resumen de su
  cabecera (`tamer`, `nickname`) y que el cuadro de texto de la historia usa para `{tamer}` y `{partner}`.
- `tamer` ([esquema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/ruleset_tamer.schema.json)): los límites del nivel de domador y los
  niveles que da una evolución cuando sale su probabilidad (`evolution_level_gain`, `evl.c: EVL_applyEvolution`).
- `story` ([esquema](../../content/schemas/ruleset_story.schema.json)): `ticks_per_frame`, los ticks del motor por
  fotograma de la historia del original (3: 60 Hz contra 20 fotogramas por segundo), y `variable_max`, el tope de una
  variable de la historia (255), y los números del flujo de un script que usa el runner: la variable y el valor de
  «jugador quieto» (0 y 3), el disparador de girarse en una charla (44) y la variable del nombre del mapa con sus tres
  valores (6; 0, 1, 2). La lee `StoryAdapter`, que en la primera lectura crea la VM de scripts de la partida,
  publica en ella la API `story` y comprueba que los módulos de historia vienen de este `dwx`; si algo de eso falla, la
  partida no se crea ([intérprete de historia](../../architecture/interprete-historia.md)).

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
| `story` | Los disparadores y las variables de la historia (`StoryAdapter`, versión 1, [game/story](../game/story.md)); guarda las variables 0 a 34, como el original, y la variable de «jugador quieto» como 0 si tiene su valor (`main_menu.c: writeSavegame`); al cargar, las demás empiezan a 0; una partida sin ella carga la historia como la deja una partida nueva, con los disparadores de criado (0x200 + número) de las formas que guardó la sección `evolution`, y lo registra |
| `battle_registry` | Los compañeros registrados en la máquina de guardado para el modo versus (`BattleRegistry`, `main_menu.c: registerBattleData`): `partners`, cada uno con `slot` (0 a 39), `name`, `species`, `hp`, `mp`, `offense`, `defense`, `speed`, `brains`, `discipline` y `techniques` (tres ids de `data/moves` o `null`); una partida sin la sección empieza sin registrados |

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

El juego y `dwe-sim` enchufan los tres con `createGameSystems(files, tablas)` (`game_systems.hpp`): da la lista para
`SessionDesc::systems`, en el orden en que avanzan (evolución, inventario, batalla), y los adaptadores por tipo para
las órdenes de la app (empezar un combate, usar un objeto), válidos mientras viva la partida. `loadSystemTables` lee
una vez las tablas de los tres (`dwe-sim` las comparte entre sus partidas); sin ellas, cada adaptador lee las suyas.

`field_facts.hpp` une la partida con los [warps de script](../../architecture/warps-de-script.md) del campo:
`fieldGuardFacts(partida, inventario, historia)` da los lectores de disparadores, bolsa, dinero, estadísticas y
condiciones del partner con los que el campo decide sus condiciones, y `fieldTriggerWriter(historia)` escribe en la
historia los `sets_flags` del warp tomado.

- **Candado de historia** (`story_lock.hpp`, sección de ruleset `story_lock`, esquema `ruleset_story_lock`): el juego
  se entrega por etapas y el candado dice qué parte está abierta. Cada etapa nombra los mapas que abre (ids o
  prefijos con `*`) y, solo mientras es la activa, su frontera: `exits` (a qué mapas abiertos llevan las salidas de un
  mapa) y `sections` (qué secciones de casilla lanza un mapa). Las etapas son **acumulativas**: la etapa N abre los
  mapas de las etapas 1 a N, y `active_stage` es la última entregada. La opción `--story-stage <id>` de `dwe-game`
  juega otra sin tocar los datos y `--story-stage all` lo abre todo (desarrollo y pruebas); el log dice al leer las
  reglas qué etapa se juega (`[port:story-lock]`). `StoryAdapter::lock()` la responde y la app
  (`StoryFieldLink`) la aplica: una salida pisada, el cambio de mapa de un script o una sección de casilla hacia lo
  cerrado no ocurre y el diálogo muestra `closed_message` (el cambio de un script cerrado así no es un error en el log:
  lo es solo si no hay mapa o nadie lo juega). Solo se cierran los cambios de mapa **nuevos**: una
  partida guardada más allá de la etapa no se rompe, el jugador se queda donde está y sale por cualquier salida hacia
  un mapa abierto. Un ruleset sin la sección no cierra nada. Abrir una etapa más es solo editar datos. La Etapa 1
  sale de la verificación E1-01 en el decomp; las etapas 3 a 9 ya tienen sus mapas (de los scripts extraídos y las
  salidas de los mapas) para probarlas con `--story-stage`, aunque `active_stage` no las alcance.

La historia es la única dueña de las formas criadas, como en el original (`setDigimonRaised` pone el disparador
0x200 + número): `EvolutionAdapter` (con `EvolutionAdapterDesc::story`) las escribe ahí al evolucionar, al renacer y
al empezar, y antes de cada comprobación de evolución lee de ahí las que ve como criadas, así que un script que pone
uno de esos disparadores también cuenta. La sección `evolution` sigue guardando una copia, de la que sale la historia
de una partida sin sección `story`.

### Evolución (`EvolutionAdapter`)

`EvolutionAdapter({.files = &files})` lee la sección `evolution` y sus tablas (las del manifiesto, una vez; los tests
le dan las suyas en `.tables`) y registra su comprobación de sección y la sección de guardado `evolution`.

- **Entradas**: llena `EvolutionInputs` desde el partner: forma, stats, peso, errores de cuidado, disciplina,
  felicidad, cansancio, batallas, días, contador de evolución, la forma virus
  (`care.toilet.virus_form`), la copia de la forma virus, el mapa
  (su id de `data/maps`, vacío fuera del campo; una regla especial nombra el mapa por id, `map: { equals: mayo02 }`,
  y un id que no está en la tabla de mapas se avisa al leer las reglas y la regla nunca se cumple; `dwx validate` lo da
  como problema), si el domador está libre
  (`careContext().tamerIdle`), si corre un guion (solo en las rutinas de guion) y las técnicas aprendidas: las del
  `BattleAdapter` (`moves().learned`, por ID), contadas con `countMasteredMoves` contra la tabla de técnicas de la
  sesión (las del original de número menor que `evolution.requirements.mastered_moves_below`, 64:
  `evolution.c: getNumMasteredMoves`, y las que nombre `mastered_move_ids`, como las que añade un paquete). El adaptador de batalla se
  crea después (necesita el inventario, que necesita la evolución), así que se le da con
  `setMoveSource(batalla)`; sin él, la evolución no ve técnicas aprendidas.
- **Cuándo mira**: en cada tick en que corre el reloj de juego, `checkNatural` con los fotogramas corridos (una
  comprobación por fotograma, como `tickConditions`; el contador lo sube el partner; si no evoluciona, el adaptador
  deja el que devuelve), salvo en la hora protegida tras despertar (`HAS_IMMORTAL_HOUR`) o si la barra de virus se
  llenó en ese tick:
  el virus gana (`partner_impl.c: tickConditions`) y la evolución natural espera. `VirusBarFull` → `fillVirusBar`;
  `Slept` → `wakeUp`; tras alabar o regañar → `praiseOrScold`; `Reborn` marca al bebé como criado
  (`evolution.c: reincarnatePartner`). Órdenes: `useItem(session, objeto)` (la llama el inventario),
  `returnFromVirus(session)` (rutina de guion 2) y `evolveByScript(session, forma)` (rutina 10).
- **Aplicar** (`evl.c: EVL_applyEvolution`): `Partner::changeSpecies` a la forma destino; `statChange` por
  `addStatGains` o, al volver de la forma virus, `restoredStats` con `setStats` y cada stat en su tope de
  `care.limits` (sin el tope por ganancia, decidido el 2026-10-20); HP y MP actuales dentro de los nuevos máximos;
  la técnica aprendida (con las que se aprenden con ella, `utils2.c: learnMove`) y las cuatro ranuras equipadas por
  `BattleAdapter::evolveMoves`; peso, errores de cuidado, batallas y contador; horas de vida extra (`addLifetime`); la probabilidad de nivel de
  domador con `tamer.evolution_level_gain`; la barra de virus a 0; la copia de la forma virus (forma y stats) en
  `PartnerState::virusBackup`; `recordEvolution` y autoguardado `Evolution`. `takeEvolutions()` da las aplicadas
  para la UI.

### Inventario (`InventoryAdapter`)

`InventoryAdapter({.files = &files, .evolution = &evolucion})` es dueño de la bolsa (`game::inventory::Inventory`) y
de los efectos (`ItemEffects`, con `data/items` y `data/item_effects`, leídos una vez; los tests le dan los suyos).
Registra las comprobaciones de las secciones `inventory` e `item_use` y la sección de guardado `inventory`
(`inventory::toYaml`, versión `SectionVersion`; la migración de la versión 1, que guardaba números, a ids es
`inventory::itemNumbersToIds`). Al leer las reglas comprueba que cada objeto que nombran está en `data/items` y que los
rangos de `item_use` solo nombran objetos del original (`checkItemIds`, `checkItemUseItems`).

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

La sección `item_use` ([esquema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/ruleset_item_use.schema.json)) es de la partida: qué objetos
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
- **Acabar** (`finish(session, BattleResult)`): HP y MP del final (`setStats`; tras perder, ya con lo que devuelve
  `battle.consequences.defeat_restore`), las subidas por `addStatGains`, y el cuidado, la herida, las vidas y los
  minutos como los da la batalla por `Partner::applyBattle` (con la cuenta horaria del partner si la batalla acaba una
  hora: `tamer.c: advanceBattleTime`); las técnicas nuevas, los bits y los objetos ganados a la bolsa (los que no caben quedan en `dropsLeft`) y, en una
  derrota, las pilas perdidas por `InventoryAdapter::loseOnDefeat`. Pide los autoguardados `BattleEnd`, `RecruitJoin`
  (si se unió el recluta: `recruitJoined`) y `TournamentResult` (torneo o arena).
- **Lanzar un objeto** (`throwItem(combate, objeto)`): el domador lanza un objeto de la bolsa al compañero. Lo usa
  `InventoryAdapter::useInBattle` con `UseContext::Battle` (solo los de `item_color` 1 o 2: `inventory.c:
  selectOptionMenu`), que lo gasta al lanzarlo (`main.c: startThrowingItem`), y su efecto se convierte en el
  `ThrownItem` del combate, que siempre llega (`main.c: tickThrownItem`). Falla si no hay bolsa o si el combate no la
  deja abrir (`Battle::bagAvailable`).

La sección `partner_moves` ([esquema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/ruleset_partner_moves.schema.json)) es de la partida.

### Tienda (`ShopCounter`)

La tienda que abre la historia (`script_interp.c: CALL_ROUTINE 8` → `script_common.c: openShop`). La historia llama a
`open(ShopVisit)` con el tendero (la variable 254 y su especie) y espera mientras `visit()` siga abierta; la pantalla
`shop_menu` del juego (`apps/game: ShopScreen`) la cierra cuando el jugador se va. Compra con
`InventoryAdapter::buy` (el surtido son los triggers `0x180 + i` de la historia) y vende con
`game::inventory::sell`. Las frases del tendero son las de su sección de MAPHEAD (`getShopkeeperLine`,
`resolveMapHeadEntry`), leídas del listado `script/maphead`. La sección `shop`
([esquema](../../content/schemas/ruleset_shop.schema.json)) es opcional: sin ella no hay tiendas. La tienda de
reciclaje (`script_menu.c: openRecycleShop`, con el trigger `recycle_trigger` o `ShopVisit::recycle`) vende lo que
quitaron las derrotas (`handleItemLoss`: los objetos de `recyclable`, hasta `recycle_max` de cada uno, que llegan por
`InventoryAdapter::setLossListener`) y no compra; su surtido se guarda en la sección `shop` de la partida.

### Guardián de objetos (`ItemKeeper`)

El guardián de objetos que abre la historia (`script_interp.c: CALL_ROUTINE 11` → `shop/script_menu.c:
tickItemKeeper`). La historia llama a `open(ItemKeeperVisit)` con el guardián (la variable 254 y su especie) y espera
mientras `visit()` siga abierta; la pantalla `item_keeper` del juego (`apps/game: ItemKeeperScreen`) la cierra cuando
el jugador se despide. `keptRows` y `bagRows` son las listas de `itemKeeperFillItemList` y `move` pasa uno, diez o la
pila (`keeperMoveOne`, `keeperMoveTen`, `keeperMoveStack`) entre lo guardado y la bolsa. Lo guardado (`SCRIPT_STATE`
0x54 + objeto en el original) va en la sección `item_keeper` de la partida, por id de objeto. La sección
`item_keeper` del ruleset ([esquema](../../content/schemas/ruleset_item_keeper.schema.json)) da la sección de MAPHEAD
con las frases, el máximo de cada objeto y el paso del botón alternativo; sin ella la visita se cierra al instante.

### Tirar un objeto (`DiscardMenu`)

El menú que abre la historia para elegir un objeto de la bolsa (`script_interp.c: CALL_ROUTINE 0` →
`script_common.c: openDiscardItem`): sale cuando el jugador recibe algo con la bolsa llena, o cuando un personaje pide
un objeto. `open()` lo abre si la bolsa tiene algo que se pueda tirar (`shopFillSellItemList`: un hueco por fila, y solo
se elige lo que tiene `droppable`); si no hay nada, pone el disparador 3 y 255 en la variable 254 y no se abre, como el
estado 0 del original. `choose()` escribe el número del objeto en la 254 y quita el disparador 3 (`tickSingleCardShop`
con «Yes»); `cancel()` solo cierra si el disparador 3 está puesto (`tickItemMenu`) y escribe 255. El menú no saca nada
de la bolsa: lo hace el guion después (rutinas 12 y 13). La sección `discard_menu` del ruleset
([esquema](../../content/schemas/ruleset_discard_menu.schema.json)) da la sección de MAPHEAD con «Is it OK?» (1240,
línea 0), el disparador, la variable y el valor de «ninguno»; sin ella la rutina se cierra al momento. La pantalla
`discard_menu` del juego (`apps/game: DiscardScreen`) lo enseña.

### Gramola (`Jukebox`)

La gramola del restaurante que abre la historia (`script_interp.c: CALL_ROUTINE 9` → `shop/script_menu.c:
openJukebox`; en room05b la abre Giromon, script 175, sección 0x6). `open()` rellena la lista como
`jukeboxFillTitleList`: las entradas 0 a 62 de `BGM_TRACK_NAMES`, la 62 solo con el disparador 50; busca la canción
que suena (fuente y pista) entre las de la lista y escribe su fila en la variable 249, o 255 y para la música si no
está. `startView` coloca la lista como `createJukeboxMenu` (la fila marcada arriba si cabe en la primera ventana, si no
abajo del todo), `choose` escribe la elegida en la 249 y `playChosen` la toca si no es la que ya sonaba
(`tickJukeboxMenu`: `stopBGM` y `playMusic(fuente, pista)`); `close` vuelve a tocar la música del mapa desde el
principio (`playBGM(ACTIVE_BGM_FONT)`, estado 2). La fuente y la pista de cada entrada (`MAIN_D_801303B8`) vienen de
`font` y `track` en `bgm_tracks` de `data/music.yaml`; un paquete extraído antes las busca en `used_by.jukebox` de
cada `music/NNN/font_meta.yaml`. La música la da `SessionMusic` por la interfaz `JukeboxMusic`. La sección `jukebox`
del ruleset ([esquema](../../content/schemas/ruleset_jukebox.schema.json)) da la sección de MAPHEAD con las frases
(0x4d6: saludo 3, pregunta 0, respuesta 2, despedida 1), el disparador, las entradas bloqueadas, la variable y el valor
de «ninguna»; sin ella la rutina se cierra al momento. La pantalla `jukebox` del juego (`apps/game: JukeboxScreen`) lo
enseña.

### Cambio de objetos (`ItemTrader`)

El trueque que abre la historia (`script_interp.c: CALL_ROUTINE 2` → `shop/script_menu.c: openMojyamonShop`). El guion
pone en la variable 249 cuál de las tres tablas usa y en la 254 quién cambia; `open(ItemTraderVisit)` lee la tabla y
escribe 255 en la 249 (estado 0). `rows()` son las filas de `mojyaTradeFillItemList`: lo que da el jugador, lo que da
el otro, si la bolsa lo tiene (si no, va en rojo) y si ese cambio ya se hizo (la marca). `offer(fila)` escribe el
objeto ofrecido en la 249 (lo pide la frase «{item:pstat=249}») o falla si la bolsa no lo tiene; `trade()` es el estado
5: primero da el objeto nuevo y, si cabe, quita el ofrecido y pone el disparador del cambio (`setMojyaItemTradedTrigger`:
5 + 3 × tabla + fila, y el 14 cuando están los nueve); si no cabe no cambia nada («Too many Items.»). La sección
`item_trader` del ruleset ([esquema](../../content/schemas/ruleset_item_trader.schema.json)) da la sección de MAPHEAD
(1238, líneas 8 a 14), la variable, los disparadores, el sonido de rechazo y las tablas (`MOJYAMON_ITEMS_GIVE` y
`MOJYAMON_ITEMS_GET`); sin ella la rutina se cierra al momento. La pantalla `trade_menu` del juego
(`apps/game: TradeScreen`) lo enseña.

### Transporte en pájaro (`BirdTransport`)

El pájaro de room12 que lleva al jugador a otra zona (`script_interp.c: CALL_ROUTINE 10` → `shop/script_menu.c:
tickBirdraTransport`). El guion pone en la variable 254 quién habla. `open()` toma las filas una vez, como
`birdraFillTargetList`: los destinos cuyo disparador está puesto, en el orden de la tabla, y si el dinero alcanza (si no,
van en rojo). `pick(fila)` falla en una fila que el dinero no paga (suena el rechazo); `travel()` es el estado 5:
escribe el mapa y la entrada del destino en las variables 247 y 248, cobra el precio y devuelve la sección de MAPHEAD
(0x4e3, «Hold on tight!» y el `teleport` de esas variables) donde sigue el guion: la rutina 10 acaba con `TaskJump`
(`StoryScenes::screenClosed(salto)`) en vez de seguir tras su llamada. La sección `bird_transport` del ruleset
([esquema](../../content/schemas/ruleset_bird_transport.schema.json)) da las frases (MAPHEAD 1238: 4 saludo, 5
despedida, 6 «Are you sure?», 7 sí o no; la del cuadro de dinero, la 6 de 1220), las variables, la sección del viaje,
el disparador que impide cancelar el sí o no (49), el sonido de rechazo (0, 11) y los seis destinos de
`BIRDRA_TRANSPORT_TARGETS` (mapa, entrada, disparador y precio); sin ella la rutina se cierra al momento. La pantalla
`bird_transport` del juego (`apps/game: BirdTransportScreen`) lo enseña.

### Tienda de cartas (`CardShop`)

La tienda de cartas de gias06b (`script_interp.c: CALL_ROUTINE 14` → `shop/script_menu.c: openBuyCardMenu`,
`openSellCardMenu`, `rollCardPack`). Con el disparador de vender la rutina abre la pantalla `CardSell`; si no, con el de
comprar cartas, `CardBuy`; si no, un sobre: tres cartas, una por estado, cada una con su frase y el jugador como
hablante (`rollCard`, `showCardTextbox`; la variable 249 lleva la especie de la carta, que la frase nombra). Las seis
cartas del día (`hasAnyCardToBuy`, la ranura vendida a 0xFF) se sortean en la partida nueva y en cada día nuevo, de la
1 a la 64 sin la 4 (pueden repetirse). `buy` cobra el precio por rareza (`CARD_PRICES`) y suma una copia; `sell` paga la
mitad por copia. La sección `card_shop` del ruleset ([esquema](../../content/schemas/ruleset_card_shop.schema.json)) da
el sorteo, el divisor de venta, las frases de MAPHEAD (1235 y «Is it OK?» de 1240), el sonido de fallo (0, 11) y el del
trato (0, 23) solo en gias06b (`playShopSoundOnlyInSavannah`); el estado (las seis cartas) va en la sección `card_shop`
de la partida. La pantalla `card_shop` del juego (`apps/game: CardShopScreen`) enseña las listas.

### Intercambio por méritos (`MeritShop`)

El intercambio del Shogun de gkyo02 (`script_interp.c: CALL_ROUTINE 1` → `shop/script_menu.c: openMeritShop`). El
jugador da cartas por puntos de mérito (el `merit` del registro del jugador) y toma los objetos que tienen precio en
méritos (`shopFillMeritItemList`). `giveCard` quita una copia y suma su `meritValue` hasta 9999; `takeItem` cobra el
precio y mete uno en la bolsa, solo si se puede tomar. La sección `merit_shop` del ruleset
([esquema](../../content/schemas/ruleset_merit_shop.schema.json)) da las frases (MAPHEAD 1236: saludo 0, opciones 1,
sí o no de la carta 2 y del objeto 3, sobre la lista de cartas 5 y de objetos 6, lo que vale la carta desde la 7 más
la rareza), la línea de la caja de puntos (7 de la sección 1220), el tope (9999), el disparador que impide cancelar
las opciones (49), el sonido de fallo (0, 11) y el del trato (0, 23) solo en gias06b; sin ella la rutina se cierra al
momento. La pantalla `merit_shop` del juego (`apps/game: MeritShopScreen`) lo enseña.

## Paridad con el original

| Regla | Decomp | Nuestro | Test | Estado |
|---|---|---|---|---|
| Partida nueva con el partner de id 3 | `main.c: main` (`initializePartner(3)`) | `new_game.partner: agum` | `a new game starts the partner, ...` | Igual |
| Estadísticas de inicio del partner | script 193 de `DG.SCN`, offsets 0x0918–0x0964 (`store_digimon_type`, `if` y `set_stat`; `script_interp.c: SCRIPT_OP_SET_STAT`) | `data/new_game_stats` (`dwx`, `findNewGameStats`), `GameSession::startingStats`; `new_game.stats` solo sin el documento | `a new game takes the partner's stats from what the new-game script sets ...`, `the user's disc, extracted: the partner starts with ...` | Igual (con datos extraídos); el guion los pone tras la introducción y aquí al empezar |
| Nivel de domador 0 y hora 8:00 al empezar | `main_menu.c: initializeDefaultSavegame` | `new_game.tamer_level`, `clock.start` | idem | Igual |
| Nivel de domador entre 0 y 10 | `tamer.c: addTamerLevel` | `tamer.level_min/level_max`, `changeTamerLevel` | `the original's tamer level ...` | Igual |
| El reloj corre en el campo y se para en los cambios de mapa | `clock.c: tickGameClock`, `tamer.c: stopGameTime/startGameTime` | `advanceGameTime` con `Field::gameTimeRan` | `field_clock_tests.cpp` | Igual |
| El reloj y el cuidado se paran durante todo el combate; al acabar, 20 minutos de golpe | `tamer.c: startBattle` (`stopGameTime`), `clock.c: tickGameClock` (vuelve si `GAME_STATE != 0`), `tamer.c: advanceBattleTime` | el campo no corre mientras se ve el combate (`apps/game`: `BattleScreen::active`), así que `Field::gameTimeRan` no avanza el reloj; los minutos los da `Partner::applyBattle` | `a won battle gives the partner its HP, raises, care, minutes...` | Igual |
| Un minuto de juego por segundo | `clock.c: tickGameClock` | sección `clock` | `on the field the game clock keeps ...` | Igual |
| Los efectos del área vuelven a empezar en cada mapa | `partner.c: setupPartnerOnWarp` | `followMap` → `Partner::enterMap` | `a map change autosaves, ...` | Igual |
| Autoguardado al dormir, al cambiar de mapa y al acabar una batalla | El original no autoguarda | sección `save` | `the partner going to sleep autosaves`, `a map change autosaves, ...` | Desviación aprobada (2026-10-20) |
| Autoguardado al empezar un día | El original no autoguarda | `save.autosave.on_new_day: true` | `a new day autosaves only when ...` | Desviación aprobada (2026-10-20) |
| Al morir, renace enseguida como bebé | `partner.c` (muerte y renacer tras la escena de muerte) | `routePartnerEvent` → `Partner::rebirth` | `a partner that dies is reborn as a baby` | Solo sin la historia; con ella, la fila siguiente |
| Una vida perdida o una muerte corren la sección `0x4DE` del script 0 (variable 255 a 0); la muerte espera a la escena de renacer (rutina de compañero 4), que usa el huevo de la caja (rutina 29) si quedan vidas, o uno al azar | `partner_impl.c: tickDeathCondition`, `doo2.c: DOO2_openEggBox`, `DOO2_tickEggInput`, `dooa.c: DOOA_getSequenceState` | `story_scenes.death`: `StoryScenes::routePartnerEvent` y `tick` piden la sección cuando el jugador está libre (`playerFree`) y no corre ningún guion; un partner muerto sin su sección (una partida guardada a media muerte) la pide otra vez; `GameSession::setRebirthByStory`, `StoryScenes::chooseEgg`, `rebirthPartner(huevo)` | `a partner whose death the story plays waits ...`, `the egg box's egg counts only while ...` | Igual, sin la carga del overlay (`loadDynamicLibrary`), los sprites de los huevos en VRAM ni la animación de la caja |
| Respuesta del partner al área (le gusta, no le gusta), calculada al entrar en cada mapa con las listas de regiones del mapa y la región favorita de la especie | `map.c: loadMapSetup` (`MAP_TILE_DATA.partnerAreaResponse`). En la decompilación lo llama `map.c: loadMap`, y a este el cambio de mapa de los guiones (`script_interp.c`); según eso, la evolución y el renacer no lo recalculan hasta que se carga otro mapa | `attachField` y `followMap` → `game::partner::areaResponseOf`; `careContext`, `partnerView` y el sueño (`care_commands.cpp`) | `how the partner feels about the map follows the map entered, ...`; en `game/partner`: `area response: ...` | Igual |
| Montones de caca en el suelo: hasta 16 por mapa y 100 en total, delante del compañero, guardados en la partida y dibujados en el campo; la caca en el campo tiene su escena: el jugador se para y mira, el compañero corre hacia él, se agacha, la deja y hace su animación de después | `partner_impl.c: createPoopPile`, `renderPoop`, `initializePoop`, `handleSleeping`; `partner.c: tickWildPoop` (estado 7) | `PoopAdapter` (`poop_adapter.hpp`): `PoopedOnGround` deja un montón pendiente y el campo lo coloca (`apps/game`: `FieldPoop`, con `Field::runPartnerUp`); con un campo (`CareContext::fieldScenes`) la caca espera su escena (`Partner::wildPoopDue`), que la deja con sus efectos (`GameSession::leaveWildPile`) y tira el nivel de domador al acabar (`GameSession::finishWildPoop`); sección de guardado `poop_piles`; reglas `poop_piles` (cómo se ven, `wild`) y `care.toilet` (cuántos caben) | `a pile the partner left waits for the field...`, `the piles go in the save and come back`; en `game/partner`: `with a field the pile and its care effects wait...`; en `game/field`: `a pile's scene runs the partner up...` | Igual: los efectos (error de cuidado, ánimo, peso) llegan al dejar el montón y el nivel de domador al acabar la escena |
| El compañero se come un montón de caca cerca del jugador | `partner.c: partnerTickEatShit` (estado 9), `partner_impl.c: tickPoopDetection` (radio 200 o 300 en x y en z), `handleEatingPoop` | `FieldPoop::update` con el campo libre: si `Partner::eatsPoop`, el primer montón del mapa con el jugador en su caja; el compañero va andando, hace la animación de comer y `GameSession::eatPoop` | `the original's piles play their scene and are found nearer when small`; en `game/partner`: los de `eatPoop` | Igual; si el compañero se atasca de camino, come donde se para |
| El primer partner y cada bebé renacido quedan como criados | `main.c: main`, `evolution.c: reincarnatePartner` (`setDigimonRaised`) | `EvolutionAdapter::startNewGame`, `Reborn` | `the evolution adapter marks the first partner as raised ...` | Igual |
| La evolución se mira en cada fotograma del reloj de juego, aunque se adelante, salvo en la hora protegida | `partner_impl.c: tickConditions` | `EvolutionAdapter::tick` → `checkNatural` con `advance.frames` | `a baby evolves when the clock's hour ...`, `the natural check runs on every frame ...`, `no natural evolution in the hour after waking` | Igual |
| Lo que cambia al evolucionar | `evl.c: EVL_applyEvolution`, `EVL_clampBaseStats` | `EvolutionAdapter::apply` | `an evolution item turns ...`, `back from the virus form ...` | Igual (vuelta del virus: decisión del 2026-10-20) |
| Nivel de domador al evolucionar a campeón o definitivo | `evl.c: EVL_applyEvolution` (`addTamerLevel(20/100, 1)` si no corre un guion) | `tamer.evolution_level_gain`, `GameSession::addTamerLevel` | `a tamer level chance of 100 ...` | Igual |
| Barra de virus llena y evolución natural en la misma hora | `partner_impl.c: tickConditions` (el virus se mira después y su destino gana) | `EvolutionAdapter::tick` no mira la evolución natural si el partner tiene pendiente `VirusBarFull` (`Partner::pendingEvents`); el evento la convierte en la forma virus | `the virus bar filling in the hour the counter reaches ...` | Igual (decidido el 2026-10-20): se decide por tick de la partida, no por fotograma |
| Qué objetos rechaza el partner | `partner_impl.c: partnerWillRefuseItem` | `item_use.refusal`, `InventoryAdapter` (`RefusalCheck`) | `the partner never takes some items ...`, `a fixed evolution item is refused ...` | Igual |
| Solo la comida responde al hambre | `partner_impl.c: partnerHandleFoodFeed` | `item_use.meals` → `FoodEffect::meal` | `food feeds the partner ...` | Igual |
| Trueque de objetos: tabla de la variable 249, el objeto nuevo antes de quitar el ofrecido, disparador 5 + 3 × tabla + fila y el 14 con los nueve | `shop/script_menu.c: openMojyamonShop`, `mojyaTradeFillItemList`, `setMojyaItemTradedTrigger` | `ItemTrader`, `tradeRows`, `makeTrade`; sección `item_trader` | `item_trader_tests.cpp`; ruta `trade` (frzl14) | Igual |
| Descuento de bebé en las tiendas | `script_common.c: isPartnerBaby` (`BABY_IDS`) | `item_use.shop_discount_levels` | `buying a key item asks ...` | Igual |
| Probabilidad de las medicinas | `item.c: handleMedicineHealing` (`randomLimit(roll) < cure`) | `CureEffect` con `cure × 100 / roll` | — | Igual con los valores del original (3/3, 2/10, 10/10) |
| Sección de guion de un chip | `item.c: handleChips` (0x16) | — | — | **Hueco** (fase 4) |
| Técnicas aprendidas en los requisitos | `evolution.c: getNumMasteredMoves` | `EvolutionAdapter::setMoveSource`, `BattleAdapter::moves`, `evolution.requirements.mastered_moves_below` | `the evolution requirements count the learned moves below ...` | Igual |
| Técnicas del primer partner: aprende y equipa una, y su remate | `partner_impl.c: initializePartner` | `partner_moves.new_game_slot`, `BattleAdapter::startNewGame` | `a new game's partner learns ...`, `the user's disc, extracted: the first partner ...` | Igual |
| Las técnicas equipadas son posiciones de la lista de la especie | `stats.base.moves`, `btl.c`, `utils2.c: entityGetTechFromAnim` | `PartnerMoves::equipped`, `partnerCombatant` | `the partner fights with its stats, ...` | Igual |
| El renacido equipa una técnica y conserva las aprendidas | `evolution.c: reincarnatePartner` | `partner_moves.reborn_slot`, `Reborn` | `a reborn partner equips one move again ...` | Igual |
| Lo que deja una batalla al partner (stats, cuidado, herida, vidas, 20 minutos y la hora) | `battle_ui.c: battleStatsGainsAndDrops`, `tamer.c: startBattle/advanceBattleTime` | `BattleAdapter::finish` → `Partner::applyBattle` | `a won battle gives ...`, `a lost battle costs ...` | Igual |
| Alabar y regañar en el campo, junto al compañero: se acerca, el jugador hace su gesto, el compañero responde y cuenta al acabar el gesto | `partner.c: partnerTickPraiseScold`, `tamer.c: tamerTickPraiseScold`, `partner_impl.c: handlePraiseScold` | `GameSession::praise`/`scold`; `field_items.praise_scold` (animaciones 38/39 y 11 al felicitar, 7 y 12/25 al regañar); la escena en `apps/game` (`PraiseScoldCall`) | `system_hook_tests.cpp`, `the original's praise reaches up to a tall partner ...` | Igual ([juego](game.md)); la pestaña del menú del compañero sigue como desviación temporal (decidida el 2026-10-20/21, TODO) |
| Dar un objeto en el campo: el compañero se acerca, lo come o lo rechaza | `game_menu.c: startFeedingItem`, `partner.c: partnerTickFeedItem`, `checkEatDistance` | `InventoryAdapter::offer`/`give`, `field_items.feed`; la escena en `apps/game` (`FeedScene`) | `the feeding scene offers an item first ...`, `the original's feeding takes the item at 360 ...` | Igual, salvo que el compañero decide al elegir el objeto, no al tendérselo, y que el objeto en la mano no se dibuja ([juego](game.md)) |
| Ir al baño: el compañero corre al baño del mapa y vuelve | `partner.c: tickPartnerToilet`, sección 0x4e2 | `field_items.toilet_walk`, los `toilets` de `maps.yaml`; la escena en `apps/game` (`FieldToilet`) | `a care scene runs the partner to a point ...` | Igual; si se atasca, se para ([juego](game.md)) |
| Autoguardado al unirse un recluta y tras un torneo | El original no autoguarda | `save.autosave.on_recruit_join/on_tournament_result` | `a recruit that joins ...`, `a tournament's battle ...` | Desviación aprobada (2026-10-20) |
| La vagoneta y el montón de tierra del Drill Tunnel: la rutina 26 carga la vagoneta sin modelo, la 19 enseña el modelo que dice la variable 254 delante del compañero, la 27 carga el montón a su tamaño y la 20 lo encoge un paso (15 al arrancar, nunca vuelve; se dibuja desde 6); la 19 y la 20 acaban el fotograma | `script_interp.c` (`call_routine` 0x13, 0x14, 0x1a, 0x1b), `map_object.c: loadDirtCartModel`, `setDirtCartModel`, `renderDirtCartModel`, `loadDirtPileModel`, `decreaseDirtPileSize`, `renderDirtPile` | `story_routines/routine_dirt_cart.cpp` (`DirtCartShown`, `DirtPileShown`, `story_props.hpp`); `apps/game`: `FieldStoryProps` | `the dirt pile shrinks a step ...` (`story_props_tests.cpp`) | Igual; el tamaño del montón no se guarda en la partida, como en el original |
| La roca de tunn06: `spawn_boulder` la pone en (-300, -500, 2000), `move_boulder` la mueve en y y z, la gira 10 en x y acaba el fotograma, `despawn_boulder` la quita; cambiar de mapa quita roca, vagoneta y montón | `script_interp.c` (`SCRIPT_OP_SPAWN_BOULDER`, `MOVE_BOULDER`, `DESPAWN_BOULDER`), `map_object.c: spawnBoulder`, `moveBoulder`, `unloadMapParts` | `StoryLuaHost::publishPropApi`, `story_props.cpp` (`BoulderShown`) | `the boulder's orders: ...`, `the boulder starts above ...`, ruta `route-etapa2.yaml` | Igual (sitio de la roca comparado con el original en tunn06) |
| La caca gigante de Trash Mountain: al cargar el mapa 0xa5 (gomi02), y solo ese, sea cual sea la historia, el modelo `ETCNA\TRY` en (1922, 0, 503), sin giro y a escala 1; se dibuja mientras se dibuja el mapa | `map.c: loadMap`, `map_object.c: initializeTrainingPoop`, `renderTrainingPoop`, `TRAINING_POOP_TRANSLATION` | `trainingPoopOn` (`story_props.hpp`); `apps/game`: `FieldStoryProps` | `Trash Mountain has the giant poop ...` (`story_props_tests.cpp`) | Igual; la posición es la constante del original |
| El pedestal del santuario de Angemon (icsa01, guion 93): la rutina 49 pone `ETCNA\ABOX` en (0, 0, 1500) y sigue; la 50 lo desliza 10 por fotograma hasta z 2000 (50 fotogramas desde el siguiente a la orden), con x en 5 cuando z es múltiplo de 20 y en -5 si no, y retiene la sección hasta el final; cambiar de mapa lo quita | `script_interp.c` (`call_routine` 0x31, 0x32; `tickScript`), `map_object.c: spawnAngemonPedestal`, `moveAngemonPedestal`, `renderAngemonPedestal`, `unloadMapParts` | `story_routines/routine_angemon_pedestal.cpp` (`AngemonPedestalShown`); `apps/game`: `FieldStoryProps` | `the Angemon pedestal starts at z 1500 ...` (`story_props_tests.cpp`) | Igual (lógica del decomp; posición y modelo leídos del ejecutable); sin medir en el original: en `_work/tmp/measure-queue.md` |
| Toy Town: la rutina 41 pone la caja de engranajes `ETCNA\GAND` de omoc03 en (-100, 0, 1700); la 43 pone en omoc08 la caja grande `ETCNA\BIGBOX` en (500, -600, 600) y la pequeña `ETCNA\SMABOX` en (-400, -400, 600), tapas cerradas; la 42 y la 44 eligen la caja con la variable 254 como byte con signo (41 y 43 la ponen a 0) y la tapa de la elegida (1 grande, 2 pequeña) gira -100 por fotograma a 20 por segundo hasta -0x400 (11 fotogramas, un cuarto de vuelta) y se queda; las cuatro siguen en el mismo fotograma; la relectura del fondo de la 42 (caja 0 con el disparador 350) no hace falta en el motor | `script_interp.c` (`call_routine` 0x29-0x2c), `map_object.c: spawnGearbox`, `renderGearbox`, `somethingToyTown`, `spawnToyTownBoxes`, `renderToyTownBoxes`, `openToyTownBox`; `map.c: readMapTFS` | `story_routines/routine_toy_town.cpp` (`ToyTownGearboxShown`, `ToyTownBoxesShown`, `ToyTownBoxSelected`, `toyTownLidAfterFrame`); `apps/game`: `FieldStoryProps`, `FieldScene::setPosedProps` | `Toy Town's gearbox and boxes go on at once ...` (`story_props_tests.cpp`) | Igual (lógica del decomp; posiciones y modelos del ejecutable); sin medir en el original: en `_work/tmp/measure-queue.md` |
| Inscripción en la Beetle Cup (rutina 33, betl02, guion 102): pone los disparadores 37, 38 y 39, la variable 2 a 0x95 (0x16 si hoy es el día 0x16; el día siguiente deja los siete bits bajos), la 3 a la copa 0x16 y la 4 a la especie del compañero, y sigue en el mismo fotograma | `script_interp.c` (`call_routine` 0x21), `script_common.c: dailyPStatTrigger` | `story_routines/routine_beetle_cup_entry.cpp` (`beetleCupEntry`) | `the Beetle Cup entry writes the cup ...` (`story_props_tests.cpp`) | Igual (lógica del decomp); el torneo que lee estas variables es la rutina 6 (E3-02); sin medir en el original: en `_work/tmp/measure-queue.md` |
| Quitar la niebla (rutina 37, mist05, guion 115): retiene la sección hasta que `tickRemoveMist` acaba (+0x10 por fotograma a las dos paletas, fin en 0xc0/0x80: 7 fotogramas desde las del arranque); como el guion pone antes el disparador 0x94 y `renderMist` limpia la niebla en cada fotograma dibujado con él (fuera de la pantalla 0xdc), la rutina acaba en su primer tick | `script_interp.c` (`call_routine` 0x25, `tickScript`), `overworld.c: tickRemoveMist`, `renderMist` (`MIST_CLUT_Y`) | `story_routines/routine_remove_mist.cpp` (`MistPalettes`, `removeMistOneFrame`) | `the mist goes in 7 frames ...` (`story_props_tests.cpp`) | Parcial: el motor no dibuja la niebla (E7-03b); el estado y la espera son los del decomp, sin medir en el original (en `_work/tmp/measure-queue.md`) |
| Cargas del jefe final y del final (rutinas 51 y 52, mgen99 y guion 192): leen `BOSS_EFE.TMD`, el overlay EAB y su banco de sonidos 0x15 (51) o la imagen del final, el overlay ENDI y el banco 0x14 (52); en el motor solo queda el banco de la ranura 8, que vuelve al del mapa cuando el campo carga otro mapa; las dos siguen en el mismo fotograma | `script_interp.c` (`call_routine` 0x33, 0x34), `sound_async.c: loadMapSounds2`, `map.c: loadMap` | `story_routines/routine_boss_and_ending_loads.cpp` (`loadRoutineSounds`, `dropRoutineSoundsOnNewMap`), `GameSession::useSceneSounds` | `routines 51 and 52 load ...` (`story_props_tests.cpp`) | Igual (bancos del decomp) |
| Carga del jefe final (rutina 40, mgen99, justo después de que el guion ponga al jefe, especie 115, en el hueco 0): pide la carga sobre el personaje de id 5 (el primer NPC del mapa, el jefe) y sigue la sección en el mismo fotograma; la app lo quita de lo dibujado, pone los anillos y el zumbido hasta que la rutina de compañero 8 lo trae | `script_interp.c` (`call_routine` 0x28), `eab.c: EAB_startBuildup`, `EAB_tickBuildup`, `EAB_addBuildupRing`, `EAB_tickBuildupRing`, `EAB_setModelColor` | `story_routines/routine_boss_buildup.cpp` (`BossBuildupStarted`); `game/field`: `BossArrival`; `apps/game`: `BossArrivalScene` | `routine 40 starts the final boss's charge ...` (`story_props_tests.cpp`), `boss_arrival_tests.cpp` | Suposición: sin medir en el original (no hay estado guardado cerca del jefe; en la cola de medidas); los números salen del decomp |
| Llegada del jefe final (rutina de compañero 8, mgen99 tras la rutina 40): la sesión pide la escena `BossAppears` a la app y la sección espera la sincronía (`wait_for_entity(201)`); la app juega 340 fotogramas (negro, el jefe solo con la cámara girando, anillos, partículas, sonidos 1, 2 y 3) y la da por acabada; luego empieza el combate contra el personaje 5 | la rutina de compañero 8 de `partner.c` y el estado 18 del jugador, `eab.c: EAB_tick`, `EAB_tickSpawn` | `PartnerScenePort` (`PartnerScene::BossAppears`); `apps/game`: `StorySceneGlue`, `BossArrivalScene` | `boss_arrival_tests.cpp`; ruta `_work/routes-e2/boss-arrival/route-mgen99.yaml` (llegada y combate) | Suposición: sin medir (no hay estado guardado cerca del jefe); faltan el dibujo en alambre del jefe, los 9 destellos de `EFECreateFlash` del primer fotograma, repetir la animación 0x33 al acabar y el `vabId` 5 del jefe |
| Empezar el final (rutina 53, guion 192): borra la sincronía de escena, empieza la escena final del jugador (estado 0x10 del jugador, overlay ENDI) y retiene la sección; desde el fotograma siguiente, cuando la escena pone la sincronía, la partida acaba (`MAIN_STATE` 3): la app la cierra, pone la película del final y vuelve a la portada | `script_interp.c` (`call_routine` 0x35, `tickScript` caso 53), `main.c: main` | `story_routines/routine_start_ending.cpp`, `StoryScenes::takeGameEnded`; `apps/game`: `PlaySession` (`MovieMoment::Ending`) | `routine 53 waits for the ending scene's sync ...` (`story_props_tests.cpp`); sonda con el guion 192 en la entrada de tend01 (`_work/routes-e2/routines-pool2/tend01-ending/`) | Parcial: la escena ENDI no existe y acaba al momento (rutina de compañero 6); la rutina 54 (guardado de fin de juego) sigue sin portar |
| El temblor de la escena del fuego (rutina 34): 210 fotogramas a 20 por segundo, pasos de 1, 2, 3, 2 y 1 píxeles, alternando | `map.c: createMeramonShake`, `tickMeramonShake` | `ScreenShakeStarted`; `apps/game`: `FieldStoryProps::advance`, `MapCamera::setShake` | `the dirt pile shrinks a step ...` (la petición), ruta `route-etapa2.yaml` | Parcial: sin el cuadrado aditivo que crece ni el HUD movido (E2-02) |
| La burbuja de humor (rutina 28): la variable 247 da el personaje y la 248 la cara (0 … 4); con 5 o más se quita una burbuja. La cara es un cuadro de 32 × 32 de la primera imagen de 4 bits del mapa (u = cara × 32), aditiva, en el punto de condición de la especie (`conditionFx`), ordenada por profundidad. Medido en el original (twna01, objetos puestos a mano): el contador baja de 30 a 0 a uno por fotograma de 20 por segundo; escala y subida son 16 − contador / 2 (escala 0x100 → 0x1000, sube 1 → 16 píxeles); dos rutinas seguidas dejan dos objetos que bajan el contador dos veces por fotograma; sin el personaje en el mapa no se dibuja ni avanza | `script_interp.c` (`call_routine` 0x1c), `script_common.c: createMonochromonMoodBubble`, `renderMonochromonMoodBubble`, `MONOCHROMON_BUBBLE_TIMER`, `translateConditionFXToEntity` | `story_routines/routine_mood_bubble.cpp` (`MoodBubbleShown`); `apps/game`: `MoodBubble`, `FieldStoryProps::takeMoodBubble`, `FieldScene::setStorySprites` | `routine 28 puts the mood ...` (`story_props_tests.cpp`), `mood_bubble_tests.cpp`; ruta en gcan11 (`_work/routes-e2/mood-bubble/`, turno en la tienda: cara enfadada sobre Gotsumon) | Igual; al cambiar de mapa la burbuja se va (en el original el objeto sigue, pero todos los guiones la quitan antes) |
| El transporte en pájaro (rutina 10): saludo, cuadro de dinero y lista de destinos con precio, «Are you sure?», sí o no del jugador; el sí cobra, escribe mapa y entrada en 247 y 248 y el guion sigue en MAPHEAD 0x4e3; el no o cancelar dan la despedida. Medido en el original (rutina forzada por RAM en el mostrador de room02 con los seis disparadores y 1800 bits): las tres primeras filas en blanco, las tres últimas en rojo; confirmar una roja no hace nada; el sí a Ancient Dino Region deja 300 bits, 247 = 0x4f, 248 = 9 y lleva a koda00 | `shop/script_menu.c: tickBirdraTransport`, `createBirdraTransportMenu`, `tickBirdraTransportMenu`, `birdraFillTargetList`, `script_common.c: calculateBirdramonMenuStrings` | `BirdTransport`, `story_routines/routine_bird_transport.cpp`, `ScreenTask` con `TaskJump`; `apps/game`: `BirdTransportScreen` | `bird_transport_tests.cpp`, `bird_cursor_tests.cpp`; ruta en room12 (`_work/routes-e2/bird-transport/`: lista, sí y llegada a koda00) | Igual; la frase del cuadro de dinero es la del primer tendero (1220), porque el pájaro no está en la tabla de tenderos |
| El fundido a blanco y vuelta (rutinas 45 y 46): la variable 254 da los fotogramas; sigue la sección en el mismo fotograma. Medido en el original (twna01, el objeto 4005 puesto a mano): `fadeToWhite(40)` sube 3 por fotograma desde el tono que dejó el último fundido (`FADE_PROGRESS` 20 → 50 en diez fotogramas) y se queda en 160 (pantalla blanca, salvo lo dibujado delante: el reloj, los cuadros de texto); `fadeFromWhite(40)` dibuja 157 … 40 en 40 fotogramas y al 41.º se quita de golpe, dejando `FADE_PROGRESS` en 40 | `script_interp.c` (`call_routine` 0x2d, 0x2e), `fade.c: fadeToWhite`, `fadeFromWhite`, `renderFadeOut`, `renderFadeIn`, `renderFade` (`FADE_MODE` 1, suma) | `story_routines/routine_white_fade.cpp` (`WhiteFadeStarted`); `game/field`: `WhiteFade`; `apps/game`: `FieldStoryProps`, `render::ScreenFade::setWhiteness` | `routines 45 and 46 fade to white ...` (`story_props_tests.cpp`), `white_fade_tests.cpp`; ruta en ogre03 (`_work/routes-e2/white-fade/`) | Igual; el blanco se dibuja detrás de toda la UI (en el original el HUD de abajo también se blanquea) |
| `spawn_sprite_at_entity`: el sprite del mapa se centra en el punto del nodo del personaje | `overworld.c: spawnSpriteAtEntity`, `spawnSpriteAtLocation` | `SpriteAtCharacter`; `apps/game`: `FieldStoryProps::placeSprite` | `the boulder's orders: ...` (los operandos) | Igual; las trampas de trop06 sin ver en una ruta (E2-03) |
| Prosperidad de la ciudad: al empezar el bucle (partida nueva o carga) y antes de cada guardado del jugador, la variable 1 se rehace con los disparadores de reclutamiento 200 + especie (3 a 0x3a, nivel 3 o más): cada una suma su nivel - 2, Numemon, Sukamon y Nanimon suman 1; sin copa inscrita (variable 3 ≥ 0x17) se borran los disparadores 0x25 a 0x27. Medido en el original (e1-cp37): solo Agumon reclutado (disparador 203) y variable 1 = 1, lo que da la fórmula | `main.c: recalculatePPandArena` (llamada en `main` y `writeSavegame`) | `CityAdapter` (`city.yaml`), `game/story: recalculateProsperity`; ganchos `SessionSystem::beforeSave/afterLoad` | `a save of the player's rebuilds the prosperity ...`, `a load rebuilds the prosperity ...` (`city_medals_tests.cpp`) | Igual; el autoguardado (nuevo) no la rehace, la carga sí |
| Pantalla de Ciudad Archivo donde sigue una partida guardada tras el final (0xa8 a 0xb3 según los disparadores 0xdc+0xd6, 0xdd, 0xe1, 0xf6) | `map.c: getFileCityTopMap` (`main_menu.c: gameClearSave`) | `CityAdapter::clearedMap`, `cleared_map` de `city.yaml` | `a finished game continues on the File City screen ...` | Hueco: el guardado tras el final (rutina 0x36) aún no existe, así que nada la llama |
| Medallas del campo: cada fotograma que el jugador anda libre se dan las que faltan y cumplen (todas las técnicas 0-56 → 5, estadísticas 9999/999 → 7, dinero 999999 → 13, año 10 → 14, criadas las especies 1-61 → 6 y +1 nivel de domador sin aviso, 100 peces → 9, las 66 cartas → 12) como disparador 0x16c + medalla, y dejan el aviso pendiente. Medido en el original: con 999999 bits en twna01 se pone el disparador 377 (0x16c + 13) y `MEDAL_AWARD_PENDING` = 1 | `tamer.c: checkMedalConditions`, `script_common.c: unlockMedal`, `hasMedal` | `MedalsAdapter` (`medals.yaml`), `game/story: checkMedalConditions` | `the field's medal check gives a medal once ...`, `each condition met gives its medal once ...` | Igual |
| Aviso de nivel de domador: `addTamerLevel` lo deja pendiente (sube o baja) solo si el nivel nuevo, antes de recortarlo, sigue entre 0 y 10 | `tamer.c: addTamerLevel` (`TAMER_LEVEL_AWARD_PENDING`, `TAMER_LEVELS_AWARDED`) | `GameSession::tamerLevelAward` | `a tamer level change is announced only when it stays within the limits` | Igual; no se guarda, como en el original |
| La máquina de guardado registra al compañero para el modo versus: 40 registros, la máquina llega a 39; guarda nombre, especie, PV y PM máximos, ataque, defensa, velocidad, cerebro, disciplina y las tres primeras técnicas equipadas | `main_menu.c: registerBattleData`, `tickMainMenu` 0x3D (`MENU_HIGHLIGHTS[15]`), `awardMachinedramonData` | `BattleRegistry` (`battle_registry.hpp`), `registeredPartnerOf`; sección `battle_registry` | `a partner registered at the save machine goes in the save and comes back`, `the registry has the original's 40 slots, the machine reaching the first 39` | Igual; el modo versus, fase 5 |
| Tienda de cartas: seis cartas del día sorteadas en la partida nueva y cada día nuevo (1 a 64 sin la 4; pueden repetirse); la vendida queda en 0xFF y sin ninguna la tienda dice «sold out» | `script_common.c: dailyPStatTrigger`, `shop/script_menu.c: hasAnyCardToBuy` | `game/inventory: rollCardStock`, `CardShop` | `a new game rolls the card shop's stock, and so does each new day` | Igual |
| Comprar una carta: precio por rareza; medido con 3000 bits: 3000 → 2900, la ranura a 0xFF, copias 2 → 3 y el menú se cierra | `script_common.c: tickSingleCardShop` | `CardShop::buy` | `the card shop sells one card of the stock for its price` | Igual |
| Vender cartas: la mitad del precio por copia; medido: 2 copias a 2500 → +5000, copias 3 → 1 | `script_common.c: tickItemMenuAmountBox` | `CardShop::sell` | `the card shop buys copies back for half their price` | Igual |
| Sobre de cartas: rareza `randomLimit(100)` (0 → 0, <5 → 1, <20 → 2, <50 → 3, si no 4) y luego `randomLimit` entre las de esa rareza; medido: con el estado 0xCF0A481B sale la carta 0x3D y con 0x0589E791 la 0x40 | `shop/script_menu.c: rollCard`, `rollCardPack` | `CardCollection::drawPackCard` | `a pack draws the cards the original drew from the same generator state` | Igual |
| La frase del sobre nombra la especie de la carta (variable 249), sin espacio antes de «that» como el original | `shop/script_menu.c: showCardTextbox` | `CardsAdapter::drawPackCard` | `a pack's card puts its species in variable 249 ...` | Igual |
| Sonidos: fallo (0, 0xB) al elegir una fila vacía o que no se puede comprar; trato (0, 0x17) solo en gias06b | `script_common.c: createSingleCardShopMenu`, `playShopSoundOnlyInSavannah` | `CardShopRules::failSound`, `tradeSound` | `the card shop's rules carry the fail sound ...` | Igual |
| Intercambio por méritos: dar una carta suma su `meritValue` (tope 9999) y quita una copia; tomar un objeto resta su precio y mete uno en la bolsa; medido en dwe con 150 méritos: carta 2 (100) → 250, copias 1 → 0; Sup.recovery (20) → 230 | `shop/script_menu.c: openMeritShop` (estados 8 y 0xc), `createMeritCardTradeDialogue` | `MeritShop::giveCard`, `takeItem` | `the merit exchange takes a card for its merit…`, `…gives an item…`, ruta `merit-shop` | Igual |
| Cancelar en las opciones contesta la última salvo con el disparador 49 (entonces suena (0, 0xB)) | `shop/script_menu.c: openMeritShop` (estado 3), `tickSelectionDialogue` | `MeritShop::choiceCancellable` | `the merit exchange's rules carry…` | Igual |
| Rutina 15 (la caja del dinero) y rutina 22 de la tienda | `shop/script_menu.c` | — | — | **Hueco** |

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
