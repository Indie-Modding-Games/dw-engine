# input

Entrada por acciones ([ADR 0014](../../docs/adr/0014-mandos-y-entrada.md)). El juego pregunta por acciones (`confirm`, `move_left`…), nunca por teclas o botones. `ActionMap` dice qué entradas producen cada acción; `InputState` guarda su valor en cada tick y qué dispositivo se usó por última vez.

En el campo se corre por defecto, como en el original; `walk` (Mayúsculas, o en el mando el botón oeste o el botón superior derecho) se mantiene para andar.

`defaultActionMap` trae también acciones de las vistas de desarrollo (`dwe-game --map`): `debug_time_day`, `debug_time_dusk` y `debug_time_night` (teclas 1, 2 y 3) cambian la hora del día del mapa y `debug_next_map` (N) pasa al siguiente mapa de la lista.
