# partner

El cuidado del partner (`dwe::game::partner`, capa `game`): hambre, sueño, baño, enfermedad y heridas, felicidad y
disciplina, cansancio, vida, edad, muerte y renacer, y el entrenamiento. Lo que hacía el original está en la
[ficha de cuidado](../../docs/reference/care.md); este módulo lo implementa con el ruleset `original` y dice abajo,
regla a regla, dónde coincide y dónde no.

No dibuja, no guarda partidas, no lanza guiones ni conoce el inventario: la sesión le da el reloj, las órdenes como
structs de efecto y lo que necesita del resto del juego (`CareContext`, `SleepContext`), y lee el estado y los
eventos. Depende solo de `core`, `assets` y `ruleset`.

## Uso

```cpp
auto ruleset = dwe::ruleset::loadRuleset(vfs, "original", dwe::game::partner::partnerSectionChecks());
auto rules = dwe::game::partner::loadPartnerRules(*ruleset);
auto species = dwe::game::partner::loadSpeciesTable(vfs, *ruleset);   // tablas digimon y raise
dwe::game::partner::Partner partner{*rules, *species, seed};
partner.start("agu", stats, clock);

// Cada tick: el reloj avanza y el partner pasa por cada fotograma cruzado.
const auto advance = clock.advanceFrames(frames);
partner.tick(clock, advance, {.tamerIdle = idle, .area = area});
for (const auto& event : partner.takeEvents()) { /* UI, guardado, guiones */ }

// Órdenes del tamer.
if (!partner.refuses({.item = id, .check = RefusalCheck::FavoriteOnly})) {
    partner.eat(foodEffectOf(id), clock);
}
partner.scold();
partner.praise();
if (partner.canSleep()) {
    partner.sleep(clock, {.hasPillow = pillow, .area = area});   // mueve el reloj a la hora de despertar
}
partner.useToilet();
partner.cure({.injuryPercent = 100, .sicknessPercent = 20});    // venda
partner.skipHours(hours, clock);                                 // descansar; la sesión mueve el reloj después
partner.finishBattle({.result = BattleResult::Won, .tookDamage = hit}, clock);   // +20 minutos
partner.applyBattle(consecuencias, clock);  // la batalla ya lo calculó (BattleAdapter): sin las reglas de finishBattle
auto gains = partner.train({.place = "trn", .course = "hp"}, clock);           // +1 hora
partner.addStatGains(evolutionGains);                                           // cada ganancia, 999 como mucho
```

- **Tiempo.** `tick` recorre los fotogramas que cruzó el reloj y aplica cada regla en el fotograma del día en que la
  aplica el original (`frameOfDay % N == 0`), así que avanzar un día de golpe es lo mismo que jugarlo (hay un test).
  Dormir, combatir y entrenar mueven el reloj con sus propias cuentas, como el original, y no recorren los
  fotogramas que saltan.
- **Órdenes.** Comer (`FoodEffect`), curar (`CureEffect`), regañar, elogiar, dormir, baño y orinal portátil, comer
  caca (`eatPoop`, para las especies que lo hacen), descansar (`skipHours`), combate (`BattleOutcome`) y
  entrenamiento (`TrainingRequest`). El inventario es dueño de la tabla de objetos; la sesión traduce un objeto a su
  efecto. Las ganancias de stats que vienen de fuera (evolución, combate) van por `addStatGains`, que aplica el tope de
  cada ganancia (`care.limits.stat_gain_max`) y el de cada stat; `setStats` pone los stats tal cual, sin tope (el HP
  y el MP tras un combate). La vida que dan o quitan los objetos va por `addLifetime`.
- **Eventos** (`EventKind`, `takeEvents`; `pendingEvents` los mira sin sacarlos): necesidades que empiezan, ventana de hambre cerrada, error de cuidado (con su causa),
  caca en el suelo (con su tamaño, para `PoopPiles`), rechazo, `Slept` (al despertar) y `NewDay` (cada medianoche
  cruzada jugando, durmiendo, combatiendo o entrenando), entrenado, vida perdida, muerte, renacer, cambio del nivel
  del tamer (ya tirado; la sesión lo limita a 0..10), barra de virus llena y regañina en el peor momento.
