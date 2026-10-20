# Editar menús

Los menús y el HUD son documentos de RmlUi en el paquete `game` (`content/packs/game/ui/`). Se editan como texto y,
en un build de desarrollo, el juego los recarga solo al guardarlos. Arquitectura en [ui.md](../architecture/ui.md).

## Dónde está cada cosa

```
content/packs/game/ui/
  common/theme.rcss          estilo común: fuente, botones, avisos de botones
  fonts/                     fuentes (.ttf/.otf) y sus licencias; se cargan todas
  pause_menu/document.rml    el menú de pausa
  pause_menu/style.rcss
  pause_menu/logic.lua
  hud/document.rml           el HUD con el minimapa
  hud/style.rcss
  hud/logic.lua
```

Cada documento va en su carpeta porque los ficheros se leen por asset id, que es la ruta sin extensión:
`menu.rml` y `menu.rcss` en la misma carpeta chocarían. Un mod puede sustituir cualquiera de estos ficheros con uno
del mismo id en su paquete.

## RML y RCSS

RML es casi HTML y RCSS casi CSS; la referencia es la [documentación de RmlUi](https://mikke89.github.io/RmlUiDoc/).
Lo propio de este proyecto:

- Las medidas, en `dp` (un dp es un píxel a 720 líneas; la UI escala sola con la ventana y la opción del jugador).
- Las rutas son relativas al documento (`href="style.rcss"`) o a la raíz de los paquetes (`/ui/common/theme.rcss`).
- Un elemento se puede elegir con teclado y mando si tiene `tab-index: auto; focus: auto; nav: auto;` (los `button`
  ya lo tienen en `theme.rcss`). `nav-up: #id` y compañía fijan a dónde va cada dirección, por ejemplo para dar la
  vuelta en una lista. El estilo del elemento elegido es `:focus`; el ratón también mueve el foco.
- `autofocus` marca el elemento que recibe el foco al abrir el documento.
- El HUD no debe tapar el ratón: su `body` tiene `pointer-events: none`.
- No hay estilos por defecto: un elemento nuevo necesita su `display` en `theme.rcss`.

## Lógica en Lua

Un documento carga sus scripts con `<script src="logic.lua"></script>` (o en línea). Corren en un entorno de Lua
propio del documento, en el sandbox de la UI (sin ficheros, ni `os`, ni cargar código). Los atributos de evento son
Lua y ven `event`, `element` y `document`:

```xml
<button id="quit" onclick="ui.send('quit')" text-key="pause_menu.quit"></button>
<button id="open-options" onclick="open_page(document, 'options')" text-key="pause_menu.options"></button>
```

Un documento no lleva texto escrito: el atributo `text-key` pone el texto de esa clave en el idioma del juego, al
cargar y al cambiar de idioma. Las claves están en `content/packs/game/localization/<idioma>/` y cada una tiene que
estar en inglés y en español ([localización](../architecture/localizacion.md#añadir-un-texto)).

El documento puede definir estas funciones, que la UI llama con el documento como primer argumento:

| Función | Cuándo |
|---|---|
| `on_show(document)` | Al mostrarse |
| `on_hide(document)` | Al ocultarse |
| `on_action(document, acción)` | Por cada paso de navegación mientras es el documento modal de arriba: `up`, `down`, `left`, `right`, `confirm`, `cancel`, `menu`. Si devuelve `true`, la UI no hace lo de por defecto |
| `on_setting(document, nombre)` | Cuando la app cambia un ajuste |
| `on_device(document)` | Cuando el jugador cambia de dispositivo (para los avisos de botones) |
| `on_language(document)` | Cuando cambia el idioma: los `text-key` ya están en el nuevo; aquí se reescribe lo que el script puso con `t()` y los avisos de botones |
| `on_tick(document, segundos)` | En cada tick lógico mientras está visible |

API disponible:

| Llamada | Qué hace |
|---|---|
| `ui.send(nombre, valor)` | Envía un comando a la app (ver la tabla de comandos en [ui.md](../architecture/ui.md)) |
| `ui.setting(nombre)` | El valor actual de un ajuste que publica la app, o `nil` |
| `t(clave[, valores])` | El texto de una clave en el idioma del juego, con sus marcadores rellenos: `t("pause_menu.ui_scale.value", {percent = 150})` |
| `ui.languages()` | Los idiomas que tienen los paquetes (`{"en", "es"}`) |
| `ui.prompt(acción)` | El nombre del control de esa acción en el dispositivo actual (`"Enter"`, `"A"`, `"Cross"`), en el idioma del juego; los textos están en `localization/<idioma>/prompt.yaml` |
| `ui.device()`, `ui.gamepad_family()` | `"keyboard_mouse"` o `"gamepad"`; `"xbox"`, `"playstation"`, `"nintendo"`, `"steam"` o `"generic"` |
| `ui.show(nombre)`, `ui.hide(nombre)`, `ui.is_visible(nombre)` | Muestra u oculta un documento cargado |
| `document_name` | El nombre del propio documento |
| `element:get(id)` | Un elemento descendiente, o `nil` |
| `element:set_text(texto)`, `element:text()` | Cambia el texto (siempre como texto, nunca como RML) o lee el contenido |
| `element:set_class(clase, sí)`, `element:has_class(clase)` | Clases |
| `element:attribute(nombre)`, `element:set_attribute(nombre, valor)` | Atributos |
| `element:set_property(nombre, valor)` | Una propiedad de estilo, como `display` |
| `element:focus()`, `element:has_focus()`, `element:click()` | Foco y clic |
| `element.id`, `element:valid()` | Su id; si sigue existiendo |
| `event.type`, `event:stop()` | En un atributo de evento: su tipo; parar la propagación |

Un menú no cambia ajustes por su cuenta: envía el comando y muestra lo que la app publica en `on_setting`. Así sus
etiquetas siempre dicen la verdad (por ejemplo, si la pantalla completa no se pudo activar).

## Probar

- `scripts/dev.sh run -- --ui-demo --content <contenido extraído>` abre la demo de la UI con el minimapa de un mapa
  (`--map <id>`) y un jugador que pasea; la acción `menu` (Tab, Start) abre el menú. Editar y guardar un `.rml`,
  `.rcss` o `.lua` de `content/packs/game/ui/` lo recarga al momento.
- Los errores de RML, RCSS y Lua salen en el log con el fichero y la línea.
- `dwe_ui_tests` recorre el menú de pausa con teclado, mando y ratón; si se cambian los ids de sus elementos hay que
  actualizar sus tests.
