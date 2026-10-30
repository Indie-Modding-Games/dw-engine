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
| Colisión al andar (área de combate, mapa, otros luchadores) | `map.c: entityCheckCollision`, `collisionGrace`; `battle_main.c: BTL_findUnblockedRotation`, `BTL_backAwayFromTarget`, `BTL_moveTowardLocation` | `movement.cpp`, puerto `BattleGround` (`ground.hpp`), `FieldBattleGround` en la app | walls on every side; the combat area | Igual, salvo que el tamer no cuenta como obstáculo y, sin suelo (simulador, pruebas), el área es `arena_radius` |
| Reserva de velocidad | `battle_main.c: BTL_increaseSpeedBuffer` | `ai_ticks.cpp` | the speed reserve grows | Igual |
| MP, usabilidad y redondeo | `battle_main.c: BTL_isMoveUsable`, `BTL_setupMoveExecution` | `usability.cpp`, `beginAttack` | MP check and payment | Igual |
| Estados del luchador (carga, enfriamiento, confusión, aplastado) | `battle_main.c: BTL_tickFighterStates` y sus `tick*State` | `fighter_actions.cpp` | flattening; stun and confusion | Igual |
| Ataque cuerpo a cuerpo y a distancia | `battle_main.c: BTL_tickMeleeAttack`, `BTL_tickRangedAttack`, `BTL_startQueuedMove` | `tickMeleeAttack`, `tickRangedAttack`, `startQueuedMove` | a battle plays to a victory | Igual |
| Viaje del proyectil, ataques de área | `battle_main.c` (animaciones y efectos) | `releaseAttack`, `tickAttacksInFlight`; el efecto dibujado termina con su golpe (ver Scripts de efecto) | seeded replay | Suposición: una velocidad para todos (`projectile_speed`), no la de cada script. Hueco medido con el oráculo (combate del bosque, Llama de Agumon): el original lanza en el fotograma 11 y el proyectil avanza 35 unidades por fotograma desde unas 290 de alcance, así que a 520 golpea en 17-18 fotogramas y a 230 en 11; aquí, 12 + distancia / 60 (21 y 16) |
| Un luchador golpeado o que bloquea pierde su propia técnica: lo que aún vuela de ella no llega a nadie | `battle_main.c: BTL_tickAttackHits` (`BTL_removeMoveEffect` antes de la tirada; no con el contraataque ni con una técnica de apoyo) | `handleAttack` | a fighter hit or blocking loses its own shots still on their way | Igual |
| Acierto y bloqueo | `battle_main.c: BTL_calculateHitChance`, `BTL_tickAttackHits` | `OriginalDamage`, `handleAttack`, `block` | the hit chance | Igual |
| Daño y su goteo | `battle_main.c: BTL_calculateDamage`, `utils.c: damageTick` | `OriginalDamage`, `drainPendingDamage` | the damage formula; pending damage drains | Igual |
| Contraataque | `battle_main.c: BTL_tickAttackHits` | `tryCounter` | a counter needs 165 MP | Igual |
| Técnicas de apoyo | `battle_main.c: BTL_applyBuffMove` | `applyBuff` | support moves raise stats | Igual |
| Estados alterados | `battle_main.c: BTL_applyMoveStatus`, `BTL_tickStatusEffects`, `BTL_healStatusEffect` | `status_effects.cpp` | poison; stun and confusion; flattening | Igual |
| Remate y su barra | `battle_main.c: BTL_tickFighterAction`, `BTL_addFinisherProgress`, `command_menu.c: BTL_tickFinisherChargeup` | `beginFinisher`, `addFinisherProgress` | the finisher gauge fills | Igual |
| El botón cuadrado llama al remate en cuanto su barra está llena, esté donde esté el cursor de la tira | `battle_tick.c: handleCommands` (botón 0x80), `battle_hud.c: BTL_renderFinisherReadyIcon` | `BattleScreen::pressFinisher` (acción `extra`, vía `PlaySession::pressBattle`); en la UI, `prompt-finisher` | — | Igual; adaptación al PC del aviso (abajo) |
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
| Técnicas equipadas de un enemigo: la casilla cuya animación no nombra una técnica de su especie queda vacía; el registro del NPC puede poner cualquier técnica en cualquier casilla y un remate empieza como remate esté donde esté (solo el partner tiene el remate fijo en la cuarta) | `btl.c: loadBattleData` (`entityGetTechFromAnim` da 0xff) | `enemyFromMap`, `resolveMoves` | — | Igual al vaciar la casilla; Suposición en el remate fuera de la cuarta casilla (2026-10-28) |
| Animación de cada luchador: postura (0x21, o 0x22 envenenado o con un quinto de HP o menos), andar (0x24/0x23), técnica (0x2E + su lugar en la lista de la especie), golpe de frente o de espalda (0x28/0x29), bloqueo (0x25), aturdido (0x22 parado), aplastado (0x22), desobediencia (0x13), desmayo (0x2B); el golpe y el bloqueo quedan parados mientras dura la invulnerabilidad y el remate mientras dura su aura | `btl.c: startBattleIdleAnimation`, `handleBattleIdle`; `battle_main.c: BTL_setWalking`, `BTL_startWalkingAnimation`, `BTL_setupQueuedMove`, `BTL_startQueuedMove`, `BTL_handleHitReaction`, `BTL_tickAttackHits`, `BTL_tickHitState`, `BTL_tickStunState`, `BTL_tickStatusEffects`, `BTL_tickSenileState` y el desmayo (`battle_main.c:1651`) | `Fighter::shown` (`shown_clips.cpp`), `stanceClipOf`; la app lo aplica: `BattleScreen::takeFigures`, `FieldScene::holdFighter` | — | Igual en los ids; el disparo del luchador aplastado (anim 0) conserva el clip que tenía |
| Giro al recibir un golpe: de frente (a un cuarto de vuelta del ataque) o aplastado mira al ataque; de espalda le da la espalda | `battle_main.c: BTL_handleHitReaction` | `turnForHit`, `Fighter::hitFromBehind` | — | Igual, con el punto de salida del ataque en vez de la posición del proyectil al llegar |
| Los luchadores son las entidades del campo: el partner y el NPC de cada enemigo toman cada fotograma la posición, el giro y el clip del combate, sin el seguimiento ni el paseo del campo; los clips corren aunque el campo esté en pausa; al cerrarse el cuadro final el campo los recupera donde quedaron | `battle_main.c: BTL_battleMain`, `BTL_handleVictorySequence` (postura 0x21 antes de la fanfarria y otra vez tras la pose 0x2A) | `BattleScreen::showFighters`, `FieldScene::holdFighter`, `runFighters`, `releaseFighters`; `mapBattleRequest` da el NPC de cada enemigo | `route` de prueba E2-01 (fuera de git) | Igual salvo los huecos de abajo |
| Tras el cuadro final de una victoria: los enemigos se levantan (0x2C); en un combate fijo se quedan en postura (0x21); si se podía huir, al acabar de levantarse andan (0x24) hacia su punto de huida hasta que todos salen de la pantalla. Al terminar, de los NPC que estaban en pantalla al empezar el combate (el `isOnScreen` del último tick del campo) salen del mapa todos si se podía huir, y en un combate fijo todos menos los luchadores. Lo hace la pantalla del combate, así que vale igual para los combates de la historia y los de consola | `battle_main.c: BTL_handleVictorySequence` (921-960), `BTL_battleMain` (783-808); `map.c: entityIsOffScreen` | `BattleScreen` (`battle_screen_exit.cpp`) | — | Igual; la pantalla es la ventana (más ancha que 320x240), la velocidad es la del clip y, si el clip no tiene movimiento, `timing.walk_speed`; tope de 10 s |
| El domador corre hacia atrás mientras se forma el rótulo: sigue el camino que guardó el campo (de la celda más nueva a la más vieja) y luego va a la celda por la que entró al mapa; para en ella o cuando su paso saldría del área (310 x 230 por el camino, 300 x 220 al final) y mira al partner; el mapa y los luchadores no lo paran. El rótulo dura 200 fotogramas o, si el domador para antes, dos más desde que para, y siempre hasta que sus piezas están en su sitio. Lo que no alcanzó del camino queda guardado y una huida corre a donde quedó. La cámara no le sigue durante el combate | `battle_setup.c: BTL_handleBattleIntro`; `map.c: initializeTamerWaypoints`, `tickTamerWaypoints`, `isLinearPathBlocked`, `entityCheckCombatArea`, `tickCameraFollowPlayer` | `TamerRetreat`; `game::field::TamerTrail`; `BattleScreen` (`battle_screen_retreat.cpp`), `BattleIntro::tamerBack`, `FieldScene::holdCamera` | the tamer runs back...; the way back keeps the cell...; the banner stays 200 frames... | Igual; el paso de 50 unidades es la medida del original con la animación 3 (`timing.retreat_step`) |
| Los NPC que no luchan huyen al empezar el combate; el domador en combate (0x1, 0xE, 0xB) | `battle_setup.c: BTL_handleBattleIntro`; `battle_tick.c: handleCommands`, `tamerTickBattle` | — | — | Hueco: mientras tanto, tras una victoria los NPC que no luchan y estaban en pantalla se van andando con los enemigos |
| Movimiento de raíz de las animaciones de combate: el golpe empuja al luchador hacia atrás (en el oráculo, el partner unas 180 unidades y el enemigo unas 300) y algunas técnicas lo adelantan (el enemigo avanza unas 270 unidades al atacar) | `battle_main.c: BTL_handleHitReaction`; las animaciones 0x28/0x29 y 0x2E+ (`BTL_battleTickFrame`) | — | — | Hueco: aquí solo andar mueve a los luchadores; es la mayor parte de lo que el oráculo aún ve en las posiciones |

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

