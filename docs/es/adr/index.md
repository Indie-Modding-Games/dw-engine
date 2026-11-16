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

1. Copia [`docs/adr/template.md`](https://github.com/Indie-Modding-Games/dw-engine/blob/main/docs/adr/template.md) como `NNNN-titulo-corto.md`, con el siguiente número libre.
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
| [0001](0001-native-reimplementation.md) | Reimplementación nativa sin limitaciones de PS1 | Aceptada |
| [0002](0002-cpp26.md) | C++26 con GCC y Clang | Aceptada |
| [0003](0003-vulkan-and-sdl3.md) | Vulkan 1.3 para render y SDL3 para plataforma | Aceptada |
| [0004](0004-lua-and-sol2.md) | Lua 5.5 + sol2, en sandbox | Aceptada |
| [0005](0005-asset-pipeline.md) | Assets en tres estados y capas, sin fidelidad binaria | Aceptada |
| [0006](0006-three-repositories.md) | Tres repositorios: código, diseño y arte | Propuesta |
| [0007](0007-no-exceptions.md) | Sin excepciones, con `std::expected` | Aceptada |
| [0008](0008-headers-before-modules.md) | Headers ahora, módulos de C++ más adelante | Aceptada |
| [0009](0009-rmlui.md) | RmlUi para la UI del juego | Aceptada |
| [0010](0010-own-rules-with-rulesets.md) | Reglas propias, organizadas en rulesets | Aceptada |
| [0011](0011-asset-legal-policy.md) | "Trae tus propios datos" y política legal de assets | Aceptada |
| [0012](0012-ecs-with-entt.md) | Entidades con EnTT | Aceptada |
| [0013](0013-native-pc-game.md) | Un juego nativo de PC | Aceptada |
| [0014](0014-gamepads-and-input.md) | Mandos y entrada por acciones | Aceptada |
| [0015](0015-container-development.md) | Compilar dentro de un contenedor Docker | Aceptada |
| [0016](0016-yaml-with-libyaml.md) | YAML con libyaml, envuelto en `core` | Aceptada |
| [0017](0017-cooking-and-pak-files.md) | Cocinado a KTX2 UASTC y Opus, y archivos `.pak` propios con zstd | Aceptada |
| [0018](0018-slang-shaders-with-vcpkg.md) | Shaders en Slang compilados en el build con `slangc` de vcpkg | Aceptada |
| [0019](0019-bindless-descriptors.md) | Un único set de descriptores bindless y datos por frame por dirección de buffer | Aceptada |
| [0020](0020-map-camera-and-depth-layers.md) | Cámara de mapa descentrada y capas de profundidad como planos | Aceptada |
| [0021](0021-cpu-animation-and-gpu-skinning.md) | Animación muestreada en la CPU, skinning en la GPU, en un módulo `animation` | Aceptada |
| [0022](0022-first-ruleset-faithful-to-the-original.md) | El primer ruleset reproduce el original; el diseño nuevo llega como rulesets | Aceptada |
| [0023](0023-remaster-pipeline.md) | Remaster por recetas, con proveedores intercambiables, caché por contenido y local por defecto | Aceptada |
| [0024](0024-co-op-with-authoritative-host.md) | Cooperativo en línea con un host autoritativo y jugadores movidos por controladores | Propuesta |
