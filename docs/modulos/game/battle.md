# battle

Las reglas del combate (`dwe::game::battle`, capa `game`) como una simulación determinista y sin pantalla: el bucle
a 20 fotogramas por segundo, las órdenes, la obediencia, la IA del partner y de los enemigos, las técnicas, el daño, el
bloqueo y el contraataque, los estados alterados, el remate, el final del combate y sus consecuencias, las
recompensas, el aprendizaje de técnicas y los enemigos que se unen. Las reglas están en la
[ficha de referencia](../../reference/battle.md).

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

- **Posiciones:** todas las del combate (`Point`) van en ejes del mundo, los del campo (`assets::toWorldPosition`).
  El paso de datos a mundo se hace una sola vez, en la frontera entre campo y combate (`mapBattleRequest`): cada
  enemigo empieza donde está su NPC en el campo (`enemyFromMap`), el tamer y el partner donde están ellos, y lo que
  suelta un enemigo vencido cae donde estaba al final.
- **Entradas del tamer:** `command`, `pressFinisherCharge` y la bolsa: `openBag`, `closeBag` y `throwItem`, que lanza
  un `ThrownItem` (`items.hpp`: curación, revivir, curar estados, protección y discos) que llega al compañero tras los
  fotogramas de `battle.item_throw`. La sesión construye el `ThrownItem` a partir del efecto del objeto del inventario
  (`BattleAdapter::throwItem`). Quedan los efectos directos (`restorePartner`, `curePartnerStatus`, `protectPartner`,
  `revivePartner`) para las herramientas. Sus eventos salen en el siguiente `step`.
- **Eventos:** `BattleEvent` con `EventKind`, el luchador, el otro, la técnica, una cantidad, el estado y la orden.
  Los eventos de un `step` valen hasta la siguiente llamada.
- **Textos:** solo claves de localización (`text_keys.hpp`: `battle.command.<orden>`, resultado, estados, objeto
  conseguido, técnica aprendida, lesión, bits, cuenta atrás).
- **Lo que hace la sesión:** aplicar `BattleResult` (stats con sus topes, cuidado, comida, vidas, pérdida de objetos,
  minutos, hora cruzada), guardar, autoguardar según `kind` y `recruitJoined`, y registrar `checkBattleSection` en
  las comprobaciones de secciones de la app.

