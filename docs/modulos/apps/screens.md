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
| `battle.menu.{top,bottom,blink}` y la revisión `battle.menu.motion` | números, booleano | La tira de órdenes en movimiento (`command_menu.c: BTL_tickCommandMenu`, `screens::CommandMenuMotion`): sus bordes de arriba y de abajo en líneas del original (0 en el centro). Al abrirse con el rótulo «BATTLE» baja de -152/-120 a -100/-68 en 13 fotogramas, primero el borde de abajo (la tira se estira) y luego el de arriba; al acabar el combate sube igual al revés y el combate termina 16 fotogramas después (`BTL_isCommandMenuClosed`). `blink` cambia el icono de la orden señalada a su segundo cuadro cada 8 fotogramas, contados desde que se abre la tira y solo mientras se lucha (medido: el primer cambio con el temporizador en 56) |
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
de abajo): `usable` dice qué objetos se pueden lanzar en combate y `discardable` es siempre falso. La bolsa que se ve
es la del campo (`ui/partner_menu`, página de objetos, con `partner_menu.context = battle`): su Usar envía `bag_use`
cuando se han cerrado sus cajas y el combate lanza el objeto (QA-M19).

El juego añade `battle.phase` (`intro`, `fight`, `victory`, `end` o `closing`; `closing` es la tira de órdenes subiendo tras la caja final y la salida de los enemigos (`battle_main.c: BTL_deinitializeCombat`), sin entrada; `victory` es la victoria antes de la caja final, sin caja ni entrada: la espera de 0x29 fotogramas de `BTL_handleVictorySequence`, el sonido 0x18 y las poses del jugador (clip 0xb) y del compañero (clip 0x2a), hasta que ambas terminan; el port corta las poses a los 200 fotogramas de batalla si alguna no termina), y el documento envía `battle_command` (el número del comando),
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
| `partner_menu.extra_presses` | número | Cuenta las pulsaciones de la acción `extra` con el menú del compañero a la vista; la piel original abre con ella la página de ayuda de técnicas. |
| `bag.slot.<i>.{empty,item,name,description,count,dimmed,usable,icon,icon_rect}` | booleano, número, claves, número, booleanos, textos | Las ranuras que tiene la bolsa, en orden (`inventory.c: renderInventoryTop`); `dimmed` es un objeto que no se usa desde la bolsa (`item_color` 255), que el original pinta de otro color; `usable`, si su `item_color` deja usarlo en ese contexto (`inventory.c: selectOptionMenu`); `icon`, la imagen de su icono con su paleta (`/texture/etcdat/item?palette=N`, de `icon_palette`), e `icon_rect`, su celda de 16 × 16 en ella, `"x y ancho alto"`, 15 de ancho en la última columna (`utils2.c: setItemTexture`; `itemIconOf`, el mismo para toda lista de objetos); los dos vacíos si el objeto no tiene icono. `ui/common/bag_slot.lua` pinta una ranura (icono, nombre, cantidad, clases de tinta) en cualquier documento que liste la bolsa |
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
accesibilidad. La bolsa que lanzaba al confirmar la ranura se retiró el 2026-10-30 (QA-M19): el combate abre la
bolsa del campo con su caja de opciones. Están en la tabla de paridad del [página de game/battle](../game/battle.md).

**Alabar y regañar solo en el campo (decisión del responsable de diseño, 2026-10-20/21).** En el original se alaba y
se regaña al compañero en el campo, junto a él (`partner.c: partnerTickPraiseScold`, `tamer.c: tamerTickPraiseScold`),
no desde un menú. La pestaña del menú del compañero (`partner_praise`, `partner_scold`) se queda, sin cambiar nada,
hasta que exista esa interacción en el campo; entonces se quita (TODO en `apps/game/src/partner_menu.hpp`).

## Menú del juego (aspecto original)

