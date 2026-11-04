# core

Lo que usan todos los módulos: `Result` y errores sin excepciones ([ADR 0007](../../adr/0007-sin-excepciones.md)), log (y `LogRing`/`keepLogTail`, las últimas líneas en memoria para los reportes de bug), `formatMessage` para las plantillas de mensajes de cada `messages.hpp`, `DWE_ASSERT`, `FixedStep`, el reloj de ticks fijos de la lógica, SHA-1, YAML (`parseYaml`/`emitYaml`, [ADR 0016](../../adr/0016-yaml-con-libyaml.md)) y `JsonSchema`, que comprueba documentos YAML contra un JSON Schema
([subconjunto admitido](../../architecture/json-schema.md)), y las matemáticas (GLM, a través de `<dwe/core/math.hpp>`, con los ejes del [espacio del mundo](../../architecture/world-space.md)). Depende de GLM, público, y de libyaml, que no sale de `src/`.

## Trigonometría del original

`<dwe/core/original_math.hpp>` (`dwe::core::original`) tiene las cuentas enteras del original para lo que debe caer
en sus números (orientaciones, pasos, anillos y partículas): `atan4096` (`math.c: _atan`), `sin512` y `cos512`
(`math.c: _sin`, `_cos`), `rsin`, `rcos` y `ratan2` (las de la biblioteca de la consola) y `lerp`
(`effect_common.c`). Batalla, efectos del campo, muerte, evolución y la aparición del jefe usan estas y no una copia
propia.

| Medida | Original | dwe antes | dwe ahora | Estado |
|---|---|---|---|---|
| `SIN_LOOKUP_TABLE` (129 entradas, 512 por vuelta, 4096 = 1) | seno × 4096 redondeado hacia abajo, leído de la RAM | redondeado al más cercano: 65 de 129 entradas una unidad de más (2: 101 en vez de 100) | las 129 iguales (`original_math_tests.cpp`) | Igual |
| Cuadrantes de `_sin` | la mitad negativa es la tabla con el signo cambiado | igual | igual | Igual |
| `_cos` | `_sin` un cuarto de vuelta después (128) | igual | igual | Igual |
| `_atan` | tabla de 512 pasos por octavo | tabla calculada, comprobada entera contra el disco | sin cambio | Igual |

