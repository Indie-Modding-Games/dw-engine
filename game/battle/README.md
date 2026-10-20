# battle

Las reglas del combate (`dwe::game::battle`, capa `game`) como una simulación determinista y sin pantalla: el bucle
a 20 fotogramas por segundo, las órdenes, la obediencia, la IA del partner y de los enemigos, las técnicas, el daño, el
bloqueo y el contraataque, los estados alterados, el remate, el final del combate y sus consecuencias, las
recompensas, el aprendizaje de técnicas y los enemigos que se unen. Las reglas están en la
[ficha de referencia](../../docs/reference/battle.md).

No conoce al partner ni a las partidas: no incluye `game/partner` ni `game/save`. Recibe `Combatant` (declarado aquí)
y la condición del partner, y devuelve un `BattleResult` que la sesión aplica. No tiene estado que guardar: un combate
empieza y acaba dentro de una sesión. Los números salen de la sección `battle` del ruleset y las decisiones (IA,
daño, obediencia) son estrategias con nombre.

## Uso

```cpp
namespace battle = dwe::game::battle;

// Al cargar el ruleset y las tablas.
auto rules = battle::readBattleRules(store->current());                // sección `battle`
auto moves = battle::MoveCatalog::build(moveTable, *rules);
auto strategies = battle::createStrategies(battleSection);             // partner_ai, enemy_ai, damage, obedience

// Al empezar un combate.
battle::BattleSetup setup{.partner = partnerCombatant, .condition = {...}, .enemies = {...}, ...};
setup.kind = battle::BattleKind::Arena;                                // la sesión lo lee en el resultado
setup.recruitOnVictory = "resident_id";                                 // combate de reclutamiento
auto fight = battle::Battle::start(std::move(setup), *rules, *moves, std::move(*strategies), seed);

// Cada fotograma (o advance(segundos) con el tiempo real).
fight->command(battle::Command::Attack);                               // error si no está en el menú
fight->pressFinisherCharge();
for (const battle::BattleEvent& event : fight->step()) { /* animar, sonar, mostrar */ }
if (fight->finished()) {
    const battle::BattleResult& result = *fight->result();             // la sesión aplica cuidado, bits, objetos...
}

// Enemigos que se unen tras el combate, y simulación sin pantalla.
auto joins = battle::rollJoiningEnemies(*rules, {.partnerType = ..., .inventory = ..., .scripted = false}, types, random);
battle::WinRates rates = battle::runBattles(setup, *rules, *moves, factory, {.count = 1000, .firstSeed = 1});
```

- **Entradas del tamer:** `command`, `pressFinisherCharge` y los objetos (`restorePartner`, `curePartnerStatus`,
  `protectPartner`, `revivePartner`). Sus eventos salen en el siguiente `step`.
- **Eventos:** `BattleEvent` con `EventKind`, el luchador, el otro, la técnica, una cantidad, el estado y la orden.
  Los eventos de un `step` valen hasta la siguiente llamada.
- **Textos:** solo claves de localización (`text_keys.hpp`: `battle.command.<orden>`, resultado, estados, objeto
  conseguido, técnica aprendida, lesión, bits, cuenta atrás).
- **Lo que hace la sesión:** aplicar `BattleResult` (stats con sus topes, cuidado, comida, vidas, pérdida de objetos,
  minutos, hora cruzada), guardar, autoguardar según `kind` y `recruitJoined`, y registrar `checkBattleSection` en
  las comprobaciones de secciones de la app.

Para medir el balance sin escribir código: `dwe-sim --scenario battle --param partner=<especie> --param
enemies=<especie>` ([Simulaciones](../../docs/guides/simulaciones.md)).

## Datos

- `content/packs/game/rules/original/battle.yaml`: la sección `battle` con cada número citado del decomp, las
  estrategias y las rarezas (`quirks:`). Su esquema es `content/schemas/ruleset_battle.schema.json`.
- La tabla de técnicas (`assets::MoveTable`, del disco del usuario) da potencia, MP, alcance, distancia,
  especialidad, estado y probabilidades de aprendizaje; ninguna tabla de especies, técnicas u objetos va en git.
- El reloj del combate es propio: fotograma 1 al empezar, 20 por segundo (`FramesPerSecond`); el reloj del juego se
  para y luego avanza `elapsedMinutes`.

## Paridad con el original

Estado: **Igual** (hace lo mismo que el decomp), **Suposición** (el decomp no lo deja claro; se eligió lo más
probable y está marcado en el código), **Hueco** (falta).

