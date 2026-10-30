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
const auto* course = rules->courseOfChoice(map, choice);   // el mapa y la variable 254 eligen el curso
dwe::game::gym::CourseRun run{*rules, *course, withSlots};   // la variable 246 pone la ruleta
run.tick(ports, seconds, frameSeconds, {.confirm = pressed, .cancel = pressed, .cancelHeld = held});
if (run.finished()) { ... }                                // TRAINING_COMPLETE: el guion sigue
```

- **Sitios** (`sites` en `gym.yaml`): cada mapa con cursos dice su sitio de ganancias (`trn` o `trn2`) y si cuenta
  la práctica. El gimnasio del pueblo (0x70) tiene los seis; el resto de mapas, uno o dos: 0x4e (ataque y velocidad) y
  0x77 (inteligencia con maestro) del primer módulo, y 0x63, 0x6b, 0x6c y 0xa5 del segundo (`src/trn2`).
- **Cursos** (`CourseRules`): lo común (puesto, cámara, duración de la sesión, descanso, sonidos y animaciones) y la
  coreografía propia de cada uno (`CourseDetail`): HP (el tronco que golpea), MP (la cascada), ataque (el saco, o el
  golpe que cambia objetos del mapa), defensa (el tronco que oscila, la guardia con golpes o el objeto que cae),
  velocidad (la carrera, o la cinta) e inteligencia (la pizarra con el domador o con un maestro).
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
  igual que un guion: caminan a la casilla del puesto a la velocidad de su clip, giran, animan y la
  cámara se desplaza al puesto en los cuadros del curso. Las nubes y
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
| El clip del partner (y el del domador en inteligencia) sigue mientras entrena: la marca 2 de la animación solo quita su movimiento de raíz, que un clip de guion nunca tiene aquí; antes se congelaba entre golpes | `anim.c: applyRootMomentum` (único lector de `animFlag & 2`); `trn_hp.c`, `trn_off.c`, `trn_brain.c` | `FieldScene` (clip de guion sin movimiento de raíz); sin `holdAnimation` | `course_flow_tests` | Igual |
| El jugador mira el entrenamiento: pone su clip 10 al empezar y gira hacia el partner cada cuadro hasta acabar; en inteligencia no, porque ese curso lo guía | `tamer.c: tamerTickTraining` (`tamerSetState(8)`); `trn_brain.c` (`tamerSetState(-1)`) | `gym.yaml: watch_animation`; `CourseRun::frame` | `course_flow_tests`: "gym.md watching…" | Igual |
| Inteligencia: al llegar a su sitio solo el jugador se gira hacia el partner; el partner se gira al echar a andar a su pupitre | `trn_brain.c: TRN_tickBrainsTraining`, estado 1 | `CourseRun::brainFrame` | — | Igual |
| Cada mapa abre sus cursos: 0x70 los seis; 0x4e ataque (254 = 1) y velocidad (2); 0x77 inteligencia (5); 0x63 HP (0) y defensa (3); 0x6b HP (0) y ataque (1); 0x6c velocidad (2) y defensa (3); 0xa5 MP (4) | rutina 7; `TRN_setup*Training`, `TRN2_setup*Training`; guiones 075, 095, 103, 104, 108, 115, 159 | `gym.yaml: sites`; `GymRules::courseOfChoice(map, choice)` | `course_flow_tests`: "gym.md sites: routine 7 picks…" | Igual |
| Los cursos del segundo módulo (0x63, 0x6b, 0x6c, 0xa5) ganan con la tabla `trn2`; los de 0x4e y 0x77, con `trn` | `trn2_reward.c`, `trn_reward.c` | `gym.yaml: sites[].place`, `gains` | `course_flow_tests`: "gym.md sites…" | Igual |
| El contador de práctica solo sube en el gimnasio del pueblo (0x70) | `trn_reward.c` (`upgrade*Counter` con `mapId == 0x70`) | `counts_practice`; `TrainingRequest::countsPractice` | `partner_tests` | Igual |
| Velocidad en 0x4e: al reiniciar la carrera el partner vuelve a z 2000 (no a una x) | `TRN_tickSpeedTraining` (mapa 0x4e) | `SpeedCourse::restartAxis` | `course_flow_tests`: "…restart along z" | Igual |
| Ataque en 0x4e y 0x6b: cada golpe esconde y muestra objetos del mapa, reinicia su animación y los deshace 46 cuadros después | `TRN_tickOffenseTraining`, `TRN2_tickOffenseTraining` | `OffenseSwap`; `CourseRun::offenseSwapFrame` | `course_flow_tests`: "offense swap…" | Igual |
| Inteligencia en 0x77: enseña el maestro del mapa (primer NPC), que repite su clip y su sonido cada 18 cuadros; todos se miran al llegar | `TRN_tickBrainsTraining` (mapa 0x77) | `BrainTeacher`; `GymScreen::Ports::animateTeacher` | `course_flow_tests` | Igual |
| Defensa en 0x6c: guardia; cada pocos cuadros un golpe con partículas y sonido; solo el estado del golpe mira cancelar | `trn2_def_map108.c: TRN2_tickDefenseTrainingMap108` | `DefensePulseCourse`; `CourseRun::defensePulseFrame` | `course_flow_tests` | Igual |
| Defensa en 0x63: el objeto cae hasta su fila límite, golpea, se recupera y vuelve a caer; solo los cuadros de caída cuentan | `trn2_def_map99.c: TRN2_tickDefenseTrainingMap99` (`moveMapObjectsWithLimit`) | `DefenseDropCourse`; `CourseRun::defenseDropFrame` | `course_flow_tests`: "only falling frames count…" | Igual |
| Velocidad en 0x6c: la cinta; el partner corre sin avanzar (animFlag 2), con un sonido en bucle que se para al acabar, y sale a (247, 965) | `trn2_reward.c: TRN2_tickSpeedTraining` | `TreadmillCourse`; `CourseRun::treadmillFrame` | `course_flow_tests`: "treadmill…" | Igual |
| `TRN2_func_8008AA84` (resultado -2 de la ruleta) no se alcanza, como en el primer módulo | `trn2_reward.c: TRN2_func_8008AA84` | sin puerto | — | Igual |
| Inteligencia: cada orden cruzada escribe sus palabras del disco en la primera fila de la caja (-88, 18, 176, 48), encima de la anterior, en amarillo (color 7); la de 300 añade "Change target!" en la segunda; "New orders!" va debajo (fila 18, o 34 con dos líneas) en el color normal | `trn_reward.c: TRN_awardBrainsTrainingGains`, `TRN_renderNewOrdersBox` (`reward_messages`) | `orderRows`; `GymScreen::teach` | `brain_learning_tests`: "an order's words take the first row…" | Igual |
| Técnica aprendida: el nombre en la primera fila y "was mastered!" en la segunda, en la caja de 35 de alto | `trn_reward.c: TRN_tryLearnMove`, `TRN_renderMoveLearnedBox` (`reward_mastered`) | `GymScreen::learnMove` | captura pendiente | Suposición: el original parte el texto en 12 + 48 px y pinta el nombre con la CLUT 3, sin medir |
