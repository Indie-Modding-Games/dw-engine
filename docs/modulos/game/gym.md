# gym

El gimnasio del pueblo (`dwe::game::gym`, capa `game`): la coreografía de cada curso de entrenamiento (`src/trn`:
`TRN_setup*Training` y `TRN_tick*Training`), la caja que cuenta las ganancias y lo que aprende el partner en el curso
de inteligencia. Las ganancias en sí son de `game/partner` (la sección `training` del ruleset); lo que hacía el
original está en la [ficha de cuidado](../../reference/care.md#entrenamiento).

No dibuja ni toca el campo: depende de `core`, `assets` y `ruleset`. Los números salen de la sección `gym` del
ruleset (`rules/<id>/gym.yaml`, esquema `content/schemas/ruleset_gym.schema.json`), en fotogramas del original.
La historia abre un curso con las rutinas 39 (`loadTrainingLibrary`) y 7, y espera a que acabe; en `apps/game` lo
hace `StoryGym` sobre la `GymScreen`.

## Uso

```cpp
auto rules = dwe::game::gym::readGymRules(ruleset);       // la sección gym; sin ella, ningún curso
const auto* course = rules->courseOfChoice(choice);        // la variable 254 elige el curso
dwe::game::gym::CourseRun run{*rules, *course, withSlots};   // la variable 246 pone la ruleta
run.tick(ports, seconds, frameSeconds, {.confirm = pressed, .cancel = pressed, .cancelHeld = held});
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
  Como la historia retiene el campo mientras espera al gimnasio, `GymScreen` mueve a los actores
  igual que un guion: caminan a la casilla del puesto a la velocidad de su clip, giran, animan (o
  congelan el cuadro) y la cámara se desplaza al puesto en los cuadros del curso. Las nubes y
  partículas siguen sin dibujarse (fase 5).
- **Inteligencia** (`brain_learning.hpp`): `brainMilestones` dice qué órdenes nuevas abre la ganancia y si toca
  intentar un movimiento (`TRN_tryLearnMove`); `trainingMove` elige el movimiento por los especiales de la especie y
  sus pesos.

## Paridad con el original

Estado: **Igual** (mismo comportamiento), **Suposición** (lo más cercano que sabemos, sin medir del todo) o **Hueco**
(falta). Medido el 2026-10-30 con el original en DuckStation (capturas en `_work/routes-e2/training-qa/`) a 320 x 240;
las cajas cuentan desde el centro de la pantalla (160, 120).

| Regla | Decomp | Nuestro código | Test | Estado |
|---|---|---|---|---|
| Cancelar (triángulo en el original) **mantenido** acaba la sesión con lo ganado; no abre el menú | `trn_*.c`: `POLLED_INPUT & CANCEL_BUTTON` en cada curso | `GymInput::cancelHeld`; `PlaySession::holdsMenu` | `course_flow_tests`: "holding the original's cancel…" | Igual |
| En la caja de estadísticas solo una pulsación nueva salta la cuenta | `trn_hud.c: TRN_renderPostTrainingStatsBox` | `CourseRun` (`input.confirm \|\| input.cancel`) | `course_flow_tests`: "holding the original's cancel…" | Igual |
| La caja crece desde el partner (nodo 1 ± 5 px) hasta (-94, -78, 188, 96) en 5 cuadros, un cuarto del camino por cuadro, y se encoge igual al cerrar; sonido 0/0 al abrir y 0/1 al cerrar | `ui.c: createAnimatedUIBox`, `renderUIBoxAnimated`; `trn_hud.c: TRN_createPostTrainingStatsBox` | `GrowingBox`; `GymScreen::tickBoxes` | `course_flow_tests`: "the outline moves a quarter…" | Igual |
| Espera de 100 cuadros con la caja en pie y luego un punto por cuadro (el cuadro 100 ya cuenta) | `trn_hud.c` (`MAIN_D_8013539E = 100`) | `StatCountup::tickFrame` | `course_flow_tests`: "it grows five frames, waits 100…" | Igual |
| Filas cada 13 px desde y 9; nombre en x 10 (#FFFF52), valor de 4 cifras alineado a x 68 (#CECECE), barra de 50 px en x 66 con marco 64..117 (#020202 / #666666), fondo #282828 y relleno #787878 (#005A96 / #69C2FF si el stat ganó), separador en x 122 (#90D9FA) y 123 (#020202), flecha en x 130 y ganancia de 3 cifras en x 142 (#31D6D6) | `trn_hud.c: TRN_renderPostTrainingStatsBox` | `ui/gym_stats/structure.rcss`; `skins/original/gym_stats/style.rcss` | captura `sbs_stats_v1.png` | Igual |
| La ruleta crece desde la máquina del curso (un cuadrado de 16 px, 8 a la izquierda y 6 arriba de su punto en pantalla) hasta (-82, -87, 164, 90) | `trn_slots.c: TRN_createSlotMachineBox`, `TRN_getTrainingSpotScreenPos` (`TRN_D_8008F1D4`) | `gym.yaml: machine`; `GymScreen::tickBoxes` | `course_flow_tests`: "slots: the box grows…" | Igual |
| Rodillos centrados en x -41, 0 y 41, ventana de 32 x 64 px de y -74 a -10; dibujos de 32 x 32 (u = (dibujo - 1) x 32, v 216 de la textura del mapa en (704, 0), paleta 3); sin texto de ritmo | `trn_slots.c: TRN_renderSlotMachine` | `ui/gym_slots/`; sprite `gym-slot-symbols` | captura `sbs_slot_spin.png` | Igual |
| El sombreado del rodillo (sprite u 224 v 216 restado) | `trn_slots.c: TRN_renderSlotMachine` | degradado negro hasta 48 % arriba y abajo | captura `sbs_slot_spin.png` | Suposición |
| Al acabar (estado 6) la ruleta se encoge y cierra el cuadro "Slot Game" (`closeBox(0)`); el ritmo llega cuando la caja ya no está | `trn_slots.c: TRN_tickSlotMachine`, `TRN_tickSlotSession` (`UI_BOX_DATA[3]`) | `SlotMachine` estados 6 y 7; `GymPorts::closeTextbox` | `course_flow_tests`: "slots: the box grows…" | Igual |
| La ruleta no tiene cancelar: `TRN_func_800888A0` (resultado -2) no se alcanza porque nada produce -2 | `trn_reward.c: TRN_func_800888A0` | sin puerto | — | Igual |
| Inteligencia: cada orden cruzada escribe sus palabras del disco en la primera fila de la caja (-88, 18, 176, 48), encima de la anterior, en amarillo (color 7); la de 300 añade "Change target!" en la segunda; "New orders!" va debajo (fila 18, o 34 con dos líneas) en el color normal | `trn_reward.c: TRN_awardBrainsTrainingGains`, `TRN_renderNewOrdersBox` (`reward_messages`) | `orderRows`; `GymScreen::teach` | `brain_learning_tests`: "an order's words take the first row…" | Igual |
| Técnica aprendida: el nombre en la primera fila y "was mastered!" en la segunda, en la caja de 35 de alto | `trn_reward.c: TRN_tryLearnMove`, `TRN_renderMoveLearnedBox` (`reward_mastered`) | `GymScreen::learnMove` | captura pendiente | Suposición: el original parte el texto en 12 + 48 px y pinta el nombre con la CLUT 3, sin medir |
