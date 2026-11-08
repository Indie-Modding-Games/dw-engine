# content

Cómo montan los paquetes las aplicaciones (`dwe::content::GameContent`): la carpeta de contenido, su carpeta
`packs/`, los paquetes del equipo del árbol de fuentes cuando no hay paquete `game`, y las carpetas de mods, en una
sola VFS ([vfs](../../architecture/vfs.md)). Lo usan el juego y `dwe-sim`, así que una simulación ve los mismos
paquetes que una partida.

`disc_install.hpp` es el contenido del propio jugador: en el primer arranque `DiscInstall` lee su imagen del disco con
`dwx identify` y `dwx extract --progress` en `content.partial/` de la carpeta de datos del usuario y mueve los
paquetes a `content/` al terminar, con un `install.yaml` (`InstallRecord`: versión del extractor e imagen) para leerla
otra vez sin preguntar cuando cambia el extractor (`installIsOutdated`). Sin `--content`, `defaultContentFolder` usa
un `./content` con paquete base o, si no hay, el instalado; `teamPacksFolder` busca los paquetes del equipo junto al
ejecutable (`packs/`, o `Resources/packs/` en macOS) o, en un build de desarrollo, en el árbol de fuentes
([beta en Linux](../../guides/beta-linux.md)).

Los paquetes del juego (la carpeta de contenido, `packs/` y los del equipo) tienen que montar todos y pedir una API de
Lua que el motor tiene (`assets::checkPackApi`); si no, montar falla. Un mod que no puede montar (un `pack.yaml` roto,
una dependencia que falta, otra versión de la API, ficheros de Lua sin `api`) se deja fuera y va al log, y los demás se montan igual
(un mod solo de datos sin `api` se monta con un aviso, `warnModsWithoutApi`):
`skippedMods()` da la lista con el motivo de cada uno, para la futura lista de mods del jugador. Primero se prueban
todos los mods juntos; solo si eso falla se prueban uno a uno, en rondas, para que un mod que necesita otro entre
aunque aparezca antes. Con assets de desarrollo, un mod con el id de un paquete de desarrollo se deja fuera y el
paquete de desarrollo se queda, así que un mod nunca impide que el juego arranque.

El **origen** de cada paquete (`assets::PackOrigin`) sale de dónde se encontró, nunca de su `pack.yaml`: la carpeta de
contenido, su `packs/`, los paquetes del equipo y los de desarrollo son `Engine`; los de las carpetas de mods y de
`--mods`, `Mod`. Solo el código de un paquete `Engine` de las capas `base` o `remaster` corre como el motor (ver
[script](../engine/script.md)). Un mod que declara la capa `base` o `remaster` se deja fuera con el
motivo (`assets::checkModLayer`): esas capas son del motor.

Con assets de desarrollo (`--dev-assets`, apagado por defecto) `GameContent::mount` monta también, debajo de todos
los paquetes normales, los paquetes de sus carpetas extraídas cuyo id no tiene ningún paquete normal, y el log los
marca. `dev_assets.hpp` es el dueño de la opción, del fichero `dev-assets.yaml` (`devAssetsConfigFolders` dice dónde
se busca; `parseDevAssetsConfig` y `loadDevAssets` lo leen, comprueban las raíces y las registran) y de la clave de
texto del aviso ([assets de desarrollo](../../architecture/assets-de-desarrollo.md)).

Si el fichero nombra una imagen del disco (`disc_image`), `dev_disc.hpp` se la pide a `dwx extract --cache`, en otro
proceso y sin shell, y añade la carpeta que le devuelve, de la caché de desarrollo de la carpeta de datos del usuario,
después de las carpetas extraídas del fichero. `openDevAssets` (en `dev_assets.hpp`) junta los dos pasos para el juego
y `dwe-sim`, que comparten carpeta de datos (`user_data.hpp`).