- **Tope de las ganancias.** Toda ganancia de un stat se queda en 999 como mucho (desviación aprobada,
  2026-10-20): el entrenamiento, las comidas y chips (`FoodEffect::permanentGain`), la herencia al renacer y
  `addStatGains` pasan por la misma función. HP y MP siguen llegando hasta 9999 sumando ganancias. Se aceptan sus dos
  efectos (decidido por el responsable de diseño, 2026-10-20): un objeto de +1000 da +999, y la media con los stats de
  referencia de una evolución sube 999 como mucho. La vuelta de la forma virus no pasa por aquí: `setStats` y después
  el tope de cada stat (ver abajo).
- **Muerte y renacer.** El partner emite `Died` (valor 1 si fue natural); la sesión juega la muerte y llama a
  `rebirth(clock, nivelDelTamer)`.
- **Evolución.** El partner lleva los contadores (`EvolutionCounters`) y avisa de la barra de virus; el módulo de
  evolución decide y la sesión aplica el resultado con `changeSpecies`, `addStatGains` o `setStats`. La copia de la
  forma virus (`VirusBackup`) guarda la forma y los stats de antes, como el original guarda la forma en la variable de
  guion `PSTAT_SUKAMON_BACKUP_DIGIMON` y los stats en `sukaBackup*` (decidido por el responsable de diseño,
  2026-10-20: la forma va con el partner). Al volver de la forma virus la sesión pone los stats reconstruidos con
  `setStats` y los deja en el tope de cada stat de `care.limits` (9999 HP y MP, 999 el resto), sin el tope por
  ganancia (`apps/session`: `EvolutionAdapter`).
- **Cacas.** `PoopPiles` guarda las cacas del mundo como el original: delante del partner, a un paso al azar si la
  casilla está ocupada, 16 por mapa y 100 en total, reutilizando las más antiguas.
- **Guardado.** `toYaml`/`fromYaml` con `PartnerStateVersion` (`state_yaml.hpp`); la sesión los envuelve en un
  `SectionSerializer` con sus migraciones. La versión 2 añade la forma a la copia de la forma virus:
  `addVirusBackupSpecies` sube una partida de la versión 1, que no la tenía, con la forma vacía. `PoopPiles::slots()`/`restore()` para las cacas.

## Datos

- `rules/original/care.yaml` ([esquema](../../content/schemas/ruleset_care.schema.json)): todos los números del
  cuidado, cada uno con la función del decomp de la que sale, los nombres de las estrategias y las `quirks`.
- `rules/original/life.yaml` ([esquema](../../content/schemas/ruleset_life.schema.json)): vida, vidas, infelicidad y
  renacer (bebés, stats fijos y herencia).
- `rules/original/training.yaml` ([esquema](../../content/schemas/ruleset_training.schema.json)): aptitudes, niveles,
  bonus, lugares y cursos.
- Tablas del disco del usuario, por rol del ruleset: `digimon` (nivel) y `raise` (horario de comida, energía, baño,
  comida favorita, patrón de sueño, aptitud y peso), leídas con `assets::loadDigimonTable` y `loadRaiseTable`.

Las tablas pequeñas que en el original están en el código (`SLEEP_PATTERN`, los stats de los bebés al renacer) son
constantes de reglas en los YAML, con su cita.

## Estrategias

La lógica que se puede rediseñar va detrás de `StrategyRegistry` (ver
[rulesets](../../docs/architecture/rulesets.md)); `original` es la del juego:

| Ruta | Interfaz | Qué decide |
|---|---|---|
| `care.strategies.care_mistakes` | `CareMistakeRule` | Si una necesidad desatendida cuenta como error de cuidado |
| `care.strategies.sickness_roll` | `SicknessRoll` | Si las horas sin dormir le enferman esta hora |
| `care.strategies.item_refusal` | `ItemRefusalRule` | Si rechaza un objeto |
| `life.rebirth.baby_choice` | `RebirthChoice` | En qué bebé renace |
| `training.gain_formula` | `TrainingGainFormula` | Cuánto da una sesión |

**El renacer es una estrategia** porque es la regla que más probablemente cambie un rediseño: el original elige un
bebé al azar (`rand() % 4` en `dooa.c`) y un mod puede querer que dependa de la especie muerta, de los errores o de
una elección del jugador. La herencia de stats (un % por nivel del tamer, solo con muerte natural) son números en
`life.yaml`; la elección del huevo es lógica y por eso va detrás de una interfaz que recibe el partner muerto.

## Rarezas

