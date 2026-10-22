# Módulos

Cada módulo del repositorio tiene aquí su página: qué hace, su API pública y cómo se usa. La página vive en
`docs/modulos/<capa>/<módulo>.md` y se actualiza en el mismo PR que cambia el módulo. Las capas y lo que puede usar
cada una están en las [reglas de dependencias](../architecture/dependency-rules.md); la forma de un módulo, en la
[estructura del repositorio](../architecture/repository-layout.md#forma-de-un-módulo).

## Motor (`engine/`)

| Módulo | Qué hace |
|---|---|
| [core](engine/core.md) | `Result` y errores sin excepciones, log, mensajes, YAML, JSON Schema y matemáticas |
| [platform](engine/platform.md) | Ventana, eventos y mandos sobre SDL3, y ejecutar otros programas |
| [rhi](engine/rhi.md) | El único módulo que habla con Vulkan 1.3 |
| [render](engine/render.md) | El render del juego encima de `rhi` |
| [audio](engine/audio.md) | La salida de sonido, el mezclador, las voces y los volúmenes |
| [input](engine/input.md) | Entrada por acciones, nunca por teclas o botones |
| [assets](engine/assets.md) | Contenido por paquetes, ids estables y tablas compuestas |
| [animation](engine/animation.md) | Muestreo y mezcla de las animaciones de los modelos |
| [scene](engine/scene.md) | Entidades y componentes con EnTT |
| [script](engine/script.md) | Lua 5.5 con sol2 y la API versionada y aislada de los mods |
| [ui](engine/ui.md) | La UI con RmlUi, con su lógica en Lua |
| [localization](engine/localization.md) | Los textos que ve el jugador en su idioma |
| [ruleset](engine/ruleset.md) | El formato de ruleset, las estrategias por nombre y el reloj de juego |
| [debug](engine/debug.md) | Consola y paneles de desarrollo con Dear ImGui |

## Juego (`game/`)

| Módulo | Qué hace |
|---|---|
| [partner](game/partner.md) | El cuidado del partner: necesidades, vida, muerte y renacer, entrenamiento |
| [evolution](game/evolution.md) | Cuándo y a qué forma evoluciona el partner |
| [battle](game/battle.md) | Las reglas del combate como simulación determinista |
| [field](game/field.md) | El campo: movimiento, colisión, disparadores, warps y seguimiento |
| [inventory](game/inventory.md) | La bolsa, el dinero, el uso de objetos y las tiendas |
| [story](game/story.md) | El estado de la historia y el runtime de los scripts de eventos |
| [save](game/save.md) | Partidas guardadas versionadas, ranuras y autoguardado |

## Aplicaciones (`apps/`)

| Módulo | Qué hace |
|---|---|
| [session](apps/session.md) | La partida: el único sitio que junta los sistemas de `game/` |
| [screens](apps/screens.md) | Los modelos de vista de las pantallas, sin reglas |
| [content](apps/content.md) | Cómo montan los paquetes las aplicaciones |
| [game](apps/game.md) | `dwe-game`, el ejecutable del juego |
| [dwx](apps/dwx.md) | La herramienta de assets: extraer, validar, cocinar y empaquetar |
| [sim](apps/sim.md) | `dwe-sim`: simulaciones de balance sin gráficos |

## Otros

| Módulo | Qué hace |
|---|---|
| [formats](formats.md) | Lectores de los formatos del original; solo los usa `dwx` |
| [shaders](shaders.md) | Módulos de Slang compartidos por los shaders del motor |
| [tests/support](tests/support.md) | Lo que comparten los tests de todos los módulos |
| [tests/mods](tests/mods.md) | Paquetes sintéticos con los que los tests comprueban los mods |
