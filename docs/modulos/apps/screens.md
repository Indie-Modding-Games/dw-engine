# screens

Los modelos de vista de las pantallas del juego (`dwe::screens`): qué enseñan la pantalla de combate, el HUD del campo
y el menú del compañero, sacado de la partida (`apps/session`) y de los sistemas (`game/*`) sin decidir ninguna regla. Corre sin
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
| `battle.menu.<i>.icon` | texto | El icono de BTLCOMND de la entrada: `command-<n>` (el número del comando, de huir a cambiar), `special-<tipo>` (una técnica, por el tipo de su efecto, 0 a 6) o vacío (el golpe final, que no tiene icono en la tira) |
| `battle.finisher.{progress,goal,ready,charging,charge}` | números, booleanos | El indicador del golpe final (`battle_hud.c: BTL_renderFinisherGauge`) y su carga durante el aura (`command_menu.c: BTL_tickFinisherChargeup`) |
| `battle.revive_seconds` | número, -1 sin cuenta | Los segundos que le quedan al compañero caído para levantarse, desde que el original enseña la cuenta (`command_menu.c: BTL_renderDeathCountdown`) |
| `battle.bag.{available,open}`, `battle.item_in_flight` | booleanos | La bolsa del domador: si se puede abrir (no huyendo, no sin domador, sin otro objeto en el aire), si está abierta y si un objeto lanzado va de camino (`battle_tick.c: tamerTickBattle`, `main.c: tickThrownItem`) |
| `battle.end.outcome` | clave | `battle.outcome.victory`, `fled` o `defeat` |
| `battle.end.<i>.{key,name,amount,subject}` | claves, número | Las líneas de la caja final (`battle_ui.c: handleBattleEndBox`): objetos que sueltan los enemigos (`subject`: el enemigo), herida, técnicas aprendidas, vidas perdidas, objetos perdidos, quien se une a la ciudad. Un salto de línea en el texto empieza otra fila |
| `battle.end.gain.<i>.value` | número | Lo que ha ganado cada estadística (HP, MP, ataque, defensa, velocidad, cerebro) |
| `battle.end.stat.<i>.{value,max}` | números | Cada estadística antes de subir y su tope: la caja cuenta desde ahí y dibuja su barra contra el tope (`renderPostBattleStatsBox`) |
| `battle.end.bits`, `battle.end.money` | números | Los bits ganados y el dinero con ellos ya dentro: la caja del saldo cuenta hasta él (`tickBitBox`) |
| `battle.frame_seconds`, `battle.end.{count_delay_frames,wait_frames}` | números | Lo que dura un fotograma de combate y el ritmo de las cajas, del ruleset (`battle.hud`): la espera antes de contar y la de cada línea. Las cajas se cierran solas al acabar; un botón salta lo que corre |

Con la bolsa abierta, el juego publica `bagViewOf(inventory, items, names, UseContext::Battle)` (los ajustes `bag.*`
de abajo): `usable` dice qué objetos se pueden lanzar en combate.

El juego añade `battle.phase` (`intro`, `fight`, `victory` o `end`; `victory` es la victoria antes de la caja final, sin caja ni entrada: la espera de 0x29 fotogramas de `BTL_handleVictorySequence`, el sonido 0x18 y las poses del jugador (clip 0xb) y del compañero (clip 0x2a), hasta que ambas terminan; el port corta las poses a los 200 fotogramas de batalla si alguna no termina), y el documento envía `battle_command` (el número del comando),
`battle_charge`, `battle_close`, `battle_end_count` y `battle_end_counted` (la cuenta de la caja del final, para sus
sonidos), `battle_bag_open`, `battle_bag_close` y `battle_throw` (el número de la ranura)
([ui.md](../../architecture/ui.md)).

### Apertura del combate

`BattleIntro` es la apertura del original paso a paso, a su ritmo de combate (20 frames por segundo): la marca de
enfado sobre cada enemigo mientras el original carga el combate (`btl.c: handleBattleStart`, `renderFleeBubble`),
el rótulo «BATTLE» con sus 155 piezas que llegan volando con su estela y parpadean al juntarse
(`battle_hud.c: BTL_renderBattleStartText`, `battle_setup.c: BTL_handleBattleIntro`) y su estallido
(`BTL_renderBattleStartTextBurst`). Las piezas salen de una semilla, así que una ruta ve siempre la misma apertura.
`battleIntroViewOf(intro, marcas)` y `settingsOf(vista, enemigos)` dan los ajustes que lee el documento, que redibuja
con `battle.intro.revision`:

