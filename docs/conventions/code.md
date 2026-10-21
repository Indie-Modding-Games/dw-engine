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
| Miembros privados | `camelCase` con `_` al final | `tickSeconds_` |
| Constantes y valores de enum | `PascalCase` | `MaxPartySize`, `Weather::Rain` |
| Macros (solo si no hay alternativa) | `UPPER_SNAKE` con prefijo | `DWE_ASSERT` |

- Una palabra por concepto en todo el código: si es `pack`, no es también `bundle`.
- Sin números ni strings mágicos: se nombran con un enum o una constante, en el header del módulo dueño.
- El nombre dice la responsabilidad: qué hace la función o qué guarda la variable, sin abreviaturas ni nombres
  genéricos (`data`, `tmp`, `helper`, `manager`).
- Los nombres vienen del juego cuando existen (ver el [glosario](../glossary.md)).

## Diseño y legibilidad

- Responsabilidad única: cada módulo, clase y función hace una sola cosa. Si su nombre necesita un "y", se separa.
- El código se lee como prosa: funciones cortas, un nivel de abstracción por función y sin trucos de una línea.
- Cada constante tiene un único dueño: se declara una vez y el resto la incluye, nunca se copia.
- Los mensajes de error y de log no van inline en la lógica: viven como constantes con nombre (texto o plantilla de
  `std::format`) en el `messages.hpp` del módulo. Un mensaje que se usa en varios sitios sale de una sola constante.
- Los textos que ve el jugador vienen del contenido (packs y localización), nunca del código.
- **Sin `goto` en ningún sitio:** ni en C++, ni en el Lua que escribimos, ni en el Lua que genera `dwx`. El flujo de
  control es estructurado y moderno: if/else, bucles, `break`, retornos tempranos, funciones auxiliares y, si hace
  falta, bucles con estado.

## Código que viene del decomp

- El decomp es la referencia del **comportamiento**, no del código. Nada se copia tal cual: se reescribe con estas
  convenciones (nombres por responsabilidad, tipos y enums propios, sin offsets ni `unkXX`, funciones cortas).
- Lo que se conserva es el resultado: las mismas fórmulas, probabilidades y orden de efectos, con la referencia al
  decomp en un comentario corto donde ayude a verificarlo.

## Comentarios

- El código se documenta solo con buenos nombres; los comentarios son pocos, cortos y precisos.

- Cada función pública tiene un comentario corto que dice qué hace.
- El resto de comentarios explica **por qué**, nunca repite lo que dice el código.
- Sin código comentado y sin TODO en el código: lo pendiente va en un issue.
- Cuando se reproduce un comportamiento raro del original a propósito, se dice, con la referencia al decomp.

## Formato y análisis

- `clang-format` obligatorio: la configuración está en `.clang-format` y la CI la comprueba.
- `clang-tidy` con el perfil del repositorio y warnings como errores en la CI.
- `check-commit` y `check-full` fallan si un fuente C++ o Lua del repositorio contiene `goto` (paso `no-goto`), y
  `clang-tidy` aplica `cppcoreguidelines-avoid-goto`. Los tests del conversor de scripts comprueban lo mismo en el Lua
  generado.
- ASan y UBSan en los builds de depuración.

## Tests

- **Unitarios:** en `tests/` de cada módulo, con doctest.
- **De integración:** en `tests/integration/`.
- **De simulación:** en `tests/simulation/`, para el balance de los rulesets (por ejemplo, mil crianzas).
- **De render:** en `tests/golden/`, contra capturas de referencia.
- Todo cambio de comportamiento trae su test, y comprueba lo que dice el GDD (ver [ciclo de diseño](design-cycle.md)).
