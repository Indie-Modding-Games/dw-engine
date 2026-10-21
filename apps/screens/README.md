# screens

Los modelos de vista de las pantallas del juego (`dwe::screens`): qué enseñan la pantalla de combate y el menú del
compañero, sacado de la partida (`apps/session`) y de los sistemas (`game/*`) sin decidir ninguna regla. Corre sin
ventana, así que sus tests no abren nada. El juego (`apps/game`) los publica a la UI y los documentos (`ui/battle`,
`ui/partner_menu` del paquete `game`) solo los dibujan.

## Cómo llega a la UI

Cada vista se convierte en ajustes de la UI (`settingsOf`) y `publish` cambia solo los que han cambiado y después sube
un ajuste de revisión (`battle.revision`, `partner.revision`, `bag.revision`). El documento redibuja cuando cambia la
revisión, así lee una vista completa y nunca media.

Las listas van como `<lista>.count` y `<lista>.<i>.<campo>` (`battle.fighter.0.hp`). Los textos van como claves:

- los nombres del original (especies, objetos, técnicas) son claves de las tablas de texto extraídas, por su índice
  en la tabla de datos (`NameKeys`: `main.digimon_names.NNNN`, `main.item_names.NNNN`, `main.move_names.NNNN`);
- las etiquetas que el original tiene (los comandos, `btl.command_names`; HP, MP, Ataque..., `main.stat_labels`;
  usar, mover, ordenar, tirar, `main.item_menu_labels`; los bits, `main.battle_ui_bits_labels`) también;
- lo demás son claves propias del juego en `localization/<idioma>/battle.yaml` y `partner_menu.yaml`.

## Pantalla de combate

`battleViewOf(battle, names)` mientras dura el combate; `battleEndViewOf(result, aftermath, names)` al acabar.

| Ajuste | Valor | Qué es |
|---|---|---|
| `battle.fighter.<i>.{name,hp,max_hp,mp,max_mp,fainted,status,hp_bar}` | clave, números, booleanos, clave | El compañero (0) y los enemigos: nombre, barras, si ha caído, su estado (`battle.status.*`) y si el HUD enseña ahora su barra de HP. La del compañero se ve siempre; la de un enemigo, solo un rato tras recibir daño y sin barra de MP (`battle_main.c: BTL_renderEnemyHPBars`) |
| `battle.menu.<i>.{command,label,available,current}` | número, clave, booleanos | Las entradas que su inteligencia permite, en orden (`command_menu.c: BTL_renderCommandMenu`): el comando, su etiqueta (los comandos del original o el nombre de la técnica), si se puede elegir (el golpe final, solo con el indicador lleno) y si es la orden que sigue |
| `battle.finisher.{progress,goal,ready,charging,charge}` | números, booleanos | El indicador del golpe final (`battle_hud.c: BTL_renderFinisherGauge`) y su carga durante el aura (`command_menu.c: BTL_tickFinisherChargeup`) |
| `battle.revive_seconds` | número, -1 sin cuenta | Los segundos que le quedan al compañero caído para levantarse, desde que el original enseña la cuenta (`command_menu.c: BTL_renderDeathCountdown`) |
| `battle.bag.{available,open}`, `battle.item_in_flight` | booleanos | La bolsa del domador: si se puede abrir (no huyendo, no sin domador, sin otro objeto en el aire), si está abierta y si un objeto lanzado va de camino (`battle_tick.c: tamerTickBattle`, `main.c: tickThrownItem`) |
| `battle.end.outcome` | clave | `battle.outcome.victory`, `fled` o `defeat` |
| `battle.end.<i>.{key,name,amount}` | claves, número | Las líneas de la caja final (`battle_ui.c: handleBattleEndBox`): objetos encontrados, herida, técnicas aprendidas, vidas perdidas, objetos perdidos, quien se une a la ciudad |
| `battle.end.gain.<i>.value` | número | Lo que ha ganado cada estadística (HP, MP, ataque, defensa, velocidad, cerebro) |
| `battle.end.bits` | número | Los bits ganados |

