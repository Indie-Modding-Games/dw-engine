# launcher

El lanzador de escritorio de `dwe` (`apps/launcher`): lee el disco del jugador, mantiene el juego al día con las
versiones publicadas del repositorio y lo arranca. Es un programa aparte del motor, hecho con
[Tauri 2](https://v2.tauri.app/): el backend en Rust (`src-tauri/`) y la interfaz en HTML, CSS y JavaScript sin
framework (`ui/`). Se prueba en Linux; para Windows hay instalador (compilado desde Linux) y paquete del juego
probado con Wine, y macOS está preparado sin probar ([beta para Windows y macOS](../../guides/beta-windows-macos.md)).

## Qué ve el jugador

- **Primer arranque.** Cuatro tarjetas de juego: el juego base, activo, y tres más en gris con «Próximamente»
  (expansión de cartas, segunda parte y tercera parte). Debajo, la zona «Arrastra aquí tu disco o pulsa para
  elegirlo» y el panel «Todo es local»: el disco se procesa en el ordenador del jugador y no se sube nada. El botón
  grande de abajo a la derecha dice «Instalar».
- **Inicio.** El fondo es una imagen del propio contenido extraído del jugador (solo local; antes de extraer, el arte
  abstracto del lanzador), a la izquierda el riel de juegos (solo el base activo, con el brillo turquesa) y el botón de
  ajustes. Las pestañas «Inicio» y «Notas de la versión» muestran las notas de la última versión publicada.
- **El botón grande** cambia según el estado: «Instalar» → «Extrayendo…» (con la barra y el paso de `dwx`) →
  «Actualizar (n MB)» cuando hay una versión nueva (con «Jugar sin actualizar» debajo) → «Jugar». Mientras se
  descarga o se extrae aparece «Detener».
- **Ajustes**, por pestañas (lista vertical a la izquierda; ↑/↓ en la lista, Ctrl+Tab y Ctrl+Mayús+Tab o
  Ctrl+AvPág/RePág desde cualquier sitio del diálogo):
  - **General:** idioma (español por defecto, inglés) y carpeta de datos.
  - **Actualizaciones:** buscar al abrir, buscar ahora, las versiones instaladas y volver a la anterior.
  - **Cuenta y acceso:** la clave de la beta.
  - **Disco:** el disco recordado y volver a leerlo.
  - **Complementos:** instalar, la lista de instalados (encender, apagar, quitar) y la sección de ajustes de cada
    complemento (ver [Complementos](#complementos)).
  - **Avanzado:** la carpeta de registros y el modo seguro.

Las cuatro tarjetas y los iconos del riel son arte propio (formas y degradados, `ui/js/art.js`, `ui/art/`); ningún
texto, nombre ni imagen de la interfaz viene del juego original. La interfaz se maneja entera con teclado (Tab,
flechas en las pestañas, Esc cierra los diálogos), tiene anillo de foco visible, contraste AA y respeta
«reducir movimiento».

## El disco

El lanzador hace lo mismo que la pantalla de primer arranque del juego ([beta para Linux](../../guides/beta-linux.md#primer-arranque)),
con el `dwx` del juego instalado:

1. Clasifica lo que eligió el jugador (`src-tauri/src/disc.rs`). `dwx` lee `.cue` (con sus `.bin`), `.bin` e
   `.iso`. Un `.zip` o un `.7z` se descomprime antes en `<datos>/tmp/disc` (con límite de tamaño y sin rutas que
   salgan de la carpeta) y se usa el `.cue`, el `.iso` o el `.bin` que traiga. ECM, PBP, RAR, CHD, MDF/MDS, CCD/IMG,
   NRG, CDI, CSO y `.gz` **no** se leen: el lanzador dice qué formato es y que hay que convertirlo antes, en vez de
   fingir que lo soporta.
2. Comprueba los archivos que nombra la hoja CUE y ejecuta `dwx identify`: `supported` sigue; otra región y otro
   juego se le explican al jugador.
3. Ejecuta `dwx extract --progress --out <datos>/content.partial/base` y muestra el porcentaje y el paso.
4. Mueve los paquetes a `<datos>/content/` y escribe `install.yaml` con la `ExtractorVersion` del juego instalado y
   la ruta del disco elegido, con las mismas claves que el juego, así que el juego monta el resultado sin preguntar.
   El lanzador recuerda además el archivo que eligió el jugador (`disc_source`, puede ser el `.zip`).

Si no hay juego instalado todavía, antes del paso 2 descarga la última versión publicada.

## Las actualizaciones

Cada versión del repositorio (GitHub Releases de `Indie-Modding-Games/dw-engine`, prereleases incluidas) lleva lo
de abajo. Mientras la beta sea cerrada, el lanzador no habla con GitHub: habla con la
[pasarela de la beta](../../../services/beta-gateway/README.md), que pide la clave de la beta y contesta como la API
de GitHub para ese único repositorio.

| Archivo | Qué es |
|---|---|
| `dwe-game-<versión>-<ordenador>.tar.xz` | El paquete del juego de cada ordenador (`linux-x86_64` de `scripts/package_linux.sh`, `windows-x86_64` de `scripts/package_windows.sh`, `darwin-aarch64` y `darwin-x86_64` de `scripts/package_macos.sh`): juego, `dwx`, paquetes del equipo; ningún dato del disco. |
| `game-manifest.json` | Versión semántica, commit, `extractor_version` y, por ordenador, el nombre, tamaño, SHA-256 y firma del paquete. |
| `latest.json`, `*.AppImage`, `*-setup.exe`, `*.app.tar.gz` y sus `.sig` | El lanzador mismo, para el actualizador de Tauri; además `.deb` y `.dmg` para instalarlo a mano. |

El lanzador pide `GET <servidor>/repos/<repo>/releases` y toma la versión más nueva cuyo `game-manifest.json` tenga
paquete para su ordenador, con la clave `<sistema>-<procesador>` de `latest.json` (`manifest::platform_key`: macOS
se llama `darwin`); una versión hecha solo para otros ordenadores se salta. La compara con la instalada (semver).
Para actualizar:

1. Descarga el paquete por su URL en la pasarela (`Accept: application/octet-stream`; la pasarela contesta con un
   302 a un enlace firmado de GitHub), a `<datos>/downloads/*.part`.
2. Comprueba el tamaño, el SHA-256 y la firma minisign con la clave pública del lanzador. Si algo no cuadra, lo
   borra y el juego se queda como estaba.
3. Lo descomprime en `<datos>/game/versions/.staging-<versión>`, comprueba que trae `bin/dwe-game` y `bin/dwx`
   (`.exe` en Windows; en macOS dentro de `dwe-game.app/Contents/MacOS/`), lo
   renombra a `versions/<versión>` y reescribe `<datos>/game/state.json` (actual y anterior) con un solo `rename`.
   Se conservan solo la versión actual y la anterior; «Volver a la versión anterior» las intercambia.
4. Si la `ExtractorVersion` del juego nuevo es otra (el archivo `EXTRACTOR_VERSION` del paquete o el manifiesto),
   vuelve a leer el disco recordado sin preguntar. Si el disco ya no está, lo dice y pide elegirlo de nuevo.
   Volver a la versión anterior hace lo mismo.

El lanzador se actualiza a sí mismo con el actualizador de Tauri (`latest.json`, la misma clave de firma); en Linux
solo funciona cuando corre como AppImage, en Windows con el instalador NSIS y en macOS con la aplicación. El actualizador pide `latest.json` y el AppImage a la pasarela con la clave
de la beta en la cabecera (nunca en la URL); la pasarela apunta la URL de `latest.json` a sí misma y el lanzador se
niega a descargar una que esté en otro servidor.

### Actualizaciones automáticas

En Ajustes → Actualizaciones hay tres interruptores, guardados en `settings.json`:

| Opción | Por defecto | Qué hace |
|---|---|---|
| Buscar actualizaciones al abrir (`check_updates`) | sí | Busca al abrir el lanzador. «Buscar ahora» busca en cualquier momento. |
| Actualizar el lanzador automáticamente (`auto_update_launcher`) | sí | Descarga el lanzador nuevo, comprueba su firma y lo instala sin preguntar. Aparece el aviso «El lanzador se actualizó a X.Y.Z» con «Reiniciar ahora» y «Al cerrar»: la versión nueva se abre al reiniciar, ahora o la próxima vez. |
| Actualizar el juego automáticamente (`auto_update_game`) | sí | Descarga, comprueba e instala el juego nuevo (el cambio atómico de arriba) y vuelve a leer el disco si cambia la `ExtractorVersion`. |

Qué hace cada búsqueda con lo que encuentra lo decide `src-tauri/src/auto_update.rs` (`plan`), por separado para el
lanzador y para el juego:

- Nada nuevo: nada.
- Con la opción apagada, o si esta copia no puede instalarlo sola (una compilación sin clave de firma, un lanzador
  que no se abrió desde su AppImage, un juego que aún no se instaló desde el disco): se ofrece con el botón de
  siempre («Actualizar el lanzador», «Actualizar (n MB)»).
- Con la opción encendida, si el juego está en marcha (o hay otra tarea): espera a que termine y entonces lo instala.
  Nunca interrumpe una partida ni reinicia el lanzador mientras se juega.
- Si no: lo instala.
- Un lanzador ya instalado en esta sesión no se vuelve a descargar: queda el aviso de reiniciar.

Si una actualización falla (red, firma, SHA-256, espacio), se queda la versión actual: el juego solo cambia con el
`rename` final y el lanzador solo se reemplaza tras comprobar la firma. Un fallo automático no abre ningún diálogo:
sale un aviso breve y el botón «Actualizar» sigue ahí para reintentar. La pestaña enseña la hora de la última
búsqueda y su resultado («Al día», «Hay una versión nueva», «Actualizado a…» o el error con «Reintentar»); se guarda
en `settings.json` (`last_check`).

Probado de punta a punta en Linux (AppImage): un lanzador 0.1.0 encuentra el 0.1.1 en el servidor falso, lo
descarga, comprueba la firma, reemplaza su AppImage y, con «Reiniciar ahora», arranca como 0.1.1. Para repetirlo
hace falta una clave de firma desechable (`tauri signer generate`), dos AppImages firmadas con ella
(`scripts/launcher.sh bundle --bundles appimage` con `DWE_LAUNCHER_PUBKEY` y `TAURI_SIGNING_PRIVATE_KEY`) y, solo en
esa compilación de prueba, `--config '{"plugins":{"updater":{"dangerousInsecureTransportProtocol":true}}}'`, porque el
actualizador de Tauri pide `https` y el servidor falso es `http`. La configuración del repositorio no lo lleva.

En Windows el instalador del lanzador cierra el programa al ejecutarse, así que el lanzador lo descarga y comprueba en
el momento y lo ejecuta al reiniciar o al cerrar.

El servidor es la URL de la pasarela con la que se compiló el lanzador (`DWE_LAUNCHER_GATEWAY` al compilar; en el
flujo de publicación, la variable del repositorio `DWE_BETA_GATEWAY`). Sin ella apunta a
`https://beta-gateway.invalid`, que no existe. `DWE_LAUNCHER_API` lo cambia para una ejecución (pruebas); no se guarda
en `settings.json`. Solo se acepta `https`, o `http` a este mismo ordenador (para las pruebas). La clave nunca se
envía a otra dirección que la del servidor: un archivo cuya URL apunte a otro sitio se rechaza, y al redirigir una
descarga a otro servidor `reqwest` quita la cabecera `Authorization` (lo prueba `tests/fake_release.rs`).

### La clave de la beta

Los probadores **nunca** reciben acceso al repositorio. El responsable les da el lanzador y una sola clave
compartida, que genera él y guarda en la pasarela (cómo crearla y rotarla:
[pasarela de la beta](../../../services/beta-gateway/README.md)). El probador la pega en Ajustes → Cuenta y acceso
la primera vez; si falta o no vale, el lanzador dice «Falta la clave de la beta o no es válida». La guarda en el
llavero del sistema (Secret Service en Linux, como GNOME Keyring o KWallet; el administrador de credenciales en
Windows; el llavero en macOS) con el servicio `dwe-launcher` y la cuenta `beta-key`, y nunca en un archivo ni en un
registro. Sin llavero disponible, el lanzador lo dice y no guarda nada. La clave solo abre las versiones: no da
acceso al código. Los nombres internos de la página (`save_token`, `clear_token`, el campo `token` del estado) se
quedan como estaban.

### La clave de firma

El repositorio **no** tiene ninguna clave secreta; `tauri.conf.json` trae un marcador y una compilación sin clave
pública rechaza todas las actualizaciones (lo dice en los ajustes). Para crear la clave de verdad, una vez y fuera del
repositorio:

```bash
scripts/launcher.sh shell "tauri signer generate -w /ruta/fuera/del/repo/dwe.key"
```

Pide una contraseña. Guarda `dwe.key` y la contraseña en un gestor de contraseñas: si se pierden, los lanzadores ya
repartidos no aceptarán más actualizaciones. Después:

- en GitHub, el secreto `DWE_SIGNING_KEY` con el texto de `dwe.key`, `DWE_SIGNING_KEY_PASSWORD` con la contraseña y la
  variable `DWE_SIGNING_PUBKEY` con el texto de `dwe.key.pub`;
- para compilar a mano, `DWE_LAUNCHER_PUBKEY="$(cat dwe.key.pub)" scripts/launcher.sh build` y, para firmar,
  `TAURI_SIGNING_PRIVATE_KEY="$(cat dwe.key)" TAURI_SIGNING_PRIVATE_KEY_PASSWORD=...` (el texto, no la ruta: el
  contenedor solo ve el repositorio).

Los tests crean su propia clave de usar y tirar en cada ejecución.

## Jugar

«Jugar» ejecuta `bin/dwe-game --content <datos>/content` desde la carpeta de la versión instalada (el juego encuentra
sus paquetes en `packs/` junto a `bin/`), con la salida en `<datos>/logs/game.log`, y oculta el lanzador. En Windows
lo arranca sin ventana de consola. Si el jugador eligió otra carpeta de datos, añade
`--saves <datos>/saves --reports <datos>/reports`; con la de por defecto las partidas quedan en la carpeta del juego. Cuando el juego
termina, el lanzador vuelve; si terminó con error, muestra las últimas 30 líneas de su registro.

## Carpetas

| Ruta | Qué es |
|---|---|
| `~/.config/indie-modding-games/dwe-launcher/settings.json` | Ajustes del lanzador (carpeta de datos, idioma, disco recordado). Nunca la clave ni el servidor. En Windows `%APPDATA%\indie-modding-games\dwe-launcher\`, en macOS `~/Library/Application Support/indie-modding-games/dwe-launcher/`. |
| `<datos>` | Por defecto la del juego, `~/.local/share/indie-modding-games/dwe` (`$XDG_DATA_HOME`; en macOS `~/Library/Application Support/indie-modding-games/dwe`), así que el juego arrancado sin lanzador ve lo mismo. En Windows, `%LOCALAPPDATA%\indie-modding-games\dwe`: lo instalado no va al perfil itinerante, y las partidas siguen en la carpeta del juego (`%APPDATA%\indie-modding-games\dwe`). |
| `<datos>/game/versions/`, `state.json` | Las versiones instaladas del juego. |
| `<datos>/content/` | Lo leído del disco, como lo deja la pantalla de primer arranque. |
| `<datos>/logs/launcher.log`, `game.log` | El registro del lanzador y la salida de la última partida. |
| `<datos>/addons/<id>/`, `addons.json` | Los complementos instalados y su estado (activo, permisos aceptados). |
| `<datos>/addon-storage/<id>.json` | El almacén de cada complemento (256 KB como mucho). |

`DWE_LAUNCHER_GAME_DIR=<carpeta>` usa un paquete ya descomprimido (por ejemplo el de `package_linux.sh`) en vez de
las versiones instaladas: sirve para probar sin publicar nada.

## Compilar y probar

Todo corre en la imagen `dwe-launcher-dev` (`.devcontainer/launcher.Dockerfile`: Rust estable, Node LTS con la CLI
de Tauri, WebKitGTK 4.1 y libsoup 3); el host solo ejecuta el resultado.

```bash
scripts/launcher.sh image    # la imagen
scripts/launcher.sh test     # cargo test (unidad y servidor falso) y clippy
scripts/launcher.sh build    # build/launcher/release/dwe-launcher
scripts/launcher.sh bundle   # AppImage, .deb y sus firmas (necesita la clave privada)
scripts/launcher.sh bundle-windows   # el instalador de Windows (NSIS) y su firma, compilado desde Linux
```

El `.dmg` de macOS solo se compila en un Mac (el flujo de publicación).

Con `DWE_BUILD_SLOTS=<carpeta de cerrojos>` cada compilación espera uno de los dos huecos de compilación de la
máquina, como `dbuild.sh`. Las fuentes se copian del paquete del juego (`content/packs/game/ui/fonts`) al compilar.

Los tests de Rust cubren el manifiesto, la comparación de versiones, el SHA-256 y la firma, la instalación con
cambio atómico y vuelta atrás, el desempaquetado de `.zip` y `.7z`, la hoja CUE, el registro `install.yaml` (con el
formato del juego) y una extracción con un `dwx` falso; `tests/fake_release.rs` levanta un servidor que contesta como
la pasarela y prueba la descarga sin clave, con clave, con un archivo alterado, con otra firma y redirigida a otro
servidor (que no recibe la clave); `through_the_beta_gateway` hace lo mismo a través de la pasarela de verdad
(`services/beta-gateway` con node) delante de `fake_release_server.py` como GitHub. `tests/addons_bridge.rs` prueba
que un complemento no alcanza ningún comando con privilegios ni la clave. La pasarela tiene sus propias pruebas
(`node --test services/beta-gateway/`).

Para probar a mano, `apps/launcher/tools/fake_release_server.py <carpeta> --port 8765` sirve como versiones las
subcarpetas de `<carpeta>` (sus archivos y `notes.md`); se arranca el lanzador con
`DWE_LAUNCHER_API=http://127.0.0.1:8765`, o la pasarela delante (ver su README, «Probar en local»).

## Publicar una versión

`scripts/package_release.sh --platform <ordenador> --out <carpeta del repositorio>` compila el paquete (o toma uno con
`--tarball`), lo firma y lo añade a `game-manifest.json` (uno por versión, con todos los ordenadores);
`scripts/package/release_manifest.py launcher` escribe `latest.json`. El flujo `.github/workflows/release.yml` hace
todo eso para Linux, Windows y macOS y lo sube a una versión **borrador**; solo se ejecuta a mano y nadie lo ha
ejecutado todavía: la beta no se publica hasta que el responsable la haya jugado. Su coste en minutos está en la
[beta para Windows y macOS](../../guides/beta-windows-macos.md#publicacion-y-su-coste).

## Puntos de montaje de la interfaz

La interfaz se organiza en cuatro ranuras con nombre (`ui/js/slots.js`): `sidebar` (el riel), `tabs` (las pestañas
de inicio), `news` (las tarjetas de novedades) y `settings` (las secciones de ajustes). Las piezas del lanzador se
registran ahí igual que lo hará un complemento. Las órdenes con privilegios (disco, llavero, descargas, extracción,
arrancar programas) están solo en `ui/js/api.js` y en los comandos de Rust (`src-tauri/src/commands.rs`); lo que una
pieza recibe para pintarse (`context()`) no lleva ni la clave de la beta ni la ruta del disco.

## Complementos

Los complementos de la comunidad usan esas mismas ranuras. Cómo se hace uno, la API v1 y sus límites están en la
guía [Complementos del lanzador](../../guides/launcher-addons.md); aquí, cómo los aloja el lanzador:

- **Backend** (`src-tauri/src/addons/`): el manifiesto (`manifest.rs`), el desempaquetado seguro de carpetas y `.zip`
  (`package.rs`: rutas que salen, enlaces, tamaño y número de archivos), la instalación en dos pasos con el aviso de
  permisos (`mod.rs`: `addon_inspect` prepara y enseña, `addon_install` instala solo si el jugador acepta), el
  almacén (`storage.rs`), `net.fetch` (`net.rs`, un cliente aparte sin clave ni cookies) y el puente (`bridge.rs`),
  que solo conoce las llamadas de la API v1 y vuelve a comprobar en cada una que el complemento esté activo y tenga
  el permiso.
- **Esquema propio** `dweaddon` (`protocol.rs`): sirve solo la carpeta de cada complemento activo, con una CSP
  estricta por complemento. La CSP del lanzador solo deja cargar marcos de ese esquema.
- **Interfaz** (`ui/js/addons.js`): cada complemento corre en `<iframe sandbox="allow-scripts">` (origen opaco, sin
  IPC de Tauri, que solo existe en la página principal). Sus mensajes se reconocen por el marco que los manda, se
  limitan a 40 por segundo y solo pasan al backend las llamadas de la API. Los marcos nunca se mueven en el DOM (eso
  los recargaría): las pestañas y secciones se ocultan y muestran.
- **Modo seguro:** `--safe-mode`, Ajustes → Avanzado o el aviso que aparece si el lanzador no se cerró bien la última
  vez con complementos activos (lo detecta un archivo `running` en la carpeta de ajustes que se borra al salir).
- Los ejemplos: `apps/launcher/addons/example-notes/` («Notas») y `apps/launcher/addons/test-fetch/` (prueba de
  `net.fetch` y del aislamiento contra el servidor falso). El cliente del puente está en `apps/launcher/addons/sdk/`.

## Pendiente

- Probar el lanzador en un Windows de verdad y todo en un Mac (el llavero, rutas y el instalador de cada sistema).
- La clave de firma real y la primera versión publicada (decisión del responsable).
- Desplegar la pasarela y fijar `DWE_BETA_GATEWAY` (el responsable, desde los paneles).
- Probar los complementos en Windows (allí el esquema es `http://dweaddon.localhost/`) y macOS.
