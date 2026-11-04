# input

Entrada por acciones ([ADR 0014](../../adr/0014-mandos-y-entrada.md)). El juego pregunta por acciones (`confirm`, `move_left`…), nunca por teclas o botones. `ActionMap` dice qué entradas producen cada acción; `InputState` guarda su valor en cada tick y qué dispositivo se usó por última vez.

En el campo se corre por defecto, como en el original; `walk` (Mayúsculas, o en el mando el botón superior derecho R1) se mantiene para andar. El cuadrado del mando ya no anda: es solo `extra`, así que mantenerlo no mueve al jugador (docs/architecture/controles.md).

`confirm` tiene dos teclas de teclado: **Espacio** (la que muestran los avisos) e **Intro**, y se añade **F** como alias moderno de «usar/interactuar», junto a `WASD`; en el mando es el Sur. En la caja de nombres `F` escribe «f» en lugar de confirmar (el texto va antes que las acciones), así que allí se confirma con Intro.

`alt` y `extra` son los otros dos botones frontales del mando original (○ y □; `dw/input.h: ALT_BUTTON` y 0x80). En el mando `alt` va en el botón norte, porque el este es `cancel`, y `extra` en el oeste; en el teclado usan **`E`** (`alt`) y **`Q`** (`extra`), ambas dentro del racimo `WASD`. En la caja de nombres esas letras se escriben en lugar de disparar la acción (el texto se procesa antes, `naming_box.cpp:137`), así que los atajos «Atrás» y «borrar todo» solo están disponibles en el mando; en teclado se cubren con las flechas del grid y `Retroceso`. Ver [Controles](../../architecture/controles.md).

`defaultActionMap` trae también acciones de las vistas de desarrollo (`dwe-game --map`): `debug_time_day`, `debug_time_dusk` y `debug_time_night` (teclas 1, 2 y 3) cambian la hora del día del mapa y `debug_next_map` (N) pasa al siguiente mapa de la lista. `report_bug` (F12) guarda un [reporte de bug](../../guides/prueba-vertical.md#reportar-un-bug-f12) de la partida.

`menu` (Tab, o el botón norte en el mando) abre el menú de pausa en el campo. **En batalla abre directamente la bolsa del domador** (lo que hoy se lanzaba con `cancel`): el mismo botón, misma acción, contexto distinto (docs/architecture/controles.md).

`settings` (`Esc` o `F10` en teclado, Select/Back en el mando, que el original no usaba en el campo) abre el menú de
pausa directamente en su página de opciones, también desde la pausa de Start, y la cierra si ya estaba abierto en ella
([opciones en partida](../apps/game.md#opciones-en-partida)). `Esc` vino del `cancel`, que pasó al par `Ctrl` (con
`Retroceso`, el clic derecho y el Este del mando como estaba): ver [Controles](../../architecture/controles.md).