### Adaptaciones al PC (2026-10-28)

- **Aviso del remate:** con la barra llena el original escribe «FINISH!!» y, en una caja gris, el botón cuadrado del
  mando y «PUSH» (un solo sprite de `BI_IN`). En el PC el cuadrado no dice nada, así que el aspecto original recorta
  el sprite a «PUSH» y pone en su sitio el aviso de la interfaz (`ui.prompt("extra")`, la clase `key` del tema) con
  la tecla de `extra` (Supr por defecto) o el botón del mando en uso (el oeste: Cuadrado, X…), según el último
  dispositivo y los controles que haya asignado el jugador. Con un nombre de una o dos letras (X, Y…) la caja queda
  donde la pone el original, a la derecha de «FINISH!!»; con uno más largo (Supr, Cuadrado…) baja una fila, debajo
  de «FINISH!!», con el mismo borde derecho, y el botón del remate para el ratón (`.command.no-icon`) baja con ella.
  Con el ratón el remate sigue respondiendo al clic sobre la caja.

## Scripts de efecto

`EffectRunner` (`effect_runner.hpp`) ejecuta los scripts de efecto de las técnicas (`assets::EffectScript`, leídos de
`effect/<técnica>/script.yaml`) como el original: `load` por técnica (`BTL_loadMoveEFE`), `start` corre el script de
inicio (`BTL_startEFE`) y devuelve la primera instancia que lanzó, `tick` es un fotograma de combate
(`BTL_tickEFEEngine`: suma el fotograma de cada sub-efecto activo y corre su script de tick) y `stop` corta un efecto
(`BTL_removeMoveEffect`). Cada ranura tiene su copia de los datos, la pila, las llamadas, `spawn` y los sub-efectos
siguen al original, así que cada sonido suena en el fotograma en que suena en el original.

