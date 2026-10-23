# Menús en los mods

Un mod puede reemplazar el documento de un menú con uno del mismo asset id en su paquete
([editar-menus.md](../guides/editar-menus.md)). El reemplazo es entero: lo que el juego añada después al documento del
paquete `game` no llega a un mod que ya lo reemplaza. Esta página dice qué entradas nuevas tiene que copiar un mod así
y con qué comandos y ajustes hablan.

## Menú de pausa (`ui/pause_menu/document`)

| Entrada | Desde | Qué hace | Comando | Ajuste que lee |
|---|---|---|---|---|
| «Dormir» (`#sleep`) | 4a, tramo 5a | Acuesta al compañero si tiene sueño; luego sale el menú de dormir si la partida tiene ranura | `sleep` | `pause_menu.sleep_enabled` (en gris si es falso) |

Un mod que reemplaza el menú de pausa y no añade «Dormir» sigue funcionando: el compañero no se acuesta desde ese menú.

## Menú de dormir (`ui/sleep_menu/document`)

Nuevo en 4a, tramo 5a. Sale sobre negro cuando el compañero ya ha dormido. Envía `sleep_only` (seguir) o
`sleep_and_save` (guardar en la ranura de la partida y seguir) y no se puede cancelar, como en el original. Si el
guardado tiene que preguntar o avisar, el juego enseña el documento `dialog` con `dialog.text`, `dialog.file` y
`dialog.question` y espera `dialog_answer`. El resultado queda en `save.last_result` (`saved`, `failed`,
`not_saved`).

Los comandos y ajustes de todos los menús están en [ui.md](../architecture/ui.md#comandos-y-ajustes).
