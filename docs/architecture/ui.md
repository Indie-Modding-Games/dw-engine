# UI

La UI del juego (menús y HUD) vive en `packages/engine/ui`, en la capa de servicios, sobre RmlUi
([ADR 0009](../adr/0009-rmlui.md)). Las herramientas de depuración siguen con Dear ImGui (`packages/engine/debug`).

## Piezas

| Pieza | Qué hace |
|---|---|
| `Ui` | La fachada que usa la app: carga documentos, los muestra, recibe eventos y acciones, devuelve comandos y dibuja |
| Interfaces de RmlUi (`src/rml_interfaces.*`) | Reloj y log (`SystemInterface`), ficheros por asset id (`FileInterface`), rutas relativas al documento y las imágenes generadas (`generated:<nombre>`) |
| `UiRenderer` | Un `render::Drawer` del pase de UI: dibuja lo que RmlUi grabó en `update()` con un pipeline propio (`shaders/rml.slang`, alfa premultiplicado) |
| Puente de render (`src/render_bridge.*`) | La `RenderInterface` de RmlUi: crea las texturas en el `Renderer` y graba cada llamada de dibujo para el `UiRenderer`; sin renderer (tests) solo da tamaños de textura |
| Scripts (`src/scripting.*`) | El `ScriptVm` propio de la UI, la API de Lua y los instanciadores de RmlUi: cada `<body>` es un `ScriptedDocument` con su entorno de Lua, y cada atributo `on*` un manejador en Lua |
| `Navigation` | Convierte las acciones de un tick en pasos de navegación, con repetición al mantener una dirección |
| `promptControl`, `promptLabel` | El control que dispara una acción en el último dispositivo usado (`key:enter`, `gamepad:south`) y su nombre para el jugador, en el idioma del juego (`localization/<idioma>/prompt.yaml`) |
| Textos | Los textos salen del `localization::Catalog` que da la app (`UiDesc::texts`; sin él, la UI carga el inglés): `text-key` en el RML, `t()` en Lua, y `refreshTexts()` al cambiar de idioma ([localización](localizacion.md)) |
| Sonidos de menú (`UiCue`, `takeCues`) | Confirmar y cancelar suenan siempre y mover cuando el cursor se mueve, en el documento modal de arriba. Un `<body own-sounds="true">` no suena: su juego toca sus propios sonidos (la caja de los huevos, `DOO2_tickEggInput`). `ui.cue` en Lua suena igual |
| `FontEngine` (`src/font_engine.*`), `BitmapFont` | El motor de fuentes de RmlUi con la familia `original-look` encima: dibuja con la fuente de mapa de bits del original o con la moderna, según el ajuste `text_font` (ver [Aspecto original](#aspecto-original)); el resto de familias va al motor de RmlUi (FreeType) |
| Aspectos (`SkinCatalog`, `src/skin_styler.*`) | Los aspectos que ofrecen los paquetes (`ui/skins/<id>/skin.yaml`) y las hojas de estilo que cada uno pone sobre cada documento; ver [Aspectos](#aspectos) |
| Imágenes (`image_reference.*`) | Lo que nombra una imagen de RCSS o RML: un asset sin extensión y, si se piden, una paleta (`?palette=N`) y un tono (`tone=grey`, unidos con `&`) |
| `buildMinimap` | Dibuja el minimapa desde una rejilla de celdas (`MinimapGrid`) y da el `MinimapLayout` que sitúa posiciones del mundo en la imagen |

`ui` no conoce el juego: recibe valores planos (la rejilla del minimapa, las posiciones de los marcadores, los
ajustes) y devuelve comandos con nombre. La app hace de pegamento.

## Ciclo de un frame

```mermaid
sequenceDiagram
    participant App as apps/game
    participant Ui
    participant Game as juego (field)
    App->>Ui: handleEvent(evento) por cada evento de platform
    Note right of App: si devuelve true, el evento no llega a InputState
    App->>Ui: tick(input, segundos) en cada tick lógico
    App->>Game: tick del juego, sin acciones si ui.capturesInput()
    App->>Ui: takeCommands() y aplica cada comando
    App->>Ui: setSetting(...) con el estado real
    App->>Ui: setMinimapMarkers(jugador, compañero, enemigos)
    App->>Ui: update() (layout y grabación)
    App->>App: renderFrame con el UiRenderer en FrameDesc::ui
```

## Arranque

```cpp
auto uiRenderer = ui::UiRenderer::create(renderer);           // después del Renderer
auto ui = ui::Ui::create({.files = &vfs, .renderer = uiRenderer->get(), .texts = &catalog, .width = w,
                          .height = h, .scale = 1.0F, .hotReload = true, .clock = {}, .scripts = {}});
ui->loadDocument("hud", hudId);                                // ui/hud/document
ui->loadDocument("pause_menu", menuId);                        // ui/pause_menu/document
ui->loadDocument("main_menu", mainMenuId);                     // ui/main_menu/document
ui->setMenuDocument("pause_menu");
ui->show("hud");
ui->show("main_menu", true);                                   // al arrancar
ui->setMinimap(ui::minimapGridOf(map), claveDelNombre);        // en cada cambio de mapa: main.map_names.0003
ui->refreshTexts();                                            // tras catalog.setLanguage(...)
```

Se destruyen al revés: `Ui` (suelta sus texturas en el renderer), `UiRenderer`, `Renderer`.

### Pantallas e interfaz por defecto

El juego pide **pantallas** (`hud`, `pause_menu`, `partner_menu`...) y la interfaz activa decide qué documento las
enseña. La interfaz por defecto se describe en `content/packs/game/ui/addon.yaml` (esquema
`content/schemas/ui_addon.schema.json`): nombra el documento de cada pantalla, y el juego lo carga con el nombre de la
pantalla (`ui::readUiAddon` + `loadDocument`). Un test comprueba que cada `ui/<carpeta>/document.rml` del paquete está
ahí, salvo `ui/dev_assets`, que carga `DevAssetsBanner`.

`Ui::requestScreen`, `closeScreen` e `isScreenOpen` piden una pantalla por su nombre. `Ui::mapScreen(pantalla,
documento)` hace que esa pantalla la enseñe otro documento cargado (el de un addon de interfaz); las que no se mapean
siguen con el documento de su nombre, y `clearScreenMap` vuelve a la interfaz por defecto. `show`, `hide`,
`isVisible`, las llamadas que leen o cambian elementos de un documento, la tecla del menú y el `ui.show` de los
scripts leen el nombre igual: primero como pantalla, luego como documento. Así las ~180 llamadas del juego que ya usan
el nombre de la pantalla no cambian.

### Addons de interfaz

Un addon de interfaz es una carpeta `ui/addons/<id>/` con su `addon.yaml` (`docs/modding/interfaces.md`). Vienen de
dos sitios: de cualquier paquete montado y de la carpeta de datos del usuario,
`<datos de usuario>/indie-modding-games/dwe/ui/addons/`, que `content::UserUiAddonsFileSystem` monta como un paquete
sintético de capa `user` (`user_ui_addons`) que solo sirve esa carpeta. `ui::findUiAddons` los busca en el VFS y
descarta los que no se leen o cuyo `id` no es el nombre de su carpeta.

`Ui::useUiAddon` comprueba que es de tipo `ui`, que su rango `ui_api` contiene la versión actual y que solo cubre
pantallas que el juego tiene; carga sus documentos como `<id>.<pantalla>` y mapea las pantallas. Si algo falla no
cambia nada y devuelve el porqué. Las pantallas visibles al cambiar se vuelven a enseñar en el documento nuevo.
`useDefaultUi` descarga los documentos del addon.

Un **complemento** (`type: plugin`) no cubre pantallas: `Ui::addUiPlugin` carga sus documentos como
`<id>.<ruta con puntos>` y los enseña, no modales y sin quitar el foco, sobre la interfaz que esté puesta, también al
cambiarla. `removeUiPlugins` los quita todos. El juego los enciende o apaga todos juntos (`ui_plugins`).

El código del addon corre aislado: `CodeTrust::allowAddon` deja correr los documentos, plantillas y scripts de su
carpeta, y su entorno Lua tiene un `ui.send` que solo deja pasar los comandos y opciones que declara el contrato
(`Ui::setAddonCommandFilter`, `app::addonMaySend`); cualquier otro lanza un error de Lua. Una plantilla de un addon
dentro de un documento del motor se rechaza.

En el juego, `UiAddonChanger` (`apps/game/src/ui_addon_setting.hpp`) publica `ui_addon`, `ui_addons`,
`ui_addon.name.<id>`, `ui_addon.safe`, `ui_addon.problem`, `ui_plugins` y `ui_plugins.count`; las filas «Interfaz» y
«Complementos» de Opciones → Interfaz mandan `ui_addon` y `ui_plugins`, que se guardan en los ajustes del usuario y
se aplican al cerrar el menú. La consola y las rutas usan `ui_addon("id")`. Si el addon no puede ponerse,
vuelve la interfaz del juego y `ui/ui_notice` lo avisa unos segundos. **Modo seguro:** `--safe-ui` o Mayús pulsada al
arrancar dejan la interfaz del juego y no montan la carpeta de addons del usuario.

## Dibujo

RmlUi dibuja en dos tiempos. `Ui::update()` hace el layout y llama a `Render()` de RmlUi fuera del frame: la
`RenderInterface` crea las texturas que falten (`Renderer::queueTexture`, con alfa premultiplicado; las imágenes de
fichero se premultiplican al cargarlas) y graba cada `RenderGeometry` con su traslación, textura, recorte (scissor) y
transformación. La geometría se sube una sola vez, cuando RmlUi la compila (`CompileGeometry`), a dos buffers de GPU
(`rhi::Buffer::createMapped`), que se liberan con la destrucción diferida del dispositivo cuando RmlUi la suelta, así
que los frames en vuelo pueden seguir dibujándola. Si no hay memoria para la geometría de un elemento, ese elemento
no se dibuja (con un aviso en el log) y el resto de la UI sí. Después, en `renderFrame`, `UiRenderer::draw` solo
enlaza esos buffers y dibuja cada llamada. El recorte se ajusta al target. Las máscaras de
recorte de RmlUi (`EnableClipMask`) no están implementadas: solo hacen falta para recortar elementos transformados.

## Entrada

- **Ratón:** movimiento, botones y rueda van a RmlUi en píxeles físicos. Un clic o una rueda se quedan en la UI si
  hay un documento modal o si el puntero está sobre un elemento que acepta el puntero. El HUD declara
  `pointer-events: none`, así que nunca roba clics al juego. Con un menú abierto el foco sigue al puntero cuando
  este entra en otra entrada; un puntero quieto (o que tiembla sobre la misma entrada) no le quita el foco al
  teclado ni al mando, y tampoco la posición que da la ventana tras un cambio de tamaño o al recuperar el foco, que
  el jugador no ha movido. El clic derecho vuelve atrás, como `cancel`.
- **Pérdida del foco de la ventana** (`FocusLost`): los botones del ratón pulsados sobre la UI se sueltan sin hacer
  clic, porque la ventana no verá cuándo se sueltan de verdad.
- **Teclado y mando:** no se leen teclas, sino las acciones de `InputState`: `move_up/down/left/right` (teclas, cruceta
  y stick), `confirm`, `cancel` y `menu`. Cada paso va primero al `on_action` del documento modal; si no lo atiende,
  las direcciones mueven el foco con las propiedades `nav` de RmlUi, `confirm` hace clic en el elemento con el foco y
  `cancel`/`menu` cierran el documento.
- **Sin menú**, solo `menu` hace algo: abre el documento de menú (`setMenuDocument`). Durante el tick en que la UI
  usa una acción, `capturesInput()` es true, así que la misma pulsación no llega también al juego.
- **Avisos de botones:** `ui.prompt(acción)` da el nombre del control según el último dispositivo y la familia del
  mando (`Intro`/`Ctrl`, `A`/`B`, `Cruz`/`Círculo`, `B`/`A` en Nintendo). El motor solo nombra el control
  (`key:enter`, `mouse:left`, `gamepad:south`, `gamepad:left_stick`); el texto sale de la tabla `prompt` del idioma
  del juego (`localization/<idioma>/prompt.yaml`): `prompt.key.enter`, `prompt.gamepad.south`, y
  `prompt.family.<familia>.<botón>` para los botones que cada familia nombra a su manera. Las letras, cifras y
  F1–F12 sin entrada se muestran tal cual. Cuando cambia el dispositivo, los documentos visibles reciben `on_device`;
  cuando cambia el idioma, `on_language`. La tecla que se muestra es la primera de la acción: confirmar es Espacio
  (también Intro).
- **Glifos de botones:** los avisos no son texto sino el dibujo del control del último dispositivo, y cambian en vivo
  al tocar otro (`on_device`). `element:set_prompt(acción)` (`promptGlyph`, `promptClasses` y `promptMarkup` de
  `prompts.hpp`) pone en el elemento las clases del control y sus partes: `glyph-key` (tecla, con su nombre en una
  tecla dibujada por el aspecto), `glyph-mouse`, `glyph-button` (Start, gatillos, cruceta) y `glyph-face` para los
  cuatro botones frontales, con el estilo de la familia: `family-letters` (sus letras, de `prompt.family.*`: unas
  familias tienen la A abajo y otras a la derecha), `family-shapes` (cruz, círculo, cuadrado y triángulo, el mando del
  original, con `shape-cross`…) o `family-positions` para un mando que el juego no conoce (cuatro puntos con el de la
  posición relleno). Las partes son `.glyph-label` (el nombre), `.glyph-char` (el carácter de la fuente del original
  para una forma, que dibuja el aspecto clásico), `.glyph-art` (la forma o la posición dibujada por el motor en blanco,
  la del aspecto moderno, cuya fuente no tiene ○ □ △) y, con `set_prompt(acción, true)`, los tres dibujos
  `.glyph-frame` del botón animado del aspecto clásico. Todo el arte es nuestro: `glyph_art.hpp` dibuja por código las
  rutas `/glyph/classic/button|cap/<símbolo>/<dibujo>` (un botón redondo de 16 x 12 o una tecla de 12 de alto al
  estilo del original, con su parte de arriba hundiéndose un píxel por dibujo, y una letra de 3 x 5 propia) y
  `/glyph/modern/<símbolo>` (64 x 64, suavizado: las cuatro formas, las cuatro posiciones y las flechas `@left`,
  `@right`, `@up` y `@down`), nunca logotipos de fabricantes.
- **Marcadores en los textos:** `element:set_prompt_text(texto)` cambia cada `{prompt:<acción>}` del texto por el glifo
  de esa acción (`promptText`); con `set_prompt_text(texto, true)`, para un texto del original, también sus caracteres
  de botón y su «X button»: la cruz (que la extracción escribe «△», celda 77 de la fuente) es `confirm`, el triángulo
  («×», celda 78) `menu` (el menú de juego y el cancelar del original), el círculo `alt` y el cuadrado `extra`, los
  botones a los que el juego los asigna. Lo usan las filas de la caja de la historia, la ayuda del curling y los
  textos con `{key}` de nuestros menús (`pause_menu.settings_hint`, «Pulsa …» de la portada). Una traducción escribe
  el marcador donde el inglés nombra un botón con palabras (`scripts/translation.sh` no lo cuenta como etiqueta).
- **Mando virtual:** `Platform::attachVirtualGamepad` conecta un mando de software que pasa por el mismo camino que uno
  real. Lo usan los tests de `platform` y la demo (`--virtual-gamepad`).

## Escala

El contexto mide la ventana en píxeles. Las medidas de RCSS van en `dp`; un dp es un píxel a 720 líneas, y la
proporción es `min(alto/720, ancho/960)` (entre 0,5 y 8) por la escala que elige el jugador (`setScale`), que se
limita a `[Ui::MinScale, Ui::MaxScale]` = [0,5, 3]; un valor que no es un número se ignora. En una pantalla de alta
densidad la ventana tiene más píxeles y la proporción crece con ellos: el HUD y los menús ocupan lo mismo.

Las cajas que van en el sitio del original (el cuadro de diálogo `story_textbox`, en dp de 486 a 663 de los 720, y las
notas que usan su caja: `field_item_notice` y `award_notice`) no se salen de la ventana con una escala grande:
`ui/common/screen_fit.lua` mide la caja y la ventana en cada tick y, si la caja no cabe con 6 dp de margen, la dibuja
más pequeña lo justo y la mete desde el borde que cruza (`transform`, sin tocar su maquetación). Una caja que cabe se
dibuja tal cual, así que a la escala por defecto nada cambia. Antes, al 115 % en una ventana 16:10 (o al 130 % en una
16:9) el cuadro de diálogo pasaba del borde de abajo y cortaba su última fila. El puntero usa `Ui::elementDrawnRect`
(el rectángulo dibujado, con las transformaciones) para elegir una opción del cuadro. Prueba: `screen_fit_tests.cpp`
(16:10 al 115 %, 16:9 al 130 % y al 150 %, 4:3 y una ventana de 800 × 600 al 200 %, en las dos pieles).

El menú del compañero (la página de estado, la técnica y la bolsa) usa lo mismo: en el aspecto original el escenario
de 960 × 720 dp entero (el panel, sus avisos y las cajas de la bolsa a su lado) se dibuja más pequeño alrededor del
panel con `screen_fit.apply_around`, que pone la transformación en `#scrim`; en el moderno solo el panel, porque sus
cajas son diálogos centrados. El puntero sigue a lo dibujado (RmlUi proyecta el clic con la transformación). La página
de opciones no se encoge: con menos de 700 dp de alto (aspecto original) o de 660 dp (moderno) la lista de opciones
pasa a desplazarse en menos filas (reglas `@media (max-height: …)` de cada piel), así que la descripción y las acciones
de debajo siguen en la ventana. Pruebas: `screen_fit_tests.cpp` (página de estado) e `in_game_options_tests.cpp`
(opciones sobre el título y en partida a 1680 × 1050 al 115 % y a 1920 × 1080 y 1280 × 720 al 130 %). La
comprobación de textos (`overflowingTexts`) no cuenta las cajas de las filas que la lista ha desplazado fuera de su
caja: no se dibujan.

## Minimapa

`MinimapGrid` es la rejilla del mapa con tres clases de celda: bloqueada (transparente), transitable y salida.
`minimapGridOf(mapDocument)` la construye desde el `MapDocument`: las celdas con el bit `0x80` están bloqueadas y las de valor 110–119 son las
salidas de los warps 0–9 que llevan a algún mapa (`tamer.c`). Una celda de rejilla cubre 100 unidades; la celda
`(columna, fila)` empieza en `x = (columna − 50) · 100`, `z = (fila − 50) · 100` del mundo (`map_collision.c:
getTileTrigger`, con `world-space.md`).

`buildMinimap` recorta la imagen al rectángulo de las celdas transitables más un margen, rellena las celdas (azul
claro translúcido), dibuja un borde claro donde tocan una pared y pinta las salidas en naranja. La imagen se registra
como `generated:minimap/<n>`.

El HUD la enseña como un **minimapa redondo** (una mejora, encendida por defecto con los dos aspectos): un disco del
mapa alrededor del jugador, con el norte (−Z) arriba, el nombre del mapa encima y el día y la hora debajo. Lo dibuja el
decorador `minimap-disc` (`packages/engine/ui/src/minimap_disc.*`), que rellena la caja de `#minimap-frame` con un abanico de
triángulos cuyas coordenadas de textura son la ventana del disco sobre la imagen (`minimapDiscView`: centro en el
jugador, radio `minimapViewRadius`, la mitad del lado mayor del mapa entre 800 y 1800 unidades) y dibuja fuera de esa
caja el aro: `plain` es un aro con un brillo hacia fuera y una sombra hacia dentro (el aspecto moderno), `original`
son cuatro franjas de un píxel del original con los colores del borde de sus ventanas (negro, cian claro, cian oscuro y
negro, `ui.c: renderUIBoxBorder`). Fuera de la imagen el muestreador toma el color de su borde, que son celdas bloqueadas
y por tanto transparentes.

**Por qué con el norte arriba y no girando:** las cámaras del original son fijas por mapa y cambian de ángulo dentro
de un mismo mapa; un minimapa que girara con la cámara saltaría en cada corte, y uno que girara con el jugador no
coincidiría con la pantalla fija. Con el norte arriba cada mapa tiene siempre la misma forma.

El marcador del jugador va siempre en el centro del disco y gira según hacia dónde mira; el del compañero va donde
está respecto al jugador (`locateOnDisc`) y, si queda fuera de la vista, en el borde (al 90 % del radio) en su
dirección, con la clase `pinned`. `setMinimapMarkers` se llama en cada frame, pero la UI guarda los elementos de los
marcadores y lo que muestran: solo los vuelve a buscar cuando se cargan documentos, y solo les cambia las propiedades
cuando el marcador se ha movido; el decorador rehace el abanico solo cuando la ventana cambia. El cálculo
(`minimapViewRadius`, `minimapDiscView`, `locateOnDisc`, `minimapHeading`) está en `dwe/ui/minimap.hpp`, sin nada del
HUD, para que otra vista del mapa (un mapa grande) pueda usarlo igual.

### Contrato del HUD

La UI rellena estos ids en cualquier documento que los tenga (constantes en `dwe::ui::hud`, en `ui.hpp`). Un HUD de
un mod puede colocarlos y darles estilo como quiera, pero con estos ids y estos papeles:

| Id | Constante | Qué hace la UI |
|---|---|---|
| `minimap-frame` | `hud::MinimapFrame` | El disco: un aspecto le pone `decorator: minimap-disc(...)` y la UI le da la ventana del mapa; los marcadores van dentro, en porcentaje de su caja |
| `minimap-name` | `hud::MinimapName` | Su texto es el nombre del mapa, el texto de la clave que da `setMinimap`, en el idioma del juego |
| `minimap-player`, `minimap-partner` | `hud::MinimapPlayer`, `hud::MinimapPartner` | Los marcadores: `left`, `top` (en porcentaje del disco; el jugador siempre al 50 %), `transform: rotate(...)`, la clase `pinned` en el compañero fuera de la vista, y `display: none` sin marcador |
| `minimap-enemies` | `hud::MinimapEnemies` | La capa de los enemigos, dentro del disco y antes de los marcadores del compañero y del jugador (que quedan encima): la UI le crea un hijo `<img class="marker enemy" id="minimap-enemy-N">` por enemigo (`hud::MinimapEnemyPrefix`, imagen `generated:minimap_enemy`: disco blanco con borde oscuro que el aspecto tiñe con `image-color`), con `left`, `top` como el compañero, y `display: none` en los que sobran o quedan fuera de la vista |
| `minimap-items` | `hud::MinimapItems` | La capa de los objetos del suelo, dentro del disco y antes de la de los enemigos (que queda encima): como la de los enemigos, un hijo `<img class="marker item" id="minimap-item-N">` por objeto (`hud::MinimapItemPrefix`, imagen `generated:minimap_item`: un rombo blanco con borde oscuro que el aspecto tiñe con `image-color`), con `left`, `top` y `display: none` en los que sobran o quedan fuera de la vista |

El script del HUD (`ui/hud/view.lua`) escribe además el día y la hora en `#minimap-day` y `#minimap-time`, y la
marca del norte es el texto `hud.minimap.north` de `#minimap-north`.

**El minimapa es una diferencia opcional: el original no tiene minimapa.** El ajuste `minimap` vale `on` (al
empezar) u `off`; lo cambian la fila «Mostrar minimapa» de las opciones en partida (`ui/common/game_options.lua`), la
página «Minimapa» del menú de pausa y las opciones del título, y se aplica al momento. Con `off` el HUD pone la clase
`hidden` en `#minimap`; también la pone en un combate (`battle.active`) y mientras el juego no lo deja ver
(`hud.minimap_allowed` falso: escenas, charlas, menús, fundidos y minijuegos; [game.md](../modulos/apps/game.md#minimapa-redondo)).
Sustituye al antiguo `auto` («según el aspecto») y al booleano `minimap_visible`: el `auto` guardado y cualquier valor
desconocido se leen como `on` (este último se avisa en el log), y el comando antiguo todavía se acepta, con `true`
como `on` y `false` como `off` (`minimapModeOf`, `apps/game/src/minimap_setting.hpp`). La elección se guarda en el
`settings.yaml` de los datos del usuario, como `ui_skin` y `text_font` (clave `minimap`, `MinimapChanger`; las
autopruebas no lo tocan), y la partida siguiente empieza con ella (`startMinimapMode`).

Los enemigos del mapa son otra opción: `setMinimapMarkers` recibe además la lista de enemigos (vacía con el ajuste
`minimap_enemies` en `false`, fila «Enemigos en el minimapa»), que el juego saca de los personajes del mapa que
pueden pelear ([game.md](../modulos/apps/game.md#enemigos-en-el-minimapa)). Cada uno va donde está respecto al
jugador (`locateOnDisc`, hasta el 92 % del radio, `MinimapEnemyRim`); uno fuera de la vista se esconde. El disco va
con el norte arriba y los puntos son redondos, así que no giran. Igual que los demás marcadores, la UI solo les
cambia las propiedades cuando se mueven.

Los objetos tirados en el suelo son una tercera lista de `setMinimapMarkers`, con su propia opción (`minimap_items`,
fila «Objetos en el minimapa», [game.md](../modulos/apps/game.md#objetos-en-el-minimapa)): se colocan igual que los
enemigos, en su capa `minimap-items`, con rombos en vez de puntos.

Una capa solo cuenta dentro del disco (`#minimap-frame`): la página de opciones da a sus filas el id de su opción, y
las filas «Objetos en el minimapa» (`minimap-items`) y «Enemigos en el minimapa» (`minimap-enemies`) se llaman igual que
las capas. Antes la UI las tomaba por capas y escondía su texto en partida. Prueba: `in_game_options_tests.cpp`.

## Estado del campo

El HUD enseña en el campo la hora del juego y el estado del compañero. Los datos y su dibujo van separados: un modelo
de datos que no sabe nada de aspectos, y cada aspecto es una vista de él (el moderno ahora; el de aspecto original
dibuja los mismos datos con su propio documento).

- **Modelo:** `FieldHudView` (`packages/screens`: `field_hud_view.hpp`), solo números y booleanos. Lo construye
  `fieldHudViewOf(hora, compañero, shown)` desde `GameSession::clock().time()` (`ruleset::GameTime`) y
  `GameSession::partnerView()` (`packages/session`: `partner_view.cpp: viewOf`), sin decidir ninguna regla: los rangos
  salen del ruleset (`care.yaml`: `limits`) y las necesidades son las condiciones que ya tiene el compañero.
- **Enlace:** `FieldHud` (`apps/game/src/field_hud.*`) publica las etiquetas una vez al crearse
  (`fieldHudLabelSettings`) y, en cada frame, construye la vista y la compara con la última que publicó. Solo si
  cambió la convierte en ajustes (`settingsOf`) y la publica con `screens::publish`, que cambia solo los ajustes que
  cambian y después sube `hud.revision` en uno. Un frame en el que no cambia nada no construye ningún texto ni toca
  la UI.
- **Vista:** el documento redibuja el estado entero cuando cambia `hud.revision` (`on_setting`), así nunca lee media
  vista; las etiquetas, cuando cambia un ajuste `*.label` y al cambiar de idioma (`on_language`). El documento del
  aspecto moderno es `ui/hud/document` con su `logic.lua`; uno de otro aspecto (`screens.hud.document` en
  `skin.yaml`) lee los mismos ajustes y pone sus propios ids.

**Contrato de datos.** Estos ajustes son el contrato entre el juego y cualquier vista del estado del campo (un
aspecto del juego o de un mod). Los nombres son constantes de `field_hud_view.hpp`; los números van como `double`
con valor entero.

| Ajuste | Valor | Qué es |
|---|---|---|
| `hud.revision` | número | Sube en uno cada vez que cambia algún valor de abajo; el documento redibuja con él |
| `hud.shown` | booleano | `false` mientras el campo está parado: un menú (pausa, principal, compañero), un combate, una escena o charla de un guion, un fundido o un cambio de mapa, o sin partida. El original saca el reloj y las barras de la pantalla cuando para la hora del juego (`clock.c: renderGameClock`, `stopGameTime`) |
| `hud.clock.hour`, `hud.clock.minute` | 0–23, 0–59 | La hora del juego. Un minuto de juego dura 20 frames del original, un segundo real a velocidad normal (`clock.c: tickGameClock`; sección `clock` del ruleset) |
| `hud.clock.day`, `hud.clock.year` | desde 1 | El día del año (1–30 con `days_per_year: 30`) y el año, contados desde 1 como los enseña el menú del original (`game_menu.c: renderDateDigits`: `DAY + 1`, `YEAR + 1`); el reloj los guarda desde 0 |
| `hud.meter.<i>.{value,min,max}` | números | Felicidad (0) y disciplina (1), con su rango del ruleset: −100 a 100 y 0 a 100 en `original` (`partner_impl.c: tickConditionBoundaries`) |
| `hud.meter.<i>.{label,two_tone}` | clave, booleano | Una vez: las claves `partner_menu.happiness` y `partner_menu.discipline`, y `true`: las dos son barras de dos tonos, que se llenan del mínimo a la mitad del rango y otra vez, en otro color, de la mitad al máximo (`partner_impl.c: renderStatusBars`) |
| `hud.need.<i>.active` | booleano | Hambre (0), sueño (1), baño (2), cansancio (3), herida (4) y enfermedad (5): si el compañero tiene ahora esa necesidad, es decir, la condición `hungry`, `sleepy`, `needs_toilet`, `tired`, `injured` o `sick` (ver abajo cuándo) |
| `hud.need.<i>.urgency` | 0–6 | Cuánto urge cada necesidad que tiene, entre las que tiene: 1 la que antes hace un daño duradero si nadie la atiende, luego 2 y siguientes; 0 si no la tiene. Lo calcula `fieldHudViewOf` (`packages/screens`) de `game::partner::needDeadlinesOf` (ver «Varias necesidades a la vez», abajo); las que no tienen plazo van detrás de las que sí, y los empates siguen el orden de la burbuja del original. Solo lo usa la burbuja apilada (mejora) |
| `hud.need.<i>.label` | clave | Una vez: `hud.need.hunger`, `hud.need.sleep`, `hud.need.toilet`, `hud.need.tired`, `hud.need.injured`, `hud.need.sick` |
| `hud.pool.<i>.{value,max}` | números | HP (0) y MP (1): lo que le queda y su máximo |
| `hud.pool.<i>.label` | clave | Una vez: las etiquetas del original, `main.stat_labels.0000` (HP) y `.0001` (MP) |
| `hud.partner.area` | 0, 1, 2 | Lo que le parece al compañero el mapa en el que está, con los números del original (`MAP_TILE_DATA.partnerAreaResponse`): 0 nada en especial, 1 le gusta, 2 no le gusta. Va con `hud.revision`, como el resto del estado. El original lo calcula al cargar el mapa (`map.c: loadMapSetup`): recorre a la vez, casilla a casilla, las dos listas de regiones del mapa (`regions.a` y `regions.b` de `map/<id>/data.yaml`) y la primera casilla de cualquiera de las dos que tenga la región favorita de la especie (`favored_region` de `data/raise`) decide: la lista `a` da 1 y la `b` da 2. La sesión lo calcula igual al entrar en cada mapa (`packages/game/partner`: `areaResponseOf`; `GameSession::areaResponse`), y lo usan también las reglas del cuidado (`CareContext::area`, `SleepContext::area`) |
| `hud.partner.revision` | número | Sube en uno cada vez que cambia alguno de los tres de abajo. Va aparte de `hud.revision` porque cambia mientras el compañero anda, y así el resto del estado no se redibuja con él |
| `hud.partner.x`, `hud.partner.y` | 0–100, décimas | Dónde se asienta la burbuja del original sobre el compañero, en porcentaje del ancho y el alto de la ventana desde la esquina de arriba a la izquierda, redondeado a décimas (menos de un píxel en una ventana de 1000). La burbuja nace a la altura de la especie menos 20 unidades sobre la posición del compañero (`bubble.c: addConditionBubble`; la altura es `height` de `data/digimon`) y en sus 10 primeros frames sube 65 más, donde se queda (`bubble.c: tickConditionBubble`); ese es el punto publicado (`BubbleDrop` y `BubbleRise` en `apps/game/src/game.cpp: partnerBubbleOf`). Su vaivén de 12 unidades arriba y abajo alrededor de él es animación del aspecto y no se publica |
| `hud.partner.visible` | booleano | `false` con el campo parado (como `hud.shown`; el original no dibuja las burbujas en un menú), salvo mientras sale el globo de la evolución, en una escena de cuidado (`handleConditionBubble` solo corre en el estado normal del compañero) o con el punto fuera de la ventana o detrás de la cámara; entonces `x` e `y` valen 0 |
| `hud.partner.evolving` | 0–51 | El cuadro del globo de la evolución (`bubble.c: CONDITION_ICON_EVOLVING`, que `main/evl.c: tickEvoSequenceLoading` crea al empezar el clip `ready` de la escena), de 1 a 51 (el estallido); 0 sin globo (`apps/game: evolving_bubble.hpp`) |

**Cuándo cuenta una necesidad.** En el original una necesidad está o no está: no hay grados de urgencia. Está desde
que se pone su condición y hasta que se atiende o se acaba su plazo, y mientras está el original lo enseña con una
burbuja sobre el compañero (`bubble.c`; por prioridad, sueño, cansancio, hambre, baño, herida y enfermedad:
`partner_impl.c: handleConditionBubble`), no en un HUD.

| Necesidad | Empieza | Acaba sola (falta de cuidado) |
|---|---|---|
| Hambre | Cuando el contador de la siguiente comida llega a 0 (`partner_impl.c: tickHungerMechanics`) | A los 90 minutos de juego sin comer |
| Sueño | Al entrar en su franja de sueño (`partner_impl.c: tickSleepMechanics`) | A la hora de despertar |
| Baño | Cuando el contador del baño llega a 0 (`partner_impl.c: tickPoopingMechanic`) | Tras `(disciplina + 20) × 0,6` minutos de juego, haciéndolo donde está |
| Cansancio | Cuando el cansancio llega a 80 (`tired_threshold` del ruleset; `partner_impl.c: tickTirednessMechanics`) | No acaba sola: el cansancio baja descansando o durmiendo, y la condición se va al bajar de 80 |
| Herida | Por azar al acabar un combate (`Partner::applyBattle`) | No acaba sola: se cura con una medicina, y a las 12 horas de juego se vuelve enfermedad (`partner_impl.c: tickSicknessMechanics`) |
| Enfermedad | Por falta de sueño, por un mapa que no le gusta, por una herida sin curar o por comida (`partner_impl.c: tickSicknessMechanics`) | No acaba sola: se cura con una medicina o en el hospital; a las 12 horas de juego pierde una vida |

Estos plazos son reglas del compañero (`packages/game/partner`, ruleset `original`); el contrato solo publica si la necesidad
está.

**Lo que dibuja el original en el campo**, como referencia para las vistas (`clock.c: renderGameClock`,
`partner_impl.c: renderStatusBars`), en la pantalla de 320×240:

- Arriba a la izquierda, un reloj de esfera: una aguja de minutos que da una vuelta por hora y un punto que da una
  vuelta al día por un anillo de 24 horas (la hora 0 arriba), con un sol o una luna encima (luna con la hora antes de
  las 6 o desde las 17). Sin cifras y sin el día.
- Abajo a la derecha, solo las barras de felicidad y disciplina, cada una con su icono (felicidad: cara contenta con
  0 o más, triste por debajo; disciplina: un icono con 50 o más y otro por debajo). Cada barra son 25 cuadritos de
  2 píxeles: 4 puntos por cuadrito en la felicidad y 2 en la disciplina, en dos tonos como arriba.
- Reloj y barras entran y salen deslizándose (50 píxeles por frame) cuando la hora del juego anda o se para: el reloj
  hacia la izquierda (`clock.c: renderGameClock`, `CLOCK_OFFSET_X` entre -135 y -220) y las barras hacia la derecha
  (`partner_impl.c: renderStatusBars`, `STATUS_UI_OFFSET_X` entre 75 y 240). El sol y la luna pasan por sus cuatro
  cuadros, uno cada 4 frames (`(CURRENT_FRAME % 16) / 4`). HP, MP, el día y las necesidades no se ven en el campo:
  HP y MP están en el menú del compañero y el día en el menú del domador.

**El parpadeo del icono de la felicidad.** En una zona que al compañero le gusta o le disgusta, el original alterna
el icono de la felicidad con otra cara: mientras `PLAYTIME_FRAMES % 10 < 5`, 5 de cada 10 frames, dibuja la cara que
está 11 píxeles a la izquierda en SYSTEM_W si le gusta y 11 a la derecha si no (`partner_impl.c: renderStatusBars`);
los otros 5 frames, la de siempre. El script pone en `#field-status` la clase `area-liked` o `area-disliked` según
`hud.partner.area`, y el aspecto original alterna la cara con una animación de 0,5 s: la otra cara la primera mitad
y la de siempre la segunda.

**Diferencia visible conocida: la fase del parpadeo.** La animación empieza cuando se pone la clase (al entrar en la
zona o al cambiar la cara), no al compás del contador de frames de juego del original (`PLAYTIME_FRAMES`): el ritmo
es el mismo, pero la fase puede ir desplazada hasta 0,25 s. El contrato no publica ese contador porque publicarlo
obligaría a redibujar el estado del campo 20 veces por segundo (ver la tabla de elecciones del aspecto original).

**La vista compartida.** Todos los aspectos del juego dibujan el estado del campo con el mismo documento
(`ui/hud/document.rml`) y el mismo `logic.lua`, y solo cambian sus hojas RCSS. Lo que el script deja para que cada
aspecto lo dibuje a su manera:

| Elemento o clase | Qué hace el script | Para qué |
|---|---|---|
| clase `shown` en `#field-status` | La pone con `hud.shown` y la quita sin él | Cada aspecto decide cómo se va el panel: el moderno lo quita (`display: none`); el original puede deslizarlo, como `clock.c` (50 píxeles por frame) |
| maquetación `bars: pips` del HUD (`skin.yaml`, `layout: hud:`) | Con ella el relleno va en 25 pasos del 4 %, redondeando hacia abajo, con la misma regla de dos tonos; la clase `pips` de las barras (`#field-meter-<i>-bar`, `#field-pool-<i>-bar`) es solo su estilo | Los 25 cuadritos de `partner_impl.c: renderStatusBars` (4 puntos por cuadrito en la felicidad, 2 en la disciplina) |
| clase `area-liked` o `area-disliked` en `#field-status` | Pone `area-liked` con `hud.partner.area` a 1, `area-disliked` con 2, y ninguna con 0 | La cara de la felicidad que alterna con otra en una zona que le gusta o le disgusta al compañero (ver arriba); el moderno no la usa |
| clase `low` en `#field-meter-<i>` | La pone si el valor está por debajo de la mitad de su rango (`(min + max) / 2`) y la quita si no | El icono de la barra: la cara de la felicidad se pone triste por debajo de 0 y el de la disciplina cambia por debajo de 50 (`partner_impl.c: renderStatusBars`); el moderno no la usa |
| `#field-partner-bubble` | `left` y `top` desde `hud.partner.x` y `hud.partner.y` (redibuja con `hud.partner.revision`); la clase `shown` solo con `hud.partner.visible` y alguna necesidad; y una sola clase de necesidad, `hunger`, `sleep`, `toilet`, `tired`, `injured` o `sick`; una clase de necesidad en cada `#field-partner-bubble-icon-<i>` que la enseña (0 a 4, la más urgente primero por `hud.need.<i>.urgency`; con una sola, el 0); con varias necesidades y `need_bubble_stack`, `stack-2` a `stack-5` en vez de la clase de necesidad de la burbuja, y un `margin-left` que la mete en la ventana si se saldría por un lado; y `frame-<n>` (1 a 60), el frame del original en que va la burbuja; con `hud.partner.evolving`, la clase `evolving` en la burbuja y en `#field-partner-bubble-icon-0`, ninguna necesidad ni `stack-<n>`, y `frame-<n>` con el cuadro que da la partida | La burbuja del original sobre el compañero (`bubble.c`). Cuelga del centro de su borde de abajo (`ui/hud/structure.rcss`), así que el aspecto solo le da tamaño y dibujo; el moderno no la enseña porque ya tiene el panel de necesidades |
| `#field-clock-minute-hand` | `transform: rotate(minuto × 6deg)` | La aguja de minutos de `clock.c: renderGameClock`, una vuelta por hora |
| `#field-clock-hour-dot` | `left` y `top` en porcentaje sobre un círculo, 15° por hora, la hora 0 arriba | El punto de las horas en el anillo de 24 horas |
| clase `night` en `#field-clock` | La pone con la hora antes de las 6 o desde las 17 | La luna en vez del sol |

**Qué burbuja.** El original elige la necesidad por prioridad, de mayor a menor sueño, cansancio, hambre y baño
(`partner_impl.c: handleConditionBubble` las mira de menor a mayor y gana la última), y cuando una burbuja lleva 50
de sus frames (a 20 por segundo, 2,5 s) cambia a la necesidad más alta de las demás que tenga, si hay alguna: con dos
necesidades se turnan, y una tercera espera a que se vaya una de ellas. El script hace lo mismo con su `on_tick`
(`bubble_priority`, `bubble_swap_frames`), y el tiempo solo cuenta mientras la burbuja se ve (el original no la dibuja
ni la cuenta en un menú).

**Los frames de la burbuja (Igual).** El original cuenta los frames de cada burbuja (`bubble.c: tickConditionBubble`)
y la dibuja en los frames 1 a 51: crece de nada a su tamaño en los 10 primeros (escala (k − 1) / 10), en el 51 estalla
(`CONDITION_SPRITES[10]` a 2,5 veces, con su centro 23 píxeles del globo sobre el ancla) y desaparece; con una sola
necesidad la vuelve a crear a los 60 frames (`partner_impl.c: handleConditionBubble`, `CONDITION_BUBBLE_TIMER`; medido:
180 vblanks), y con otra necesidad la cambia a los 50 sin estallido (medido: 150). Dentro, cada necesidad anima por el
mismo contador (`renderConditionBubble`): la comida pasa por sus cuatro dibujos cada 19 frames
(`CONDITION_HUNGRY_FRAMES`, 3 píxeles más arriba el frame antes de cada cambio), el baño por tres cada 13, la herida
por tres cada 20, la enfermedad alterna dos cada 20, la gota del cansancio cae en 13 frames (`CONDITION_TIRED_FRAME_U`
y `CONDITION_TIRED_FRAME_MY`: de 1 píxel sobre el borde de arriba del globo a 8 por debajo, cambiando de dibujo) y el sueño enseña sus
tres «Z» una a una (`CONDITION_ICON_SLEEPY`: frames 11 a 14 la baja, 15 a 18 dos, 19 a 26 las tres, 27 las dos altas,
28 la más alta, 29 ninguna, y vuelve a empezar en el 30). La «Z» baja tiene forma de reloj de arena en el propio
dibujo del original (medido en DuckStation: frames 12, 17, 23 y 28). El script cuenta los frames y publica
`frame-<n>`; el aspecto original dibuja cada frame con reglas de clase (el crecimiento con `transform`, el dibujo de
cada necesidad y su sitio con `decorator` por frame, el estallido y el hueco del 52 al 60 con `visibility`), sin
animaciones RCSS, así que no hay desfase entre el script y el dibujo. El vaivén de 12 unidades no se imita. El moderno
no enseña la burbuja. Prueba: `ui_tests` «the need bubble plays the original's frames: growth, the need's animation,
burst, gap».

**Herida y enfermedad.** Van por debajo del baño en la prioridad (`partner_impl.c: handleConditionBubble`: bits
`0x20` y `0x40` de la condición, burbujas 6 y 2: la venda de `CONDITION_SPRITES[8]` y la jeringuilla de
`CONDITION_SPRITES[7]`, con sus cuadros como las demás). Al ponerse enfermo, el original además
para la hora y abre el cuadro de los premios con el nombre del compañero y `main.partner_impl_is_sick.0000`
(`partner_impl.c: tickSicknessMechanics`; ver `award_notice` en el juego).

**Mejora (opción): varias necesidades a la vez.** El ajuste `need_bubble_stack` (fila «Varias necesidades», «Several
needs» en inglés, de las opciones; encendido por defecto, guardado en los ajustes del usuario; no cambia ninguna regla)
junta en un solo globo hasta cinco necesidades, una al lado de otra, en una fila, y la más urgente a la izquierda; con
una sola necesidad la burbuja es la del original, y apagado vuelve el original entero (una cada vez, por su prioridad
fija, turnándose las dos más altas). Con las seis a la vez la sexta, la menos urgente, espera.

*Orden por urgencia.* La urgencia es cuánto falta para que la necesidad haga un daño duradero si nadie la atiende,
medido con los mismos contadores que ya corren las reglas del cuidado (`packages/game/partner`:
`need_deadlines.hpp`, `needDeadlinesOf`; `partner_tick.cpp`), en frames del reloj desde ahora:

| Necesidad | Daño | Plazo (reglas `original`) |
|---|---|---|
| Baño | Se hace encima en el suelo: fallo de cuidado, barra del virus, nivel del domador (`tickPoop`) | `urgeFrames`, uno por frame: (disciplina + 20) × 1200 / 100, de 240 a 1440 frames (12 a 72 minutos de juego); 0 si el montón ya espera su escena |
| Hambre | Se cierra la ventana sin comer: fallo de cuidado (`tickHunger`) | `windowSteps` pasos de 10 frames: 90 minutos desde que tiene hambre |
| Sueño | Llega la hora de despertar sin haberlo acostado: fallo de cuidado (`tickSleep`: `missSleep`) | Hasta la hora de despertar (`wakeHour`), horas |
| Herida | A las 12 horas herido enferma (`tickSickness`: `injuredHours`) | (`injury_hours` − horas herido) horas; sin plazo si ya está enfermo, porque no puede volver a enfermar |
| Enfermedad | A las 12 horas sin curar pierde una vida (`tickSickness`: `sickHours`) | (`untreated_hours` − horas enfermo) horas |
| Cansancio | Sin plazo: solo resta felicidad de vez en cuando y le da hambre antes (`tickTiredness`, `tired_hunger_*`); dormir o descansar lo quita | Ninguno: va detrás de las que tienen plazo |

`fieldHudViewOf` las ordena por ese plazo y publica el puesto de cada una en `hud.need.<i>.urgency`; los empates (y
las que no tienen plazo entre sí) siguen el orden de la burbuja del original (sueño, cansancio, hambre, baño, herida,
enfermedad). Como todos los plazos bajan al mismo ritmo, el orden solo cambia cuando entra o sale una necesidad o el
juego reinicia un contador, así que no se vuelve a publicar en cada frame. No cuenta lo que retiene un contador un rato
(el domador ocupado, la hora protegida al despertar). En la práctica el baño sale casi siempre primero, luego el
hambre, el sueño, y la herida y la enfermedad, que dan medio día. Se eligió el tiempo hasta el daño y no una prioridad
fija porque es lo que decide qué atender antes; el orden fijo del original sigue en la burbuja sin la opción.

*Dibujo.* El aspecto original ensancha el globo de 32 píxeles a 50 (dos), 68 (tres), 86 (cuatro) o 104 (cinco) con sus
propias piezas, sin estirar nada: cada mitad es su borde redondo, una columna de su cuerpo repetida
(`tiled-horizontal`) y su mitad del centro con el pico, que queda en medio (`need-bubble-left` … `need-bubble-right`
en `skin.yaml`). Cada icono va en su hueco de 16 píxeles con 2 entre ellos y 8 a los bordes, como el único del
original, a la altura que el original le da y con su animación en los mismos frames. Se eligió una fila y no dos
(3 + 2) porque guarda la altura y el pico del globo del original y se lee en el orden de urgencia; a cinco ocupa un
tercio de la pantalla del original, y el script la mete en la ventana (`margin-left`, que no tocan sus transformaciones)
cuando el compañero está cerca de un borde; la burbuja del original no se mueve. El aspecto moderno no dibuja la
burbuja: su panel de necesidades ya las enseña todas. Se eligió el globo ancho y no turnarlas porque el usuario pidió
verlas a la vez, y las piezas del globo lo ensanchan sin romper su dibujo (queda una píldora con los extremos y el pico
del original). Pruebas: `need_deadlines_tests.cpp` (cada plazo es el frame en que la regla hace el daño), `screens`
«the field HUD ranks the partner's needs by how soon they do harm...», `ui_tests` «the need bubble shows several needs
side by side, most urgent first, unless turned off» (con la opción apagada, también «the need bubble shows the need
the original's bubble would ...») y `hud_fit_tests` «the stacked need bubble keeps five needs in one row inside the
window at every UI scale», «the original's need bubble stays over the partner's head...».

Las constantes (25 cuadritos, 6° por minuto, 15° por hora, las 6 y las 17) están con nombre al principio del
script, con su referencia a `clock.c` o `partner_impl.c`. Al cambiar de aspecto (el ajuste `ui_skin`) el script vuelve a
dibujar el estado y la burbuja sin esperar a otra revisión, porque el aspecto nuevo puede cambiar su maquetación
(`bars: pips`). El aspecto moderno esconde la esfera (`#field-clock-dial`) y no llena a cuadritos.

**Cómo pone un aspecto una clase.** Las hojas RCSS no pueden poner clases, así que un aspecto pone `pips`, y cualquier
otra clase en los elementos del documento compartido, con el campo `document_classes` de su `skin.yaml`: por documento e
id de elemento, las clases que el juego le pone al cargar el documento y cambia al cambiar de aspecto. El mecanismo
llega con el aspecto original. El script no lee esas clases: lo que cambia su cálculo lo pregunta con `ui.layout`.

**El aspecto moderno** (abajo a la izquierda, `ui/skins/modern/hud/style.rcss`) dibuja todo el contrato. Lo que
cambia frente al original es solo de presentación y es opcional, porque es del aspecto: con el aspecto original se
ve lo que enseña el original. Las diferencias, todas del aspecto moderno:

| Diferencia | Por qué |
|---|---|
| La hora en cifras (`HH:MM`, 24 horas) en vez de la esfera, con la opción `digital_time` (encendida por defecto; el aspecto original la pone en una caja bajo la esfera, ver [game](../modulos/apps/game.md#hora-en-dígitos)) | Se lee de un vistazo a cualquier escala |
| El día y el año en el campo | El original solo los enseña en el menú del domador |
| Las barras se llenan de forma continua, con su número, en vez de cuadritos | Más legible en pantallas grandes |
| Las necesidades en un panel, siempre a la vista, resaltadas cuando están | El original solo tiene la burbuja sobre el compañero, que se pierde si no se le ve |
| HP y MP con su barra (lo que queda sobre el máximo) | El original solo los enseña en el menú del compañero |
| Al cambiar de mapa el panel se desvanece con `screen_fade`, como el resto del HUD, en vez de deslizarse | Igual que los otros paneles del HUD (decisión de 2026-10-20 en la tabla del aspecto original) |

## Comandos y ajustes

Los menús envían comandos con `ui.send(nombre, valor)` y la app los aplica. El primer menú usa:

| Comando | Valor | Ajuste que la app publica después |
|---|---|---|
| `quit` | — | — |
| `play` | — | — (en la portada: empieza una partida nueva; ver [guardar-y-cargar.md](guardar-y-cargar.md)) |
| `minimap` | `auto`, `on`, `off` | `minimap` |
| `minimap_size` | `small`, `medium`, `large` | `minimap_size` |
| `fullscreen` | booleano | `fullscreen` |
| `ui_scale` | número (0,75–2) | `ui_scale` |
| `vsync` | booleano | `vsync` |
| `language` | código de idioma (`en`, `es`) | `language` (y la UI vuelve a poner todos sus textos) |
| `text_font` | `modern`, `original` | `text_font` (y la UI vuelve a componer los documentos con la fuente) |
| `ui_skin` | id de un aspecto (`modern`, ...) | `ui_skin` (y la UI cambia el estilo de todos los documentos en el momento) |
| `sleep` | — | — (en partida: el menú de pausa acuesta al compañero si tiene sueño; ver [guardar-y-cargar.md](guardar-y-cargar.md#36-dormir)) |
| `sleep_only`, `sleep_and_save` | — | `save.last_result` con `sleep_and_save` (el menú de dormir: seguir durmiendo, o guardar en la ranura de la partida antes) |
| `to_title` | — | — (en partida: pregunta y vuelve a la portada sin guardar; ver [guardar-y-cargar.md](guardar-y-cargar.md#38-guardar-volver-a-la-portada-y-salir-del-juego)) |

La pantalla de combate (`ui/battle`) envía `battle_command` (el número del comando, `game::battle::Command`),
`battle_charge` (cada pulsación que carga el golpe final), `battle_close` (cierra la caja del final),
`battle_end_count` y `battle_end_counted` (la cuenta de esa caja: cada punto o bit contado, y los bits ya sumados; el juego
toca sus sonidos),
`battle_bag_open` y `battle_bag_close` (la bolsa del domador, que abre la acción `menu`; el juego muestra entonces
`ui/partner_menu` en su página de objetos con `partner_menu.context = battle`, y su `bag_use` lanza el objeto) y
`battle_throw` (el número de la ranura cuyo objeto se lanza al compañero, para la consola y las pruebas). El menú de
pausa abre el del compañero con `open_partner_menu`, y este (`ui/partner_menu`) envía `bag_use`, `bag_drop` (el
número de la ranura), `bag_move` (`"<desde> <hasta>"`), `bag_sort` (el orden, 0 a 2), `partner_praise` y
`partner_scold`. Lo que dibujan lo publica el juego como ajustes con prefijo `battle.` (y `battle.phase`: `intro`,
`fight`, `victory` o `end`), `partner.` y `bag.`; el contrato está en el [página de packages/screens](../modulos/apps/screens.md).

En la consola de depuración (y en los pasos `script` de los autotests), `battle("")` empieza un combate contra el
primer NPC del mapa que puede luchar, o contra los NPC que se le den (`battle("0,2")`), y `give_item("meat 3")` mete
objetos en la bolsa. `evolve("species_b")` hace evolucionar al compañero a esa forma como lo hace un guion de mapa
(`EvolutionAdapter::evolveByScript`). Son ayudas de depuración hasta que corran los scripts de los mapas y existan cofres y tiendas:
el original empieza los combates desde los scripts de los NPC (`script_interp.c`: la operación `START_BATTLE`) y la
partida nueva empieza con la bolsa vacía. Decisión del responsable de diseño (2026-10-21): hasta que corran los guiones
de los mapas en la fase 4, `battle("")` es el único disparador de combates. `use_item("medicine")` usa uno de la bolsa
con el compañero, y `toilet("")`, `sleep("")`, `save_machine("")` (la máquina de guardado), `gym("hp")` (un curso del gimnasio, solo en su mapa), `scold("")`, `train("trn brain")` y `clock_speed("600")` llaman a los
comandos de cuidado de la sesión y aceleran el reloj, hasta que existan los baños, las camas, las pantallas de
entrenamiento y las opciones del reloj (fase 4); la lista está en [la guía de la prueba
vertical](../guides/prueba-vertical.md). Decisión del responsable de diseño (2026-10-21): `toilet`, `sleep`, `train`, `scold` y
`clock_speed` se quedan como ayudas de depuración.

**CAMBIO DE CONTRATO (aditivo):** los comandos `sleep`, `sleep_only` y `sleep_and_save`, los ajustes
`pause_menu.sleep_enabled` y `save.last_result` y el documento `sleep_menu` (fase 4, tramo 5a); el comando `to_title` y el botón `#to-title` del menú de pausa (tramo 5b). Un mod que
reemplaza `pause_menu` no tiene «Dormir» hasta que lo añade: un botón `#sleep` que envía `sleep` y se pone en gris
cuando `pause_menu.sleep_enabled` es falso.

Otros ajustes los publica el juego sin que un menú los pida:

| Ajuste | Valor | Qué es |
|---|---|---|
| `screen_fade` | número de 0 a 1 | Cuánto quita de la pantalla el fundido del cambio de mapa, que el render resta de la escena debajo de la UI; el HUD desvanece con él sus paneles |
| `pause_menu.sleep_enabled` | booleano | Si el compañero puede dormir ahora: el menú de pausa pone «Dormir» en gris si no |
| `save.last_result` | `saved`, `failed`, `not_saved` | Cómo acabó el último guardado que pidió el jugador en partida |
| `map_title_key` | clave de texto | La clave del nombre del mapa al que lleva el cambio de mapa (`main.map_names.NNNN`) mientras el original lo enseña (fotogramas 10 a 20 del fundido de salida); vacía el resto del tiempo o si el mapa no tiene nombre. El HUD pone su texto en el idioma del jugador (`t()`) en el centro, delante del fundido, y lo reescribe al cambiar de idioma |

## Aspectos

Un **aspecto** (skin) es el estilo de toda la UI: colores, tipos, tamaños, marcos e imágenes de cada pantalla. Hay
un solo ajuste global, `ui_skin`, que vale para todas las pantallas y se cambia en vivo; por defecto, el aspecto
original (`original`), y el moderno (`modern`) es una opción en «Aspecto de la interfaz». Los mods pueden añadir
aspectos: este es su contrato, y es estable.

**CAMBIO DE CONTRATO: el juego se ve como el original por defecto.** Decidido por el usuario (2026-10-21). Hasta
ahora `ui/ui_skin.yaml` decía `default: modern`; ahora dice `default: original` con `fallback: modern`. Lo que
cambia y lo que no:

- Un jugador que ya eligió un aspecto lo conserva: el `ui_skin` guardado en `settings.yaml` manda sobre el de los
  paquetes. Solo quien no ha elegido ninguno (sin `ui_skin` guardado) empieza con el original. Lo prueban
  `apps/game/tests/ui_skin_setting_tests.cpp` (los dos casos) y `packages/engine/ui/tests/skin_switch_tests.cpp`.
- El original necesita las imágenes que extrae `dwx extract` desde la versión 2 del extractor (el paquete
  `ui_original` y las imágenes de la base que agranda). Con un contenido extraído antes, o sin él (los tests sin
  disco), el original no se ofrece y el juego empieza con el moderno, el de reserva, con un aviso en el log.
- `--ui-skin` y la consola siguen igual. Las rutas de autotest no leen los ajustes del jugador, así que corren con
  el original salvo que pidan otro; `route-skins.yaml` empieza ahora en el original y pasa al de prueba y al moderno.
- Un mod que reemplaza `ui/ui_skin.yaml` sigue mandando sobre el del paquete `game`.

El cambio llegó con el aspecto original completo (todas las pantallas que había; el cuadro de texto, `story_textbox`,
llegó después con el intérprete de historia y también tiene su hoja en el aspecto original), como se pidió.

**Lo que es de cada documento y lo que es del aspecto.** Un documento (`ui/<pantalla>/document.rml`) solo enlaza
hojas de estructura, lo que no depende del aspecto: `ui/common/base.rcss` (los `display` por defecto, el tamaño del
`body`, `.hidden`, qué elementos toman el foco y `.original-look`) y, si la pantalla lo necesita, su
`ui/<pantalla>/structure.rcss` con el comportamiento propio de esa pantalla (las listas de los menús principal y de
pausa dan la vuelta con `nav-up` y `nav-down`). Todo lo demás (la fuente, los colores, los tamaños, la posición de
los paneles) lo pone el aspecto. La excepción es el aviso de los assets de desarrollo (`ui/dev_assets`): es un aviso
para quien desarrolla, no parte del aspecto del juego, así que todo su estilo está en su `structure.rcss` y se ve
igual con todos los aspectos.

**Un aspecto no cambia cómo se navega.** Es una convención, también para los aspectos de los mods: ni sus RCSS ni
sus RML de reemplazo escriben propiedades `nav-*` (`nav-up`, `nav-down`, `nav-left`, `nav-right`). La navegación
de una pantalla está en su `structure.rcss`, que es igual con todos los aspectos. Un test (`skin_switch_tests.cpp`)
recorre los aspectos del paquete `game` y de `tests/mods` y falla si alguno escribe `nav-`. Los ids de los elementos, las clases que usan los scripts, los scripts
y los ajustes y comandos que leen y envían son el contrato con el juego y no cambian con el aspecto.

**`ui/skins/<id>/skin.yaml`** (esquema `ui_skin`, registrado en `pack.yaml`):

```yaml
id: framed                          # igual que el nombre de la carpeta
name: framed.skin_name              # la clave del nombre que enseñan los menús
extends: modern                     # opcional: el aspecto al que este añade
theme: [ui/skins/framed/theme]      # hojas para todos los documentos
cursor: cursor                      # opcional: imagen PNG que el juego instala como cursor
cursor_hotspot: [4, 4]              # opcional: punto que hace clic (x, y en píxeles de la imagen)
screens:                            # por pantalla, con el nombre con el que se carga el documento
  main_menu:
    styles: [ui/skins/framed/main_menu/style]
    document: ui/skins/framed/main_menu/document   # opcional: otro RML para esa pantalla
sprite_sheets:                      # opcional: hojas de sprites para RCSS
  - name: frames
    image: texture/stddat/time      # asset, sin extensión
    palette: 3                      # opcional: otra paleta de la imagen indexada
    original_size: [66, 29]         # opcional: el tamaño del original, para copias escaladas
    dp_per_pixel: 3                 # opcional: cuántos dp ocupa un píxel del original (1 por defecto)
    upscale: smooth                 # opcional: suavizada por el reescalador de texturas (ver abajo)
    mirror_sides: [100, 26]         # opcional: columnas en espejo a izquierda y derecha (ver abajo)
    sprites:
      corner: [0, 0, 8, 8]          # x, y, ancho, alto en píxeles del original
document_classes:                   # opcional: clases que pone en elementos de los documentos compartidos
  hud:                              # el nombre con el que se carga el documento
    field-meter-0-bar: [pips]       # id del elemento: sus clases
layout:                             # opcional: opciones de maquetación que leen los scripts (ui.layout)
  pause_menu: {menu: original, box: original}
```

| Pieza | Qué hace |
|---|---|
| `parseSkin`, `parseSkinChoice` (`skin.hpp`) | Leen `skin.yaml` y `ui/ui_skin.yaml` (`default` y, opcional, `fallback`; esquema `ui_skin_choice`): el aspecto con el que arranca el juego es dato del paquete |
| `SkinCatalog` (`skin_catalog.hpp`) | Los aspectos de todos los paquetes montados, por id. Uno cuyo id no es su carpeta, que nombra un asset que falta o que extiende uno que no está (o en bucle) se queda fuera con un aviso en el log |
| `SkinStyler` (`src/skin_styler.*`) | La hoja de cada pantalla con el aspecto actual: sus hojas de sprites y las de `theme` y `screens.<pantalla>.styles` de cada aspecto de la cadena, el aspecto base primero, encima de las hojas propias del documento |
| `spriteSheetRcss` (`sprite_sheet_rcss.hpp`) | Escribe el `@spritesheet` de RCSS de una hoja de sprites. Si la imagen es un múltiplo entero de `original_size` (una copia escalada del remaster), multiplica los rectángulos y ajusta `resolution`, así que los sprites ocupan los mismos dp. Del archivo de la imagen solo lee la cabecera, para su tamaño (`imageSize`) |

- **Ids relativos a la carpeta del aspecto.** `theme`, `screens.<pantalla>.styles`, `screens.<pantalla>.document` y
  `cursor` se pueden escribir sin el prefijo `ui/skins/<id>/` (`theme: [theme]`, `styles: [pause_menu/style]`,
  `cursor: cursor`): `parseSkin` se lo pone delante al leerlos. Una id que ya empieza por `ui/skins/` se queda tal
  cual, así que los aspectos escritos con la ruta absoluta siguen valiendo. Las `sprite_sheets` **no** se relativizan:
  nombran assets de las capas base. Para crear un aspecto sin partir de cero, `python scripts/new_skin.py fork <id>
  --from <skin>` copia uno existente (`.rcss` y `cursor.png`), reescribe sus rutas y deja una hoja vacía por pantalla
  (`--flatten` disuelve `extends:` en un skin autocontenido). Guía para quien moddea:
  [modding/aspectos](../modding/aspectos.md).

- **El cursor del ratón lo pone el aspecto.** Con `cursor: <imagen>` (una PNG con alfa recto, no premultiplicado) y
  `cursor_hotspot: [x, y]` (el punto que hace clic, en píxeles desde el rincón superior izquierdo), el motor instala
  esa imagen como **cursor del sistema** mientras el aspecto esté puesto (`SkinCatalog::cursorOf` recorre la cadena
  `extends:` y gana el más cercano; `apps/game` lo aplica vía `platform::Platform::setColorCursor`). No es una hoja
  de RmlUi: es el cursor del propio Windows/macOS/Linux, así que queda siempre arriba, también sobre los menús
  modales y el HUD, sin pelear con el apilado. Si un aspecto no declara `cursor`, la flecha del sistema se queda;
  cambiar de aspecto la restaura con `clearColorCursor`. Para redibujar la flecha desde el arte propio del aspecto,
  usa `scripts/make_cursor.py <skin>/cursor.png`.

- **Imágenes suavizadas (`upscale: smooth`).** Una hoja que lo dice se dibuja, con el filtro de texturas
  «reescalado» (`Ui::setSmoothPictures`, que el juego llama al publicar las opciones gráficas), pasada por el mismo
  reescalador que las texturas de los modelos (`render::upscaleRgba`): se dobla hasta 2560 píxeles de ancho (tres veces
  como mucho; `smoothDoublings`), así una imagen que cubre una ventana de 1440 líneas tiene al menos tantos texels como
  píxeles ocupa y no se ve borrosa; el alfa se interpola como un color y se filtra en vez de dibujarse nítida. El
  resultado queda en la caché del reescalador (`Ui::setUpscaleCache`). La referencia de la imagen lleva entonces
  `&upscale=smooth`, así que al cambiar el filtro RmlUi la carga de nuevo. Así se dibujan las dos imágenes de la
  portada del aspecto original; una extracción anterior a la versión 14 trae su copia ×4 de `index_scale`, que se dobla
  una vez y se dibuja filtrada.
- **Lados en espejo (`mirror_sides: [izquierda, derecha]`).** Una hoja que lo dice dibuja su imagen más ancha: cada
  lado sigue como un espejo de sus columnas del borde (la del borde repetida, así no se ve la costura al filtrarla ni
  al suavizarla; `withMirroredSides`). La referencia lleva `&mirror_left=N&mirror_right=M`; los números son píxeles de
  la imagen del juego (la del primer paquete), así una copia mayor (de un remaster o del jugador) refleja el mismo
  trozo de dibujo, y `original_size` los cuenta (TITLE2 con `[100, 26]` mide `[446, 240]`). El espejo se hace antes del
  reescalador, que suaviza la imagen entera de una vez. El aspecto original lo usa para que la portada llene ventanas
  más anchas que 16:9 ([Imágenes a pantalla ancha](../modulos/apps/game.md#imágenes-a-pantalla-ancha)).
- **Cambio en vivo:** `Ui::setSkin(id)` cambia la hoja de cada documento abierto sin volver a cargarlo: los scripts,
  su estado, el foco y lo que está visible siguen igual. Solo un documento cuyo RML cambia (`document`) se carga de
  nuevo, y se muestra donde estaba. `Ui::skins()` da el catálogo, `Ui::skin()` el aspecto actual. Desde Lua,
  `ui.skins()` da los ids y `ui.skin_name_key(id)` la clave de su nombre.
- **Clases de documento (`document_classes`).** Un aspecto puede marcar elementos de un documento que comparten
  todos los aspectos sin copiarlo: por documento (el nombre con el que se carga) y por id de elemento, una lista de
  clases. El `SkinStyler` las pone al cargar el documento y, en un cambio de aspecto, quita de sus elementos todas
  las clases que nombra cualquier aspecto del catálogo y pone las del nuevo; con `extends` se suman las de la cadena.
  Los scripts las leen (`has_class`) pero no deben ponerlas ni quitarlas ellos: el cambio de aspecto las quitaría.
  Un id que el documento no tiene es un aviso en el log al darle el aspecto, y un documento que el juego no carga
  también (`Ui::checkSkinDocuments`, que el juego llama al cargar sus documentos y que repite cada cambio). El
  aspecto original lo usa para poner `pips` en las cuatro barras del estado del campo. Cambio de contrato aditivo:
  los `skin.yaml` de antes siguen valiendo (Decidido por el manager (2026-10-21): un mecanismo genérico en vez de
  copiar el documento del HUD o de un ajuste propio del aspecto original).
- **Opciones de maquetación (`layout`).** Las pantallas que comparten documento y script entre aspectos cambian de
  disposición según opciones con nombre, no según el id del aspecto ni sus clases: por documento, una opción y su
  valor (`pause_menu: {menu: original}`). El script de la pantalla las lee con `ui.layout(opción)`, que da el valor o
  `nil`; el `SkinStyler` pone además la clase `layout-<opción>-<valor>` en el `<body>` del documento para las hojas.
  Con `extends`, el aspecto más cercano que pone una opción manda (`SkinCatalog::layoutOf`). Las opciones y sus
  valores los fija el esquema `ui_skin` (una errata es un error al cargar el paquete):

  | Opción | Pantallas | Valores |
  |---|---|---|
  | `menu` | `pause_menu`, `partner_menu`, `battle`, `main_menu`, `save_slots`, `new_game_rules` | `modern`; `original`, la disposición del original (la cuadrícula de iconos y la página del jugador del menú de pausa, las páginas de estado y técnicas del compañero, la tira de combate que entra deslizándose, la portada y las ranuras del original) |
  | `box` | `pause_menu`, `partner_menu`, `egg_box`, `field_item_notice` | `modern`; `original`, las cajas que crecen desde un contorno gris (`common/original_box.lua`) |
  | `pages` | `partner_menu` | `modern`; `original`, las páginas de estado, técnicas y bolsa como las pantallas completas del original (las clases de página de `#scrim`); `compact`, una sola página del compañero con su estado y sus técnicas, sin fila de pestañas, la bolsa como «Bolsa» y Cancelar volviendo al menú de pausa (las clases `new-partner` y `compact-partner` de `#scrim`) |
  | `pool` | `partner_menu` | `maximum` (sin la opción, igual), la barra de PV y PM del máximo frente al tope, como el original; `current`, la barra de lo que queda frente al máximo |
  | `tabs` | `pause_menu` | `all` (sin la opción, igual), las pestañas del jugador del original más la ciudad; `minimal`, solo Jugador, Árbol y Prosperidad (la ciudad), que Q y E (los gatillos del mando) recorren desde cualquier sitio de la página, con sus teclas a los lados y una pestaña «Volver» |
  | `medals` | `pause_menu` | `page` (sin la opción, igual), las medallas en su página; `profile`, en una caja del perfil, sin su pestaña |
  | `picture` | `main_menu`, `save_slots`, `new_game_rules` | `modern`; `original`, con «imágenes panorámicas» las cajas y el logo van a sus sitios del original sobre la imagen ensanchada (`common/title_picture.lua`) |
  | `bars` | `hud` | `continuous` (por defecto); `pips`, las barras del estado del campo se llenan en los 25 cuadritos del original |
  | `heading` | `save_machine` | `original` (o sin poner), el título de la máquina solo sobre el registro y las ranuras, como el original; `always`, también sobre la pregunta de guardar |

  El moderno declara las suyas, y el original (y el new, que lo extiende) las del original. `pages: compact`, `pool`,
  `tabs` y `medals` solo las pone el aspecto DevVago; sin ellas cada pantalla hace lo de siempre (`tabs`, `medals` y
  `pool` solo cambian algo con la disposición del original del menú de pausa o en el menú del compañero). Las clases
  de `document_classes` quedan para el estilo (`skin-original`, `title-original`, `slots-original`, `pips`): ningún
  script las lee (un test lo comprueba). El HUD llena las barras a cuadritos con la maquetación `hud: {bars: pips}`.
- **El aspecto de reserva (`fallback`).** `ui/ui_skin.yaml` puede nombrar, además de `default`, un `fallback`: el
  aspecto con el que arranca el juego si el de `default` no se ofrece. El original necesita imágenes que extrae
  `dwx extract` del disco del usuario; sin ellas se queda fuera del catálogo y el juego arranca con el moderno. Un
  `default` que no está, sin `fallback` que esté, deja la UI sin aspecto, con un aviso, como antes. Cambio de
  contrato aditivo (Decidido por el manager (2026-10-21)).
- **Herencia:** con `extends`, las hojas del aspecto base van primero y las del nuevo después, así que un aspecto
  pequeño solo escribe lo que cambia. Si varios nombran `document` para una pantalla, gana el más cercano. Con las
  hojas de sprites también gana el más cercano: una hoja con el nombre de una del aspecto base la reemplaza, y si
  dos hojas dan un sprite con el mismo nombre, se queda el de la más cercana, con un aviso en el log (RmlUi se
  quedaría con el primero que lee).
- **Si falla:** el cambio va en dos pasos. Primero se cargan, ocultos, los documentos que el aspecto nuevo cambia
  de RML; si alguno no se puede cargar, se descartan todos, la UI vuelve al aspecto que tenía y `setSkin` devuelve
  el error. Hasta ahí ningún documento ha cambiado, así que volver no puede fallar, y nada se carga dos veces ni
  recibe `on_show` dos veces. Los documentos de reemplazo descartados sí han corrido ya los scripts que se ejecutan
  al cargarse (el código de nivel superior de su `logic.lua`), así que esos scripts no deben tener efectos fuera de
  su documento. Si todos cargan, cada uno ocupa el sitio del suyo (visible, en el mismo lugar de la pila modal y con
  su `on_show`) y los demás documentos toman el aspecto sin recargarse. El menú sigue enseñando el aspecto que hay
  de verdad.
- **El ajuste `ui_skin`** sigue el camino de `text_font`: al arrancar manda `--ui-skin <id>`, luego el guardado en
  `settings.yaml` y luego el de los paquetes (`ui/ui_skin.yaml`); un id que no existe deja el de los paquetes con
  un aviso. La página de opciones (desde el título o desde el menú de pausa) tiene la fila «Aspecto de la interfaz»
  (`ui/common/ui_skin_choice.lua`, compartido), que recorre `ui.skins()` y manda el comando `ui_skin`; la app lo
  aplica, lo guarda y lo publica. En la consola de depuración, `ui_skin("modern")`.
- **Un mod que añade un aspecto** trae `ui/skins/<id>/skin.yaml`, sus hojas y el nombre en un archivo de textos
  propio (`localization/<idioma>/<mod>.yaml`): un archivo con el nombre de uno del juego (`ui_skin.yaml`) lo
  reemplaza entero. El ejemplo mínimo es `tests/mods/ui_skin/test_contrast`, que extiende `modern` y cambia colores
  en todas las pantallas.
- **Un aspecto de un mod no trae scripts:** se cargan su `skin.yaml`, sus RML y sus RCSS, pero sus scripts no. Un
  `<script src>` externo solo se ejecuta si el archivo del script viene de las capas base o remaster, también desde
  un documento de un mod; un script en línea solo se ejecuta en documentos de las capas base o remaster. Así, un RML
  de reemplazo de un mod enlaza el script de su pantalla del paquete `game` por su ruta
  (`<script src="/ui/<pantalla>/logic.lua">`) y conserva sus ids, y la lógica sigue siendo la del juego. Cambiar de
  aspecto y la entrada de los menús son del motor y del paquete `game` (`ui/common/ui_skin_choice.lua`); ningún
  aspecto necesita scripts propios.
- **Recarga en caliente:** con `hotReload`, las hojas RCSS de un aspecto se vigilan como las del documento: al
  guardar una, los documentos que la usan se cargan de nuevo con el aspecto actual. `skin.yaml`, `ui/ui_skin.yaml` y
  las imágenes de las hojas de sprites no se vigilan: el catálogo se lee al arrancar, y un cambio en ellos necesita
  reiniciar el juego. Decisión del responsable de diseño (2026-10-21): la recarga de `skin.yaml` llega más tarde,
  con la documentación de modding.

### Imágenes

Las imágenes de RCSS y RML (`decorator: image(...)`, `<img src>`, `@spritesheet`) se leen por asset id, sin mirar la
extensión: `texture/x.png` encuentra `texture/x.ktx2` si un remaster lo reemplaza, así que los aspectos pueden
escribir siempre `.png`. Se leen PNG (`decodePng`) y KTX2 (`decodeKtx2`: transcodifica todos los niveles y la UI
se queda con el primero). Una KTX2 tiene que llevar datos Basis Universal (UASTC o ETC1S, como los escribe
`dwx cook`); una KTX2 con texeles sin comprimir no se lee.

Antes de decodificar, reservar o descomprimir nada, una imagen de la UI pasa estos límites:

- su archivo tiene como mucho `MaxUiImageFileBytes` (64 MiB), mirado sin leerlo (`VirtualFileSystem::size`);
- tiene como mucho `MaxUiImageSide` (4096) píxeles de lado, según su cabecera;
- decodificada a RGBA ocupa como mucho `MaxUiImageBytes`, sumando todos sus niveles (`checkUiImageBudget`): lo que
  ocupa un cuadrado de 4096 con su cadena de mips completa (13 niveles, unos 85,3 MiB), que es lo que escribe
  `dwx cook`, así que una imagen cocinada de 4096x4096 se carga;
- una KTX2 pasa además `checkPlain2dKtx2` (de `packages/engine/assets`, la misma comprobación que hace cualquier carga de
  una KTX2): una sola imagen 2D (sin capas, sin caras de cubo, profundidad 1), no más niveles de los que da su
  tamaño, datos Basis Universal sin supercompresión o con BasisLZ o Zstd, y ningún nivel que al cargarse ocupe más
  de lo que cabe en un nivel de su tamaño (así una «bomba» Zstd se rechaza antes de inflarse).

Pendiente: no hay un presupuesto total de textura por aspecto. Cada imagen tiene sus límites, pero un aspecto con
muchas hojas de sprites grandes no tiene un tope conjunto.

Una imagen indexada extraída lleva su paleta 0; las demás están en `<imagen>_meta.yaml` (`palettes`, colores de
16 bits). `texture/x.png?palette=3` la dibuja con la paleta 3, con la regla de transparencia de `dwx` (`0x0000`
transparente, el resto opaco). Pedir una paleta a una imagen sin metadatos es un error en el log. Pedírsela a una
imagen cuyo archivo es una KTX2 también (`PaletteNeedsPng`): pasa cuando un remaster reemplaza la PNG indexada con
una KTX2, que ya no tiene paletas, así que un aspecto que use `?palette=` de esa imagen se queda sin ella; el
remaster tiene que dar las variantes que necesite como imágenes propias.

`?tone=grey` (o `&tone=grey` tras la paleta) dibuja cualquier imagen, PNG o KTX2, como silueta gris claro: cada píxel
toma un gris de su luminancia (Rec. 601) entre `GreyToneDarkest` (64) y `GreyToneLightest` (176) y conserva su alfa,
así que la forma y el sombreado se reconocen y nunca brilla tanto como en color. Se aplica una vez, al cargar la
imagen (`applyImageTone`); la misma imagen en color y en gris son dos texturas, y una opción que no se conoce es un
error en el log (`BadImageOption`).

### Escala tipográfica

El aspecto moderno usa cinco tamaños de letra para sus propios textos, y ninguno más (`ui/skins/modern/theme.rcss`).
Cada tamaño es un papel del texto, no una pantalla: el mismo papel tiene el mismo tamaño en el menú de pausa, en el del
compañero y en el HUD.

| Tamaño | Papel |
|---|---|
| 14dp | Leyendas: indicaciones de botones y sus teclas, ayudas, etiquetas pequeñas del HUD, encabezados de sección y de columna, insignias (vidas, estados del compañero), el subtítulo de la fecha |
| 18dp | Cuerpo: entradas de menú, pestañas, filas de página (etiqueta y valor), descripciones, mensajes |
| 22dp | Filas de listas largas (bolsa, tiendas, listas del original), la hora del HUD |
| 30dp | Títulos: el de un panel, el nombre del compañero en su estado, el nombre del mapa |
| 48dp | Números grandes |

Peso: negrita para títulos, valores y la entrada o pestaña elegida; regular para el resto. Una sola familia (Noto Sans)
en los menús modernos, también para los textos que en el aspecto original usan su letra (ver «Aspecto moderno de los
menús»).

Los textos con la letra del original (familia `original-look`, también los que la opción «Texto con aspecto original»
dibuja así en el aspecto moderno) se quedan en múltiplos enteros de su celda de 11 píxeles (22dp, 33dp), los únicos
tamaños que dibuja nítidos. Las listas de las pantallas del original que el aspecto moderno no estiliza aparte (tienda
de cartas, guardaobjetos, tienda de méritos, tirar objetos, transporte del pájaro, gramola) toman 22dp para sus filas
(`ui/skins/modern/original_lists/style.rcss`) en vez de los 33dp del original con la letra moderna. El combate
conserva sus tamaños por ahora. El aspecto original no cambia: sus tamaños son los del original.

Mayúsculas: el aspecto moderno no pone textos en mayúsculas con `text-transform`; un texto sale como está escrito en
los textos del idioma. Las mayúsculas de las etiquetas copiadas del original en inglés (`POWER`, `RANG`, `SPEC`…) son
las del original y se quedan.

### Aspecto moderno de los menús

El menú de pausa (`ui/skins/modern/pause_menu/style.rcss`) y el del compañero
(`ui/skins/modern/partner_menu/style.rcss`) comparten un estilo propio, que solo cambia el aspecto: las teclas, el orden
de las entradas y lo que hace cada una son los mismos que en el aspecto original (Tab abre el menú, flechas mueven,
Intro elige, Ctrl vuelve).

- Panel: tarjeta oscura casi opaca (`#121922`) con borde fino (`#263243`) y esquinas de 12dp, centrada sobre un velo
  oscuro; el de pausa mide 440dp de ancho y el del compañero 760dp. Dentro, tarjetas más claras (`#1a2330`, esquinas
  de 10dp) agrupan el retrato, los parámetros, el estado y la descripción de un objeto.
- Colores por papel: títulos y valores `#e9eef5`, cuerpo `#c9d3de`, etiquetas y leyendas `#9aa8ba`, lo que no se puede
  elegir `#5d6a7c`; el ámbar `#f2a93b` marca el foco y el dinero. Las barras usan un color por medida (parámetros
  ámbar, vida verde, energía azul, cansancio violeta) sobre un fondo `#263243` de 6dp.
- Foco: la entrada con foco lleva una barra ámbar de 3dp a la izquierda y un tinte ámbar de fondo, y su texto pasa a
  blanco; la pestaña elegida va en negrita con un subrayado ámbar de 2dp. El ratón solo aclara la fila.
- Movimiento: el panel aparece con un fundido de 0,14 s (`ui/common/panel_enter.lua` pone la clase `panel-enter` dos
  fotogramas al abrir el documento); el foco cambia de color con una transición de 0,1 s. No hay más animaciones.
- Letra: Noto Sans en todo el panel, también los nombres de objetos, técnicas y del compañero, que el aspecto original
  dibuja con su letra; la opción «Texto con aspecto original» sigue aplicándose al nombre del mapa, la caja de
  historia, el combate y las tiendas.

El aspecto original hereda las hojas del moderno (`extends: modern`), así que pone la clase `skin-original` en el
`#panel` de cada documento que comparte este panel (`ui/skins/original/skin.yaml`), y las reglas propias del moderno se
escriben como `#panel:not(.skin-original) …`. Así el aspecto original sigue dibujando sus cajas al píxel sin tener que
deshacer cada regla moderna; una pantalla nueva que use `#panel` necesita esa clase en el aspecto original.

### El aspecto Pixel art (id `new`)

El jugador lo ve como «Pixel art» (`ui_skin.new` en los textos); el id y la carpeta siguen siendo `new`, así que los
ajustes guardados y `--ui-skin new` siguen valiendo.

`ui/skins/new` es la maqueta del concepto de interfaz llevada al juego: hereda del aspecto original
(`extends: original`), así que conserva su disposición, sus pantallas y sus sprites, y cada pantalla solo añade una
hoja de aspecto (`<pantalla>/style.rcss`) que cambia colores, letra, marcos y, donde la maqueta mueve o redimensiona una
caja, su sitio. Las hojas compartidas son `theme.rcss` (letra, ventana `.original-box`, foco de los botones, teclas),
`panel/style.rcss` (el panel del menú de pausa y de las pantallas que lo comparten) y `title/style.rcss` (el fondo de
la portada y el logotipo, también tras la lista de partidas, las reglas, el modo de juego y la máquina de guardado).

- Ventanas: azul marino casi opaco con doble línea azul clara y, en las ventanas principales, escuadras doradas en
  dos esquinas. Son ninepatch de `assets/frames.png` (hoja `nb-frames`, dos píxeles por dp): `nb-box` (ventana),
  `nb-half` (semitransparente), `nb-over` (opaca, encima de otra), `nb-thin` (cajas pequeñas), `nb-row` (fila con
  foco), `nb-ocur` (cursor de cuatro esquinas), `nb-key` (tecla), `nb-tab`/`nb-tab-on` (pestañas); y como imágenes las
  escuadras `nb-brk-tl`/`nb-brk-br`, el cursor `nb-cursor` (▶), flechas, rellenos de barra y corazones.
- Foco: el ▶ dorado fuera de la fila y una línea dorada alrededor de ella (la fila tiene `margin-left: -26dp` y un
  borde izquierdo transparente de 26dp donde se pinta el ▶, así el texto no se mueve); en rejillas y pestañas, el
  cursor de cuatro esquinas.
- Letra: dos fuentes de píxel, «Pixel Body» a 30dp para todo el texto y «Pixel Small» a 26dp para teclas, avisos,
  descripciones y etiquetas pequeñas (`ui/fonts/pixel_body.ttf`, `pixel_small.ttf`; Latin-1 más «…»).
- Colores: tinta `#050c1a`, azul marino `#181f2c`, azul claro `#b6e4f7`, azul `#7cb6e2`, oro `#efc63c`, crema
  `#f2f0e1` (texto), apagado `#93a6bc`, atenuado `#5f6f82`, verde `#5fd36a`, rojo `#e2483d`.
- Avisos de botones: como teclas (`nb-key`), también el de la caja de historia mientras espera, en lugar del icono
  animado del original.
- HUD: sin el reloj de esfera de la izquierda; la hora va en la caja bajo el minimapa, con el icono de la parte del
  día (mañana, día, tarde, noche) delante. Los aspectos original y moderno conservan su reloj.

Como el aspecto original, necesita los sprites que `dwx extract` saca del disco del jugador; sin ellos queda fuera y
el juego arranca con el aspecto de reserva. RmlUi no tiene sombras, contornos ni degradados libres en este motor, así
que lo que la maqueta dibuja con ellos va en los ninepatch. De la disposición del original solo cambia la imagen de la
portada, de la lista de partidas y de las reglas (`layout: {picture: modern}`): la maqueta pone su propio fondo 4:3 y
no la imagen ancha. Lo que aún difiere de la maqueta depende de cambios en los documentos o en su lógica.

### El aspecto DevVago (id `devvago`)

El diseño es de **DevVago**, que lo trajo en #308 como el aspecto `new`; cuando `new` pasó a ser el «Pixel art» de la
maqueta (#310), su diseño se quedó como un aspecto propio con su nombre («DevVago» en los menús, `ui_skin.devvago`), en
Opciones → Interfaz y en la caja del primer arranque.

- `ui/skins/devvago` extiende el moderno (`extends: modern`): cada pantalla pone su hoja encima de la del moderno, como
  en su versión, que llevaba una copia del moderno en `new/_base`. De esa copia solo queda lo que él añadió (los colores
  de las listas del original, `original_lists/style.rcss`). Pone `skin-original` en el `#panel` de las pantallas que
  comparten el panel del menú de pausa, así que las reglas `#panel:not(.skin-original)` del moderno no lo tocan.
- Imágenes propias: la portada sin marca (`assets/hero_title.png`, la de #309), su fondo, sus iconos del menú del campo
  (`assets/menu_icons.png`, hoja `new-field-icons`), los de la parte del día junto a la hora del minimapa
  (`assets/icon_sources/`) y su cursor. Las hojas de sprites del original van copiadas en su `skin.yaml`, con los
  nombres sin marca, así que, como el original, necesita lo que `dwx extract` saca del disco del jugador.
- Disposición (`layout`): la portada, la lista de ranuras, las reglas de la partida nueva y el menú de pausa como las
  del original (`menu: original`;
  las cajas se abren de golpe y la portada no se ensancha); el menú del compañero moderno, pero compacto
  (`pages: compact`) y con las barras de PV y PM de lo que queda (`pool: current`); la página del jugador con las
  pestañas mínimas (`tabs: minimal`) y las medallas en el perfil (`medals: profile`, que coloca las celdas en la caja
  del perfil desde `pause_menu/logic.lua: refresh_medals`). El combate y lo demás, como el moderno.
- Lo que no volvió: la pestaña «Evoluciones» del menú del compañero, que llevaba al árbol del menú de pausa y volvía.
  Necesitaba un comando y un ajuste propios del juego (`open_evolutions`, `pause_menu.evolutions`), que la API de la
  interfaz quitó en su 0.2.0. Por eso el árbol tiene su pestaña «Árbol» en la tira mínima, entre Jugador y
  Prosperidad; la de Cartas no se ofrece, como en su versión.
- Arreglos sobre su versión: la tira mínima recorre sus pestañas sin el `nil + 1` de cuando la página abierta no
  estaba en ella (`index_of(...) or 1`); el mensaje de las técnicas no se borra al mover el foco; las barras de PV y PM
  de los demás aspectos siguen siendo las del máximo; y los nombres de la ciudad («Prosperidad») y de las páginas del
  compañero se escriben en cada refresco, así que un cambio de aspecto en vivo deja el nombre correcto. Sus clases de
  documento de entonces (`player-minimal-tabs`, `player-medals-on-profile`, `partner-compact`, `new-partner`) son
  ahora las opciones de `layout` de arriba y las clases `layout-*` del `<body>`; ninguna regla de su hoja depende de
  una clase que el código ya no pone. Las cajas de una carta (`card-detail`, `card-count-box`) se cierran también
  sin la animación del original (`menu: original` con `box: modern`), y se quitaron las reglas de ids que ya no
  existen (`#balance` del gimnasio, la tecla de la pestaña Evoluciones, `.on-options` de la portada).
- Arreglos de su diseño: el menú de la portada usa letra de 22dp, porque «Continuar partida» se salía de la caja, y
  «Nueva partida» solo se rellena de amarillo bajo el cursor (antes había dos filas marcadas); la comida favorita es
  una fila más del perfil del compañero, no el texto grande encima del retrato y de los parámetros; en el editor de
  técnicas, las técnicas equipadas van bajo la cuadrícula y no detrás; y en una ventana ancha la tienda se aparta a la
  derecha de la caja del dinero, que tapaba.

## Aspecto original

El texto que imita al original (de momento, el nombre del mapa del cambio de mapa) usa la clase `.original-look` de
`ui/common/base.rcss`, que pone la familia `original-look` y el color de la tinta de la fuente del original (`#d6d6d6`,
el color 1 de su CLUT, `0x6B5A`). Cualquier documento puede usarla para otro texto con ese aspecto.

- **La familia** la describe `ui/fonts/original_look.yaml` del paquete `game` (esquema `ui_text_font`): su nombre, la
  fuente moderna (`Noto Sans`), la elección por defecto y la fuente de mapa de bits (`font/main.png` y
  `font/main_meta.yaml` del paquete base, que escribe `dwx extract`; ver [textos](../formats/textos.md#fuente)), con su
  línea base y el tamaño de la moderna que dibuja lo que le falta. El juego solo lee ese PNG y ese YAML, nunca el disco.
  **El atlas tiene que estar en `font/`** (`font/<nombre>`; lo exigen el esquema y `parseOriginalLook`, desde
  2026-10-21): la fuente lee los píxeles exactos del PNG, y `dwx cook` solo deja como PNG las imágenes de `font/`
  (`assets::PngOnlyImages`); fuera de ahí lo convertiría a KTX2. Un mod que apuntaba a un atlas en otra carpeta tiene
  que moverlo a `font/`.
- **`FontEngine`** se instala sobre el motor de RmlUi al arrancar la UI y le pasa todas las familias menos
  `original-look`. Con `original`, esa familia dibuja cada glifo desde el atlas: un glifo responde a su carácter
  (`Ａ`) y a su equivalente ASCII (`A`), la tinta es blanca y la tiñe el `color` de RCSS, y crece en múltiplos enteros
  (a 720 de alto, `round(tamaño / 11)`, al menos 1; a otra altura ese múltiplo por los píxeles por dp,
  redondeado hacia abajo, para que el texto no crezca más que sus cajas) con los píxeles en coordenadas enteras, así que se ve nítida a cualquier
  resolución. Cada glifo cae en la rejilla de 4 píxeles del original desde el inicio de su texto (`font.c: drawGlyph`
  sube el glifo al área de texto de 4 bits en `x / 4`), redondeando hacia abajo, mientras la pluma avanza lo que dice
  cada glifo: tras uno de 5 píxeles (`,` `.` `!`) el siguiente cae hasta 3 píxeles antes. Eso solo mueve el dibujo; los
  anchos que mide la maquetación no cambian.
- **Las letras con tilde y los signos que el original no tiene** se componen al cargar la fuente
  (`src/composed_glyphs.cpp`) con sus propios glifos, en celdas nuevas bajo las del atlas: la letra base con una marca
  (aguda, grave, circunfleja, diéresis, virgulilla, cedilla) de un píxel de grosor centrada sobre su tinta con una fila
  libre en medio (la i pierde el punto); una mayúscula, que llega arriba de la celda, se aprieta fundiendo las filas
  vecinas que menos cambian (los trazos largos primero) para que la marca quepa en la línea; `¡` y `¿` son `!` y `?`
  dados la vuelta; `«` `»` `º` `ª` `…` `·` son dibujos pequeños del motor; `“ ‘ − – —` usan `” ’ -`. Cubre las vocales
  con tilde, `ñ`, `ç` e `ý ÿ` en minúscula y mayúscula, así que una palabra como «ánimo» no mezcla dos fuentes.
  Nada de eso sale de los datos del juego: las marcas y los dibujos están en el código, y las bases se leen del atlas
  del jugador. Una letra que la fuente ya trae se queda como está, y sin la letra base no se compone. Lo que sigue
  faltando (paréntesis, otros alfabetos...) lo dibuja la moderna en la misma línea base, al tamaño de la línea de mapa
  de bits por `fallback_size`; nunca se dibuja el glifo de sustitución del original. Sin la fuente extraída,
  `original` dibuja con la moderna (con un aviso en el log).
- **La familia de los diálogos** (`dialogue_family: original-look-dialogue`, la clase `.original-dialogue` junto a
  `.original-look`) es la misma fuente con el espacio de las ventanas de texto del guion y sus listas de objetos
  (`script_textbox.c: drawString2`): allí un espacio no usa su propio ancho (4 píxeles) sino que avanza lo que avanzó
  el glifo anterior (borra la celda y deja el avance del último glifo), así que tras una minúscula mide 8, tras una
  mayúscula 12 y tras `,` o `.` 5. Medido en el original (DuckStation, los primeros diálogos de la llegada a la
  ciudad: sus líneas caen al píxel con ese avance y la rejilla; con espacios de 4, no). Fuera de ahí (`drawString`: menús, combate) el
  espacio mide 4. La usan la caja de diálogo (`story_textbox`) y la conversación y las filas de las tiendas y menús
  de objetos del guion (`trade_menu`, `bird_transport`, `item_keeper`, `merit_shop`, `jukebox`, `card_shop`,
  `discard_menu`). La maquetación mide cada palabra con el carácter anterior, así que el ajuste de línea cuenta
  esos anchos.
- **El ajuste `text_font`** (`modern` u `original`) es del jugador: `Ui::setTextFont` lo cambia y vuelve a componer y
  dibujar en el momento todos los documentos abiertos (`Rml::ReleaseFontResources`). El valor por defecto es dato
  (`default` en `original_look.yaml`). El jugador lo elige en el menú principal: la entrada «Texto con aspecto
  original», con los valores «Sí» y «No», junto al idioma (se llamaba «Fuente del texto» hasta que llegó el aspecto
  original, como se decidió el 2026-10-21), con los textos `main_menu.text_font`, `main_menu.text_font.modern` y
  `main_menu.text_font.original` de `localization/<idioma>/main_menu.yaml`. La entrada manda el comando `text_font`;
  la app lo guarda junto al idioma en `settings.yaml` de los datos del usuario y lo publica como ajuste `text_font`.
  También lo ponen `--text-font` (autotests, que no leen ni escriben los ajustes del jugador) y
  `text_font("original")` en la consola de depuración.

Decisiones del responsable de diseño sobre el nombre del mapa (ver la [ficha del campo](../reference/campo.md)):

| Decisión | Qué hace el motor |
|---|---|
| La fuente del nombre del mapa es un ajuste del jugador: la de mapa de bits del original o la moderna. Por defecto, la de mapa de bits desde la revisión de menús (QA-M08, 2026-10-30): el aspecto original escribe con ella todas sus ventanas; hasta entonces, la moderna (decisión del 2026-10-20) | Ajuste `text_font`; `default: original` en `original_look.yaml` |
| El HUD del motor se desvanece con alfa durante el cambio de mapa. El original no tiene HUD fijo en el campo: lo que dibuja (personajes, barras de estado de `partner_impl.c: renderStatusBars`, ventanas) queda detrás del rectángulo del fundido (capa 1 del orden, `fade.c: renderFade`) y se oscurece restándole el mismo gris; solo el nombre del mapa va delante (capa 0). Desviación aprobada (decisión del responsable de diseño, 2026-10-20) | `screen_fade` en el HUD, que baja la opacidad de sus paneles |
| El nombre del mapa va centrado exacto, sin imitar el desvío de unos píxeles del original (`x = 12 − (largo / 2) × 8` con los espacios de relleno), y sin opción para imitarlo (decisión del responsable de diseño, 2026-10-20) | `text-align: center` en `#map-title` |

### El aspecto `original`

`content/packs/game/ui/skins/original/` extiende el moderno y redibuja cada pantalla con la disposición, los colores
y las proporciones del original, nítida a cualquier resolución: un píxel del original son 3dp (sus 240 líneas sobre
los 720dp de la UI), así que cada medida es un número entero de píxeles por 3. Las primitivas (marcos, barras,
degradados, rellenos) son RCSS; los iconos y sprites salen de las imágenes que extrae `dwx extract` y agranda su
receta incluida `ui_original` (×4, `index_scale` en modo `edge`, en la máquina del jugador), por sus asset ids y
paletas, sin arte original en el repositorio. Cada constante de las hojas cita su origen en un comentario: la
función de la descompilación (`clock.c: renderGameClock`, `partner_impl.c: renderStatusBars`,
`game_menu.c: renderDigimonStatsView`, `inventory.c: renderInventoryTop`, `command_menu.c: BTL_renderCommandMenu`,
`main_menu.c: renderInitialMenu`, `ui.c: renderUIBoxBorder`, `bubble.c`, `map.c: renderMapName`) o la captura de
referencia en la que se midió.

Esas imágenes (las indexadas que se dibujan con una de sus paletas, `?palette=n`; `isPixelArt` en
`image_reference.hpp`) se muestrean nítidas: cada texel es un cuadrado plano y solo el píxel de pantalla que cruza su
borde mezcla los dos texels (`rml.slang: sharpUv`). Con muestreo lineal sin más, cada borde se emborrona; con el más
cercano sin más, a una proporción que no es entera (3dp por píxel del original, a 1,5 o 2 píxeles por dp, dibujan un texel de la copia
×4 de 1,125 o 1,5 píxeles) los texels salen de uno o dos píxeles a turnos y la imagen tiembla al moverse o al crecer
la burbuja. Se comprobó con capturas a 1920 × 1080 (1,5) y 2560 × 1440 (2), fuera del repositorio porque llevan arte
del disco; el resto de imágenes de la UI se siguen filtrando.

| Pantalla | Qué dibuja | Fuente |
|---|---|---|
| Portada | La pantalla de «Press Start», el menú de inicio, las listas de ranuras, la pregunta del modo batalla, las opciones y la caja de idiomas ([La portada](#la-portada-del-aspecto-original)) | `main.c: runLandingScreen`, `main_menu.c`; capturas del título |
| Menú de pausa | Un cuadro del original (borde cian, esquinas de SYSTEM_W, relleno #2D3840 a media opacidad) sobre el campo sin oscurecer | `ui.c: renderUIBoxBorder`, `game_menu.c: renderGameMenu` |
| Menú del compañero | La ficha (cuadro opaco, recuadros hundidos, iconos de parámetros, barras de felicidad, disciplina y virus) y la bolsa (dos columnas de filas de 18 píxeles, el selector claro arriba y oscuro abajo, el cuadro de opciones, los de orden y de tirar) | `game_menu.c: renderDigimonStatsView`, `inventory.c`; capturas de la ficha y de la bolsa |
| HUD del campo | El reloj de esfera (esfera, campanas, martillo, minutero y punto de la hora), las barras de felicidad y disciplina a cuadritos (`pips`) con sus iconos, la burbuja de la necesidad sobre el compañero y el nombre del mapa | `clock.c`, `partner_impl.c`, `bubble.c`, `map.c`; capturas del campo |
| Combate | La marca de enfado y el rótulo «BATTLE» de la apertura, las barras de HP y MP del compañero (BI_IN), las letras de «FINISH!!» y el aviso «PUSH» del remate, la tira de iconos de órdenes (BTLCOMND), las barras de los enemigos y los cuadros del final | `btl.c`, `battle_hud.c`, `command_menu.c`; capturas del combate y de sus cuadros finales |
| Minimapa | Con el ajuste `minimap` en `on` (por defecto): el minimapa redondo con la disposición del moderno y el dibujo del original: nombre y hora en sus ventanas, aro `original`, N y jugador en amarillo, sin el día (el original no tiene minimapa) | Diferencia opcional |

**Captura frente a descompilación.** En las capturas de referencia el campo sale 6 líneas más abajo que las
coordenadas de la descompilación y el combate 6 más arriba; las hojas usan lo que se ve (la captura) y lo dicen. Medido
de nuevo con los menús (2026-11-03, capturas de `dw-original` y de `_work/ref-duckstation/ref`): las capturas
estiran en vertical las 228 líneas que se ven a 240 (×1,053 respecto al centro de la pantalla) y no tocan la
horizontal. Con esa corrección, los bordes de la caja del menú (40 a 150), de la ficha y del jugador (31 a 221) y de
la bolsa (16 a 126 y 197 a 225) caen donde dice la descompilación (`createMenuBox`, `createInventoryView`) y donde
los dibuja el aspecto, a un píxel; sin ella parecen 5 o 6 píxeles más altos. Para comparar, la captura se encoge a 228
líneas y se centra antes de medir. Las cifras de la ficha (HP, MP y los parámetros, la edad y el peso) van a la derecha de
sus campos, como las escribe `renderNumber` en la ficha del compañero (`game_menu.c`): una edad de una cifra queda en
la segunda celda; medidas contra la captura, la última cifra acaba en el mismo píxel (antes, 9 píxeles a la derecha
en HP y MP, 5 en los demás y la edad 8 a la izquierda). La
rejilla de la bolsa con varias filas sale de la descompilación (`createInventoryView`) y sus medidas de la captura con
un objeto.

**En pantalla ancha** cada panel guarda su distancia al borde del original (el reloj a la izquierda, las barras a la
derecha); los menús van en un escenario de 960 × 720dp en el centro.

**Orden de los decoradores.** RmlUi dibuja los decoradores de un elemento con el primero de la lista encima
(comprobado con la burbuja del campo): el que tapa va antes. Su alineación solo admite px, % o palabras clave, no dp:
las posiciones dentro de un elemento van en %.

**Elecciones documentadas** (diferencias con el original, todas a propósito):

| Diferencia | Por qué |
|---|---|
| Los bordes de los cuadros pierden las líneas negras de fuera y de dentro: queda el cian (la derecha, #4A9FC5) | RCSS sin shaders da un solo color por lado |
| El degradado de cuatro esquinas del menú de inicio es uno vertical de los promedios (#000060 a #000020) | Lo mismo |
| El selector mide 12 píxeles de alto en vez de 14 | Con 14 se solapa con las filas vecinas y el teclado no puede pasar de una a otra |
| El menú de pausa es más ancho (184 píxeles) y lista opciones con texto | Tiene más entradas que el del original, con valores en el idioma del jugador |
| El menú del compañero tiene cuatro pestañas; las barras de HP y MP de su ficha miden 82 píxeles en vez de 104 | El menú tiene más páginas; los valores «800 / 800» necesitan sitio |
| El golpe final se elige en el recuadro de «PUSH», que es su entrada de la lista; en la tira no tiene icono, como el original | El original lo lanza con un botón propio; aquí es una orden más para el teclado, el mando y el ratón |
| Con el aspecto moderno, la bolsa del combate tiene una entrada «Objetos» tras el último icono de la tira (añadido del PC); con el del original no sale, como en el juego, y la bolsa se abre con la acción menú (Tab o North) | El original solo dibuja los iconos de las órdenes; la entrada del aspecto moderno sirve al ratón |
| La marca de enfado no se inclina hacia su enemigo, no cambia de tamaño con la distancia y su cara no es semitransparente | El contrato publica dónde está la marca, no la cámara del combate |
| No salen las marcas de huida de los demás Digimon de la pantalla ni el domador vuelve andando a su sitio antes del rótulo; el rótulo espera 60 frames en su lugar | El campo está quieto durante el combate; esos movimientos llegarán con la escena del combate |
| Las dos últimas marcas de «FINISH!!» salen en cuanto el indicador se llena, no 3 y 5 frames después | El contrato publica si el indicador está lleno, no cuánto lleva lleno |
| Las barras de los enemigos van en columna arriba en el centro | El combate no publica dónde está cada enemigo en la pantalla |
| Las ganancias del final dicen «+n» en vez de la flecha azul; «Continuar» cierra el tercer cuadro | El original avanza con un botón sin nada en pantalla |
| El botón del menú del HUD, mientras el campo corre (en combates, escenas y menús se esconde, como los demás paneles del HUD); los avisos de botones de los menús | El juego también se usa con teclado y ratón; el combate no los lleva, como el original |
| El parpadeo de la cara de la felicidad empieza cuando el compañero entra en una zona que le gusta o le disgusta (o cuando cambia la cara), no al compás del contador de tiempo de juego del original (`PLAYTIME_FRAMES % 10`): el ritmo es el mismo, 5 frames y 5 frames, pero la fase puede ir hasta 0,25 s desplazada | La animación del aspecto (RCSS) empieza al ponerse la clase; el contrato no publica el contador de frames, y publicarlo cada frame obligaría a redibujar el estado del campo 20 veces por segundo |
| La burbuja no hace su vaivén de 12 unidades arriba y abajo | El contrato publica dónde se asienta la burbuja y su frame, no el vaivén |

#### La portada del aspecto original

El mismo documento `main_menu` sirve a los dos aspectos: el original declara `layout: main_menu: {menu: original}` y
`logic.lua` cambia de disposición al leerlo (`ui.layout("menu")`), también al cambiar de aspecto en marcha; la clase
`title-original` de `#panel` queda para sus hojas. Lo mismo hace `save_slots` con `slots-original`. El aspecto moderno
sigue con su menú de siempre.

| Pantalla | Cómo es | Fuente |
|---|---|---|
| «Press Start» | TITLE256 hasta la fila 206 (sin la línea del copyright) sobre negro, y la línea en verde, centrada en la del original, que parpadea 30 de cada 60 frames; tras pulsar alterna cada frame hasta el negro. Fundido desde negro de 40 frames al entrar y a negro al salir. Start, confirmar o un clic siguen | `main.c: runLandingScreen`, `main.c: renderPressStartToContinue`, `fade.c` |
| Menú | NUEVA PARTIDA, CONTINUAR PARTIDA, BORRAR PARTIDA, MODO BATALLA, OPCIONES en el cuadro de (72, 50) sobre TITLE2. Continuar y borrar en gris sin partidas: el cursor se para en ellas y confirmar no hace nada. El cursor no da la vuelta; el menú recuerda la entrada al volver | `main_menu.c: renderInitialMenu`, `main_menu.c: tickMainMenu`, `main_menu.c: tickMenuInput` |
| Listas | «CONTINUAR/BORRAR RANURA n» y los autoguardados antiguos que queden, una fila por ranura, en el cuadro de (60, 50) ([guardar-y-cargar.md](guardar-y-cargar.md#35-continuar)) | `main_menu.c: renderSelectCardSlot` |
| Modo batalla | Cuadro de título, de mensaje y de Sí/No con «No» enfocado; las dos respuestas vuelven al menú | `main_menu.c: renderConfirmVSSlot` |
| Opciones | La misma página de opciones por pestañas que en partida (Juego, Gráficos, Sonido, Interfaz; Q/E, ←/→, descripción de cada fila), sobre la imagen del título y sin el recuadro del menú; su barra solo tiene «Salir del juego». Volver devuelve al menú con el cursor en OPCIONES ([Desde el título](../modulos/apps/game.md#desde-el-título)) | Añadido |
| Idioma | Caja alta en el centro, con el encabezado «LANGUAGE - IDIOMA» pequeño arriba a la izquierda, una fila por idioma de los paquetes con su bandera y su nombre en su idioma y en mayúsculas (`language.native.<código>`), y «START» / «COMENZAR» abajo en el centro. Sale antes de cada pantalla de «Press Start», con el cursor en el idioma actual (el de los ajustes o `--lang`): un confirmar elige y pasa, con fundido, a «Press Start» (en el primer arranque, antes a la caja del aspecto, abajo). En OPCIONES el idioma es una fila más de la pestaña Interfaz. Elegir cambia todos los textos al momento y se guarda en los ajustes | Captura 12 (un remaster) |
| Aspecto | Solo en el primer arranque, justo después del idioma y antes de «Press Start»: una caja con el título «Interfaz» («Interface» en inglés), en el idioma recién elegido, y una fila por aspecto que ofrecen los paquetes (la lista y el orden de OPCIONES → Interfaz), su nombre y una pista de una línea (`<clave del nombre>.hint` en `ui_skin.yaml`) en ese idioma. Al elegir el idioma, el fundido a negro lleva a esta caja en vez de a «Press Start» (el menú manda `title_skin_picker` y el juego lo esconde mientras tanto); sin caja de idiomas (un aspecto con el menú moderno o una ruta sin `language_first`) sale nada más empezar la portada. Mover el cursor pone el aspecto al momento sin guardarlo (`ui_skin_preview`); confirmar lo guarda como la fila de OPCIONES (`ui_skin`) y sigue a «Press Start» sin volver a preguntar el idioma (`skin_picker_done`). Sin cancelar. Sale mientras ni `--ui-skin` ni los ajustes nombran un aspecto y hay más de uno; un autotest solo si su ruta dice `skin_first: true` (`ui/skin_picker`, `TitleScreen::setAsksSkin`, `asksUiSkin`) | Añadido |

**Banderas.** Cada idioma nombra la suya con `language.flag.<código>`, el asset id de una imagen, igual en los
textos de todos los idiomas (como `language.native.<código>`); sin clave, la fila va sin bandera. Las de `en` y `es`
(`ui/flags/en.png`, `ui/flags/es.png`, 144 × 96, a 72 × 48dp) son dibujo propio, formas planas sin escudos, que
genera `scripts/art/make_flags.py` (solo la biblioteca estándar de Python). Un idioma nuevo trae su imagen y su clave.

Los sonidos son los del original: mover 2, confirmar 3, cancelar 4 (`ui_sounds.yaml`, `main_menu.c: tickMenuInput`).
El juego publica `title.boot` (la portada empieza en «Press Start»: al arrancar, tras la película y al volver de una
partida) y `title.language_first` (la caja de idiomas va antes; un autotest solo la ve si su ruta dice
`language_first: true`). En el primer arranque publica también `title.skin_first` (tras la caja de idiomas el menú
pide la caja del aspecto con `title_skin_picker`) y, al volver de ella, `title.after_skin_picker` (directo a «Press
Start», sin volver a preguntar el idioma). Las preguntas
de la portada llevan `dialog.at_title`, y el aspecto original las dibuja con su cuadro sobre la lista.

**Mejoras documentadas** (opcionales, a propósito): con partidas guardadas el cursor empieza en CONTINUAR PARTIDA
(`FocusContinueWhenSaved` en `logic.lua`; el original empieza en NUEVA PARTIDA); Escape y cancelar vuelven un nivel
(el menú del original no tiene «atrás»); Start se acepta también durante el fundido de entrada; la caja de idiomas
antes de «Press Start», en cada arranque y cada vuelta a la portada (añadido nuestro, como el remaster de la
captura 9; el original no la tiene); el cuadro de resumen de la partida enfocada y el paso de ocho en ocho filas con
izquierda y derecha; el ratón enfoca al pasar y confirma con clic, y el mando hace lo mismo que el teclado.

**Diferencias y huecos:** la línea «Press Start» es texto en el idioma del jugador, no la imagen del original, y es
nuestra adaptación al PC: nombra lo que sigue con el dispositivo en uso, con los nombres de los avisos de botones
(`ui.prompt`): «PULSA INTRO» / «PRESS ENTER» con el teclado (la tecla de confirmar de los controles), «PULSA» y el
botón Start con el mando, «HAZ CLIC» / «CLICK» si el ratón se movió el último (el motor junta teclado y ratón en un
dispositivo, así que lo distingue el documento); el original dice «Press Start Button»; sin la
línea del copyright; las cajas crecen a la derecha con textos más largos que los del original (español); no hay
avisos de botones en estas cajas, como en el original; la pregunta del modo batalla dice que aún no existe y el modo
batalla queda pendiente; las opciones de la portada son las de partida, y las que solo aplica una partida se guardan
para la siguiente; el reinicio de la película por inactividad sigue como estaba.

El cuadro de texto (`ui/story_textbox`) llegó con el intérprete de historia y el aspecto original lo dibuja
(`ui/skins/original/story_textbox`).

## Scripts

- **Entornos:** cada documento tiene su entorno de Lua con copias propias de las bibliotecas (`string`, `table`,
  `math`, `utf8`, `coroutine`) y de la tabla `ui`; `_G` es el propio entorno y `getmetatable` solo enseña las de
  tablas. Un documento no puede cambiar lo que ve otro.
- **Presupuesto:** cada llamada de la UI a Lua (un manejador, un `on_*`, `callScript`) tiene el presupuesto de
  instrucciones de `UiDesc::scripts`. Lo que esa llamada provoca (los manejadores de los elementos a los que hace
  `click()`, los `on_show` de lo que muestra) corre dentro de ella y gasta del mismo presupuesto, así que un bucle de
  clics también se para.
- **Límites:** los nombres que pasa un script (ids, clases, documentos, comandos, ajustes) tienen como mucho 256
  bytes y los textos 64 KiB; en la cola caben 256 comandos entre dos `takeCommands()`. Pasarse es un error de Lua en
  el script.
- **Errores de Lua y C++:** Lua lanza sus errores con `longjmp`, que se salta los destructores de C++. Las llamadas a
  Lua convierten sus argumentos dentro de un `lua_pcall`, y las funciones de la API toman las cadenas como vistas de
  las de Lua y devuelven sus resultados en una llamada protegida tras la que solo quedan valores planos: quedarse sin
  memoria en medio no deja nada sin liberar (hay un test con LeakSanitizer). Lo que queda fuera son las referencias
  de sol2 que se crean al cargar un documento.

## Recarga en caliente

Con `hotReload`, la UI mira cada medio segundo los ficheros de cada documento (el RML, sus estilos y sus scripts).
Si alguno cambió, carga el documento de nuevo y solo lo cambia por el viejo si la carga funcionó: una edición rota
deja el viejo funcionando (con un error en el log) hasta la siguiente edición. Un documento visible se vuelve a
mostrar en su sitio: si era modal, los documentos que estaban encima siguen encima.

El juego activa esta recarga con `--tuning`. Permite ajustar RML, RCSS y Lua sobre la partida abierta;
los cambios de C++ requieren volver a compilar y reiniciar la instancia. Cambiar la declaración de sprites en
`skin.yaml` o el catálogo de textos requiere volver a cargar esos recursos, no solo el documento.

## Demo

`dwe-game --ui-demo [--lang <idioma>] [--map <id>] [--size <ancho>x<alto>] [--virtual-gamepad <fichero>]` abre el HUD con el minimapa
del mapa (por defecto `twna01`; uno de prueba si no hay contenido extraído) con un jugador y un compañero que pasean
solos, y el menú de pausa con sus comandos aplicados (`quit`, ventana completa, escala, minimapa). Registra en el log
cada comando y cada cambio de foco del menú. Con `--virtual-gamepad`, lee del fichero, según crece, órdenes para un
mando virtual: `attach playstation`, `tap dpad_down`, `press south`, `release south`, `axis left_y 0.8`.

También acepta `--ui-skin original --text-font original` para revisar distribución, iconos y tipografía sin repetir
la introducción de una partida. La demo publica el aspecto elegido a Lua, igual que el juego; sus datos ficticios
no validan el estado ni las acciones del compañero en una partida real. La cabecera usa una fecha ficticia Year 1 / Day 1.
`--ui-demo-player` abre directamente Player, sin pasar por el principal ni inyectar teclas. El fixture publica CODEX, nivel 0, Bits 0 y 7h42m; el rango se consulta por nivel en la tabla de títulos del contenido local. Estos valores son de la demo, no de una partida. Esta entrada permite revisar por separado la reconstrucción de Player y documentar sus diferencias pendientes.

En el aspecto original, Player usa su marco y retrato 3D independientes del campo. Tab abre las utilidades del motor en un panel separado; Ctrl regresa al perfil. La demo muestra Raised 1 y 0 medallas como fixture; el juego pone las de la partida. Chart consume [metadatos de posiciones, iconos y detalle](../formats/arbol-de-evolucion.md) de la copia local y las formas criadas de la historia. Su vista, detalle y transiciones están implementados, con escenarios de demo, y el menú de pausa lo abre en la partida. Med. y Card también están:
las medallas y las cartas de la partida. `--lang` permite comprobar explícitamente el idioma de la demo sin modificar las preferencias del jugador.

`--ui-demo-fishing` habilita la variante del menú con FISH y SLEEP a la vez para revisar sus siete entradas
sin iniciar una partida ni desplazarse a una zona de pesca. La demo permite abrir ITEM desde el principal
con una bolsa vacía de diez posiciones; comprueba presentación, opciones y retorno, sin validar sus efectos sobre una sesión. Con --ui-demo-bag-size, Move/Sort/Drop sí modifican
el inventario aislado del preview; Use continúa sin simularse.

## Tests

`dwe_ui_tests` funciona sin GPU: carga los documentos reales del paquete `game` desde el árbol de fuentes, recorre el
menú con teclado, con mando y con ratón, comprueba los avisos, el minimapa y sus marcadores, el texto con aspecto original con una fuente de mapa de bits de
prueba (el cambio de fuente en el momento y los caracteres que faltan), la escala (también en
alta densidad), los entornos de Lua, el presupuesto compartido, los límites, la falta de memoria y la recarga en
caliente. `skin_tests`, `skin_switch_tests` e `image_reference_tests` comprueban la lectura de `skin.yaml`, el
catálogo con el paquete de prueba `tests/mods/ui_skin/test_contrast`, el cambio en vivo en todos los documentos
(estilo nuevo, foco y estado intactos), el cambio de RML de una pantalla y la vuelta atrás si no carga, las hojas
de sprites a lo largo de `extends`, la recarga en caliente de una hoja del aspecto, la entrada de los menús, las
paletas y la KTX2 que responde a un `.png`. `dwe_game_tests` (`apps/game/tests`, que compila los fuentes de los
ajustes del juego) comprueba el orden de arranque de `ui_skin` y que el ajuste se guarda y se vuelve a leer.

La demo registra cada pocos segundos lo que cuesta la UI en la CPU por frame (marcadores, layout y grabación de las
llamadas) y cuántas llamadas de dibujo hace.

### Escenario de inventario de la demo

`--ui-demo-bag-size 10|20|30` prepara una bolsa con objetos, nombres e iconos de los paquetes locales del usuario,
mediante ItemTable, Inventory y BagView. Permite revisar cantidades, descripciones y la ventana de cinco/nueve filas
sin recorrer una partida. Las cantidades son ficticias. DemoBag conserva ItemTable e Inventory y aplica Move/Sort/Drop mediante los métodos
del modelo, refrescando BagView. No hay sesión ni guardados y Use solo se registra. Sin esta opción se conserva la
bolsa vacía de diez posiciones. Los logs incluyen también el foco del menú de compañero para revisar navegación.

El inventario original muestra una barra de posición proporcional, sin flechas ni scrollbar del navegador. La ventana
de nueve filas publica su desplazamiento en el rail; cancelar Move restaura el foco y la ventana del origen aunque
este haya salido de pantalla. Estos comportamientos se revisan en la demo sin iniciar una partida.
