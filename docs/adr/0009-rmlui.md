# 0009. RmlUi para la UI del juego

- **Estado:** Aceptada
- **Fecha:** 2026-10-18 (aceptada el 2026-10-19, con los detalles de la fase 2)

## Contexto

Queremos mejorar los menús, añadir un HUD y minimapas, y que diseño y los mods puedan cambiar la UI sin recompilar.
La UI tiene que verse bien a cualquier resolución y manejarse entera con ratón, con teclado y con mando
([ADR 0013](0013-juego-nativo-de-pc.md), [ADR 0014](0014-mandos-y-entrada.md)).

## Decisión

La UI del juego se hace con **RmlUi**: documentos tipo HTML (RML), estilos tipo CSS (RCSS) y lógica en Lua. Se dibuja
con nuestro módulo `render`. Dear ImGui queda solo para las herramientas de depuración y el editor.

Detalles, fijados en la fase 2:

| Tema | Decisión |
|---|---|
| Versión | RmlUi 6.3, del puerto `rmlui` de vcpkg, sin sus funciones por defecto y con `freetype`. Sin `lua`, `svg` ni `lottie` |
| Módulo | `packages/engine/ui` (capa de servicios). RmlUi es privado del módulo: su API pública no expone tipos de RmlUi |
| Ficheros | Los documentos, estilos, scripts, fuentes e imágenes se leen **por asset id** a través del VFS: `ui/pause_menu/logic.lua` es el asset `ui/pause_menu/logic`. Por eso cada documento va en su carpeta (`document.rml`, `logic.lua`; su estilo, en la del aspecto): dos ficheros con el mismo nombre y distinta extensión darían el mismo id |
| Render | Una `RenderInterface` sobre la API pública de `render` (sin Vulkan en `ui`). `Ui::update()` llama a `Render()` de RmlUi fuera del frame: guarda la geometría en la CPU, crea las texturas en la tabla bindless y graba las llamadas; el `UiRenderer` (un `render::Drawer` del pase de UI, con su shader Slang) las reproduce desde buffers de GPU que se crean una vez al compilar la geometría, con recorte (*scissor*) y transformaciones. Todo en alfa premultiplicado, como espera RmlUi: las imágenes de fichero se premultiplican al cargarlas. Sin máscaras de recorte por ahora |
| Resolución | El contexto mide la ventana en píxeles físicos. Un *dp* es un píxel a 720 líneas: la proporción es `min(alto/720, ancho/960)` por la escala de UI que elige el jugador, así que la UI es igual de legible a 800×600 que a 4K |
| Fuente | Noto Sans (normal y negrita), con licencia SIL OFL 1.1, en `content/packs/game/ui/fonts/` con su licencia al lado. Se cargan todas las fuentes `.ttf`/`.otf` de `ui/fonts/`, así que un paquete puede añadir otras |
| Entrada | La navegación sale de las **acciones** (`move_*`, `confirm`, `cancel`, `menu`), no de teclas: el teclado, la cruceta y el stick navegan igual y se pueden reasignar. Las direcciones usan las propiedades `nav` de RmlUi; `confirm` hace clic en el elemento con el foco. El ratón va directo a RmlUi (movimiento, botones, rueda), y mientras hay un menú abierto el foco sigue al puntero. Los avisos de botones salen del mapa de acciones y del último dispositivo usado |
| Captura | Mientras se muestra un documento modal (el menú) la UI se queda con la entrada: el juego no ve sus acciones ni los clics. Sin menú, el HUD no captura nada (`pointer-events: none`) |
| Lua | El plugin de Lua de RmlUi no sirve: está hecho para Lua 5.1–5.4 y su API no es la nuestra. La UI usa **su propio `ScriptVm`** (el mismo sandbox de `script`, sin `io`, `os` ni carga de código, con límites de memoria e instrucciones) y su propia API: la tabla `ui` (`send`, `setting`, `prompt`, `device`, `show`, `hide`…), los tipos `Element` y `Event`, y los manejadores `on_show`, `on_action`, `on_setting`, `on_device`, `on_tick`. Cada documento tiene su propio entorno de Lua, donde corren sus `<script>` y los atributos de evento (`onclick="..."`) |
| Mando de la app | Los menús no cambian nada por sí mismos: envían **comandos** (`ui.send("fullscreen", true)`) que la app aplica, y la app publica el estado con `setSetting`, que es lo que muestran. Así la UI no depende del juego |
| Recarga en caliente | En desarrollo, la UI vuelve a leer cada medio segundo los ficheros de los documentos abiertos y recarga los que cambian: el documento nuevo sustituye al viejo solo si carga bien, y conserva si estaba visible y su sitio entre los modales |

## Consecuencias

- Los menús se editan como texto, con recarga en caliente (guía: [Editar menús](../guides/editar-menus.md)).
- Hay que mantener el backend de render de RmlUi sobre `render`.
- La API de Lua de la UI es parte del contrato con los mods, como la de `script`.
- RmlUi trae FreeType y sus dependencias (zlib, libpng, bzip2, brotli) al ejecutable.
- RmlUi comparte instancias de contenedores de libc++ con el motor; en los builds con AddressSanitizer unas copias
  anotan los contenedores y otras no. Los ejecutables que usan `ui` enlazan el dispositivo de `rhi`, que ya apaga
  `detect_container_overflow` por lo mismo con vk-bootstrap, así que RmlUi se compila igual en todos los builds y
  un build de depuración sin sanitizers enlaza.

## Alternativas descartadas

- **Una UI propia:** control total, pero mucho trabajo antes de tener el primer menú.
- **Dear ImGui para el juego:** está pensada para herramientas, no para menús con estilo propio.
- **El plugin de Lua de RmlUi:** no soporta Lua 5.5 y expondría su propia API, sin nuestro sandbox.
- **Compartir el `ScriptVm` del juego:** un menú roto o un mod de UI podría pisar el estado del juego.
