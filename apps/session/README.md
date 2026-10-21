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

Las órdenes del jugador (`feed`, `cure`, `scold`, `praise`, `useToilet`, `sleep`, `train`, `finishBattle`) reparten
también sus eventos y su autoguardado. `takePartnerEvents` da los eventos del último tick u orden para la UI, y
`takeAutosaved` el autoguardado escrito (ranura y motivo).

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
| `evolution` | Las formas ya criadas (`evolution::toYaml`, versión `EvolutionStateVersion`); la marca de objeto de evolución usado no se guarda, como en el original |

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
`afterSystemCommand()`, que reparte los eventos y escribe el autoguardado como las órdenes de la partida. Hasta que
`game/battle` tenga el suyo, `finishBattle` hace de final de batalla.

### Evolución (`EvolutionAdapter`)

`EvolutionAdapter({.files = &files})` lee la sección `evolution` y sus tablas (las del manifiesto, una vez; los tests
le dan las suyas en `.tables`) y registra su comprobación de sección y la sección de guardado `evolution`.

- **Entradas**: llena `EvolutionInputs` desde el partner: forma, stats, peso, errores de cuidado, disciplina,
  felicidad, cansancio, batallas, días, contador de evolución, barra de virus, la copia de la forma virus, el mapa
  (índice original de `data/maps`, `UnknownMapIndex` sin tabla de mapas), si el domador está libre
  (`careContext().tamerIdle`) y si corre un guion (solo en las rutinas de guion). Las técnicas aprendidas van a 0
  (**Hueco**: el partner aún no las guarda).
- **Cuándo mira**: en cada tick que cruza horas de juego, `checkNatural` con las horas cruzadas (el contador lo sube
  el partner; si no evoluciona, el adaptador deja el que devuelve). `VirusBarFull` → `fillVirusBar`; `Slept` →
  `wakeUp`; tras alabar o regañar → `praiseOrScold`; `Reborn` marca al bebé como criado
  (`evolution.c: reincarnatePartner`). Órdenes: `useItem(session, objeto)` (la llama el inventario),
  `returnFromVirus(session)` (rutina de guion 2) y `evolveByScript(session, forma)` (rutina 10).
- **Aplicar** (`evl.c: EVL_applyEvolution`): `Partner::changeSpecies` a la forma destino; `statChange` por
  `addStatGains` o, al volver de la forma virus, `restoredStats` con `setStats` y cada stat en su tope de
  `care.limits` (sin el tope por ganancia, decidido el 2026-10-20); HP y MP actuales dentro de los nuevos máximos;
  peso, errores de cuidado, batallas y contador; horas de vida extra (`addLifetime`); la probabilidad de nivel de
  domador con `tamer.evolution_level_gain`; la barra de virus a 0; la copia de la forma virus (forma y stats) en
  `PartnerState::virusBackup`; `recordEvolution` y autoguardado `Evolution`. `takeEvolutions()` da las aplicadas
  para la UI.

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
| Barra de virus llena y evolución natural en la misma hora | `partner_impl.c: tickConditions` (gana el virus) | el partner reparte `VirusBarFull` después del `tick` del adaptador: se aplican las dos, la del virus la última | — | **Hueco**: pregunta abierta |
| Técnicas aprendidas en los requisitos | `evolution.c: getNumMasteredMoves` | `EvolutionInputs::learnedMoves = 0` | — | **Hueco** |

## Tests

`tests/session_tests.cpp` (partida nueva, misma semilla, criar días a avance rápido → guardar → cargar en otra
partida → seguir igual, ranuras, autoguardado al dormir y por día nuevo según el ruleset, ajuste en marcha, muerte y
renacer, la vista del partner, nombres de ranura), `tests/field_clock_tests.cpp` (con un campo de prueba: el ritmo del
reloj de antes, el reloj parado en los fundidos a cualquier velocidad, autoguardado al cambiar de mapa y el sitio
guardado), `tests/save_migration_tests.cpp` (partidas con secciones de versiones anteriores),
`tests/evolution_adapter_tests.cpp` (con las tablas inventadas de `game/evolution/tests/fixture.hpp`: evolución por
horas y autoguardado, barra de virus, vuelta con los stats topados, objeto, guion, guardar y cargar, comprobación de
sección), `tests/system_hook_tests.cpp` (alabanza y regañina a los sistemas, nivel de domador con probabilidad) y `tests/rules_tests.cpp`
(esquemas de `new_game` y `tamer`, valores del original y comprobaciones). Usan el
paquete `game` del árbol de fuentes sobre un paquete base de sustitución con las tablas vacías, especies inventadas
(no las del original) y `MemorySaveStorage`.
