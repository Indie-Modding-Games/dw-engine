# ruleset

El formato de ruleset (`dwe::ruleset`, capa de servicios): carga `rules/<id>/` de los paquetes, sigue `extends`,
comprueba las secciones, crea estrategias por nombre, recarga y ajusta en caliente, y lleva el reloj de juego. No sabe
qué significa cada sección: cada sistema del juego lee la suya en su propio struct. El formato y cómo lo usa un
sistema están en [rulesets](../../docs/architecture/rulesets.md).

## Uso

```cpp
auto store = dwe::ruleset::RulesetStore::load(vfs, "original", checks);   // checks: SectionChecks de los sistemas
const dwe::ruleset::Ruleset& rules = store->current();
const dwe::ruleset::Section* care = rules.section("care");                // YAML, fichero y ruleset de origen
const dwe::assets::AssetId* raise = rules.table("raise");                 // data/raise
dwe::ruleset::GameClock clock{*dwe::ruleset::readClockRules(rules)};
clock.setSpeed(60.0);                                                      // una hora de juego por minuto real
dwe::ruleset::ClockAdvance step = clock.advance(1.0 / 60.0);               // step.hours: reglas de cada hora
store->setValue("care", "hunger.window_minutes", "45");                   // panel de ajuste; sube revision()
store->reload();                                                           // tras editar los YAML
```

| Header | Qué tiene |
|---|---|
| `ruleset.hpp` | `Ruleset`, `Section`, `SectionCheck(s)`, `loadRuleset`, `checkRuleset`, `findValue`, `Quirks` y `readQuirks` |
| `ruleset_store.hpp` | `RulesetStore`: el ruleset activo, `revision()`, `reload()`, `select(id)` y `setValue` |
| `strategy.hpp` | `StrategyRegistry<Interfaz>`: implementaciones por nombre; un nombre desconocido es un error en su línea |
| `game_clock.hpp` | `ClockRules` (sección `clock`), `readClockRules`, `GameTime`, `ClockAdvance` y `GameClock` |
| `messages.hpp` | Las plantillas de los mensajes de error del módulo |
| `random.hpp` | `Random`: PCG32 con semilla, `below(n)` como `randomLimit` del original y `chance(p)` |

## Tests

`ruleset_tests.cpp` usa dos paquetes en memoria: carga, `extends`, errores con fichero y línea (claves desconocidas,
valores fuera de rango, manifiesto roto, estrategia desconocida), `setValue` con su revisión, recarga que conserva el
ruleset anterior si falla, cambio de ruleset y rarezas. `game_clock_tests.cpp` prueba el reloj contra `clock.c`
(minutos, días, años, avance rápido, pausa, salto al dormir) y que `Random` se repite con su semilla.
`original_ruleset_tests.cpp` carga el ruleset `original` del paquete `game` y, si `DWE_CONTENT` apunta al contenido
extraído, comprueba que cada tabla que nombra está en el paquete base y cumple su esquema (R3.2).
