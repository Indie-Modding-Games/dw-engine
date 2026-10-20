# Simulaciones con `dwe-sim`

`dwe-sim` (R3.5) juega escenarios de los sistemas del juego muchas veces, sin ventana ni gráficos, y da un informe con
la distribución de lo que mide cada uno: para comparar el balance de dos rulesets, o el efecto de cambiar un valor,
sin jugar a mano. El código está en [`apps/sim`](../../apps/sim/README.md).

## Uso

```sh
dwe-sim --list
dwe-sim --scenario clock --runs 10
dwe-sim --content <contenido> --ruleset original --scenario clock --runs 1000 --seed 7 --threads 8 \
        --days 30 --speed 3600 --set clock.frames_per_minute=10 --out informes/
dwe-sim --scenario ruleset-diff --param against=original --set care.hunger.window_minutes=45
```

| Opción | Qué hace | Por defecto |
|---|---|---|
| `--scenario <nombre>` | El escenario que se juega (`--list` los enumera) | Obligatoria |
| `--content <carpeta>`, `--mods <carpeta>` | Los paquetes, montados igual que en el juego (`apps/content`) | `content` |
| `--ruleset <id>` | El ruleset con el que se juega | `original` |
| `--runs <n>` | Cuántas partidas | 100 |
| `--seed <n>` | La semilla base: cada partida usa la suya, derivada de esta y de su número | 1 |
| `--threads <n>` | Hilos; el resultado es el mismo con cualquier número | Uno por hilo del procesador |
| `--days <n>` | Días de juego que juega cada partida | 1 |
| `--speed <x>` | El avance rápido del reloj (`GameClock::setSpeed`) | 3600: una hora por segundo |
| `--set <sección>.<ruta>=<valor>` | Cambia un valor del ruleset sin tocar sus ficheros; se repite. Pasa las mismas comprobaciones que el panel de ajuste | |
| `--param <clave>=<valor>` | Un parámetro del escenario; se repite | |
| `--out <carpeta>` | Escribe `<escenario>.md`, `<escenario>-runs.csv` y `<escenario>-summary.csv` | Solo Markdown por la salida estándar |

El informe dice el ruleset, las partidas, la semilla, los días, la velocidad y los `--set`; da para cada métrica la
media, la desviación, el mínimo, los percentiles 5, 25, 50, 75 y 95 y el máximo, y un histograma. No incluye el número de
hilos: el informe de una semilla es el mismo en cualquier máquina, así que dos informes se comparan con un `diff`.

## Escenarios

| Escenario | Qué mide |
|---|---|
| `clock` | Juega el reloj `--days` días a `--speed` en fotogramas de duración desigual, como un bucle de juego, y comprueba que cruza los mismos minutos, horas y días que avanzar esos fotogramas de golpe (`mismatch` es 0). Es lo que necesitan las reglas que van cada hora o cada día: el avance rápido da lo mismo que jugar |
| `ruleset-diff` | Lista cada valor en que el ruleset (con sus `--set`) difiere de `--param against=<id>` (por defecto `original`) |

## Añadir un escenario

Un escenario es **un fichero** en `apps/sim/scenarios/`: CMake los recoge todos, y `dwe-sim` puede usar todos los
módulos de `game/`, así que no hay que tocar nada más.

```cpp
// apps/sim/scenarios/evolution_scenario.cpp
#include <dwe/game/evolution/...>
#include <dwe/ruleset/random.hpp>
#include <dwe/sim/scenario.hpp>

namespace dwe::sim {
namespace {

class RaisingScenario final : public Scenario {
public:
    // Una vez: lee la sección del ruleset, las tablas y los parámetros.
    core::Result<void> setup(const ScenarioContext& context) override { /* context.rules, context.files, ... */ }

    // Cada partida, en cualquier hilo: solo lee lo que preparó setup. Toda la aleatoriedad sale de la semilla.
    [[nodiscard]] core::Result<Metrics> run(std::uint64_t seed) const override {
        ruleset::Random random{seed};
        // ... mil días de crianza con el reloj a context.speed ...
        return Metrics{{.name = "agumon", .value = 1.0}, {.name = "greymon", .value = 0.0}};
    }

    // Opcional: tablas para el informe.
    [[nodiscard]] std::vector<ReportTable> tables() const override { return {}; }
};

const ScenarioRegistration registration{{
    .name = "raisings",
    .description = "Raises a partner from its egg and counts what it becomes.",
    .create = [] { return std::make_unique<RaisingScenario>(); },
}};

} // namespace
} // namespace dwe::sim
```

Reglas:

- **Lo mismo para una semilla.** `run(seed)` solo depende de la semilla y de lo que preparó `setup`: nada de estado
  compartido que cambie, ni de relojes reales. Así el informe es el mismo con cualquier número de hilos.
- **Las mismas métricas en el mismo orden** en cada partida. Para un histograma de resultados (en qué Digimon acaba
  una crianza, quién gana un combate), una métrica por resultado con 0 o 1: la media es la proporción.
- **El tiempo con `GameClock`** a `context.speed`, y `context.days` días, para que el avance rápido sea lo que se
  prueba y no un atajo.
- **Las reglas del ruleset.** El escenario lee las secciones de `context.rules` con las funciones de su sistema, así
  que `--set` y `--ruleset` le llegan solos.
- Un test en `apps/sim/tests/` que juegue el escenario con pocas partidas con el contenido de sustitución de los tests.
