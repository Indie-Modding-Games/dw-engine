# Ciclo de diseño de un sistema

El juego se reinventa ([ADR 0010](../adr/0010-reglas-propias-con-rulesets.md)), así que la especificación de cada
sistema es su documento de diseño en el GDD, no el comportamiento del original. Cada sistema (cuidado, evolución,
batalla, economía, un modo de juego…) sigue este ciclo.

| Paso | Qué | Dónde | Quién |
|---|---|---|---|
| 1. Ficha de referencia | Cómo funcionaba el original, sacado del decomp | [`docs/reference/`](../reference/README.md) | Programación |
| 2. Diseño | Cómo funcionará ahora, justificado contra los pilares de diseño | GDD (repositorio de diseño) | Diseño |
| 3. Aprobación | La persona responsable de diseño lo aprueba | PR en el repositorio de diseño | Responsable de diseño |
| 4. Implementación | El sistema en C++; sus reglas y valores, en el ruleset (YAML o Lua), nunca fijos en el código | `game/` + `content/packs/` | Programación |
| 5. Tests | Que el sistema hace lo que dice el GDD, y simulaciones de balance | `tests/` | Programación |
| 6. Ajuste y pruebas de juego | Valores cambiados en vivo desde el panel de depuración y sesiones de juego | Editor y equipo | Todos |
| 7. Iterar | Lo aprendido vuelve al paso 2 | — | Diseño |

## Reglas

- **No se programa un sistema sin diseño aprobado.** Un prototipo para explorar sí, pero no se hace merge.
- **Los números no viven en C++.** Si un valor puede ajustarse, está en el ruleset.
- **Cada desviación del original es una decisión.** El documento de diseño dice qué se conserva, qué cambia y por qué.
