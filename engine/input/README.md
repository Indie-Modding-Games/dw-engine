# input

Entrada por acciones ([ADR 0014](../../docs/adr/0014-mandos-y-entrada.md)). El juego pregunta por acciones (`confirm`, `move_left`…), nunca por teclas o botones. `ActionMap` dice qué entradas producen cada acción; `InputState` guarda su valor en cada tick y qué dispositivo se usó por última vez.