| Ajuste | Valor | Qué es |
|---|---|---|
| `battle.intro.step` | texto | `anger`, `banner`, `burst` o `done` |
| `battle.intro.anger.<i>.{shown,x,y}` | booleano, % | Si la marca del enemigo `i` se ve y dónde, en % de la ventana (su centro) |
| `battle.intro.anger.scale` | % | El tamaño de las marcas: crecen 5 frames y luego laten (`FLEE_BUBBLE_SCALE`) |
| `battle.intro.flash` | 0 a 2 | La paleta con la que parpadea el rótulo cuando todas sus piezas han llegado |
| `battle.intro.pieces` | texto | Las piezas que se ven, siete números cada una en píxeles de la pantalla de 320 × 240 del original: x, y, ancho, alto, textura (0 a 2), estela y 1 si la estela va a la izquierda |

### Números de daño

`DamageNumbers` son los números que saltan de los luchadores (`entity_text.c`): una cola por luchador de hasta 8, cada número fijo donde estaban sus pies al añadirse y movido fotograma a fotograma al ritmo del combate (ver la fila en [battle.md](../game/battle.md#paridad-con-el-original)). `BattleScreen` añade los de cada fotograma y da un paso; tras la victoria siguen solos. `settingsOf(números)` da los ajustes que lee el documento, que redibuja con `battle.damage.revision`:

| Ajuste | Valor | Qué es |
|---|---|---|
| `battle.damage.numbers` | texto | Los números dibujados en el último fotograma, seis valores cada uno: x e y del ancla en % de la ventana, desplazamiento de la primera cifra desde ella en píxeles del original (x, y), valor y color (0 normal, 12 veneno) |

### Barras de HP de los enemigos

`enemyHpBarOf` coloca y tiñe la barra de un enemigo (`battle_main.c: BTL_renderEnemyHPBars`): recibe dónde se ve el nodo 1 de su modelo (fracción de la ventana), la proporción de la ventana y su HP, y da la esquina de la barra desde ese punto en píxeles del original, el ancho del relleno y su tinte (ver la fila en [battle.md](../game/battle.md#paridad-con-el-original)). `BattleScreen` la calcula en cada tick para los enemigos cuya barra se ve (`Battle::hpBarShown`), así que sigue al enemigo, y la quita al acabar el combate. `settingsOf(barras)` da los ajustes que lee el documento, que redibuja con `battle.hp_bars.revision`; solo el aspecto original las enseña:

| Ajuste | Valor | Qué es |
|---|---|---|
| `battle.hp_bars` | texto | Las barras dibujadas en el último fotograma, nueve valores cada una: enemigo (1 a 3), x e y del ancla en % de la ventana, esquina de la barra desde ella en píxeles del original (x, y), ancho del relleno (0 a 50), tinte rojo y verde (128 tal cual, 255 el doble) y HP |

## Caja de vidas

`LivesBox` es la caja de vidas de una vida perdida (`murd.c: MURD_createLivesBox`, `MURD_tickLivesBox`,
`MURD_renderLivesBox`, `MURD_animateLivesBoxOut`, `MURD_removeLivesBox`) sobre la caja animada del original (`ui.c:
createAnimatedUIBox`, `renderUIBoxAnimated`), fotograma a fotograma al ritmo de la escena: su contorno gris crece
desde el compañero durante 5 fotogramas, la caja enseña un hueco por vida, con la recién perdida llena hasta el
fotograma 0xa, parpadeando de dos en dos hasta 0x1e y vacía después; al cerrarse encoge otros 5 fotogramas hacia el
compañero. Al abrirse suena el 0 y al empezar a cerrarse el 1, en el canal 0. El documento `ui/lives_box` redibuja
con `lives_box.revision`:

| Ajuste | Valor | Qué es |
|---|---|---|
| `lives_box.shown`, `lives_box.outline` | booleanos | Si la caja se ve, y si solo su contorno (mientras crece o encoge) |
| `lives_box.grow` | número | Cuánto ha crecido: 0 en el compañero, 1 en su sitio |
| `lives_box.anchor_x`, `lives_box.anchor_y` | números | El punto medio del compañero en pantalla, en porcentaje de la ventana |
| `lives_box.lives`, `lives_box.slots` | números | Las vidas que quedan tras perder una y los huecos (las vidas con las que se empieza) |
| `lives_box.lost_full` | booleano | Si la vida recién perdida se ve llena este fotograma |
## HUD del campo

`fieldHudViewOf(hora, compañero, shown)` es el estado del campo: la hora y el día, felicidad y disciplina, hambre,
sueño, baño y cansancio, y HP y MP, solo con números y booleanos para que el juego la compare en cada frame sin construir textos.
`fieldHudLabelSettings()` da las etiquetas, que se publican una vez. Los ajustes `hud.*`, sus unidades y cuándo se
actualizan son el contrato de datos de cualquier aspecto: están en [ui.md](../../architecture/ui.md#estado-del-campo).

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
| `partner.profile.special.<i>` | número | Especialidades de especie (0–6); -1 representa una posición ausente. |
| `partner.profile.type` | número | Tipo original de especie; 0 no dibuja icono. |
| `partner.profile.active` | número | Patrón de actividad de SpeciesCare; -1 cuando falta la fuente. La presentación original toma los rótulos del texto local del disco. |
| `partner.technique.<i>.{empty,name,power,mp,range,effect}` | booleano, clave, números | Tres equipadas (0–2) y final (3), resueltas del catálogo de combate; MP ya convertido por sus reglas. Un hueco no presenta cifras ni efecto. |
| `partner.technique.choice.<i>.{name,available,equipped,known,slot,special,power,mp,range,effect}` | clave, booleanos | 56 celdas de la cuadrícula original; slot resuelve la posición compatible de especie (o -1). Aprendizaje, variantes y selección proceden de sesión; Lua no decide reglas. |
| `partner.technique.can_finish` | booleano | El editor puede terminar si queda una técnica ofensiva equipada; el final no cuenta. |
| `bag.slot.<i>.{empty,item,name,description,count,dimmed,usable,icon,icon_rect}` | booleano, número, claves, número, booleanos, textos | Las ranuras que tiene la bolsa, en orden (`inventory.c: renderInventoryTop`); `dimmed` es un objeto que no se usa desde la bolsa (`item_color` 255), que el original pinta de otro color; `usable`, si su `item_color` deja usarlo en ese contexto (`inventory.c: selectOptionMenu`); `icon`, la imagen de su icono con su paleta (`/texture/etcdat/item?palette=N`, de `icon_palette`), e `icon_rect`, su celda de 16 × 16 en ella, `"x y ancho alto"` (`utils2.c: setItemTexture`); los dos vacíos si el objeto no tiene icono |
| `bag.slot.<i>.item_color` | número | Clasificación del objeto; el aspecto original distingue campo (0), combate (1), ambos (2) y nunca (255) con su tinta. |
| `bag.slot.<i>.discardable` | booleano | Objeto presente, contexto Field y valor droppable aceptado por UseRules; la UI desactiva Drop sin duplicar reglas. |
| `bag.money` | número | Los bits |
| `bag.result` | clave | Qué hizo la última orden sobre la bolsa (`partner_menu.bag_result.*`); el juego añade `bag.result_item`, la clave del nombre del objeto |

El juego añade `partner.care_result` (`partner_menu.care_result.praised` o `scolded`).

**Barra del virus proporcional (decisión del responsable de diseño, 2026-10-21).** El original la dibuja con 3 píxeles
por punto de la barra (`game_menu.c: renderDigimonStatsView`: `renderBox(..., virusBar * 3, 6, ...)`); aquí se llena
en proporción a su máximo del ruleset. Es una diferencia solo de aspecto y se queda.

**Combate (decisiones del responsable de diseño, 2026-10-21).** El panel de HP de un enemigo lleva su nombre hasta que
exista la escena 3D del combate; el título de la caja final («Has escapado.» al huir) se queda como desviación de
accesibilidad; y la bolsa lanza el objeto al confirmar su ranura, sin la caja de opciones del original. Están en la
tabla de paridad del [página de game/battle](../game/battle.md).

**Alabar y regañar solo en el campo (decisión del responsable de diseño, 2026-10-20/21).** En el original se alaba y
se regaña al compañero en el campo, junto a él (`partner.c: partnerTickPraiseScold`, `tamer.c: tamerTickPraiseScold`),
no desde un menú. La pestaña del menú del compañero (`partner_praise`, `partner_scold`) se queda, sin cambiar nada,
hasta que exista esa interacción en el campo; entonces se quita (TODO en `apps/game/src/partner_menu.hpp`).

## Menú del juego (aspecto original)

Paridad con la revisión de menús contra el original (QA-M, 2026-10-30). Estado: **Igual**, **Suposición** o **Hueco**.

| Comportamiento | Original | dwe | Prueba | Estado |
|---|---|---|---|---|
| Volver de la bolsa o del compañero al menú de Triángulo | `game_menu.c: tickTriangleMenu` (1372-1379) e `inventory.c: tickInventoryUI` (385-392): al cancelar, el menú vuelve y responde al mando | `partner_menu/logic.lua: close_menu` muestra `pause_menu` modal (`ui.show(nombre, true)`); antes quedaba visible sin recibir la navegación (QA-M01) | `ui_tests`: "a menu a script shows modal again takes the navigation back" | Igual |