| Regla | Decomp | Aquí | Test | Estado |
|---|---|---|---|---|
| Preparación, penalización por enfermedad | `battle_setup.c: BTL_initializeCombat` | `Battle::start`, `applySicknessPenalty` | sick partner loses a fifth | Igual |
| Menú por inteligencia, sin huida en guion | `battle_setup.c: BTL_initializeCombat` | `buildMenu`, `menus` | the menu grows with brain | Igual |
| Órdenes y su retraso | `battle_tick.c: handleCommands` | `Battle::command`, `OriginalObedience` | commands outside the menu | Igual |
| Desobediencia | `battle_main.c: BTL_tickPartnerAI` | `OriginalObedience`, `tickDisobeyState` | a battle plays to a victory | Igual |
| Elección de objetivo y puntuación | `battle_main.c: BTL_selectFighterTarget`, `BTL_calculateTargetScore`, `BTL_getWeakestEnemy` | `targeting.cpp`, `OriginalPartnerAi` | change command; seeded replay | Igual |
| Técnica "a su aire" | `battle_main.c: BTL_selectPartnerMove`, `BTL_calculateElementBonus` | `OriginalPartnerAi` | seeded replay | Igual |
| Técnica de los enemigos | `battle_main.c: BTL_selectEnemyMove` | `OriginalEnemyAi` | disc pairwise | Igual |
| Órdenes de movimiento (huir, defensa, distancia) | `battle_main.c: BTL_handlePartnerMoveCommand` | `followMoveCommand`, `keepDistance` | fleeing ends the battle | Igual |
| Reserva de velocidad | `battle_main.c: BTL_increaseSpeedBuffer` | `ai_ticks.cpp` | the speed reserve grows | Igual |
| MP, usabilidad y redondeo | `battle_main.c: BTL_isMoveUsable`, `BTL_setupMoveExecution` | `usability.cpp`, `beginAttack` | MP check and payment | Igual |
| Estados del luchador (carga, enfriamiento, confusión, aplastado) | `battle_main.c: BTL_tickFighterStates` y sus `tick*State` | `fighter_actions.cpp` | flattening; stun and confusion | Igual |
| Ataque cuerpo a cuerpo y a distancia | `battle_main.c: BTL_tickMeleeAttack`, `BTL_tickRangedAttack`, `BTL_startQueuedMove` | `tickMeleeAttack`, `tickRangedAttack`, `startQueuedMove` | a battle plays to a victory | Igual |
| Viaje del proyectil, ataques de área | `battle_main.c` (animaciones y efectos) | `releaseAttack`, `tickAttacksInFlight` | seeded replay | Suposición |
| Acierto y bloqueo | `battle_main.c: BTL_calculateHitChance`, `BTL_tickAttackHits` | `OriginalDamage`, `handleAttack`, `block` | the hit chance | Igual |
| Daño y su goteo | `battle_main.c: BTL_calculateDamage`, `utils.c: damageTick` | `OriginalDamage`, `drainPendingDamage` | the damage formula; pending damage drains | Igual |
| Contraataque | `battle_main.c: BTL_tickAttackHits` | `tryCounter` | a counter needs 165 MP | Igual |
| Técnicas de apoyo | `battle_main.c: BTL_applyBuffMove` | `applyBuff` | support moves raise stats | Igual |
| Estados alterados | `battle_main.c: BTL_applyMoveStatus`, `BTL_tickStatusEffects`, `BTL_healStatusEffect` | `status_effects.cpp` | poison; stun and confusion; flattening | Igual |
| Remate y su barra | `battle_main.c: BTL_tickFighterAction`, `BTL_addFinisherProgress`, `command_menu.c: BTL_tickFinisherChargeup` | `beginFinisher`, `addFinisherProgress` | the finisher gauge fills | Igual |
| Victoria, huida, derrota, revivir | `battle_setup.c: BTL_isBattleFinished` | `Battle::step`, `revivePartner` | victory; flee; falls and revives | Igual |
| Cuidado tras el combate | `tamer.c: startBattle`, `partner_impl.c: handlePostBattleTiredness`, `tamer.c: advanceBattleTime` | `settleBattle` | a battle plays to a victory | Igual |
| Ganancia de stats y objetos | `battle_ui.c: battleStatsGainsAndDrops` | `settleBattle` | the stat gains; no drops | Igual |
| Lesión | `battle_ui.c: handleBattleEndBox`, `handleBattleInjury` | `settleBattle` | the injury roll | Igual |
| Aprender técnicas | `battle_ui.c: battleMoveLearning` | `settleBattle`, `MoveCatalog::learnedWith` | the partner learns a seen technique | Igual |
| Enemigos que se unen | `btl.c: handleBattleStart` | `rollJoiningEnemies` | enemies on screen join | Igual |
| Orden aprendida y bonus de MP en el cuadro final | `battle_ui.c: handleBattleEndBox` | — | — | Hueco |
| Cuenta atrás parada mientras el tamer usa un objeto | `battle_setup.c: BTL_isBattleFinished` | — | — | Hueco |
| Enemigo fuera de pantalla prudente | `battle_main.c: BTL_tickEnemyAI` | tira como los demás | — | Hueco (decidido: se hará cuando el campo dé visibilidad) |
| Tiempos de animación (golpe, bloqueo, revivir) | animaciones de cada especie | `timing` del ruleset | — | Suposición |

### Decisiones del responsable de diseño (2026-10-20)

- **Sin MP en ningún bando:** fiel al original, el combate no tiene límite de tiempo. Solo las simulaciones y los
  tests ponen un tope de fotogramas.
- **`TYPE_FACTORS`, `FLEE_CHANCES` y los factores de ganancia:** se quedan como constantes del ruleset con su cita
  del decomp; no se extraen del disco.
- **Reclutas:** decide la sesión con `recruitOnVictory` y lee `recruitJoined`; el combate no mira el mapa.
- **Contadores de cuidado por hora durante el combate:** los aplica la sesión o el compañero con `elapsedMinutes`,
  `hourCrossed` y `lifetimeHoursLost`; el combate sigue independiente.
- **Enemigo fuera de pantalla prudente:** se implementa más adelante, cuando el campo dé al combate una entrada de
  visibilidad.

## Tests

`dwe_battle_tests`: una prueba por regla, un valor del ruleset cambiado (el tope de daño), una estrategia cambiada
(un daño de prueba), la repetición con semilla y `runBattles`. Las pruebas lentas, "the user's disc, extracted: ...",
leen `DWE_CONTENT`: el catálogo con todas las técnicas del disco y un combate por cada pareja de especies (con paso 7).
