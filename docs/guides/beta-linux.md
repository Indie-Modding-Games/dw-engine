# Beta para Linux

El paquete que recibe un jugador: un tarball que se descomprime y se ejecuta, sin Docker ni compilador. Por ahora
Linux; Windows y macOS tienen su propia [guía](beta-windows-macos.md).

## Construirlo

```bash
scripts/package_linux.sh --out <carpeta>
```

Compila el preset `linux-clang-package` en el contenedor (`scripts/dev.sh`) con `DWE_BUILD_COMMIT`, para que los
reportes F12 del jugador nombren el commit. Ese preset es el de release con `DWE_PACKAGE` y el triplete
`x64-linux-libcxx-package`: los programas no llevan ninguna carpeta de la máquina que compila (buscan `packs/` junto
al ejecutable, y los nombres de fuentes del proyecto y de las librerías van relativos con `-ffile-prefix-map`), y el
script se niega a empaquetar si `strings` encuentra la raíz del repositorio o `/home/` en un programa. Deja en `<carpeta>` el tarball
`dwe-beta-<versión>-<commit>-linux-x86_64.tar.xz` y, aparte, los símbolos de depuración de los dos programas.
`--no-build` empaqueta el build tal como está. El script se niega a empaquetar si encuentra un paquete `base`:
**nunca se distribuye nada leído de un disco**.

## Qué lleva

| Ruta | Qué es |
|---|---|
| `dwe` | El lanzador: ejecuta `bin/dwe-game` desde donde se descomprimió. Antes pregunta `bin/dwe-game --check-system`; si ninguna tarjeta gráfica sirve, avisa y arranca con el controlador por software de `lib/lavapipe`. |
| `bin/dwe-game`, `bin/dwx` | El juego y la herramienta que lee el disco, instalados con `cmake --install` (sin la ruta de librerías del árbol de build) y sin información de depuración. |
| `packs/` | Los paquetes del equipo (`content/packs`) cocinados y empaquetados: `game.pak` y `es-translation.pak` (`scripts/package/cook_team_packs.sh`, con el `dwx` del paquete). `content::teamPacksFolder` los busca junto a la carpeta del ejecutable antes que en el árbol de fuentes; el juego empaquetado solo monta los `.pak`. |
| `lib/libvulkan.so.1` | El cargador de Vulkan (puerto `vulkan-loader` de vcpkg). `bin/dwe-game` lo busca primero en `../lib` (su ruta de librerías es `$ORIGIN/../lib`), así que el jugador no necesita el del sistema; el cargador sí usa los controladores del sistema. |
| `lib/lavapipe/` | El controlador de Vulkan por software de Mesa (lavapipe) de la imagen de desarrollo, con su manifiesto `lvp_icd.json` y las librerías que pide y que un escritorio puede no tener (LLVM, ICU, libxml2, extensiones de xcb...): unos 183 MB, 37 MB comprimidos. Lo copia `scripts/package/software_vulkan_linux.sh`. Solo se usa cuando ninguna tarjeta sirve (el lanzador y `dwe` apuntan `VK_DRIVER_FILES` y `LD_LIBRARY_PATH` a esta carpeta). |
| `licenses/` | Las licencias de las librerías (una por puerto de vcpkg), de la fuente Noto Sans, de libc++ y, en `licenses/lavapipe/`, las de cada paquete del controlador por software. |
| `LICENSE`, `THIRD_PARTY_NOTICES.txt` | La licencia MIT del proyecto y, en un solo archivo, los créditos (FreeType, y dónde está el código de las librerías MPL-2.0), los NOTICE de Apache-2.0 y todos los textos de `licenses/`. Lo escribe `scripts/package/third_party_notices.py`, que falla si un componente nuevo con licencia Apache-2.0 no dice dónde está su NOTICE o que no tiene; `scripts/package_release.sh` no firma un paquete sin estos dos archivos. |
| `README.md`, `VERSION`, `EXTRACTOR_VERSION` | Lo que lee el jugador (`scripts/package/README.md`), el commit y la `ExtractorVersion` de su `dwx` (el lanzador vuelve a leer el disco cuando cambia). |

Requisitos del equipo del jugador: glibc 2.38 o más nueva (también la del controlador por software) y, para ir a velocidad
normal, un controlador con Vulkan 1.3; sin él el juego arranca igual con el controlador por software. No hace falta
instalar nada más: el cargador de Vulkan va en `lib/`, libc++ va enlazada de forma estática y SDL abre X11 o Wayland y
el audio del sistema en tiempo de ejecución. `scripts/package_linux.sh` comprueba que toda librería que piden
`dwe-game` y `dwx` va en `lib/` o es de la biblioteca de C.

## Primer arranque

