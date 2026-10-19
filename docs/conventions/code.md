# Convenciones de código

## Lenguaje

- C++26 ([ADR 0002](../adr/0002-cpp26.md)), siguiendo las
  [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines).
- Solo se usan las características que soportan todos los compiladores de la CI. Esta lista se amplía con un PR cuando
  un compilador incorpora algo nuevo.
- Sin excepciones: los errores recuperables se devuelven con `std::expected` ([ADR 0007](../adr/0007-sin-excepciones.md)).
- Headers con `#pragma once` ([ADR 0008](../adr/0008-headers-antes-que-modulos.md)).
- Sin `new` ni `delete` a mano: RAII y punteros inteligentes, o los allocators de `core`.

## Nombres

| Qué | Estilo | Ejemplo |
|---|---|---|
| Ficheros y carpetas | `snake_case` | `evolution_rules.cpp` |
| Namespaces | Siguen la carpeta | `dwe::game::partner` |
| Tipos, clases y enums | `PascalCase` | `EvolutionRule` |
| Funciones | `camelCase`, con verbo | `loadPack`, `tickCare`, `isSleeping` |
| Variables y miembros | `camelCase` | `hunger`, `currentMap` |
| Constantes y valores de enum | `PascalCase` | `MaxPartySize`, `Weather::Rain` |
| Macros (solo si no hay alternativa) | `UPPER_SNAKE` con prefijo | `DWE_ASSERT` |

- Una palabra por concepto en todo el código: si es `pack`, no es también `bundle`.
- Sin números mágicos: se nombran con un enum o una constante.
- Los nombres vienen del juego cuando existen (ver el [glosario](../glossary.md)).

## Comentarios

- Cada función pública tiene un comentario corto que dice qué hace.
- El resto de comentarios explica **por qué**, nunca repite lo que dice el código.
- Sin código comentado y sin TODO en el código: lo pendiente va en un issue.
- Cuando se reproduce un comportamiento raro del original a propósito, se dice, con la referencia al decomp.

## Formato y análisis

- `clang-format` obligatorio: la configuración está en `.clang-format` y la CI la comprueba.
- `clang-tidy` con el perfil del repositorio y warnings como errores en la CI.
- ASan y UBSan en los builds de depuración.

## Tests

- **Unitarios:** en `tests/` de cada módulo, con doctest.
- **De integración:** en `tests/integration/`.
- **De simulación:** en `tests/simulation/`, para el balance de los rulesets (por ejemplo, mil crianzas).
- **De render:** en `tests/golden/`, contra capturas de referencia.
- Todo cambio de comportamiento trae su test, y comprueba lo que dice el GDD (ver [ciclo de diseño](design-cycle.md)).
