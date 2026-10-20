# ui

La UI del juego con RmlUi ([ADR 0009](../../docs/adr/0009-rmlui.md)): documentos RML y estilos RCSS leídos por asset id
del VFS, con su lógica en Lua (un `ScriptVm` propio, en sandbox). `Ui` abre el menú con la acción `menu`, lo navega con
las acciones de cualquier dispositivo, envía a la app comandos (`UiCommand`) y muestra el HUD con el minimapa.
Los textos salen de un `localization::Catalog` (`text-key` en el RML, `t()` en Lua) y `refreshTexts()` los cambia de
idioma sin recargar. `UiRenderer` la dibuja en el pase de UI de `render`, y `minimapGridOf` + `buildMinimap` sacan el minimapa de la
colisión de un `MapDocument`. El `FontEngine` pone sobre el motor de fuentes de RmlUi la familia `original-look`, que
dibuja con la fuente de mapa de bits del original (`BitmapFont`, leída del paquete base) o con Noto Sans según el ajuste
`text_font` del jugador. Detalles en
[docs/architecture/ui.md](../../docs/architecture/ui.md); cómo editar menús en
[docs/guides/editar-menus.md](../../docs/guides/editar-menus.md).
