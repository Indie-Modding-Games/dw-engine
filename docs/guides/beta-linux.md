# Beta para Linux

El paquete que recibe un jugador: un tarball que se descomprime y se ejecuta, sin Docker ni compilador. Por ahora
solo Linux; Windows queda para más adelante.

## Construirlo

```bash
scripts/package_linux.sh --out <carpeta>
```

Compila el preset `linux-clang-release` en el contenedor (`scripts/dev.sh`) con `DWE_BUILD_COMMIT`, para que los
reportes F12 del jugador nombren el commit, y deja en `<carpeta>` el tarball
`dwe-beta-<versión>-<commit>-linux-x86_64.tar.xz` y, aparte, los símbolos de depuración de los dos programas.
`--no-build` empaqueta el build tal como está. El script se niega a empaquetar si encuentra un paquete `base`:
**nunca se distribuye nada leído de un disco**.

## Qué lleva

| Ruta | Qué es |
|---|---|
| `dwe` | El lanzador: ejecuta `bin/dwe-game` desde donde se descomprimió. |
| `bin/dwe-game`, `bin/dwx` | El juego y la herramienta que lee el disco, instalados con `cmake --install` (sin la ruta de librerías del árbol de build) y sin información de depuración. |
| `packs/` | Los paquetes del equipo (`content/packs`): el paquete del juego y la traducción. `content::teamPacksFolder` los busca junto a la carpeta del ejecutable antes que en el árbol de fuentes. |
| `licenses/` | Las licencias de las librerías (una por puerto de vcpkg), de la fuente Noto Sans y de libc++. |
| `README.md`, `VERSION` | Lo que lee el jugador (`scripts/package/README.md`) y el commit. |

Requisitos del equipo del jugador: glibc 2.38 o más nueva, el cargador de Vulkan del sistema (`libvulkan.so.1`) y un
controlador con Vulkan 1.3. libc++ va enlazada de forma estática; SDL abre X11 o Wayland y el audio del sistema en
tiempo de ejecución.

## Primer arranque

Sin `--content`, el juego monta `./content` si tiene paquete base (lo que deja `dwx extract` por defecto) y, si no, la
carpeta `content/` de los datos del usuario (`~/.local/share/indie-modding-games/dwe/`, o bajo `$XDG_DATA_HOME`).
Cuando esa carpeta no tiene paquete base, antes de montar nada el juego abre la pantalla de primer arranque
(`apps/game/src/first_run_screen.cpp`), en inglés o español según el idioma guardado o el del sistema:

1. El jugador escribe, pega, busca (diálogo del sistema) o suelta en la ventana su imagen `.cue`, `.bin` o `.iso`.
2. `content::DiscInstall` comprueba que existan la imagen y los archivos que nombra la hoja CUE, y ejecuta
   `dwx identify`: `supported us` sigue; `other-version` (otra región) y `unknown` (otro juego) se explican al
   jugador, igual que una imagen que no se puede leer.
3. Ejecuta `dwx extract --progress --out <datos>/content.partial/base` y muestra el paso y el porcentaje de las
   líneas `progress <porcentaje> <paso>`. Con la imagen norteamericana tarda menos de un minuto y ocupa unos 1,5 GB.
4. Mueve `base/` y `ui_original/` a `<datos>/content/` y el juego sigue como siempre, hasta la pantalla de título.

Cerrar la ventana o pulsar Detener mata a `dwx` y borra la carpeta parcial. La pantalla usa Dear ImGui porque la UI
del juego necesita el contenido que todavía no existe; sus textos están en `localization/<idioma>/first_run` del
paquete del juego. `dwx` corre como proceso aparte (`platform::ChildProcess`): el juego sigue sin leer discos
(regla 2 de [dependencias](../architecture/dependency-rules.md)).

`--content`, `--dev-assets`, `--autotest` y `--control` nunca abren esta pantalla.

## Valores por defecto de la beta

El ruleset `original` con su `active_stage` del bloqueo de historia, el aspecto `original` y la fuente original
(los que dan los paquetes), y el idioma del sistema cuando es inglés o español. Las partidas, los ajustes y los
reportes F12 van a los datos del usuario, como en desarrollo.

## Pendiente

- Releer el disco solo cuando cambie `ExtractorVersion` (hoy se borra `content/` a mano).
- AppImage y paquete de Windows.