Los sonidos salen por `EffectSounds`: `play_sound` llama a `play` (banco 8) y los ids 0x12 y 0x20–0x22 a `playHeld`,
que guarda la voz; la voz se para cuando su sub-efecto termina, como `EFE_SOUND_DATA`. Las sub-operaciones de golpe
(`check_collision`, `apply_*_hit`, `check_target_collision`, `is_target_unhit`) responden con el golpe de la simulación
(`noteHit`) en vez de las cajas de colisión del original. `hit_target` (`BTL_addAttackObjectToTarget`) marca al
objetivo como golpeado, que es lo que mira `is_target_unhit` (el `isHit` del original), y el daño sigue en la
simulación; Suposición: la marca dura hasta el siguiente inicio del efecto. Sin ella, el bucle de objetivos de Poison
Powder repetía su sonido sin fin. `random_in_range` es `customRandom`. Mejora documentada:
`stopAll` para todas las voces guardadas, mientras que `BTL_stopEFESounds` solo mira la primera (no avanza el puntero).

### Dibujo

Las demás sub-operaciones (`effect_runner_subops.cpp`) hacen las cuentas del original en sus ejes (y hacia abajo) y
unidades (4096 es 1; ángulos de 4096 por vuelta, 512 en `_sin`/`_cos`), con las matrices de libgte (`RotMatrix`,
`RotMatrixYXZ`, `RotMatrixZYX`) y `matrixToEuler1`/`2` en `effect_math.cpp`. Los luchadores se leen por el puerto
`EffectBodies`: sitio, giro (`rotation.vy`), radio y alto de la tabla de especies, el tipo original
(`battle.effects.original_types`, la tabla de tipos originales de `effect_common.c`) y la matriz de cada nodo del
modelo que dibuja la app. `start` recibe atacante y objetivo (los del combate, como en el original); los
hijos heredan los del padre. Después de `tick`, `render` corre los scripts de render (`BTL_renderEFEEngine`) y deja en
`draws()` lo que dibujaron: `EffectSprite` (`BTL_renderProjectedSprite`, `BTL_renderCenteredSprite`: posición, color,
escala, texeles, pivote, giro, mezcla y fila de paleta) y `EffectModel` (`BTL_drawTMD`, `BTL_drawTMDYXZ`: objeto del
modelo, matriz con escala, color de `set_model_object_color` y desplazamiento de la animación UV). La batalla no sabe
de renderers.