Paridad con la revisión de menús contra el original (QA-M, 2026-10-30). Estado: **Igual**, **Suposición** o **Hueco**.

| Comportamiento | Original | dwe | Prueba | Estado |
|---|---|---|---|---|
| Bolsa en combate | `battle_tick.c: tamerTickBattle` → `inventory.c: addInventoryUI`: la misma bolsa del campo, con la descripción abajo y la caja de opciones (Tirar en gris); Usar cierra las cajas y lanza (`startThrowingItem`), 41 a 45 vblanks después; cancelar la cierra y vuelve al combate, que no se para | `ui/partner_menu` con `partner_menu.context = battle` (sin pestañas, vuelve al combate, no al menú del juego); la caja propia de `ui/battle` («Throw an item», guiones en los huecos) desaparece (QA-M19) | `ui_tests`: "the battle opens the field's bag…", "the field's bag uses an item at once…"; `route-battle-*` | Igual |
| Volver de la bolsa o del compañero al menú del juego | `game_menu.c: tickTriangleMenu` (1372-1379) e `inventory.c: tickInventoryUI` (385-392): al cancelar, el menú vuelve y responde al mando | `partner_menu/logic.lua: close_menu` muestra `pause_menu` modal (`ui.show(nombre, true)`); antes quedaba visible sin recibir la navegación (QA-M01) | `ui_tests`: "a menu a script shows modal again takes the navigation back" | Igual |
| La acción menú (Tab) abre el menú del juego en el campo | `game_menu.c: addGameMenu` (1306): el botón del menú del mando | La acción `menu` es Tab en el teclado y el botón norte del mando (antes la tecla de pausa); el norte sigue siendo `alt` en la caja de nombres, que lo lee primero (QA-M03) | `navigation_tests`, `ui_tests`: "the pause menu works with a gamepad alone…" | Igual |
| La acción pausa (P o Pausa) pausa el juego | `utils2.c: handlePause` (402) y `createPauseBox` (443): el botón de pausa del mando alterna `PAUSE_STATE`; caja de -26, -14, 56 × 24 desde el centro (versión US), «Pause» (`main.utils2_pause.0000`), `playSound(0, 3)` al abrir, sin sonido al cerrar; el campo, el combate y los menús se congelan | `PauseBox` (`apps/game/src/pause_box.cpp`) con la acción `pause` (P y Pausa en el teclado, el botón de pausa del mando); el documento `pause_box` encima de todo; el tick del juego y de la UI no corre mientras dura. En la portada, una película o la caja de nombres el botón de pausa del mando hace lo de antes (`menu`) (QA-M02) | `game_tests`: "pause box: …" | Igual |
| Campo oscurecido bajo la caja de pausa | La captura del original (`orig/pause_box.png`) se ve más oscura, pero `pauseFrame` (utils.c:75) solo vuelve a dibujar los objetos; sin medir | El campo se queda como estaba | — | Suposición |
| Letra de los menús | `font.c`, `drawInventoryText` (inventory.c:255): letra de píxeles del disco en todos los textos | `ui/fonts/original_look.yaml`: `default: original` (antes la TTF suave); la piel moderna sigue con Noto Sans (QA-M08) | `bitmap_font_tests`: "…defaults to it" | Igual |
| Borde de las cajas | `renderUIBoxBorder` (ui.c:268): arriba y abajo negro, claro (#90D9FA), negro; a la izquierda negro, oscuro (#4A9FC5), claro y una línea negra interior; a la derecha la línea negra interior, claro, oscuro, negro; esquinas de 4 × 4 | Decorador `original-box` (`engine/ui/src/original_box.cpp`) con esas 14 líneas en todas las cajas de la piel original (menús, bolsa, pausa, textos, combate, ranuras, HUD) y la clase `.original-box` en `theme.rcss` (QA-M09) | `original_box_tests`: "the original box's border has renderUIBoxBorder's lines" | Igual |
| Las cajas se abren y se cierran con un contorno | `createAnimatedUIBox` / `renderUIBoxAnim` (ui.c:216/324): contorno gris de 1 px (#808080) que crece en 5 fotogramas (borde a frame/4) desde el jugador, la casilla o el cursor; al cerrar 4..0. Medido: 20 fotogramas por segundo (3 vblanks), menú 0..4 en los vblanks 22362..22374; menú → bolsa: el menú se cierra (4..0, 22383..22392) y la bolsa se abre (0 en 22396), unos 10 fotogramas | `ui/common/original_box.lua`: `open`/`close` con el mismo cálculo a 20 por segundo; el menú y la bolsa desde el jugador, la descripción y las opciones desde la casilla; la caja no recibe mandos mientras dura (QA-M05) | Rutas `qa-box` (capturas en `_work/routes-e2/menus-qa/m05-*`) | Igual |
| Sonidos de abrir y cerrar cajas | `ui.c:222` `playSound(0, 0)` al crear y `ui.c:335` `playSound(0, 1)` en el primer fotograma de cierre | `ui_sounds.yaml`: `box_open` (ss 0) y `box_close` (ss 1), que suenan con `ui.cue` al empezar la animación (QA-M06) | `ui_tests`: "a script sounds the original boxes…" | Igual |
| Icono de cada objeto en la bolsa | `renderInventoryTop` (inventory.c:590) con `renderItemSprite` y `setItemTexture` (utils2.c:269/288): 16 × 16 de ETCDAT/ITEM.TIM, la celda del número del objeto en filas de 16 (la que empieza en 0xF0, 15 de ancho), con su paleta (`ITEM_CLUT_DATA`); en x+10 (x+154 a la derecha) e y+8+fila×18 de la caja: 3 px dentro del cursor y 1 px por debajo de su borde (medido en `field_item_bag_1item`: icono en 18, 17 con el cursor en 15, 16). Siempre a color 0x80: un objeto que no se usa no atenúa el icono, solo su nombre (TEXT_COLORS 8). Una ranura vacía no pinta nada | `itemIconOf` (`bag_view.cpp`) publica `bag.slot.<i>.icon` e `icon_rect`; `ui/common/bag_slot.lua` los pinta (también para la bolsa del combate); `.slot-icon` de 48dp a 9dp y 3dp del borde exterior de la ranura: medido +3, +1 px y 16 px. La QA no vio iconos porque usó un contenido extraído antes de `icon_palette` (`content-f8-s2-4a`); con una extracción nueva salen (QA-M07) | `screens_tests`: "an item's icon is its cell of the items' image…"; ruta `bag-icons` (`_work/routes-e2/menus-qa/bag/`) | Igual |
| Descripción al mover un objeto | `renderInventoryBottom` (inventory.c:634): en todos los estados, también al mover (estado 4, que solo añade el contorno rojo 0x5D4AF1 de `renderInventoryTop`, inventory.c:607), pinta la descripción del objeto bajo el cursor con TEXT_COLORS 9; sobre una ranura vacía no pinta nada | `partner_menu/logic.lua: draw_description` con `bag_slot.description`: en la piel original la descripción de la ranura enfocada mientras se mueve un objeto; antes «Where to put …?», que el original no tiene. La piel moderna conserva la pregunta (QA-M17) | `ui_tests`: "the original bag describes the slot under the cursor, also while an item moves"; ruta `bag-icons` (capturas `move-*`) | Igual |
| Cursor de la caja de opciones y ranura bajo ella | `renderItemOption` (inventory.c:721) y `getItemOptionColor` (209): las cuatro opciones en TEXT_COLORS 9 (0x6E; medido #B5B5B5) o 0xA si no se pueden usar (0x46), la elegida igual, sobre el cursor de `renderSelectionCursor`: borde #B0B0B0 / #101010 y relleno gris a media opacidad (medido #525A63 sobre la caja #293942). La ranura de la que salen sigue con su cursor (`renderInventoryTop` lo pinta siempre). La descripción, TEXT_COLORS 9 (medido #B5B5B5) | Relleno del cursor `#80808080` en las opciones, el orden y la confirmación (antes gris opaco #808080, que apagaba el texto; medido ahora #565C60); clase `.picked` en la ranura con las cajas abiertas; tinta de opciones y descripción #B2B2B2 (medida 178, igual que antes con la letra de píxeles) (QA-M18) | `ui_tests`: "the original bag describes…" (`picked`); ruta `bag-icons` (captura `options`) | Igual |
| Descripción de una ranura vacía | `renderInventoryBottom` (inventory.c:636) no pinta nada si la ranura está vacía; las opciones salen con Use y Drop en 0xA (`getItemOptionColor`) | Caja de descripción vacía (`bag_slot.description` da "" para una ranura vacía); Use y Drop `.unavailable` (QA-M24) | `ui_tests`: "the original bag describes…"; ruta `bag-icons` (captura `empty-slot`) | Igual |
| Punto del jugador en pantalla | `getEntityScreenPos` (hueso 1) | `publishPlayerPoint` en `game.cpp` publica `field.player_x/y`; la altura de la cintura (120 unidades) es una estimación | — | Suposición |
| Pestañas de la ficha del jugador | `tickPlayerMenu` (game_menu.c:1860): izquierda y derecha pasan entre Player, Chart, Med. y Card siempre | Las cuatro pestañas se alcanzan siempre, en ese orden; sin la tabla de evoluciones (`data/evolution_chart`, que `dwx extract` saca; las extracciones anteriores no la tienen) Chart sale con la tabla vacía en vez de saltarse. Antes Med. dependía de un ajuste que el juego no publicaba, Card estaba desactivada y Chart se saltaba (QA-M04) | Rutas `pl-tabs` y `pl-held` (`_work/routes-e2/menus-qa/player`) | Igual |
| Criados y medallas | `renderPlayerInfoView` (2976): `raisedCount` del domador (1 en partida nueva, `main_menu.c:4939`; +1 por renacimiento hasta 99, `partner_impl.c:803`) y las medallas `hasMedal(0..17)` (disparadores 0x16c + n) | `TamerState::raised` (sección `tamer`, clave `raised`, 1 si falta) y `StoryAdapter::medalCount`, publicados como `player.raised` y `player.medals`; antes solo los ponía la demo de la UI (QA-M04) | `session_tests`: "the player counts the first partner raised…"; `story_adapter_tests`: "the player page counts the medals…" | Igual |
| Lista de cartas | `renderCardsView` (3750): «Card List», 11 × 6 casillas de 18 × 18 (`renderBorderBox`: #BEBEBE arriba e izquierda, #3C3C3C abajo y derecha, relleno #696969), cursor de 24 × 20 al confirmar (Enter), que se mueve sin dar la vuelta; en la casilla de una carta que se tiene (`getCardAmount`), su icono de 16 × 16 de `SBOY` en ((n % 8) × 32, (n / 8) × 16) con la paleta `CARD_SPRITE_CLUT[n]` (0x80123E28, 66 bytes) | `#player-cards-view` en `pause_menu` con esas medidas; el juego publica `player.cards.copies` (las copias de las 66 cartas) y `player.card.<n>.palette`, que `dwx extract` saca como `icon_palette` en `data/cards` (las extracciones anteriores no lo tienen y usan la paleta 0). `dwe-game` no cargaba la tabla de cartas (`createGameSystems` con tablas vacías), así que el jugador nunca tenía cartas; ahora la carga del disco | `card_table_tests`, `exe_tables_tests`, `tables_yaml_tests`, `game_systems_tests`: "the cards system loads its table…"; ruta `pl-held` | Igual |
| Abrir una carta | `tickPlayerMenu` (2070-2088): confirmar en una carta con copias (`playSound(0, 3)`) abre dos cajas animadas desde la casilla: la imagen (`renderCardImage`, -75, -84, 150 × 180, caja -75, -83) y el número de copias (`renderCardCount`, caja 74, 69, 54 × 24); cancelar las cierra. Medido en el original: imagen de x 85 a 235 e y 36 a 216, caja del número de y 189 a 213 (la captura del original está estirada 16/15 en vertical) | `#card-detail` (imagen `/card/NNN`) y `#card-count-box` en `pause_menu` con esas medidas y `original_box.open` desde el centro de la casilla; en una casilla vacía confirmar no hace nada. Las dos cajas se animan una tras otra; el original las anima a la vez | Ruta `pl-held` (capturas 08 a 12) | Igual |
| Objetos del jugador | `renderPlayerInfoView` (3028-3042): con los disparadores 45 a 48 (las dos cañas y los otros dos objetos), un icono de 16 × 16 de `SYSTEM_W` (u 176, 192, 224 y 208, v 32, paleta 7) en x 52 + 21 × i, y 55; las casillas están a 20 píxeles, así que cada icono se corre un píxel más | Sprites `player-item-0..3` en la piel original (sobre la `system_w` ampliada, por eso no van con `rect`), mostrados según `player.items.held` (`StoryAdapter::hasPlayerItem`) | `story_adapter_tests`: "the player page shows the medals and the item boxes…"; ruta `pl-held` | Igual |
| Tabla de medallas | `renderMedalView` (3664): 15 medallas (5 × 3); la que no se tiene (`hasMedal`, disparador 0x16c + n) lleva encima la sombra oscura (`sboy` 240, 200); al confirmar, el cursor va a la medalla 0, y con la medalla del cursor en su poder salen su nombre y sus tres líneas | `player.medals.held` (15 valores) pone la clase `held`; `player.medal.<n>.name` y `.line.<j>` vienen de `data/medals` (`MedalTable`). Falta la medalla en 3D del cuadro de la derecha (`renderDigiviceMedals`) | `medal_table_tests`, `story_adapter_tests`; ruta `pl-held` (capturas 03 a 06) | Hueco |
| Pestañas inactivas de la ficha | `renderMenuTab` (2960) dibuja la pestaña no elegida con `renderRectPolyFT4` bandera 1: color 0x32 sobre 0x80, la textura oscurecida, no transparente. Medido: (132, 239, 255) la elegida, (49, 90, 99) las otras | `image-color: #646464` en las pestañas no elegidas de `pause_menu` y `partner_menu` (antes `opacity` 0,25 y 0,45, casi invisibles sobre el campo) (QA-M12) | Rutas `pl-tabs` y `pl-held` | Igual |
| Indicaciones de teclas en las páginas de técnicas y del jugador | `drawMoveViewHelpStrings` (game_menu.c:3318) y las páginas Chart, Med. y Card: botones del mando en azul y verde con «Change Techniques», «Select» y «Cancel» | Cada indicación nombra la tecla del dispositivo en uso con `ui.prompt`: Enter y Esc con teclado, el botón del mando solo cuando juega un mando, en los colores del original; las de Chart, Med. y Card terminan en el borde derecho del panel porque el nombre de una tecla es más ancho que un glifo (QA-M10) | Rutas `qa-tech` y `qa-player` | Mejora documentada |
| Encabezado «Techs» y caja de ayuda al cambiar técnicas | `renderDigimonMovesView` (2577) y `MOVES_VIEW_HELP_LINES`: «Techs» donde estaba «Final»; caja de x -142 a -80 e y 26 a 93 con líneas negra, clara (148, 222, 255 medido) y negra, «Tech» y «help», la flecha (`system_w` 128, 12) y el botón que abre la página de ayuda (`MENU_STATE` 7) | `#techs-heading` y `#tech-help` en `partner_menu` con esas medidas; el botón es la tecla de la acción `extra` (Supr con teclado). Las líneas claras de la página Tech pasan de #6987A0 a #94DEFF, el color medido, y la página lleva el borde `original-box` como la de Status | Ruta `qa-tech` (`_work/routes-e2/menus-qa/tech/`) | Igual |
| Página de ayuda de técnicas | `tickDigimonMenu` y `renderDigimonMovesView` (`MENU_STATE` 7 a 9, rama US): el botón cuadrado la abre con `playSound(0, 3)`; «Technique Select Help» en (-142, -82), dos líneas con viñeta (`system_w` 180, 12, paleta 4): «Select - choose [fuego] to select tech» y «Cancel - ✕ deselects tech [rojo]»; separador en y -4; leyenda en x -120 (Current, Mastered, Unmastered, Unusable dos veces, Selected con el cursor en -123, 80); solo cancelar responde y vuelve al cambio | `#tech-help-page` en `partner_menu` con esas medidas; el juego cuenta las pulsaciones de `extra` en `partner_menu.extra_presses` y el script abre la página con `ui.cue("confirm")`; Esc la cierra. La ✕ es la tecla de confirmar del dispositivo en uso (Enter con teclado) | `skin_switch_tests`: "the original skin's Tech page: no Final range, the help page…"; ruta `qa-tech` | Igual |
| Casillas de la cuadrícula de técnicas | `renderDigimonMovesView`: cada casilla es una caja sustractiva de 0x68, negra sobre el panel (0, 0, 0 medido); las aprendibles (0x4E, 0x60, 0x6E); la equipada, rojo (200, 0, 40) al 50 % sobre el pictograma (173, 74, 90 medido); una técnica conocida que el compañero no puede usar deja el pictograma bajo la caja sustractiva | La base pasa de gris #686868 a negro; `.tech-cell-shade` pone encima el rojo al 50 % de la equipada o el negro al 55 % de la conocida que no puede usar | Ruta `qa-tech` | Igual |
| Alcance de la técnica final | `renderDigimonMovesView`: la fila Final nunca muestra alcance; medido escribiendo `MOVE_DATA[59].range` a 1 y 3 (sigue en blanco) mientras el de la fila Techset cambia (captura `orig-final-range-written-3.png`) | `logic.lua: draw_tech` deja vacío el alcance de la fila 3 con la piel original; antes mostraba «L» (QA-M11) | `skin_switch_tests`: "the original skin's Tech page…" | Igual |
| Nombres de los parámetros del compañero | `renderDigimonStatsView` (2402): HP, MP, Off y Def con la letra a tamaño completo en el color 3 (255, 247, 115 medido); SPEED y BRAINS son dibujos en versalitas de `system_w` (212, 70) y (212, 82), 36 × 12, paleta 4 | `#status-parameters .label` a 36 dp en #FFF371; `#stat-2-label` y `#stat-3-label` con los sprites `status-speed-name` y `status-brains-name`; el icono de BRAINS mide 10 de ancho, sin el «]» que asomaba (QA-M13) | `skin_switch_tests`: "the original skin's Tech page…" | Igual |

## Guardián de objetos

El guardián de objetos de la ciudad (room02, script 171, sección 0x51 → `call_routine(11)` →
`shop/script_menu.c: tickItemKeeper`). La sesión (`session::ItemKeeper`, sección `item_keeper` del ruleset) guarda lo
que el jugador deja, hasta 99 de cada objeto, y lo escribe en la partida; la pantalla `item_keeper` del juego
(`apps/game: ItemKeeperScreen`) pregunta, enseña las dos ventanas y despide. Medido con el original en el mostrador
(capturas `orig/bank/keeper-open.png`, `keeper-row4.png`, `keeper-moved-one.png`).

| Ajuste | Valor | Qué es |
|---|---|---|
| `item_keeper.phase` | texto | `ask`, `choice`, `windows`, `description`, `farewell` o `closed` |
| `item_keeper.{speaker,line_key,choices_key,description_key}` | claves | El nombre del guardián, lo que dice (MAPHEAD 1237, líneas 0 y 2), el «Sí/No» (línea 1) y la descripción del objeto |
| `item_keeper.{choice,side}` | números | La respuesta marcada (0 sí, 1 no) y la ventana en la que trabaja el jugador (0 lo guardado, 1 la bolsa) |
| `item_keeper.{kept,bag}.{total,top,cursor}` | números | Las filas de cada lista, la primera que se ve y la del cursor entre las cinco visibles |
| `item_keeper.{kept,bag}.<i>.{name,count,movable}` | clave, número, booleano | Las cinco filas que se ven; una fila que no puede pasar al otro lado se pinta en rojo (color 3) |

| Comportamiento | Original | dwe | Prueba | Estado |
|---|---|---|---|---|
| Pregunta, sí o no y despedida | `tickItemKeeper` (script_menu.c:892): líneas 0, 1 y 2 de la sección 1237 de MAPHEAD | `ItemKeeperScreen`: `ask`, `choice`, `farewell` con las mismas líneas | Ruta `bank` (room02) | Igual |
| Ventanas «Keeping» e «In hand» | `createItemKeepWindow`: cajas de x 8 a 155 y de 160 a 307, y 15 a 150; barra azul en la activa, rótulos MENU y COUNT de SYSTEM_W, cinco filas cada 19,25 px, barra de desplazamiento de x 144 a 151 | `item_keeper/structure.rcss` y la piel original con esas medidas; los títulos son textos del paquete (el original los lleva en el ejecutable) | Ruta `bank` | Igual |
| Listas | `itemKeeperFillItemList`: lo guardado por número de objeto; la bolsa, una fila por hueco; una fila no se mueve sin hueco o pila libre en la bolsa, o con 99 guardados | `ItemKeeper::keptRows` y `bagRows` | `itemKeeperFillItemList: a row that has nowhere to go cannot move` | Igual |
| Pasar uno (Enter o Espacio) | `keeperMoveOne` | `ItemKeeper::move(…, One)` | `keeperMoveOne moves one item each way…` | Igual |
| Pasar diez (Inicio, el botón norte del mando) | `keeperMoveTen`: hacen falta 10 y que el otro lado tenga menos de 90 | `ItemKeeper::move(…, Ten)` | `keeperMoveTen needs ten to move…` | Igual |
| Pasar la pila (Supr, el botón oeste del mando) | `keeperMoveStack`: lo que quepa hasta 99 | `ItemKeeper::move(…, Stack)` | `keeperMoveStack moves the whole stack…` | Igual |
| Descripción del objeto (P o Pausa, Start en el mando) | Start abre `tickItemMenuDescriptionBox`; cualquier botón la cierra | Fase `description`; cualquier botón la cierra | Ruta `bank` | Igual |
| Saltar al principio o al final (Mayús con arriba o abajo, R1 en el mando) | R1 mantenido con arriba o abajo | La acción `walk` mantenida | — | Igual |
| Ayuda bajo las ventanas | `updateKeeperTextbox`: línea 3 de MAPHEAD con los glifos ✕, ○ y □ | Tres líneas del paquete con la tecla del dispositivo en uso (`ui.prompt`) en los colores del original | Ruta `bank` | Mejora documentada |
| Repetición al mantener confirmar o alt | La máscara de repetición del original repite arriba, abajo, confirmar y alt | Arriba y abajo repiten (la navegación de la UI); confirmar y alt no | — | Hueco |
| Lo guardado se guarda en la partida | `SCRIPT_STATE` (0x54 + objeto), dentro de la partida | Sección `item_keeper` de la partida: `kept: [{item, count}]`; sin ella, nada guardado | `what the keeper holds goes in the save…` | Igual |
