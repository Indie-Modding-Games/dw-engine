# Editar menús

Los menús y el HUD son documentos de RmlUi en el paquete `game` (`content/packs/game/ui/`). Se editan como texto y,
en un build de desarrollo, el juego los recarga solo al guardarlos. Arquitectura en [ui.md](../architecture/ui.md).

## Dónde está cada cosa

```
content/packs/game/ui/
  common/base.rcss           la estructura común, igual en todos los aspectos: display, tamaño, navegación
  common/ui_skin_choice.lua  la entrada «Aspecto de la interfaz» de los menús principal y de pausa
  skins/modern/skin.yaml     el aspecto moderno: qué hojas pone sobre cada pantalla
  skins/modern/theme.rcss    su estilo común: fuente, botones, avisos de botones
  skins/modern/<pantalla>/style.rcss
  skins/original/skin.yaml   el aspecto original (el de por defecto): extiende el moderno, sus hojas de sprites
                             (imágenes del disco por asset id y paleta) y document_classes
  skins/original/<pantalla>/style.rcss
  fonts/                     fuentes (.ttf/.otf) y sus licencias; se cargan todas. original_look.yaml
                             describe la familia original-look (ver docs/architecture/ui.md)
  pause_menu/document.rml    el menú de pausa
  pause_menu/structure.rcss  su comportamiento en todos los aspectos: las listas dan la vuelta
  pause_menu/logic.lua
  sleep_menu/                el menú de dormir (dormir, o guardar y dormir)
  save_slots/, dialog/       la lista de ranuras de la portada y el diálogo de guardar
  hud/document.rml           el HUD con el minimapa
  hud/logic.lua
  ui_sounds.yaml             qué sonido toca cada aviso de los menús (mover, confirmar, cancelar): un banco de
                             sound/ y un sonido de él (ver docs/architecture/audio.md)
```