Para medir el balance sin escribir código: `dwe-sim --scenario battle --param partner=<especie> --param
enemies=<especie>` ([Simulaciones](../../guides/simulaciones.md)).

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
| Cuidado tras el combate | `tamer.c: startBattle`, `partner_impl.c: handlePostBattleTiredness`, `tamer.c: advanceBattleTime` | `settleBattle`, `addPostBattleTiredness` (dueña de los números: `battle.consequences`; la pérdida de objetos es de `inventory`) | a battle plays to a victory; fleeing costs happiness and discipline and tires more | Igual |
| HP y MP tras perder | guion del mapa de la derrota (script 212, sección 0x48); `murd.c: MURD_tick`; `script_value.c: enforceStatsLimits` | `settleBattle` con `consequences.defeat_restore` | a lost battle gives the partner back the section's HP and MP | Igual en efecto: lo hace la batalla, no un guion (decisión pendiente: moverlo al motor de guiones en la fase 4) |
| Ganancia de stats y objetos | `battle_ui.c: battleStatsGainsAndDrops` | `settleBattle` | the stat gains; no drops | Igual |
| Lesión | `battle_ui.c: handleBattleEndBox`, `handleBattleInjury` | `settleBattle` | the injury roll | Igual |
| Aprender técnicas | `battle_ui.c: battleMoveLearning` | `settleBattle`, `MoveCatalog::learnedWith` | the partner learns a seen technique | Igual |
| Enemigos que se unen | `btl.c: handleBattleStart` | `rollJoiningEnemies` | enemies on screen join | Igual |
| Restauración de la vida perdida por enfermedad tras una derrota | `tamer.c: tamerTickBattleLostLife`, `tamerTickSicknessLostLife`; `murd/murd.c: MURD_tick` y el guion del mapa de la derrota | — (la restauración de la derrota sigue en `battle.consequences.defeat_restore` hasta los guiones de la fase 4) | — | Hueco |
| Cuadro final: espera de 100 fotogramas, cada stat sube 1 punto por fotograma con su flecha, luego los bits ganados bajan mientras el saldo sube, las líneas (objeto soltado, lesión) se escriben y esperan 60 fotogramas o una pulsación; se cierra solo, sin botón | `battle_ui.c: handleBattleEndBox`, `renderPostBattleStatsBox` | `ui/battle/logic.lua` (fase `end`) con `hud.end_count_delay_frames` y `hud.end_wait_frames` del ruleset; los objetos soltados quedan en el suelo al cerrarse (`item.c: spawnDroppedItems`) | `route-battle-*`, `route-etapa1` | Igual salvo el sonido de la cuenta (0x16), que falta |
| Orden aprendida y bonus de MP en el cuadro final | `battle_ui.c: handleBattleEndBox` | — | — | Hueco |
| Bolsa del domador en combate: su propio botón, no con huida, sin domador ni con otro objeto en el aire; el menú de órdenes espera mientras está abierta y el combate sigue | `battle_tick.c: tamerTickBattle`, `handleCommands`; `inventory.c: addInventoryUI`; `BTL_battleMain` | `Battle::bagAvailable`, `openBag`, `closeBag`, `command` (`BagOpen`) | the tamer's bag opens only with the tamer... | Igual |
| Qué se puede lanzar: `item_color` 1 o 2 | `inventory.c: selectOptionMenu` | `inventory::useItem` con `UseContext::Battle` (sesión) | an item thrown in battle is consumed at once... (session) | Igual |
| Lanzamiento: se gasta al lanzarlo, sale en el fotograma 7 de la animación, vuela 24 fotogramas, siempre llega, uno cada vez, cierra la bolsa | `main.c: startThrowingItem`, `tickThrownItem` | `throwItem`, `tickThrownItem` (`battle.item_throw`) | a thrown item closes the bag and lands...; the throw's frames come from the ruleset | Suposición (los fotogramas de la animación del domador van a un fotograma de combate cada uno); pendiente de comprobar con el sistema de animación (2026-10-21) |
| Elegir un objeto de la bolsa en combate | `inventory.c: selectOptionMenu` (caja de opciones antes de lanzar) | `ui/battle`: confirmar una ranura envía `battle_throw` | `route-battle-*` | Desviación aprobada (2026-10-21): lanza al confirmar |
| Título de la caja final | `battle_ui.c: handleBattleEndBox` (sin título) | `outcomeTextKey` (`battle.outcome.*`) | `route-battle-*` | Desviación aprobada (2026-10-21): accesibilidad |
| Nombre del enemigo | `battle_main.c: BTL_renderEnemyHPBars` (barra sobre el enemigo, sin nombre) | `battle.fighter.<i>.name` en el panel de HP | — | Desviación aprobada (2026-10-21) hasta la escena 3D del combate |
| Efecto al llegar: curación hasta el máximo, revivir, curar, proteger, discos hasta el 130 % del valor inicial (el omnidisco sin ese límite); con 0 HP solo funcionan los de revivir | `item.c: ITEM_FUNCTIONS`, `handleRestore`; `main.c: handleBuffDisks`; `battle_main.c: BTL_healStatusEffect` | `land`, `boost` | a thrown item heals up to the maximums... | Igual |
| Cuenta atrás parada mientras vuela un objeto | `battle_setup.c: BTL_isBattleFinished` | `Battle::step` | the defeat countdown waits while a thrown item flies... | Igual |
| Barra de HP de los enemigos: solo HP, tras recibir daño, se oculta 21 fotogramas después de acabar el retroceso | `battle_main.c: BTL_renderEnemyHPBars` | `tickHpBars` (`battle.hud.enemy_hp_bar_frames`), `hpBarShown` | an enemy's HP bar shows after damage... | Igual |
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

### Decisiones del responsable de diseño (2026-10-21)

- **Cómo empieza un combate:** el original solo los empieza desde los guiones de los NPC (`script_interp.c`: la
  operación `START_BATTLE`); no hay encuentros al azar. Hasta que corran los guiones de los mapas (fase 4) solo existe
  el disparador de depuración `battle("")` de la consola y de los autotests (`apps/game/src/battle_trigger.cpp`).
- **Nombre del enemigo en su panel de HP:** el original no lo escribe; sus barras de HP van sobre los enemigos de la
  escena 3D (`battle_main.c: BTL_renderEnemyHPBars`). El panel lo enseña hasta que exista la escena 3D del combate.
- **«Has escapado.» (`battle.outcome.fled`):** el original no pone título a la caja final
  (`battle_ui.c: handleBattleEndBox`). La pantalla dice cómo acabó el combate (victoria, huida o derrota) como
  desviación de accesibilidad documentada, y se queda.
- **La bolsa lanza al confirmar:** en el original, elegir un objeto de la bolsa abre su caja de opciones
  (`inventory.c: selectOptionMenu`) antes de lanzarlo; en la pantalla de combate confirmar una ranura lo lanza
  directamente (`battle_throw`). Desviación documentada.
- **Fotograma 7 del lanzamiento (pendiente):** que el objeto salga en el fotograma 7 de la animación del domador
  (`main.c: startThrowingItem`) sigue como suposición; se comprueba cuando llegue el sistema de animación del combate.

## Tests

`dwe_battle_tests`: una prueba por regla, un valor del ruleset cambiado (el tope de daño), una estrategia cambiada
(un daño de prueba), la repetición con semilla y `runBattles`. Las pruebas lentas, "the user's disc, extracted: ...",
leen `DWE_CONTENT`: el catálogo con todas las técnicas del disco y un combate por cada pareja de especies (con paso 7).
