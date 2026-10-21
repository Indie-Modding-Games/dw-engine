# ui

La UI del juego con RmlUi ([ADR 0009](../../docs/adr/0009-rmlui.md)): documentos RML y estilos RCSS leídos por asset id
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
[docs/architecture/ui.md](../../docs/architecture/ui.md); cómo editar menús en
[docs/guides/editar-menus.md](../../docs/guides/editar-menus.md).