Cada documento va en su carpeta porque los ficheros se leen por asset id, que es la ruta sin extensión:
`menu.rml` y `menu.rcss` en la misma carpeta chocarían. Un mod puede sustituir cualquiera de estos ficheros con uno
del mismo id en su paquete, o añadir un aspecto entero con su `ui/skins/<id>/skin.yaml` (ver
[Aspectos](../architecture/ui.md#aspectos)). Un documento sustituido no recibe las entradas que el juego añada luego
al suyo: un menú de pausa sustituido no tiene «Dormir» (`#sleep`, que envía `sleep` y se pone en gris con
`pause_menu.sleep_enabled`) hasta que lo añade. Un aspecto que necesita marcar elementos de un documento compartido (por
ejemplo, que las barras del HUD se llenen a cuadritos con la clase `pips`) no copia el documento: lo dice en
`document_classes` de su `skin.yaml`, por documento e id de elemento, y el juego pone y quita esas clases al
cambiar de aspecto.

Las hojas de sprites de un aspecto nombran imágenes del paquete base por su asset id (`texture/etcdat/system_w`), su
paleta y su tamaño en el original (`original_size`); si un paquete de capa remaster trae la misma imagen más grande
(su `_meta.yaml` dice `scale`), el aspecto la usa sin cambiar nada y se ve más nítida. Así llegan las del aspecto
original: `dwx extract` las agranda ×4 con la receta incluida `content/enhance/ui_original.recipe.yaml`. Un aspecto
de un mod que quiere sus propias imágenes agrandadas escribe una receta igual (las imágenes que dibuja, `scale` y el
modo de `index_scale`) y la pasa a mano por `dwx upscale <receta> <paquetes> -o <carpeta>`, que escribe un paquete
remaster para montar al lado ([página de dwx](../modulos/apps/dwx.md#dwx-upscale-y-las-recetas-incluidas)). Nunca
se reparte arte del original: cada jugador lo saca de su disco. Las medidas del aspecto original están en
[UI](../architecture/ui.md#el-aspecto-original).

El estilo va en el aspecto, no en el documento: un documento solo
enlaza `../common/base.rcss` y, si la pantalla tiene un comportamiento propio que no cambia con el aspecto (por
ejemplo, que una lista dé la vuelta), su `structure.rcss`.

## RML y RCSS

RML es casi HTML y RCSS casi CSS; la referencia es la [documentación de RmlUi](https://mikke89.github.io/RmlUiDoc/).
Lo propio de este proyecto:

- Las medidas, en `dp` (un dp es un píxel a 720 líneas; la UI escala sola con la ventana y la opción del jugador).
- Las rutas son relativas al documento (`href="../common/base.rcss"`) o a la raíz de los paquetes
  (`/ui/skins/modern/theme.rcss`). Las imágenes se piden sin depender de la extensión (`.png` encuentra la `.ktx2`
  de un remaster) y con `?palette=N` usan otra paleta de una imagen indexada.
- Un elemento se puede elegir con teclado y mando si tiene `tab-index: auto; focus: auto; nav: auto;` (los `button`
  ya lo tienen en `base.rcss`). `nav-up: #id` y compañía fijan a dónde va cada dirección, por ejemplo para dar la
  vuelta en una lista. El estilo del elemento elegido es `:focus`; el ratón también mueve el foco.
- `autofocus` marca el elemento que recibe el foco al abrir el documento.
- El HUD no debe tapar el ratón: su `body` tiene `pointer-events: none`.
- No hay estilos por defecto: un elemento nuevo necesita su `display` en `base.rcss`.

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
| `ui.skins()`, `ui.skin_name_key(id)` | Los ids de los aspectos que ofrecen los paquetes (`{"modern"}`) y la clave del nombre de uno, para `t()` |
| `ui.prompt(acción)` | El nombre del control de esa acción en el dispositivo actual (`"Enter"`, `"A"`, `"Cross"`), en el idioma del juego; los textos están en `localization/<idioma>/prompt.yaml` |
| `ui.device()`, `ui.gamepad_family()` | `"keyboard_mouse"` o `"gamepad"`; `"xbox"`, `"playstation"`, `"nintendo"`, `"steam"` o `"generic"` |
| `ui.show(nombre[, modal])`, `ui.hide(nombre)`, `ui.is_visible(nombre)` | Muestra u oculta un documento cargado; con `modal` verdadero el documento recibe la navegación (como el menú que abre el juego) |
| `ui.cue(nombre)` | Suena `box_open` o `box_close` (las cajas del aspecto original, `ui_sounds.yaml`) |
| `document_name` | El nombre del propio documento |
| `element:get(id)` | Un elemento descendiente, o `nil` |
| `element:set_text(texto)`, `element:text()` | Cambia el texto (siempre como texto, nunca como RML) o lee el contenido |
| `element:set_class(clase, sí)`, `element:has_class(clase)` | Clases |
| `element:attribute(nombre)`, `element:set_attribute(nombre, valor)` | Atributos (`set_attribute` no acepta los de evento, `on...`) |
| `element:set_property(nombre, valor)` | Una propiedad de estilo, como `display` |
| `element:focus()`, `element:has_focus()`, `element:click()` | Foco y clic |
| `element:rect()` | Izquierda, arriba, ancho y alto del elemento en dp desde la esquina de la ventana |
| `element.id`, `element:valid()` | Su id; si sigue existiendo |
| `event.type`, `event:stop()` | En un atributo de evento: su tipo; parar la propagación |

Las cajas del aspecto original se abren y se cierran con el contorno gris del original con
`ui/common/original_box.lua` (`original_box.open`, `close`, `tick`, `cancel`, `busy`): el documento enlaza el script,
pone `<div id="box-outline" class="box-outline hidden"></div>` bajo el `body` y llama a `tick` desde `on_tick` y a
`cancel` desde `on_hide`. Fuera del aspecto original todo termina al momento. El borde de tres líneas es el decorador
`original-box(3dp)`, o la clase `.original-box` de `skins/original/theme.rcss`.

Un menú no cambia ajustes por su cuenta: envía el comando y muestra lo que la app publica en `on_setting`. Así sus
etiquetas siempre dicen la verdad (por ejemplo, si la pantalla completa no se pudo activar).

## Probar

- `scripts/dev.sh run -- --ui-demo --content <contenido extraído>` abre la demo de la UI con el minimapa de un mapa
  (`--map <id>`) y un jugador que pasea; la acción `menu` (Tab, botón norte) abre el menú. Editar y guardar un `.rml`,
  `.rcss` o `.lua` de `content/packs/game/ui/` lo recarga al momento.
- Los errores de RML, RCSS y Lua salen en el log con el fichero y la línea.
- `dwe_ui_tests` recorre el menú de pausa con teclado, mando y ratón; si se cambian los ids de sus elementos hay que
  actualizar sus tests.
