# ui

La UI del juego con RmlUi ([ADR 0009](../../docs/adr/0009-rmlui.md)): documentos RML y estilos RCSS leídos por asset id
del VFS, con su lógica en Lua (un `ScriptVm` propio, en sandbox). `Ui` abre el menú con la acción `menu`, lo navega con
las acciones de cualquier dispositivo, envía a la app comandos (`UiCommand`) y muestra el HUD con el minimapa.
`UiRenderer` la dibuja en el pase de UI de `render`, y `minimapGridOf` + `buildMinimap` sacan el minimapa de la
colisión de un `MapDocument`. Detalles en
[docs/architecture/ui.md](../../docs/architecture/ui.md); cómo editar menús en
[docs/guides/editar-menus.md](../../docs/guides/editar-menus.md).
