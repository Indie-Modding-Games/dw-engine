# Controles

Qué acciones ofrece el motor, qué las dispara en cada dispositivo y cómo se usan según el contexto. Es la
referencia del mapa de acciones (`defaultActionMap`) y de los comportamientos por documento. El re-mapeo
moderno está **aplicado** (2026-11-04): la bolsa en batalla se abre con `menu` (igual que en campo), el
teclado se ha movido al racimo `WASD`, y las secciones *Inconsistencias*, *Modelo propuesto* y *Fases*
quedan como registro histórico de por qué se hicieron esos cambios. La
[reasignación de 2026-11](#reasignación-2026-11-esc-abre-las-opciones-cancel-pasa-al-ctrl) (`Esc` abre las opciones,
`cancel` pasa al `Ctrl`) también está aplicada.

## Principios

Un mismo botón físico debe significar siempre lo mismo dentro de una familia:

- **`confirm`** — aceptar / elegir / avanzar. Nunca abre menús nuevos.
- **`cancel`** — volver / cerrar / deshacer. Nunca abre nada: si hoy abre la bolsa en batalla, está roto.
- **`menu`** — el menú del contexto. En campo, el menú de pausa completo (bolsa, compañero, opciones, guardar,
  salir). En batalla, el menú de batalla: solo bolsa y opciones, sin guardar ni salir.
- **`pause`** — congela el tiempo. Está disponible donde hay una partida en marcha.
- **`walk`** — modificador mantenido. No debe compartir botón con ninguna acción pulsada.
- **`alt` / `extra`** — acciones secundarias ligadas al mando original (○ y □). Solo se usan donde el contexto
  las lee. En el mando `alt` comparte el norte con `menu` (en la pesca y la tienda de cartas se lee mantenido
  —`isDown`— y ahí `menu` no abre nada); es una excepción consciente. En el teclado pasan al racimo `WASD`
  (ver *Teclado moderno*), fuera de `Inicio`/`Supr`.

Además, dos reglas prácticas para el teclado:

- **Teclas modernas, jugables con una o dos manos.** Las acciones frecuentes van en el racimo de `WASD` y sus
  vecinos (`Q` `E` `R` `F` `C` `V` `X` `Z`, `Tab`, `Espacio`, `Shift`, `Ctrl`), no en filas de función ni en teclas
  de navegación del documento (`Inicio`, `Fin`, `Supr`, `RePág`).