Cada rareza de la ficha que el juego nota va detrás de una bandera de `quirks:` (todas a `true` en `original`):

| Bandera | Rareza |
|---|---|
| `sleeping_hungry_keeps_weight` | Dormir con hambre no baja el peso |
| `sleep_adds_default_hours_to_evolution` | Dormir suma las horas de sueño del patrón al contador de evolución |
| `sickness_chance_wraps` | La probabilidad de enfermar vive en un byte y un negativo da casi el 100 % |
| `young_sleep_without_variation` | El horario del bebé no varía (`randomLimit(1)`) |
| `battle_midnight_skips_age` | Un combate que cruza la medianoche no suma edad |
| `toilet_doubles_poop_interval` | Tras el baño o una caca en el suelo el aviso tarda el doble |
| `post_battle_mp_tiredness_truncated` | El cansancio por MP divide antes de multiplicar |
| `food_clears_training_boost` | Cualquier comida borra el bonus de entrenamiento |
| `skipped_hunger_window_counts_twice` | Saltar horas cuenta el error de la ventana de hambre dos veces |
| `skipped_hours_add_sickness_tries` | Saltar horas enfermo suma intentos de enfermar |
| `skipped_hours_unhappiness_only_at_midnight` | Saltar horas solo resta vida por infelicidad a medianoche y con grupos de 4 horas; `false` la resta por cada múltiplo de 4 horas cruzado |
| `training.quirks.brain_training_uses_mp_aptitude` | La inteligencia usa el multiplicador del MP |

Dos rarezas no tienen bandera: el contador muerto `subTiredness`, porque nada lo sube, y el año de 31 días del
entrenamiento, que se queda como hueco conocido sin bandera (decisión del responsable de diseño, 2026-10-20; ver la
tabla).

## Paridad con el original

Igual: hace lo mismo que el decomp. Desviación aprobada: distinto a propósito. Hueco: falta.

