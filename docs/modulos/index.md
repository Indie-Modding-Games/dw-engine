# Módulos

Cada módulo del repositorio tiene aquí su página: qué hace, su API pública y cómo se usa. La página vive en
`docs/modulos/<grupo>/<módulo>.md` y se actualiza en el mismo PR que cambia el módulo; el README de la carpeta del
módulo es corto y enlaza a ella. Las páginas conservan su dirección aunque el código se movió a `packages/` y `apps/`
(las de session, screens y content siguen en `modulos/apps/` y la de beta-gateway en `modulos/services/`). Las
capas y lo que puede usar cada una están en las [reglas de dependencias](../architecture/dependency-rules.md); la forma de un paquete, en la
[estructura del repositorio](../architecture/repository-layout.md#forma-de-un-paquete).

## Motor (`packages/engine/`)

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
| [signs](engine/signs.md) | Los carteles pintados en los mapas, escritos otra vez en el idioma del jugador |
| [video](engine/video.md) | Las películas: leer el vídeo frame a frame y dar la imagen de cada momento |
| [ruleset](engine/ruleset.md) | El formato de ruleset, las estrategias por nombre y el reloj de juego |
| [debug](engine/debug.md) | Consola y paneles de desarrollo con Dear ImGui |
| [net](engine/net.md) | Red entre dos máquinas: UDP, canal fiable y ordenado, lockstep y códigos para unirse |

## Juego (`packages/game/`)

| Módulo | Qué hace |
|---|---|
| [partner](game/partner.md) | El cuidado del partner: necesidades, vida, muerte y renacer, entrenamiento |
| [evolution](game/evolution.md) | Cuándo y a qué forma evoluciona el partner |
| [battle](game/battle.md) | Las reglas del combate como simulación determinista |
| [field](game/field.md) | El campo: movimiento, colisión, disparadores, warps y seguimiento |
| [inventory](game/inventory.md) | La bolsa, el dinero, el uso de objetos y las tiendas |
| [fishing](game/fishing.md) | La pesca: la caña, el corcho, el sedal y los peces del agua |
| [weather](game/weather.md) | El clima del campo y las estaciones (mejoras, opciones «Clima» y «Estaciones»): reglas por zona, semilla propia, fundidos |
| [gym](game/gym.md) | El gimnasio del pueblo: la coreografía de los cursos y la cuenta de las ganancias |
| [tournament](game/tournament.md) | Los torneos de la arena: rivales, cuadro, rivales generados y victorias |
| [curling](game/curling.md) | El juego de las piedras sobre el hielo: deslizamiento, choques, puntos y el rival |
| [versus](game/versus.md) | El versus en línea: identidad, serie, alineaciones y resultados entre dos máquinas en lockstep |
| [story](game/story.md) | El estado de la historia y el runtime de los scripts de eventos |
| [save](game/save.md) | Partidas guardadas versionadas, ranuras y autoguardado |
| [modes](game/modes.md) | Los modos de juego: el manifiesto `mode.yaml` y el catálogo de los paquetes |

## Librerías compartidas (`packages/`)

| Módulo | Qué hace |
|---|---|
| [session](apps/session.md) | La partida: el único sitio que junta los sistemas de `packages/game/` |
| [screens](apps/screens.md) | Los modelos de vista de las pantallas, sin reglas |
| [content](apps/content.md) | Cómo montan los paquetes las aplicaciones |
| [formats](formats.md) | Lectores de los formatos del original; solo los usa `dwx` |

## Aplicaciones (`apps/`)

| Aplicación | Qué hace |
|---|---|
| [game](apps/game.md) | `dwe-game`, el ejecutable del juego |
| [dwx](apps/dwx.md) | La herramienta de assets: extraer, validar, cocinar y empaquetar |
| [sim](apps/sim.md) | `dwe-sim`: simulaciones de balance sin gráficos |
| [launcher](apps/launcher.md) | El lanzador de escritorio: lee el disco, actualiza el juego y lo arranca |
| [studio](apps/studio.md) | DWE Studio, banco de trabajo de desarrollo: editor, compilar, arrancar y comparar con el original |
| [mapper](apps/mapper.md) | `dwe-mapper`, el editor de mapas (por ahora visor de solo lectura): colisiones, salidas y personajes sobre el fondo |
| [beta-gateway](services/beta-gateway.md) | La pasarela de la beta entre el lanzador y las versiones de GitHub, con la clave de la beta |

## Otros

| Módulo | Qué hace |
|---|---|
| [shaders](shaders.md) | Módulos de Slang compartidos por los shaders del motor (`packages/engine/rhi/shaders/`) |
| [tests/support](tests/support.md) | Lo que comparten los tests de todos los módulos |
| [tests/mods](tests/mods.md) | Paquetes sintéticos con los que los tests comprueban los mods |
| [tests/autotest](tests/autotest.md) | Las rutas que juegan el juego de verdad con teclado, ratón o mando |