Sin `--content`, el juego monta `./content` si tiene paquete base (`base.pak`, o la carpeta `base/` que deja
`dwx extract` por defecto) y, si no, la
carpeta `content/` de los datos del usuario (`~/.local/share/indie-modding-games/dwe/`, o bajo `$XDG_DATA_HOME`).
Cuando esa carpeta no tiene paquete base, antes de montar nada el juego abre la pantalla de primer arranque
(`apps/game/src/first_run_screen.cpp`), en inglés o español según el idioma guardado o el del sistema:

1. El jugador escribe, pega, busca (diálogo del sistema) o suelta en la ventana su imagen `.cue`, `.bin` o `.iso`.
2. `content::DiscInstall` comprueba que existan la imagen y los archivos que nombra la hoja CUE, y ejecuta
   `dwx identify`: `supported us` sigue; `other-version` (otra región) y `unknown` (otro juego) se explican al
   jugador, igual que una imagen que no se puede leer.
3. Ejecuta `dwx extract --progress --out <datos>/content.partial/base` y muestra el paso y el porcentaje de las
   líneas `progress <porcentaje> <paso>`. Con la imagen norteamericana tarda menos de un minuto.
4. Ejecuta `dwx cook-pak <datos>/content.partial/base -o <datos>/content.partial/base.pak --progress`: cocina el
   paquete base (texturas a KTX2, sonidos a Opus, películas a AV1) en un solo archivo y comprueba que el motor lo abre
   entero. Es la parte larga (varios minutos); la barra le da el 80 % y la extracción el 20 % primero.
5. Borra la carpeta `base/` de `content.partial`, mueve `base.pak` y `ui_original/` a `<datos>/content/` (un `base/`
   suelto de una instalación anterior se quita después de que `base.pak` esté en su sitio), escribe
   `<datos>/content/install.yaml` (la `ExtractorVersion` del `dwx` que lo leyó y la ruta absoluta de la imagen) y el
   juego sigue como siempre, hasta la pantalla de título.

`--content <carpeta>` acepta las dos formas del paquete base: `base.pak` o una carpeta `base/` (la de un desarrollador,
de `dwx extract`). Si están las dos, gana la carpeta. El juego sin lanzador no convierte una instalación suelta
antigua: la sigue jugando así; la convierte el lanzador al abrirse.

Si el contenido instalado tiene paquete base pero su `install.yaml` falta o es de otra `ExtractorVersion` (el juego
se actualizó y lee el disco de otra forma), la misma pantalla se abre con el título «El juego se actualizó», explica
que hay que leer el disco otra vez y trae escrita la imagen de la última vez: basta con Intro. Solo se reemplazan los
paquetes de `content/`; las partidas (`saves/`) y los ajustes (`settings.yaml`) están fuera y no se tocan. Salir
cierra el juego sin montar el contenido viejo.

Un disco de otra región (EU o JP, que `dwx` reconoce por la huella del ejecutable pero cuyas tablas no conoce) se
rechaza en `dwx identify` con `other-version <versión>` y la pantalla dice cuál es («la versión europea del
juego») y que por ahora solo se lee la norteamericana. Antes `identify` daba `supported` a esas versiones porque
tienen una entrada (sin direcciones) en `executableLayouts`; ahora solo es `supported` la que tiene sus tablas
localizadas.

Cerrar la ventana o pulsar Detener mata a `dwx` y borra la carpeta parcial. La pantalla usa Dear ImGui porque la UI
del juego necesita el contenido que todavía no existe; sus textos están en `localization/<idioma>/first_run` del
paquete del juego. `dwx` corre como proceso aparte (`platform::ChildProcess`): el juego sigue sin leer discos
(regla 2 de [dependencias](../architecture/dependency-rules.md)).

`--content`, `--dev-assets`, `--autotest` y `--control` nunca abren esta pantalla.

## Con el lanzador (beta cerrada)

El probador instala el lanzador y lo abre: busca la última versión, descarga el juego y pide el disco como siempre.
No hay que escribir ninguna clave ni crear ninguna cuenta: la **clave de la beta** va dentro del lanzador
([cómo](../modulos/apps/launcher.md#la-clave-de-la-beta)).

Si el responsable cambió la clave y el lanzador es muy antiguo, dice «El servidor de la beta ya no acepta este
lanzador. Actualiza el lanzador o vuelve a instalarlo desde la última versión publicada.»: basta con instalar el
lanzador de la última versión.

## Valores por defecto de la beta

El ruleset `original` con su `active_stage` del bloqueo de historia, el aspecto `original` y la fuente original
(los que dan los paquetes), y el idioma del sistema cuando es inglés o español. Las partidas, los ajustes y los
reportes F12 van a los datos del usuario, como en desarrollo.

## Pendiente

- AppImage del juego sin lanzador (el lanzador ya se reparte como AppImage).