| Regla | Decomp | Nuestro | Test | Estado |
|---|---|---|---|---|
| Hora: evolución +1, vida −1; medianoche: edad +1 | `clock.c: tickGameClock` | `Partner::stepClock`, `startDay` | `life_tests`: spends an hour, the day turns | Igual |
| Año: nivel del tamer ±1 | `clock.c: tickGameClock` | `startDay`, `care.year` | `mood_tests`: a new year | Igual |
| Orden de las reglas por fotograma | `partner_impl.c: tickConditions` | `Partner::stepCare` | `simulation_tests`: three days at once | Igual |
| Horario de comida | `partner_impl.c: setFoodTimer` | `setFoodTimer`, `nextAdultMealHour` | `hunger_tests`: meal hours, baby every 2 | Igual |
| Ventana de hambre y error de cuidado | `partner_impl.c: tickHungerMechanics` | `tickHunger`, `closeHungerWindow` | `hunger_tests`: unfed window | Igual |
| Energía y peso sin energía | `partner_impl.c: tickHungerMechanics` | `tickHunger` | `hunger_tests`: energy falls | Igual |
| Cansancio adelanta el hambre | `partner_impl.c: tickTirednessMechanics` | `tickTiredness`, `tickHunger` | `hunger_tests`: long tiredness | Igual |
| Comer: alimentado, sobrealimentado, favorita, tope | `partner_impl.c: partnerHandleFoodFeed`; `item.c: handleFood` | `eat`, `applyFood`, `answerHunger` | `hunger_tests` | Igual |
| Rechazo de objetos | `partner_impl.c: partnerWillRefuseItem` | `OriginalItemRefusal` | `hunger_tests`, `mood_tests` | Igual |
| Horario de sueño | `partner_impl.c: setSleepTimes` | `setSleepTimes`, `scheduleNextSleep` | `sleep_tests`: pattern, baby | Igual |
| El cansancio adelanta el sueño | `partner_impl.c: tickSleepMechanics` | `tickSleep`, `cutAwakeTime` | `sleep_tests`: cut of 10 minutes | Igual |
| Penalización con sueño, sueño perdido | `partner_impl.c: tickSleepMechanics` | `tickSleep`, `missSleep` | `sleep_tests`: sleepy adult, misses its sleep | Igual |
| Dormir y recuperación | `partner_impl.c: handleSleeping`, `sleepRegen` | `sleep`, `regenerateWhileAsleep`, `wakeUp` | `sleep_tests` | Igual |
| Hora protegida | `partner_impl.c: setImmortalHour`, `tickDeathCondition` | `inProtectedHour` | `life_tests`: death waits | Igual |
| Baño: aviso, margen, en el suelo | `partner_impl.c: tickPoopingMechanic`, `handleWildPoop` | `tickPoop`, `poopOnGround` | `toilet_tests` | Igual |
| Baño y orinal portátil | `partner_impl.c: handleToilet`; `item.c: handlePortaPotty` | `useToilet`, `usePortableToilet` | `toilet_tests` | Igual |
| Peso tras la caca | `partner_impl.c: handlePoopWeightLoss` | `loseWeightAfterPoop` | `toilet_tests`: the toilet gives | Igual |
| Cacas en el mapa | `partner_impl.c: createPoopPile` | `PoopPiles` | `toilet_tests`: piles | Igual |
| Comer caca | `partner_impl.c: handleEatingPoop` | `eatPoop` | `toilet_tests`, `health_tests` | Igual |
| Barra de virus | `partner_impl.c: tickConditions` | `tickVirusBar` | `toilet_tests`: sixteenth pile | Igual |
| Copia de la forma virus: forma y stats | `partner_impl.c: tickConditions` (`sukaBackup*`); variable de guion `PSTAT_SUKAMON_BACKUP_DIGIMON` | `PartnerState::virusBackup` (`VirusBackup`), versión 2 del estado | `toilet_tests`: sixteenth pile; `state_yaml_tests`: version 1 | Igual (2026-10-20): la forma se guarda con el partner y no en una variable de guion; las partidas de la versión 1 la cargan vacía |
| Enfermar por no dormir, por zona, por herida | `partner_impl.c: tickSicknessMechanics` | `tickSickness`, `OriginalSicknessRoll` | `health_tests` | Igual |
| Enfermedad sin tratar: vida | `partner_impl.c: tickSicknessMechanics` | `tickSickness`, `loseLife` | `health_tests`: twelve hours sick | Igual |
| Comida que enferma | `item.c: handleItemSickness` | `rollFoodSickness` | `health_tests`: a food | Igual |
| Venda y medicina | `item.c: handleStatusItems`, `handleMedicineHealing` | `cure` | `health_tests`: cures | Igual |
| Regañar y elogiar | `partner_impl.c: handlePraiseScold` | `scold`, `praise` | `mood_tests` | Igual |
| Límites | `partner_impl.c: tickConditionBoundaries` | `clampBoundaries` | `mood_tests`: within -100 and 100 | Igual |
| Enfado | `partner_impl.c: tickUnhappinessMechanics` | `tickSulking` | `mood_tests`: sulking | Igual |
| Zona que gusta o no | `partner_impl.c: tickTirednessMechanics` | `tickTiredness` | `mood_tests`: liked area | Igual |
| Cansado | `partner_impl.c: tickTirednessMechanics` | `tickTiredness` | `mood_tests`: at 80 | Igual |
| Vida por infelicidad | `partner_impl.c: tickConditions` | `tickUnhappyLifetime`, `loseUnhappyLifetime` | `life_tests`: unhappy | Igual |
| Muerte natural | `partner_impl.c: tickDeathCondition` | `tickDeath` | `life_tests`: natural death | Igual |
| Renacer y herencia | `evolution.c: reincarnatePartner`; `partner_impl.c: initializeReincarnatedPartner`; `dooa.c` | `rebirth`, `OriginalRebirthChoice` | `life_tests`: rebirth | Igual |
| Combate: ánimo, cansancio, tiempo | `tamer.c: startBattle`, `advanceBattleTime`; `partner_impl.c: handlePostBattleTiredness` | `finishBattle`, `passBattleTime` | `activity_tests` | Igual |
| Combate que ya resolvió `game/battle`: sus números y la cuenta horaria si acaba una hora | `tamer.c: startBattle`, `advanceBattleTime` | `applyBattle` (`BattleConsequences`), `passBattleTime` | `a battle the battle system settled ...`, `a settled battle that ends an hour ...` | Igual |
| Herida en combate | `battle_ui.c: handleBattleInjury` | `rollBattleInjury` | `activity_tests`: hurt partner | Igual |
| Saltar horas | `partner_impl.c: skipHours` | `skipHours` | `activity_tests`: rest | Igual |
| Infelicidad al saltar horas | `partner_impl.c: skipHours` | `unhappyChecksWhileSkipping`, `care.quirks.skipped_hours_unhappiness_only_at_midnight` | `activity_tests`: rest, only at midnight | Igual (2026-10-20): la rareza queda por defecto; `false` resta por cada múltiplo de 4 horas cruzado |
| Multiplicador de entrenamiento | `trn_reward.c: TRN_calculateTrainingMultiplier` | `OriginalGainFormula` | `training_tests` | Igual |
| Ganancias y cansado del todo | `trn_reward.c: TRN_award*TrainingGains` | `OriginalGainFormula` | `training_tests` | Igual |
| Tope de cada ganancia: 999 | `trn_reward.c: TRN_award*TrainingGains` (solo la primera ganancia de la sesión); sin tope por ganancia en `item.c: handleFood`, `evl.c: EVL_applyEvolution` y `partner_impl.c: setReincarnateStats` | `addStatGains`, `care.limits.stat_gain_max` | `stat_gain_tests` | Desviación aprobada (2026-10-20): toda ganancia se queda en 999, también la de un objeto (un objeto de +1000 HP da +999), la media de la evolución (sube 999 como mucho) y la herencia; los dos efectos se aceptan |
| Tope de cada stat: 9999 y 999 | `item.c: addWithLimit`; `evl.c: EVL_clampBaseStats` | `addStatGains`, `care.limits` | `training_tests`: stop at 9999 | Igual |
| Vuelta de la forma virus: stats reconstruidos | `partner.c: callDigimonRoutine` (2); `evl.c: EVL_scaleBaseStats`, `EVL_clampBaseStats` | `setStats` y el tope de `care.limits` en `apps/session` (`EvolutionAdapter`) | `apps/session`: `back from the virus form ...` | Igual (2026-10-20): sin el tope de 999 por ganancia |
| Hora de entrenamiento | `trn_reward.c: TRN_advanceTrainingTime` | `passTrainingHour` | `training_tests`: takes an hour | Igual |
| Año de 31 días al entrenar | `trn_reward.c: TRN_advanceTrainingTime` (el día vuelve a 0 en 31; el reloj, en 30) | — (usa el reloj, 30) | — | Hueco conocido (2026-10-20): se queda así, sin bandera |
| Patrones de sueño | `partner_impl.c: SLEEP_PATTERN` | `care.sleep.patterns`, con su cita | `rules_tests`: the original's numbers | Igual (2026-10-20): constantes del ruleset |
| Stats fijos de los bebés del renacer | `partner_impl.c: initializeReincarnatedPartner`; `dooa.c: REINCARNATE_BABY_TYPE` | `life.rebirth.babies`, con su cita | `rules_tests`: the original's numbers | Igual (2026-10-20): constantes del ruleset |
| Evoluciones al despertar | `partner_impl.c: handleSleeping` | — (módulo de evolución) | — | Hueco (fuera de este módulo) |
| Técnicas tras renacer | `evolution.c: initializeReincarnatedPartner` | — (batalla) | — | Hueco (fuera de este módulo) |

