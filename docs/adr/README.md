# Registros de decisiones de arquitectura (ADR)

Un ADR deja escrita una decisión importante: qué problema había, qué se decidió, qué consecuencias tiene y qué
alternativas se descartaron. Sirve para que quien llegue después entienda **por qué** el proyecto es como es, sin
tener que preguntar.

Usamos una versión simplificada del formato [MADR](https://adr.github.io/madr/).

## Cuándo hace falta un ADR

- Elegir o cambiar una tecnología (lenguaje, librería, formato).
- Cambiar la arquitectura o las [reglas de dependencias](../architecture/dependency-rules.md).
- Cualquier excepción a una regla del proyecto.

## Cómo se propone uno

1. Copia [template.md](template.md) como `NNNN-titulo-corto.md`, con el siguiente número libre.
2. Rellénalo con estado **Propuesta** y ábrelo en un PR.
3. Se discute en el PR. Cuando hay acuerdo, el estado pasa a **Aceptada** y se hace el merge.

## Estados

| Estado | Significado |
|---|---|
| Propuesta | En discusión |
| Aceptada | En vigor |
| Rechazada | Se discutió y se descartó; se conserva para no repetir la discusión |
| Sustituida por NNNN | Ya no está en vigor; el ADR NNNN la reemplaza |

Un ADR aceptado **no se reescribe**. Si la decisión cambia, se escribe uno nuevo y el antiguo se marca como
sustituido.

## Índice

| # | Decisión | Estado |
|---|---|---|
| [0001](0001-reimplementacion-nativa.md) | Reimplementación nativa sin limitaciones de PS1 | Aceptada |
| [0002](0002-cpp26.md) | C++26 con GCC y Clang | Aceptada |
| [0003](0003-vulkan-y-sdl3.md) | Vulkan 1.3 para render y SDL3 para plataforma | Aceptada |
| [0004](0004-lua-y-sol2.md) | Lua 5.5 + sol2, en sandbox | Aceptada |
| [0005](0005-tuberia-de-assets.md) | Assets en tres estados y capas, sin fidelidad binaria | Aceptada |
| [0006](0006-tres-repositorios.md) | Tres repositorios: código, diseño y arte | Propuesta |
| [0007](0007-sin-excepciones.md) | Sin excepciones, con `std::expected` | Propuesta |
| [0008](0008-headers-antes-que-modulos.md) | Headers ahora, módulos de C++ más adelante | Propuesta |
| [0009](0009-rmlui.md) | RmlUi para la UI del juego | Propuesta |
| [0010](0010-reglas-propias-con-rulesets.md) | Reglas propias, organizadas en rulesets | Aceptada |
| [0011](0011-politica-legal-de-assets.md) | "Trae tus propios datos" y política legal de assets | Propuesta |
| [0012](0012-ecs-con-entt.md) | Entidades con EnTT | Propuesta |
| [0013](0013-juego-nativo-de-pc.md) | Un juego nativo de PC | Propuesta |
| [0014](0014-mandos-y-entrada.md) | Mandos y entrada por acciones | Propuesta |
| [0015](0015-desarrollo-en-contenedor.md) | Compilar dentro de un contenedor Docker | Aceptada |
| [0016](0016-yaml-con-libyaml.md) | YAML con libyaml, envuelto en `core` | Propuesta |
