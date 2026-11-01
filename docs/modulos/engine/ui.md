# ui

La UI del juego con RmlUi ([ADR 0009](../../adr/0009-rmlui.md)): documentos RML y estilos RCSS leídos por asset id
del VFS, con su lógica en Lua (un `ScriptVm` propio, en sandbox). `Ui` abre el menú con la acción `menu`, lo navega con
las acciones de cualquier dispositivo, envía a la app comandos (`UiCommand`) y muestra el HUD con el minimapa.
Los textos salen de un `localization::Catalog` (`text-key` en el RML, `t()` en Lua) y `refreshTexts()` los cambia de
idioma sin recargar. `UiRenderer` la dibuja en el pase de UI de `render`, y `minimapGridOf` + `buildMinimap` sacan el minimapa de la
colisión de un `MapDocument`. El `FontEngine` pone sobre el motor de fuentes de RmlUi la familia `original-look`, que
dibuja con la fuente de mapa de bits del original (`BitmapFont`, leída del paquete base) o con Noto Sans según el ajuste
`text_font` del jugador. Solo los documentos de los paquetes del motor (`PackOrigin::Engine`) de las capas base y
remaster corren su propio Lua (scripts en línea y atributos de evento como `onclick`); un `<script src>` corre solo si
el fichero viene de uno de ellos. Un mod que declara `layer: base` no cuenta: el origen sale de dónde se encontró. Un documento de un
mod, o uno que lee una plantilla de un mod, se carga sin su propio código y el log dice por qué, una vez por documento
(`CodeTrust`, con la regla de `script::checkEngineDocument`). Cada carga vacía antes la caché de plantillas de RmlUi,
que guarda las plantillas por ruta y por nombre: así cada documento lee, y juzga, las plantillas que usa, y una
plantilla de un mod leída por otra carga no le llega sin ser vista. `element:set_attribute` no acepta atributos de
evento (`on...`), para que un script no pueda convertir texto en Lua.

El estilo lo pone el **aspecto** (skin) que elige el jugador: `SkinCatalog` lee los `ui/skins/<id>/skin.yaml` de
todos los paquetes montados (un mod añade un aspecto añadiendo una carpeta) y `Ui::setSkin` cambia en vivo las hojas
de todos los documentos abiertos sin recargarlos, con los mismos ids y scripts. Los documentos solo enlazan
`ui/common/base.rcss`; el resto del estilo es del aspecto. Las imágenes se piden sin depender de la extensión (una
KTX2 del remaster responde a `.png`) y una imagen indexada se dibuja con otra paleta de sus metadatos con
`?palette=N`. Detalles en
[docs/architecture/ui.md](../../architecture/ui.md); cómo editar menús en
[docs/guides/editar-menus.md](../../guides/editar-menus.md).

La fuente de mapa de bits se dibuja a un múltiplo entero de su celda (11 píxeles de alto), para que cada píxel siga
nítido: a 720 de alto, el más cercano al tamaño de letra, como se maquetaron las cajas; a otra altura, ese múltiplo
por los píxeles por dp (`FontEngine::setDpRatio`), redondeado hacia abajo. Antes se redondeaba el tamaño a cada
altura y, como las cajas crecen con la ventana, a 1920x1080 un texto de 36dp (54 píxeles) salía 5 veces mayor que la
celda donde las cajas crecían 4,5 y se salía de ellas; ahora sale 4 veces mayor y cabe.

Una caja con la animación del aspecto original (`original_box.lua`, clases `box-animating` y `box-closed`) se oculta
con opacidad 0 y además encogida a nada (`transform: scale(0)` en `ui/common/base.rcss`): RmlUi hereda la opacidad
como un valor que un hijo puede volver a poner (las entradas no disponibles del menú de campo, las pestañas de la
página del jugador), y esos hijos se veían solos los cinco frames del contorno, como el «Dormir» fantasma al cerrar el
menú. No se oculta con `visibility`: un elemento oculto no toma el foco, y una caja lo toma antes de abrirse.

`Ui::overflowingTexts()` (`text_fit.cpp`) da los textos que no caben en los documentos que se ven: cada texto que el
jugador ve se compara con la caja dibujada que lo rodea (fondo opaco, decorador, borde, o el marco de piezas de un
cuadro), con lo que lo recorta y con la ventana, y luego con los bordes de las cajas y los textos de al lado. Lo que
tapa una caja opaca posterior no cuenta. El margen es un píxel del original (3dp). Lo usan el paso de autotest
`expect_text_fits` y la clave de ruta `text_fit` ([prueba vertical](../../guides/prueba-vertical.md#formato-de-una-ruta));
sus casos están en `engine/ui/tests/text_fit_tests.cpp` (a 1280x720, 1920x1080, 1366x768 y 1024x768).

`Ui::drawnRects(exceptDocument)` da los rectángulos de lo que el jugador ve en los documentos mostrados menos uno
(textos, cajas con fondo, borde o decorador, e imágenes), recortados a la ventana; deja fuera lo oculto, lo
transparente, lo que está bajo una transformación (una caja que se abre o se cierra) y las capas que cubren media
ventana o más (un velo). `Ui::drawnRectsIn(document)` da lo mismo de un solo documento y `Ui::elementRect` la caja de
un elemento. Los usan el aviso de guardado, para no pisar ninguna ventana ([game](../apps/game.md#aviso-de-guardado)),
y el paso de autotest `expect_clear`. `Ui::drawnRectsOf(document, elementId)` da lo que se ve de un elemento y lo que contiene (nada si
él o lo que lo contiene está oculto) y `Ui::drawnRectsAround(document, elementId)` lo que se ve de todos los documentos
menos ese elemento y las cajas que lo rodean entero (su panel; una caja junto a él sí cuenta): con ellas `expect_clear`
comprueba una parte de un documento contra el resto, como la hora en dígitos del HUD.