## Tests

Un test por regla de la ficha, en `hunger_tests.cpp`, `sleep_tests.cpp`, `toilet_tests.cpp`, `health_tests.cpp`,
`mood_tests.cpp`, `life_tests.cpp`, `activity_tests.cpp`, `training_tests.cpp` y `stat_gain_tests.cpp` (el tope de
cada ganancia, por cada camino que sube un stat), con especies de prueba inventadas (`partner_fixture.hpp`) sobre el
ruleset `original` del paquete `game`. Además:

- `rules_tests.cpp`: los números del original, los esquemas, valores que las comprobaciones rechazan, un valor del
  ruleset cambiado (la ventana de hambre) y una estrategia cambiada (sin errores de cuidado).
- `simulation_tests.cpp`: tres días de golpe, por horas y fotograma a fotograma dan el mismo estado y los mismos
  eventos; ocho días cuidados con la misma semilla dan el mismo partner, que vuelve igual de `toYaml`/`fromYaml`.
- `state_yaml_tests.cpp`, `species_tests.cpp` (tablas `digimon` y `raise`).
- `the user's disc, extracted: ...` (lento): con `DWE_CONTENT`, todas las especies del disco cargan, sus patrones de
  sueño existen, cada novato pasa un día y los bebés del renacer son bebés.
