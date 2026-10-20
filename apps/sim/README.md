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
| `scenarios/*.cpp` | Un escenario por fichero, que se registra solo: `clock` y `ruleset-diff` |

El módulo puede usar todos los módulos de `game/` (CMake los recoge con la propiedad `DWE_GAME_MODULES`), así que un
escenario nuevo es un fichero nuevo. Los paquetes se montan con `apps/content`, igual que en el juego.

## Tests

`tests/sim_tests.cpp`: el registro, que las partidas son las mismas con cualquier número de hilos, el informe
(percentiles, histograma, Markdown y CSV), la línea de órdenes, el reloj a varias velocidades, que `--set` cambia lo
que ve el escenario y que un valor que el ruleset no acepta detiene la simulación. Usan un paquete base de sustitución
con las tablas vacías que CMake escribe en la carpeta del build, y el paquete `game` del árbol de fuentes. `sim.smoke`
ejecuta `dwe-sim --scenario clock --runs 10` y pide que ninguna partida se desvíe.