Con la bolsa abierta, el juego publica `bagViewOf(inventory, items, names, UseContext::Battle)` (los ajustes `bag.*`
de abajo): `usable` dice qué objetos se pueden lanzar en combate.

El juego añade `battle.phase` (`fight` o `end`), y el documento envía `battle_command` (el número del comando),
`battle_charge`, `battle_close`, `battle_bag_open`, `battle_bag_close` y `battle_throw` (el número de la ranura)
([ui.md](../../docs/architecture/ui.md)).

## Menú del compañero

`partnerStatusViewOf(partner, names)` es la página de estado y `bagViewOf(inventory, items, names, context)` la bolsa
(`context` es `UseContext::Field` en el menú y `UseContext::Battle` en combate).

| Ajuste | Valor | Qué es |
|---|---|---|
| `partner.{name,age,weight,lives,hp,max_hp,mp,max_mp}` | clave, números | Lo que enseña la vista de estadísticas del original (`game_menu.c: renderDigimonStatsView`): nombre, edad en días, peso, vidas, HP y MP actuales y máximos |
| `partner.pool.<i>.{label,value,min,max}` | clave, números | Las barras de HP (0) y MP (1): el máximo de cada uno sobre 9999, no lo que le queda, como las dibuja el original debajo de los números (`game_menu.c: renderDigimonStatsView`, `renderDigimonStatsBar`) |
| `partner.stat.<i>.{label,value,min,max}` | clave, números | Las barras de ataque, defensa, velocidad y cerebro, sobre 999 |
| `partner.meter.<i>.{label,value,min,max,two_tone}` | clave, números, booleano | Felicidad, disciplina y virus, con sus límites del ruleset. Felicidad y disciplina son barras de dos tonos: la barra se llena del mínimo a la mitad del rango y vuelve a llenarse en otro color de la mitad al máximo (`game_menu.c: renderDigimonStatsView`) |
| `partner.condition.<i>.key` | clave | Lo que le pasa ahora (sueño, cansancio, hambre, baño, enfado, herida, enfermedad) |
| `bag.slot.<i>.{empty,item,name,description,count,dimmed,usable}` | booleano, número, claves, número, booleanos | Las ranuras que tiene la bolsa, en orden (`inventory.c: renderInventoryTop`); `dimmed` es un objeto que no se usa desde la bolsa (`item_color` 255), que el original pinta de otro color; `usable`, si su `item_color` deja usarlo en ese contexto (`inventory.c: selectOptionMenu`) |
| `bag.money` | número | Los bits |
| `bag.result` | clave | Qué hizo la última orden sobre la bolsa (`partner_menu.bag_result.*`); el juego añade `bag.result_item`, la clave del nombre del objeto |

El juego añade `partner.care_result` (`partner_menu.care_result.praised` o `scolded`).

**Barra del virus proporcional (decisión del responsable de diseño, 2026-10-21).** El original la dibuja con 3 píxeles
por punto de la barra (`game_menu.c: renderDigimonStatsView`: `renderBox(..., virusBar * 3, 6, ...)`); aquí se llena
en proporción a su máximo del ruleset. Es una diferencia solo de aspecto y se queda.

**Combate (decisiones del responsable de diseño, 2026-10-21).** El panel de HP de un enemigo lleva su nombre hasta que
exista la escena 3D del combate; el título de la caja final («Has escapado.» al huir) se queda como desviación de
accesibilidad; y la bolsa lanza el objeto al confirmar su ranura, sin la caja de opciones del original. Están en la
tabla de paridad del [README de game/battle](../../game/battle/README.md).

**Alabar y regañar solo en el campo (decisión del responsable de diseño, 2026-10-20/21).** En el original se alaba y
se regaña al compañero en el campo, junto a él (`partner.c: partnerTickPraiseScold`, `tamer.c: tamerTickPraiseScold`),
no desde un menú. La pestaña del menú del compañero (`partner_praise`, `partner_scold`) se queda, sin cambiar nada,
hasta que exista esa interacción en el campo; entonces se quita (TODO en `apps/game/src/partner_menu.hpp`).
