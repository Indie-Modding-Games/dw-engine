# content

Cómo montan los paquetes las aplicaciones (`dwe::content::GameContent`): la carpeta de contenido, su carpeta
`packs/`, los paquetes del equipo del árbol de fuentes cuando no hay paquete `game`, y las carpetas de mods, en una
sola VFS ([vfs](../../docs/architecture/vfs.md)). Lo usan el juego y `dwe-sim`, así que una simulación ve los mismos
paquetes que una partida.

Con assets de desarrollo (`--dev-assets`, apagado por defecto) `GameContent::mount` monta también, debajo de todos
los paquetes normales, los paquetes de sus carpetas extraídas cuyo id no tiene ningún paquete normal, y el log los
marca. `dev_assets.hpp` es el dueño de la opción, del fichero `dev-assets.yaml` (`devAssetsConfigFolders` dice dónde
se busca; `parseDevAssetsConfig` y `loadDevAssets` lo leen, comprueban las raíces y las registran) y de la clave de
texto del aviso ([assets de desarrollo](../../docs/architecture/assets-de-desarrollo.md)).
