# Controles

Qué acciones ofrece el motor, qué las dispara en cada dispositivo y cómo se usan según el contexto. Es la
referencia del mapa de acciones (`defaultActionMap`) y de los comportamientos por documento. La bolsa en batalla
se abre con `menu` (igual que en campo) y el teclado está en el racimo `WASD`; `Esc` abre las opciones y `cancel` va en
`Ctrl` ([más abajo](#esc-abre-las-opciones-cancel-va-en-ctrl)).

## Principios

Un mismo botón físico debe significar siempre lo mismo dentro de una familia:

- **`confirm`** — aceptar / elegir / avanzar. Nunca abre menús nuevos.
- **`cancel`** — volver / cerrar / deshacer. Nunca abre nada.
- **`menu`** — el menú del contexto. En campo, el menú de pausa completo (bolsa, compañero, opciones, guardar,
  salir). En batalla, el menú de batalla: solo bolsa y opciones, sin guardar ni salir.
- **`pause`** — congela el tiempo. Está disponible donde hay una partida en marcha.
- **`walk`** — modificador mantenido. No debe compartir botón con ninguna acción pulsada.
- **`alt` / `extra`** — acciones secundarias ligadas al mando original (○ y □). Solo se usan donde el contexto
  las lee. En el mando `alt` comparte el norte con `menu` (en la pesca y la tienda de cartas se lee mantenido
  —`isDown`— y ahí `menu` no abre nada); es una excepción consciente. En el teclado están en el racimo `WASD`,
  fuera de `Inicio`/`Supr`.

Además, dos reglas prácticas para el teclado:

- **Teclas modernas, jugables con una o dos manos.** Las acciones frecuentes van en el racimo de `WASD` y sus
  vecinos (`Q` `E` `R` `F` `C` `V` `X` `Z`, `Tab`, `Espacio`, `Shift`, `Ctrl`), no en filas de función ni en teclas
  de navegación del documento (`Inicio`, `Fin`, `Supr`, `RePág`).
- **Cuidado con las teclas que escriben texto.** El mapa de acciones usa teclas sueltas, sin modificadores. La caja
  de nombres (`naming_box.cpp`) procesa el texto **antes** que las acciones (`line 137`: *"its keys are also the
  movement and confirm actions"*), así que una letra asignada a una acción **se escribirá** al nombrar y no
  disparará la acción en esa pantalla. Por eso `E`/`Q` no
  disparan `alt`/`extra` en esa caja (ver la nota bajo el mapa).

Los avisos en pantalla se generan a partir de la acción (`ui::promptControl`), no del botón, así que reasignar
no rompe las pistas del jugador. El dispositivo usado por última vez decide el glifo que se muestra. Un
modificador asignado a sus dos lados (cancelar con los dos Ctrl, caminar con los dos Mayús) se nombra como el par:
el aviso dice «Ctrl», no «Ctrl izq.», que además cabe en las filas de avisos estrechas (la del árbol de evolución).

## Mapa actual de acciones

Fuente única: `packages/engine/input/src/action_map.cpp:defaultActionMap`. La primera vinculación de una acción es la
que muestran los avisos.

| Acción | Teclado | Ratón | Mando | Uso |
|---|---|---|---|---|
| `confirm` | Espacio, Intro, **F** | Clic izq. | Sur | Elegir comando (batalla), aceptar, avanzar cajas de texto, clic del elemento enfocado. `F` es el alias de «usar/interactuar» junto a `WASD`; `Espacio` es el que muestran los avisos. |
| `cancel` | Ctrl izq./der., Retroceso | Clic der. | Este | Volver / cerrar. `Esc` se liberó para `settings` (el menú clásico de pausa del PC); los avisos muestran el Ctrl. |
| `menu` | Tab | — | Norte | Menú de pausa en campo; **en batalla abre la bolsa**. |
| `pause` | P | — | Start | Congela el tiempo; la caja de pausa se abre también desde batalla. Se quitó la tecla multimedia «Pausa». |
| `settings` | Esc, F10 | — | Back | Opciones en partida: abren el menú de pausa directo en su página de opciones, y lo cierran si ya estaba ahí. El aviso bajo la caja de pausa nombra la tecla del dispositivo. |
| `world_map` | M | — | LeftShoulder | Mapa de la isla. Mejora de dwe; sin uso en el original. |
| `move_left/right/up/down` | A/D/W/S o flechas | — | Cruceta o stick izq. | Movimiento del jugador y navegación de la UI. |
| `walk` | Mayús. izq./der. | — | **RightShoulder (R1)** | Mantener para andar en vez de correr. Ya no comparte el oeste con `extra`. |
| `alt` | **E** | — | **Norte (△)** | Botón ○ del original. Baja la cámara durante la pesca, comprar 10 en la tienda de cartas, cursor a «Atrás» en la caja de nombres (mando). Comparte norte con `menu`. |
| `extra` | **Q** | — | Oeste (□) | Botón □ del original. Dispara el remate en batalla; borra todo en la caja de nombres (mando); avanza fases en tiendas. |
| `tab_previous` / `tab_next` | **Q** / **E** | — | LeftShoulder (L1) / RightShoulder (R1) | Pestaña anterior / siguiente de una página con pestañas (las [opciones en partida](../modules/apps/game.md#opciones-en-partida)); llegan como `NavCommand::TabPrevious`/`TabNext` y una página sin pestañas no las usa. Comparten tecla con `extra`/`alt` y botón con `world_map`/`walk`, que con el menú abierto no actúan. |
| `help` | H | — | Gatillo izq. | Reglas del minijuego en pantalla (el trabajo en la tienda: abre y cierra su panel mientras se ve su barra). Mejora de dwe; F1 es de la consola. |
| `trade` | T | — | RightStick (R3) | Solo en cooperativo: pide un intercambio de objetos al jugador más cercano a su alcance ([multijugador, §6.2](multiplayer.md)). La pista bajo la etiqueta de ese jugador nombra la tecla. |
| `debug_console` | ` o F1 | — | — | Consola de desarrollo. |
| `report_bug` | F12 | — | — | Reporte de bug de la partida. |
| `debug_time_day/dusk/night` | 1 / 2 / 3 | — | — | Vistas de desarrollo (`dwe-game --map`). |
| `debug_next_map` | N | — | — | Vistas de desarrollo. |

> **Teclado y la caja de nombres.** `E`/`Q` son letras, así que al escribir un nombre se teclean y no disparan
> `alt`/`extra` (el texto se procesa antes que las acciones, `naming_box.cpp:137`); esos atajos quedan solo en el
> mando.

Los botones del mando se identifican por posición (`south`, `east`, `west`, `north`, `left_shoulder`,
`right_shoulder`, `start`, `back`, `dpad_*`, stick). El glifo que ve el jugador (X/Cross, ○/Circle, □/Square,
△/Triangle, A/B/X/Y, etc.) lo decide la familia detectada en `packages/engine/ui/src/prompts.cpp`.

## Cómo se entrega cada acción al juego

Hay dos caminos. El mapa completo importa para entender dónde hay que tocar cuando cambiamos una asignación.

### 1. La UI (documentos modales)

`packages/engine/ui/src/ui.cpp:Ui::takeCommands` convierte las acciones `move_left/right/up/down`, `confirm`, `cancel`
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
- `cancel` **no** abre la bolsa: se consume
  porque si no, el `cancel` por defecto cerraría el documento de batalla y la terminaría.
- `extra` (**Q**) dispara el remate (`BattleScreen::pressFinisher`) cuando el medidor está lleno.
- `up`/`down` se consumen para navegación y no llegan a abrir la pausa.
- El documento de batalla no responde durante `intro`, `victory` ni `closing` (las poses y el cierre).

## `Esc` abre las opciones; `cancel` va en `Ctrl`

El PC clásico usa `Esc` para el menú de pausa; aquí `Esc` es la acción `settings` — el menú de opciones en partida, que
desde la caja de pausa de Start o desde el campo abre el `pause_menu` directo en su página «Opciones», y lo cierra si ya
estaba abierto en ella. Para que un pulso no dispare dos acciones a la vez, `cancel` va en el par `Ctrl` (izquierdo y
derecho, como los dos `Shift` del `walk`); también valen `Retroceso`, el clic derecho y el Este del mando. Los avisos
`{prompt:cancel}` dibujan la cap «CTRL» en teclado, y la pista de la caja de pausa («Esc Opciones») se muestra también en
teclado.
