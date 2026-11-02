# input

Entrada por acciones ([ADR 0014](../../adr/0014-mandos-y-entrada.md)). El juego pregunta por acciones (`confirm`, `move_left`…), nunca por teclas o botones. `ActionMap` dice qué entradas producen cada acción; `InputState` guarda su valor en cada tick y qué dispositivo se usó por última vez.

En el campo se corre por defecto, como en el original; `walk` (Mayúsculas, o en el mando el botón oeste o el botón superior derecho) se mantiene para andar.

`alt` y `extra` son los otros dos botones frontales del mando original (`ALT_BUTTON`, el círculo en la versión americana, y el cuadrado). En el mando, `alt` va en el botón norte, porque el este es `cancel`, y `extra` en el oeste (compartido con `walk`); en el teclado, Inicio y Supr, que no escriben texto y por eso funcionan también en la caja de nombres, donde `alt` lleva el cursor a Atrás y `extra` borra todo el nombre.

`defaultActionMap` trae también acciones de las vistas de desarrollo (`dwe-game --map`): `debug_time_day`, `debug_time_dusk` y `debug_time_night` (teclas 1, 2 y 3) cambian la hora del día del mapa y `debug_next_map` (N) pasa al siguiente mapa de la lista. `report_bug` (F12) guarda un [reporte de bug](../../guides/prueba-vertical.md#reportar-un-bug-f12) de la partida.

`settings` (F10, o Select en el mando, que el original no usaba en el campo) abre el menú de pausa directamente en su página de opciones, también desde la pausa de Start ([opciones en partida](../apps/game.md#opciones-en-partida)).