- **Cuidado con las teclas que escriben texto.** El mapa de acciones usa teclas sueltas, sin modificadores. La caja
  de nombres (`naming_box.cpp`) procesa el texto **antes** que las acciones (`line 137`: *"its keys are also the
  movement and confirm actions"*), así que una letra asignada a una acción **se escribirá** al nombrar y no
  disparará la acción en esa pantalla. Por eso `Home`/`Delete` servían antes de `alt`/`extra`. La solución está en
  *Teclado moderno*.

Los avisos en pantalla se generan a partir de la acción (`ui::promptControl`), no del botón, así que reasignar
no rompe las pistas del jugador. El dispositivo usado por última vez decide el glifo que se muestra. Un
modificador asignado a sus dos lados (cancelar con los dos Ctrl, caminar con los dos Mayús) se nombra como el par:
el aviso dice «Ctrl», no «Ctrl izq.», que además cabe en las filas de avisos estrechas (la del árbol de evolución).

## Mapa actual de acciones

Fuente única: `engine/input/src/action_map.cpp:defaultActionMap`. La primera vinculación de una acción es la
que muestran los avisos.

| Acción | Teclado | Ratón | Mando | Uso |
|---|---|---|---|---|
| `confirm` | Espacio, Intro, **F** | Clic izq. | Sur | Elegir comando (batalla), aceptar, avanzar cajas de texto, clic del elemento enfocado. `F` es el alias de «usar/interactuar» junto a `WASD`; `Espacio` es el que muestran los avisos. |
| `cancel` | Ctrl izq./der., Retroceso | Clic der. | Este | Volver / cerrar. `Esc` se liberó para `settings` (el menú clásico de pausa del PC); los avisos muestran el Ctrl. |
| `menu` | Tab | — | Norte | Menú de pausa en campo; **en batalla abre la bolsa** (antes era `cancel`). |
| `pause` | P | — | Start | Congela el tiempo; la caja de pausa se abre también desde batalla. Se quitó la tecla multimedia «Pausa». |
| `settings` | Esc, F10 | — | Back | Opciones en partida: abren el menú de pausa directo en su página de opciones, y lo cierran si ya estaba ahí. El aviso bajo la caja de pausa nombra la tecla del dispositivo. |
| `world_map` | M | — | LeftShoulder | Mapa de la isla. Mejora de dwe; sin uso en el original. |
| `move_left/right/up/down` | A/D/W/S o flechas | — | Cruceta o stick izq. | Movimiento del jugador y navegación de la UI. |
| `walk` | Mayús. izq./der. | — | **RightShoulder (R1)** | Mantener para andar en vez de correr. Ya no comparte el oeste con `extra`. |
| `alt` | **E** | — | **Norte (△)** | Botón ○ del original. Baja la cámara durante la pesca, comprar 10 en la tienda de cartas, cursor a «Atrás» en la caja de nombres (mando). Comparte norte con `menu`. |
| `extra` | **Q** | — | Oeste (□) | Botón □ del original. Dispara el remate en batalla; borra todo en la caja de nombres (mando); avanza fases en tiendas. |
| `tab_previous` / `tab_next` | **Q** / **E** | — | LeftShoulder (L1) / RightShoulder (R1) | Pestaña anterior / siguiente de una página con pestañas (las [opciones en partida](../modulos/apps/game.md#opciones-en-partida)); llegan como `NavCommand::TabPrevious`/`TabNext` y una página sin pestañas no las usa. Comparten tecla con `extra`/`alt` y botón con `world_map`/`walk`, que con el menú abierto no actúan. |
| `debug_console` | ` o F1 | — | — | Consola de desarrollo. |
| `report_bug` | F12 | — | — | Reporte de bug de la partida. |
| `debug_time_day/dusk/night` | 1 / 2 / 3 | — | — | Vistas de desarrollo (`dwe-game --map`). |
| `debug_next_map` | N | — | — | Vistas de desarrollo. |

> **Teclado y la caja de nombres.** `E`/`Q` son letras, así que al escribir un nombre se teclean y no disparan
> `alt`/`extra` (el texto se procesa antes que las acciones, `naming_box.cpp:137`); esos atajos quedan solo en el
> mando. Ver *Teclado moderno*.

Los botones del mando se identifican por posición (`south`, `east`, `west`, `north`, `left_shoulder`,
`right_shoulder`, `start`, `back`, `dpad_*`, stick). El glifo que ve el jugador (X/Cross, ○/Circle, □/Square,
△/Triangle, A/B/X/Y, etc.) lo decide la familia detectada en `engine/ui/src/prompts.cpp`.

## Cómo se entrega cada acción al juego

Hay dos caminos. El mapa completo importa para entender dónde hay que tocar cuando cambiamos una asignación.

### 1. La UI (documentos modales)

`engine/ui/src/ui.cpp:Ui::takeCommands` convierte las acciones `move_left/right/up/down`, `confirm`, `cancel`
y `menu` en comandos de navegación (`up`, `down`, `left`, `right`, `confirm`, `cancel`, `menu`) y los entrega
al documento modal que esté arriba mediante su `on_action(document, action)`. Si devuelve `true`, la UI no
aplica la acción por defecto (mover el foco, confirmar el enfocado, volver, abrir el menú de juego).

Acceso directo a la bolsa y a las cajas por fuera de la UI: las pantallas de tienda, `naming_box`,
`item_keeper_screen`, `discard_screen`, etc. leen acciones concretas (`wasPressed("alt")`,
`wasPressed("extra")`, `isDown("alt")`) en su `tick`.

### 2. El juego y la batalla

En campo:

- `menu` abre el documento de pausa completo (`pause_menu`), con sus páginas: compañero, bolsa, opciones,
  guardar, salir.
- `pause` (Start) muestra la caja de pausa simple; `settings` (`Esc` o `F10` en teclado, Select en mando) salta
  directo a la página de opciones, y la cierra si ya estaba abierto en ella.
- `cancel` (`Ctrl` en teclado, Este en mando) cierra el menú.

En batalla (`content/packs/game/ui/battle/logic.lua:on_action` y `apps/game/src/play_session.cpp:pressBattle`):

- `confirm` elige el comando enfocado, carga el medidor del remate mientras dura el aura (fase `charging`) y
  avanza las cajas de texto del resultado.
- `menu` **abre la bolsa** del domador (`open_bag`); en la caja final, `confirm` y `cancel` avanzan el texto.
- `cancel` **no** abre la bolsa (eso lo hacía antes, era la fuente de la inconsistencia nº1): se consume
  porque si no, el `cancel` por defecto cerraría el documento de batalla y la terminaría.
- `extra` (Supr antes, ahora **Q**) dispara el remate (`BattleScreen::pressFinisher`) cuando el medidor está lleno.
- `up`/`down` se consumen para navegación y no llegan a abrir la pausa.
- El documento de batalla no responde durante `intro`, `victory` ni `closing` (las poses y el cierre).

## Inconsistencias detectadas

1. **La bolsa se abre con `cancel` en batalla y con `menu` en campo.** El jugador asocia Esc a «volver», y en
   batalla hace lo contrario. Es la causa principal de la confusión.
2. **`menu` no hace nada en batalla**, aunque en campo abre el menú completo. Rompe el principio del menú
   contextual.
3. **`walk` comparte el botón oeste con `extra`.** Mantener □ anda, pulsarlo dispara el remate; una pulsación
   larga del remate anda sin querer. Es un conflicto real, porque ambos se usan en el mismo contexto (batalla y
   campo).
4. **`alt` comparte el botón norte con `menu`.** Es un conflicto *suave*: `alt` solo se lee mantenido (`isDown`)
   y en contextos modales (pesca, tienda de cartas) donde `menu` no tiene efecto. Aun así obliga a recordar por
   qué no se pisan.
5. **`settings` (F10 / Back) no tiene efecto en batalla**, aunque las opciones del motor (volumen, aspecto,
   controles) tienen sentido ahí igual que en campo.
6. **El teclado usa teclas de documento, no de juego.** `extra` va en `Supr`, `alt` en `Inicio`, `settings` en `F10`
   y `pause` también en la tecla `Pause`. Son teclas lejanas al racimo `WASD` y difíciles de pulsar con una mano; un
   jugador de PC espera el ataque especial en `Q`/`E`/`F`/`R`, no en `Supr`.

## Modelo normalizado propuesto

Tres ajustes, en orden de impacto: mover la apertura de la bolsa en batalla de `cancel` a `menu`, quitar la colisión
real `walk`/`extra` del cuadrado del mando, y pasar el teclado al racimo `WASD` para jugar con una o dos manos sin
teclas raras.

### Reglas por contexto

| Contexto | `confirm` | `cancel` | `menu` | `extra` | `pause` | `walk` | `settings` |
|---|---|---|---|---|---|---|---|
| Campo | elegir / avanzar | cerrar menú, volver | abrir menú de pausa completo | (ninguna) | caja de pausa | mantener para andar | abrir opciones directamente |
| Batalla | elegir comando / cargar remate durante `charging` / avanzar caja final | cerrar la bolsa abierta; sin bolsa, no-op | abrir la **bolsa de lanzamiento** (lo que hoy hace `cancel`) | remate si el medidor está lleno | caja de pausa con bolsa + opciones, **sin guardar ni salir** | mantener para andar (Mayús. + R1) | abrir opciones (seguimiento opcional) |
| Menú modal (pausa, compañero, tienda, mapa, caja de nombres, avisos) | aceptar el enfocado / cerrar aviso | volver al nivel anterior o cerrar | cerrar (salvo caja de nombres, donde mueve a «Atrás») | según la pantalla | caja de pausa | (no aplica) | (no aplica) |
| Pesca | (la pantalla propia) | cerrar | (sin efecto) | (sin efecto) | caja de pausa | (sin efecto, es el `alt` mantenido el que baja la cámara) | (sin efecto) |

### Cambios en el mapa

- **`walk`**: quitar el botón oeste del mando. Se queda en Mayús. (izq./der.) + RightShoulder (R1).
- **`cancel` en batalla**: deja de abrir la bolsa.
- **`menu` en batalla**: abre la bolsa, el mismo `battle_bag_open` que hoy se lanza con `cancel`.
- **Teclado moderno**: las acciones frecuentes pasan al racimo de `WASD` (ver *Teclado moderno*). `extra` pasa de
  `Supr` a `Q`, `alt` de `Inicio` a `E` (en el mando `alt` sigue en Norte, compartido con `menu`), `settings` se
  queda sin atajo de teclado y `pause` pierde la tecla multimedia.
- **`pause` en batalla**: la caja de pausa se limita a las páginas «Opciones» y «Bolsa», sin «Guardar» ni «Salir». Se
  marca desde el juego con un contexto en `pause_menu` (`pause_menu.context = battle`).
- **`settings` en batalla** (seguimiento opcional): con el contexto de batalla ya hecho, la tecla de opciones puede
  abrir ese mismo menú directo en «Opciones». No es bloqueante.

### Teclado moderno (una o dos manos)

Las acciones rápidas se mueven al racimo `WASD` para jugar con una mano; las deliberadas (mapa, pausa, opciones)
pueden quedar algo más lejos. Este es el esquema acordado:

| Acción | Actual | Propuesto | Motivo |
|---|---|---|---|
| `move_*` | WASD / flechas | **WASD / flechas** (igual) | Base del racimo. |
| `confirm` | Intro, Espacio | **Espacio, Intro** (igual) | Pulgar y mano derecha. |
| `cancel` | Esc, Retroceso | **Esc, Retroceso** (igual) | Estándar de «volver/borrar». No escriben texto. |
| `menu` (bolsa/menú) | Tab | **Tab** (+ clic) | No escribe texto; pinky arriba de `Q`. |
| `extra` (remate / especial) | **Supr** | **`Q`** | Tecla de habilidad clásica, junto a `WASD`. |
| `alt` (secundario mantenido) | **Inicio** | **`E`** | Interactuar/alternativa; junto a `WASD`. |
| `walk` (mantener) | Mayús. | **Mayús.** (igual) | No escribe texto. |
| `pause` | P, Pausa | **P** (se quita la tecla multimedia «Pausa») | Un solo estándar; muchos teclados no tienen «Pausa». |
| `settings` (opciones) | F10 | **sin atajo de teclado** (solo `Back` en el mando; en teclado se entra por el menú) | Elimina la fila de función; las opciones siempre están a un `Tab`. |
| `world_map` | M | **M** (igual) | Tecla de mapa estándar; apertura deliberada, a dos manos. |
| `report_bug` | F12 | **F12** (igual) | Solo QA; fuera del juego, da igual que esté lejos. |
| `debug_console` / vistas | ` ` / F1 / 1-3 / N | **igual** | Solo desarrollo. |

**Consecuencia en la caja de nombres (resuelta con la opción A).** Al poner `extra`=`Q` y `alt`=`E`, al escribir el
nombre esas letras **se teclean y no disparan la acción**, porque `takeTyping` corre antes que `pressButtons`
(`naming_box.cpp:137`). Se pierden en teclado los dos atajos que hoy dan `Supr`/`Inicio` (borrar todo y cursor a
«Atrás»), y se cubren así: borrar letra a letra con `Retroceso` (`cancel`), llegar a «Atrás» y «Aceptar» con las
flechas del grid y `Intro` (`confirm`). **No hace falta tocar `naming_box.cpp`**: el orden de prioridades ya lo hace.
En el **mando** los atajos se conservan porque sus botones no escriben texto (Oeste □ = borrar todo, Norte △ =
«Atrás»); ahí `Q`/`E` no existen.

### Mapa de acciones resultante (por dispositivo)

Respecto al mapa actual cambian: `walk` pierde el oeste del mando; en batalla la bolsa pasa de `cancel` a `menu`; y
el teclado pasa al racimo `WASD` (`extra`=`Q`, `alt`=`E`, `settings` sin atajo).

| Acción | Teclado | Ratón | Sur | Este | Norte | Oeste | Start | Back | L.S. | R.S. | Cruceta | Stick izq. |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| `confirm` | Espacio, Intro, F | Clic izq. | ✓ | | | | | | | | | |
| `cancel` | Esc, Retroceso | Clic der. | | ✓ | | | | | | | | |
| `menu` | Tab | | | | ✓ | | | | | | | |
| `pause` | P | | | | | | ✓ | | | | | |
| `settings` | (sin tecla) | | | | | | | ✓ | | | | |
| `world_map` | M | | | | | | | | ✓ | | | |
| `walk` | Mayús. | | | | | | | | | ✓ | | |
| `extra` | Q | | | | | ✓ | | | | | | |
| `alt` | E | | | | ✓ | | | | | | | |
| `move_*` | WASD / flechas | | | | | | | | | | ✓ | ✓ |
| `debug_console` | ` / F1 | | | | | | | | | | | |
| `report_bug` | F12 | | | | | | | | | | | |

## Impacto en el código

Los cambios reales son: quitar una línea del mando (`walk` en oeste), mover la apertura de la bolsa en la batalla,
dar contexto al menú de pausa, y reasignar varias teclas del teclado al racimo `WASD` (con su consecuencia en la caja
de nombres).

### Motor

- `engine/input/src/action_map.cpp`
  - Quitar `map.bind("walk", GamepadButtonBinding{GamepadButton::West});`. `walk` se queda en Mayús. y R1.
  - **Teclado** (valores definitivos):
    - `extra`: quitar `KeyBinding{keys::Delete}`, añadir `KeyBinding{keys::Q}`. En el mando se queda en Oeste.
    - `alt`: quitar `KeyBinding{keys::Home}`, añadir `KeyBinding{keys::E}`. **En el mando `alt` sigue en Norte**
      (compartido con `menu`, como hoy; las lecturas son `isDown` y no se pisan).
    - `pause`: quitar `KeyBinding{keys::Pause}` (la tecla multimedia); se queda en `P`.
    - `settings`: quitar `KeyBinding{keys::F10}`. Se queda **solo** en `Back` del mando; en teclado las opciones se
      abren desde el menú.
    - `confirm`/`cancel`/`menu`/`world_map` **no cambian** (Espacio/Intro, Esc/Retroceso, Tab, M).
  - La **caja de nombres no requiere tocar el motor ni `naming_box.cpp`** (opción A): el texto ya se procesa antes
    que las acciones, así que `Q`/`E` escriben y no disparan; los atajos de teclado desaparecen solos y en el mando
    se conservan.
- `engine/input/tests/input_state_tests.cpp`
  - `walk is held with either shift key or the gamepad's west button` → usar `RightShoulder` en lugar de `West`.
- `engine/input/tests/action_map_tests.cpp`
  - Los casos de `settings` por teclado (`keys::F10`) se quitan o se pasan al `Back` del mando (ya no hay tecla).
- `engine/ui/tests/prompts_tests.cpp`
  - `glyph(KeyboardMouse, ..., "settings").symbol == "F10"` (línea 139): sin tecla de teclado, el glifo pasa a
    `None`/vacío en teclado y sigue siendo `Back` (o su glifo) en mando. Ajustar el test al nuevo comportamiento.
  - `glyph(..., "extra").shape == "square"` (línea 157) sigue igual: en el mando `extra` no se mueve.

### Aviso de «Opciones» en la caja de pausa

`pause_box/logic.lua` muestra `pause_menu.settings_hint` con `{prompt:settings}`. Al no quedar tecla de teclado para
`settings`, en **teclado** ese aviso sale sin tecla (o vacío) y en **mando** sigue saliendo `Back`. Detalle a cuidar al
aplicar: o se oculta la pista de opciones en teclado, o se cambia el texto a uno que no prometa una tecla (por
ejemplo «Opciones en el menú»). Lo decide quien implemente la fase 3.

### Juego (apps/game)

- `apps/game/src/play_session.cpp:pressBattle`
  - Sin cambios: `extra` sigue disparando `pressFinisher` (la acción no cambia, solo su tecla).
- `apps/game/src/game.cpp` (cámara abajo `isDown("alt")`) y `card_shop_screen.cpp` (`tenHeld_ = isDown("alt")`)
  - Sin cambios de código: leen la acción `alt`, que sigue existiendo (`E` en teclado, Norte en mando).

### UI de contenido (content/packs/game/ui)

- `battle/logic.lua:on_action`
  - `action == "menu"` **abre la bolsa** (llama `open_bag()`, que manda `battle_bag_open`), en lugar de
    consumirse sin efecto.
  - `action == "cancel"` deja de abrir la bolsa: si hay una sub-pantalla abierta (la propia bolsa), no la toca
    (la bolsa es otro documento modal y ya consume sus botones); si no, devuelve `false` para que la UI haga lo
    de siempre con «volver».
  - En la caja final (`phase == "end"`), `confirm` y `cancel` siguen avanzando el texto.
  - La última línea pasa de `return action == "menu" or ...` a devolver `true` solo para `up`/`down` (navegación
    consumida), porque `menu` ya se atiende arriba.
- `pause_menu/logic.lua`
  - Soportar un contexto «batalla»: ocultar las páginas «Guardar» y «Salir». Se marca desde el juego con un
    ajuste (`pause_menu.context = battle`) que la lógica del menú lee en `on_show`.
- `partner_menu/logic.lua`
  - Sin cambios estructurales. Es la bolsa; ahora la abre `menu` en batalla en vez de `cancel`.

### Autotests

- La tabla `Controls` de `apps/game/src/autotest.cpp:1948` usa `Return`/`Escape`/`Tab`/`P` para
  `confirm`/`cancel`/`menu`/`pause`: **todas esas teclas se conservan**, así que no cambia.
- Rutas que abren la bolsa con `east`/`cancel` en batalla: `tests/autotest/route-battle-gamepad.yaml`,
  `route-battle-item-throw.yaml`, `route-battle-recovery-throw.yaml`. Cambiar el botón a `north`/`menu`.
- Las rutas `route-*new*` que escriben un nombre siguen valiendo: con la opción A el `naming_box` no cambia, y sus
  letras `Q`/`E` se teclean igual que antes se tecleaban otras letras del grid.

### Documentación

- `docs/getting-started/jugar.md` — tabla de controles: bolsa en batalla con «Menú», `walk` solo R1 / Mayús., y
  `extra`/`alt` en sus teclas nuevas (`Q`/`E`).
- `docs/modulos/engine/input.md` — actualizar la nota de `alt`/`extra` (ya no `Inicio`/`Supr`) y quitar `walk` del
  oeste.
- `docs/modulos/apps/game.md` — secciones de batalla y menú de pausa, donde hoy se dice que `cancel` abre la bolsa.
- Este documento — pasar de «propuesto» a «aplicado».

## Fases recomendadas

Cada fase es un PR independiente y jugable.

1. **Definir** (este PR): el documento, sin tocar el código. Sirve de referencia para decidir en equipo.
2. **Batalla y mapa de mando** (un PR):
   - Quitar `walk` del oeste en `action_map.cpp` y ajustar `input_state_tests.cpp`.
   - `battle/logic.lua`: la bolsa con `menu` en lugar de `cancel`.
   - `pause_menu`: contexto «batalla» sin «Guardar» ni «Salir».
   - Actualizar las rutas `route-battle-*`.
3. **Teclado moderno** (un PR):
   - `action_map.cpp`: `extra` → `Q` (teclado; Oeste en mando igual), `alt` → `E` (teclado; Norte en mando igual),
     quitar `F10` de `settings` (se queda solo en `Back`) y la tecla multimedia `Pausa`.
   - La caja de nombres (opción A) **no se toca**: `takeTyping` ya hace que `Q`/`E` escriban y no disparen.
   - Decidir el aviso `settings_hint` de `pause_box` en teclado (ocultar o reescribir el texto).
   - Ajustar `action_map_tests.cpp` (caso `settings` por teclado) y `prompts_tests.cpp` (símbolo de `settings`).
4. **Documentación final** (un PR): reflejar lo aplicado en `jugar.md`, `modulos/engine/input.md` y
   `modulos/apps/game.md`, y pasar este documento de «propuesto» a «aplicado».

### Decisiones adoptadas

- `extra` en teclado = **`Q`** (en el mando sigue en Oeste).
- `alt` en teclado = **`E`** (en el mando sigue en Norte, compartido con `menu`).
- `settings`: **sin atajo de teclado** (solo `Back` en el mando); en teclado, opciones desde el menú.
- Caja de nombres: **opción A**, sin tocar `naming_box.cpp`.
- `pause`: se quita la tecla multimedia, se queda `P`.

Queda pendiente solo un detalle menor de UI: cómo se muestra la pista de «Opciones» de la caja de pausa en teclado
sin tecla asignada (decidir en la fase 3). Resuelto después por la reasignación de 2026-11 (abajo).

## Reasignación 2026-11: `Esc` abre las opciones; `cancel` pasa al `Ctrl`

Decisión nueva, posterior al «Teclado moderno». El PC clásico usa `Esc` para el menú de pausa; aquí
`Esc` pasa a la acción `settings` — el menú de opciones en partida, que desde la caja de pausa de
Start o desde el campo abre el `pause_menu` directo en su página «Opciones», y lo cierra si ya estaba
abierto en ella. Para que un pulso no dispare dos acciones a la vez, `cancel` deja `Esc` y se
reúlica al par `Ctrl` (izquierdo y derecho, como los dos `Shift` del `walk`); se conservan además
`Retroceso`, el clic derecho y el Este del mando.

- `action_map.cpp`: `cancel` pierde `Escape` y gana `LeftCtrl`/`RightCtrl` (primera vinculación = la que
  muestran los avisos: el Ctrl); `settings` gana `Escape` y `F10` en teclado, mantiene `Back` en mando.
- `keys.hpp`: constantes nuevas `LeftCtrl = 224`, `RightCtrl = 228` (los códigos HID de la tabla de
  `prompts.cpp`, que ya conocía «CTRL»).
- Los avisos `{prompt:cancel}` salen solos: en teclado dibujan la cap «CTRL», y la pista de la caja de
  pausa («Esc Opciones») vuelve a mostrarse también en teclado (`pause_box/logic.lua` ya no filtra por
  dispositivo) — resuelve el detalle pendiente de la fase 3.
- `device_names.cpp` (control remoto): nombres nuevos `left_ctrl`/`right_ctrl`; `escape` sigue
  nombrando la tecla, que ahora hace `settings`.
- Autotests: la tabla `Controls` de `autotest.cpp` pulsa `LeftCtrl` para `cancel`.
- Tests: `action_map_tests.cpp` (Esc/F10 = `settings`, Ctrl = `cancel`), `prompts_tests.cpp` /
  `prompt_glyphs_tests.cpp` (etiquetas), y los casos de UI que pulsaban `Escape` para volver pasan a
  `LeftCtrl`.
- Sobrescribe la decisión de 2026-10 «`settings`: sin atajo de teclado»: las opciones pasan a estar a
  un `Esc`, no a un `Tab`.