La sesión (`BattleEffects`) pasa los luchadores y expone `draws()`; la app (`BattleEffectBodies`, `FieldScene::setEffects`)
pone cada sprite como billboard con la textura `effect/<técnica>/texture` y su paleta (los colores con STP se mezclan con
la mezcla del sprite; tamaño en el mundo = texeles × escala / 32, que es lo que da la proyección del original con
`VIEWPORT_DISTANCE` 500) y cada objeto del modelo `effect/<técnica>/model` como su hueso con la matriz del efecto (los
demás huesos a cero), teñido con su color.

| Comportamiento | Original | Aquí | Prueba | Estado |
|---|---|---|---|---|
| Transformaciones (hueso, `init_transform`, padre, seguir, girar, dispersar) | `battle_effect.c: BTL_setTransformTo*`, `BTL_initializeEFETransform`, `BTL_steerTransformTowardPoint`, `BTL_applyHomingMovement`, `BTL_getScatteredSpawnPosition` | `transformOp` | a render script's sprite and model come out as draws on the source's bone | Igual |
| Cuentas de vectores, ángulos e interpolación | `battle_effect.c` (operaciones de vector), `vecmath.c`, `effect_common.c: lerp` | `vectorOp`, `effect_math` | Spit Fire's trail of sprites flies from the attacker toward its target | Suposición en senos y arcotangentes: se calculan y redondean, no se leen las tablas |
| Lista de huesos por especie | `BTL_checkTechCompatibility`, `getOriginalType` | `fighterOp`, `battle.effects.original_types` | an effect's bone list decodes by species; ... lacks the source's species draws nothing | Igual; el port además libera la instancia que el inicio ya creó |
| Sprites | `BTL_renderProjectedSprite`, `BTL_renderCenteredSprite`, `GsSortSprite` | `drawOp`, billboards de `MapRenderer` (pivote y giro) | a render script's sprite ... | Igual en sitio, tamaño, color, mezcla y paleta; el orden es el de los billboards, no la tabla de orden por profundidad |
| Modelos | `BTL_drawTMD`, `BTL_drawTMDYXZ`, `BTL_setEFEModelObjectColor` | `drawOp`, `ModelRenderer` con `tint` | Sonic Jab and Poison Claw draw their model's object | Igual en matriz y color; Hueco: el desplazamiento UV (`BTL_offsetEFEPrimitiveUVs`) se calcula pero no se dibuja |
| Animación UV | `BTL_initializeUVAnim`, `BTL_tickEFEUVAnimation` | `tickUvAnims` | every extracted effect runs and draws on two fighters | Igual en cuentas; Hueco en pantalla (ver Modelos) |
| Elegir objetivo, tamaños, posición del objetivo | `BTL_selectNextTargetEntity`, `BTL_selectRandomTargetEntity`, `BTL_get*Size`, `BTL_copyTargetEntityPosition` | `fighterOp`, `selectTarget` | every extracted effect runs ... | Igual |
| Girar al atacante, golpear la entidad | `BTL_faceTargetEntity`, `BTL_setTargetToHitEntity` | nada | — | Desviación: la batalla ya gira al luchador y elige el objetivo |
| Cámara, sprites en pantalla, partículas, nubes, cintas, destellos, cargas de CLUT, `mark_finished` | `BTL_*Camera*`, `BTL_renderScreen*`, `BTL_*Particle*`, `BTL_*Ribbon*`, `BTL_loadCLUT*` | se saltan (sacan sus argumentos) | — | Hueco |

El proyectil dibujado sigue su propio script (Spit Fire avanza 35 unidades por fotograma desde el fotograma 11) y
termina cuando la simulación golpea (`noteHit`): la simulación manda, como pide la fila «Viaje del proyectil». Su
velocidad (`timing.projectile_speed`, 60) no es la de cada técnica, así que un proyectil lento se corta antes de llegar
y uno rápido pasa de largo hasta que llega el golpe. Hueco: medir la velocidad de cada técnica en su script y llevarla a
la simulación.

## Tests

`dwe_battle_tests`: una prueba por regla, un valor del ruleset cambiado (el tope de daño), una estrategia cambiada
(un daño de prueba), la repetición con semilla y `runBattles`. Las pruebas lentas, "the user's disc, extracted: ...",
leen `DWE_CONTENT`: el catálogo con todas las técnicas del disco y un combate por cada pareja de especies (con paso 7).
