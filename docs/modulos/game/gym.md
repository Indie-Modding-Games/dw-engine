# gym

El gimnasio del pueblo (`dwe::game::gym`, capa `game`): la coreografía de cada curso de entrenamiento (`src/trn`:
`TRN_setup*Training` y `TRN_tick*Training`), la caja que cuenta las ganancias y lo que aprende el partner en el curso
de inteligencia. Las ganancias en sí son de `game/partner` (la sección `training` del ruleset); lo que hacía el
original está en la [ficha de cuidado](../../reference/care.md#entrenamiento).

No dibuja ni toca el campo: depende de `core`, `assets` y `ruleset`. Los números salen de la sección `gym` del
ruleset (`rules/<id>/gym.yaml`, esquema `content/schemas/ruleset_gym.schema.json`), en fotogramas del original.

## Uso

```cpp
auto rules = dwe::game::gym::readGymRules(ruleset);       // la sección gym; sin ella, ningún curso
const auto* course = rules->courseOfChoice(choice);        // la variable 254 elige el curso
dwe::game::gym::CourseRun run{*rules, *course, withSlots};   // la variable 246 pone la ruleta
run.tick(ports, seconds, frameSeconds, {.confirm = pressed, .cancel = cancel});
if (run.finished()) { ... }                                // TRAINING_COMPLETE: el guion sigue
```

- **Cursos** (`CourseRules`): lo común (puesto, cámara, duración de la sesión, descanso, sonidos y animaciones) y la
  coreografía propia de cada uno (`CourseDetail`): HP (el tronco que golpea), MP (la cascada), ataque (el saco), defensa
  (el tronco que oscila), velocidad (la carrera) e inteligencia (la pizarra con el domador).
- **Ejecución** (`CourseRun`): avanza fotogramas enteros del original con los estados numerados como el
  `TRAINING_STATE` de cada curso. La sesión acaba al cumplir su duración o al cancelar; luego la caja de estadísticas
  (`StatCountup`) cuenta las ganancias, un punto por fotograma, y confirmar o cancelar la termina de golpe.
- **Ruleta** (`SlotMachine`, `trn_slots.c`): con la variable 246 a 1, la ruleta decide cuándo acaba la sesión y el
  ritmo de las ganancias en décimas: el jugador para tres rodillos y la línea paga más con tres sietes o con tres del
  símbolo del curso (`slot_symbol`). Una ayuda oculta, sorteada por sesión, acerca la parada a una pareja o al siete
  (o empuja el último rodillo más allá de un casi acierto). La sección `slots` tiene los rodillos, las ayudas, los
  pagos y los sonidos.
- **Puertos** (`GymPorts`): todo lo que el curso pide al juego (caminar, animar, mover objetos del mapa, nubes,
  partículas, sonidos, cámara y la recompensa). Los implementa `apps/game` (`GymScreen`).
- **Inteligencia** (`brain_learning.hpp`): `brainMilestones` dice qué órdenes nuevas abre la ganancia y si toca
  intentar un movimiento (`TRN_tryLearnMove`); `trainingMove` elige el movimiento por los especiales de la especie y
  sus pesos.
