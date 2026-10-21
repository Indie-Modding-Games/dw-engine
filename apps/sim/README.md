# sim

`dwe-sim` (R3.5): juega escenarios de los sistemas del juego muchas veces, sin gráficos, con semillas, y escribe un
informe en Markdown y CSV con la distribución de cada métrica. Uso, opciones y cómo añadir un escenario:
[simulaciones](../../docs/guides/simulaciones.md).

| Fichero | Qué tiene |
|---|---|
| `scenario.hpp` | `Scenario` (`setup`, `run(seed)`, `tables`), `ScenarioRegistry`, `builtInScenarios()` y `ScenarioRegistration` |
| `runner.hpp` | `runScenario`: las partidas repartidas entre hilos, en orden y con su semilla (`seedOfRun`, SplitMix64) |
| `report.hpp` | `summarize`, `percentileOf` y el informe en Markdown y CSV |
| `sim_options.hpp` | La línea de órdenes, los `--set` (`parseValueOverride`, `applyOverrides`) |
| `simulation.hpp` | `simulate`: carga el ruleset, aplica los `--set`, prepara el escenario y lo juega; `writeReport` |
| `headless_session.hpp` | `HeadlessSessions`: partidas sin ventana (`session::GameSession`) para las partidas de un escenario, con el ruleset y los `--set` del contexto y los sistemas del juego (evolución, inventario y batalla, con sus tablas leídas una vez en `prepare`); `prepare` en `setup`, `start(seed)` en `run`; `startGame(seed)` da además los adaptadores por tipo (`HeadlessGame`) para las órdenes de objetos y combates, y `tables()` las tablas leídas |
| `scenarios/*.cpp` | Un escenario por fichero, que se registra solo: `battle`, `care`, `clock`, `raisings` y `ruleset-diff` |

El módulo puede usar todos los módulos de `game/` (CMake los recoge con la propiedad `DWE_GAME_MODULES`), así que un
escenario nuevo es un fichero nuevo. Los paquetes se montan con `apps/content`, igual que en el juego.

## Tests

`tests/sim_tests.cpp`: el registro, que las partidas son las mismas con cualquier número de hilos, el informe
(percentiles, histograma, Markdown y CSV), la línea de órdenes, el reloj a varias velocidades, que `--set` cambia lo que
ve el escenario, que una partida sin ventana es la misma con la misma semilla, que un valor que el ruleset no acepta
detiene la simulación, y el escenario `care` (sus métricas, que la política del cuidador cambia las crianzas y los
parámetros que rechaza). `tests/loop_tests.cpp` es la prueba del criterio de cierre de la fase 3 (ver abajo). Usan un paquete base de sustitución que CMake escribe en la carpeta del build: tablas vacías,
salvo `digimon` y `raise`, con unas especies inventadas (`tests/stand-in/`) para `care` y las formas que nombra la
sección `evolution`, `moves`, con técnicas inventadas con los ids que nombra la sección `battle`, y `evolution`, sin
filas; y el paquete `game` del árbol de fuentes. `sim.smoke` ejecuta `dwe-sim --scenario clock --runs 10` y pide que ninguna partida se desvíe;
`sim.care.smoke`, cuatro crianzas de una semana con `care`.

### El bucle de la fase 3

`tests/loop_tests.cpp` (`the user's disc, extracted: raising, training and battling the first partner evolves it as
the original`) juega el bucle entero sobre una `HeadlessGame` con el ruleset `original`, el disco extraído
(`DWE_CONTENT`; sin él se salta) y el reloj a 60 (una hora de juego por segundo), en turnos de un minuto de juego:

- **Partida nueva**: el partner de `new_game` (agum, novato).
- **Forma buscada**: la primera de su carta (`data/evolution`, `paths`) cuyos puntos puede ganar el cuidador sin el
  peso (`evolution.c: calculateRequirementScore`): un solo stat que no sea HP ni MP, errores de cuidado como máximo y
  disciplina o batallas de bonus. Con el disco del usuario es el campeón que pide cerebro 100, como mucho 3 errores y disciplina 60.
- **Cuidador** (una cosa por turno, como un jugador): cura la enfermedad o la herida con la medicina que cura seguro
  (`item.c: handleMedicineHealing`), da la comida favorita (`raise`) por el inventario, lleva al baño, acuesta,
  entrena el curso de ese stat (`training`) sin pasar del cansancio de `overwork_tiredness`, regaña hasta la
  disciplina pedida y pelea dos combates contra el primer NPC que puede luchar del mapa de inicio, como el disparador
  de depuración del juego.
- **Comprueba** cada efecto (energía, baño, sueño y reloj, stat entrenado y cansancio, disciplina y felicidad, stats,
  HP, batallas y vidas tras el combate), que no muere y que a las horas de novato (`check_hours.rookie`), antes del
  fallback, evoluciona por la comprobación periódica a la forma buscada con al menos `pass_points` puntos.

Juega seis semillas (en la sexta el partner enferma) y tarda unos 2 s en el build `ci`:

```sh
DWE_CONTENT=<contenido> scripts/dev.sh shell -c "build/ci/apps/sim/dwe_sim_tests -tc='*evolves it as the original*'"
```
