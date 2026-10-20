# content

Cómo montan los paquetes las aplicaciones (`dwe::content::GameContent`): la carpeta de contenido, su carpeta
`packs/`, los paquetes del equipo del árbol de fuentes cuando no hay paquete `game`, y las carpetas de mods, en una
sola VFS ([vfs](../../docs/architecture/vfs.md)). Lo usan el juego y `dwe-sim`, así que una simulación ve los mismos
paquetes que una partida.
