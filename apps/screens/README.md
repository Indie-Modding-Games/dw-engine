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
| `battle.fighter.<i>.{name,hp,max_hp,mp,max_mp,fainted,status}` | clave, números, booleano, clave | El compañero (0) y los enemigos: nombre, barras, si ha caído y su estado (`battle.status.*`) |
| `battle.menu.<i>.{command,label,available,current}` | número, clave, booleanos | Las entradas que su inteligencia permite, en orden (`command_menu.c: BTL_renderCommandMenu`): el comando, su etiqueta (los comandos del original o el nombre de la técnica), si se puede elegir (el golpe final, solo con el indicador lleno) y si es la orden que sigue |
| `battle.finisher.{progress,goal,ready,charging,charge}` | números, booleanos | El indicador del golpe final (`battle_hud.c: BTL_renderFinisherGauge`) y su carga durante el aura (`command_menu.c: BTL_tickFinisherChargeup`) |
| `battle.revive_seconds` | número, -1 sin cuenta | Los segundos que le quedan al compañero caído para levantarse, desde que el original enseña la cuenta (`command_menu.c: BTL_renderDeathCountdown`) |
| `battle.end.outcome` | clave | `battle.outcome.victory`, `fled` o `defeat` |
| `battle.end.<i>.{key,name,amount}` | claves, número | Las líneas de la caja final (`battle_ui.c: handleBattleEndBox`): objetos encontrados, herida, técnicas aprendidas, vidas perdidas, objetos perdidos, quien se une a la ciudad |
| `battle.end.gain.<i>.value` | número | Lo que ha ganado cada estadística (HP, MP, ataque, defensa, velocidad, cerebro) |
| `battle.end.bits` | número | Los bits ganados |

El juego añade `battle.phase` (`fight` o `end`), y el documento envía `battle_command` (el número del comando),
`battle_charge` y `battle_close` ([ui.md](../../docs/architecture/ui.md)).

## Menú del compañero

`partnerStatusViewOf(partner, names)` es la página de estado y `bagViewOf(inventory, items, names)` la bolsa.

| Ajuste | Valor | Qué es |
|---|---|---|
| `partner.{name,age,weight,lives,hp,max_hp,mp,max_mp}` | clave, números | Lo que enseña la vista de estadísticas del original (`game_menu.c: renderDigimonStatsView`): nombre, edad en días, peso, vidas, HP y MP actuales y máximos |
| `partner.stat.<i>.{label,value,min,max}` | clave, números | Las barras de ataque, defensa, velocidad y cerebro, sobre 999 |
| `partner.meter.<i>.{label,value,min,max}` | clave, números | Felicidad, disciplina y virus, con sus límites del ruleset |
| `partner.condition.<i>.key` | clave | Lo que le pasa ahora (sueño, cansancio, hambre, baño, enfado, herida, enfermedad) |
| `bag.slot.<i>.{empty,item,name,description,count,dimmed}` | booleano, número, claves, número, booleano | Las ranuras que tiene la bolsa, en orden (`inventory.c: renderInventoryTop`); `dimmed` es un objeto que no se usa desde la bolsa (`item_color` 255), que el original pinta de otro color |
| `bag.money` | número | Los bits |
| `bag.result` | clave | Qué hizo la última orden sobre la bolsa (`partner_menu.bag_result.*`); el juego añade `bag.result_item`, la clave del nombre del objeto |

El juego añade `partner.care_result` (`partner_menu.care_result.praised` o `scolded`).
