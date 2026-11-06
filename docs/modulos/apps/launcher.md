# launcher

El lanzador de escritorio de `dwe` (`apps/launcher`): lee el disco del jugador, mantiene el juego al día con las
versiones publicadas del repositorio y lo arranca. Es un programa aparte del motor, hecho con
[Tauri 2](https://v2.tauri.app/): el backend en Rust (`src-tauri/`) y la interfaz en HTML, CSS y JavaScript sin
framework (`ui/`). Se prueba en Linux; para Windows hay instalador (compilado desde Linux) y paquete del juego
probado con Wine, y macOS está preparado sin probar ([beta para Windows y macOS](../../guides/beta-windows-macos.md)).

## Qué ve el jugador

La pantalla principal es oscura y minimalista: el arte del juego a pantalla completa, desenfocado y oscurecido hacia
la izquierda (una imagen del propio contenido extraído del jugador, solo local; antes de extraer, el arte abstracto
del lanzador), superficies casi negras y un solo color de acento, verde azulado, reservado para «Jugar», el elemento
activo y el anillo de foco.

- **Barra lateral** (estrecha, a la izquierda): el logo; «Inicio», «Juegos», «Complementos» (abre Ajustes →
  Complementos) y «Ajustes»; debajo de una línea, los iconos que añaden los complementos; abajo del todo, «Acerca de»
  (versiones y teclado). Cada icono enseña su nombre al pasar el ratón o al llegar con el teclado.
- **Arriba, las píldoras de estado:** la versión instalada con «Beta cerrada» (mientras las versiones sean 0.x o
  prereleases), «En juego» mientras el juego corre, la descarga o la lectura del disco en curso con su barra, o
  «Versión nueva: X» (abre las novedades).
- **En el centro a la izquierda:** el título grande, una descripción corta y dos botones. **El botón con acento**
  cambia según el estado: «Instalar» → «Descargando…»/«Extrayendo…» (con «Detener» al lado) → «Actualizar (n MB)»
  cuando hay una versión nueva (con «Jugar sin actualizar» debajo) → «Jugar». **Configurar** (fantasma) abre un menú:
  Ajustes, volver a leer el disco, volver a la versión anterior (pregunta antes) y abrir los registros.
- **Primer arranque:** el mismo hueco dice «Bienvenido», «Instalar» elige el disco y debajo se explica qué formatos se
  leen y que todo es local (el disco se procesa en el ordenador del jugador y no se sube nada). Se puede soltar el
  disco en cualquier parte de la ventana.
- **Abajo a la izquierda, «Complementos»:** la lista de complementos instalados con su estado, «Abrir» para los que
  tienen página y «Gestionar complementos».
- **Abajo a la derecha:** «Novedades», un panel plegable con las notas de la última versión («Ver todo» las enseña
  enteras) y las tarjetas de los complementos, y hasta tres enlaces con iconos genéricos (repositorio, comunidad y
  web). Los enlaces se fijan al compilar con `DWE_LAUNCHER_LINKS`, un objeto JSON con una URL `https` por tipo
  (`{"repo": "https://…", "community": "https://…", "web": "https://…"}`; en el flujo de publicación, la variable del
  repositorio `DWE_LAUNCHER_LINKS`). El tipo que falta no se enseña y, sin la variable, no hay ninguno: los
  probadores de la beta cerrada no tienen acceso al repositorio privado, así que no se enlaza por defecto.
- **Juegos:** cuatro tarjetas: el juego base, activo, y tres más en gris con «Próximamente» (expansión de cartas,
  segunda parte y tercera parte).
- La página de un complemento ocupa el centro de la pantalla cuando se abre desde su icono o desde «Complementos».
- **Ajustes**, por pestañas (lista vertical a la izquierda; ↑/↓ en la lista, Ctrl+Tab y Ctrl+Mayús+Tab o
  Ctrl+AvPág/RePág desde cualquier sitio del diálogo):
  - **General:** idioma (español por defecto, inglés) y carpeta de datos.
  - **Actualizaciones:** buscar al abrir, buscar ahora, las versiones instaladas y volver a la anterior.
  - **Cuenta y acceso:** la clave de la beta.
  - **Disco:** el disco recordado y volver a leerlo.
  - **Complementos:** instalar, la lista de instalados (encender, apagar, quitar) y la sección de ajustes de cada
    complemento (ver [Complementos](#complementos)).
  - **Avanzado:** la carpeta de registros y el modo seguro.

Las cuatro tarjetas, los iconos y el fondo son arte propio (formas y degradados, `ui/js/art.js`, `ui/art/`); ningún
texto, nombre ni imagen de la interfaz viene del juego original. La interfaz se maneja entera con teclado (Tab y
Mayús+Tab; ↑/↓ en la barra lateral y en el menú de «Configurar», que también se abre con ↓; Esc cierra menús, paneles
y diálogos), tiene anillo de foco visible, contraste AA y respeta «reducir movimiento». Se adapta de 960×600 a
2560×1440: el texto crece con el ancho de la ventana.

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
3. Ejecuta `dwx extract --progress --out <datos>/content.partial/base` y muestra el porcentaje y el paso. Si
   falla, el aviso muestra el error de `dwx` con cada archivo que nombra (en un recuadro que se desplaza; sin línea
   `dwx: `, sus últimas líneas), y `launcher.log` guarda el código de salida y las últimas 60 líneas de `dwx`.
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
| `latest.json`, `*.AppImage`, `*.deb`, `*-setup.exe`, `*.app.tar.gz` y sus `.sig` | El lanzador mismo, para el actualizador de Tauri; además el `.dmg` para instalarlo a mano. `latest.json` nombra el AppImage con `linux-x86_64` y `linux-x86_64-appimage`, y el `.deb` con `linux-x86_64-deb`: el actualizador busca primero la clave de su forma de instalación, así que un lanzador instalado desde el `.deb` se actualiza con otro `.deb` (con el AppImage fallaría con «invalid updater binary format»). |

El lanzador pide `GET <servidor>/repos/<repo>/releases` y toma, según el **canal de actualizaciones** (abajo), la
versión más nueva cuyo `game-manifest.json` tenga paquete para su ordenador, con la clave `<sistema>-<procesador>` de
`latest.json` (`manifest::platform_key`: macOS se llama `darwin`); una versión hecha solo para otros ordenadores se
salta. La compara con la instalada (semver) y nunca instala una más vieja. Para actualizar:

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

### Canal de actualizaciones

En Ajustes, «Canal de actualizaciones» elige de qué versiones salen las actualizaciones del juego y del propio
lanzador (su `latest.json` es el de la misma versión):

| Canal | Qué recibe |
|---|---|
| Estable | Solo versiones estables (las promovidas, [Versión estable](#version-estable-promover-una-prerelease)): la más nueva de la lista con paquete para este ordenador o, si no está entre las 20 más nuevas, `releases/latest`. Mientras no exista ninguna estable, recibe lo mismo que «Pruebas». |
| Pruebas | La versión más nueva de cualquier tipo: cada cambio que entra en `main`. |

Una instalación nueva empieza en «Estable»; un lanzador que ya estaba instalado antes de que existiera el canal sigue
en «Pruebas», para que quien ya probaba no pierda versiones. Pasar de «Pruebas» a «Estable» no baja el juego: se queda
la versión instalada hasta que salga una estable más nueva. La pasarela de la beta ya contesta `releases/latest` (404
mientras no hay estable), así que el canal no necesita nada más del servidor.

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

## Requisitos del equipo

El jugador no instala nada: el paquete del juego trae lo que necesita y el lanzador comprueba el equipo antes de que
haga falta (`src-tauri/src/system_check.rs`). Lo mira al abrirse, después de cada tarea (instalar, actualizar, volver a
la versión anterior), antes de leer el disco y antes de jugar, y cuando el jugador pulsa «Comprobar de nuevo» en
Ajustes › General › Requisitos del equipo.

| Qué | Cómo se comprueba | Qué ve el jugador |
|---|---|---|
| Espacio libre | El del disco donde está `<datos>` (la carpeta existente más cercana): `statvfs` en Linux y macOS, `GetDiskFreeSpaceExW` en Windows. Leer el disco necesita unos 3 GB (la copia parcial, el archivo descomprimido y la descarga del juego); jugar, unos 200 MB. | Si falta, «Falta espacio en el disco» en el inicio con cuánto hace falta, dónde y cuánto hay, y los botones «Comprobar de nuevo» y «Elegir otra carpeta». Leer el disco y «Jugar» se niegan con `no-space` en vez de fallar a medias. |
| Tarjeta gráfica | `dwe-game --check-system` del juego instalado (abajo), primero con los controladores del equipo. | Nada si sirve. Si ninguna tarjeta sirve, el juego arranca igual en **modo de compatibilidad** (abajo): un aviso que se cierra con «Entendido», una etiqueta «Modo de compatibilidad» arriba y el botón del controlador del fabricante. |
| Nada puede mover el juego | Ni las tarjetas del equipo ni el controlador por software del paquete (o el paquete no lo trae: Windows y macOS por ahora). | «Tu tarjeta gráfica no puede mover el juego»: qué hacer, el botón de la página de controladores del fabricante (NVIDIA, AMD o Intel, según el `vendor` de la tarjeta; las tres si no se sabe), en Windows también «Abrir Windows Update», y «Comprobar de nuevo». «Jugar» se niega con `no-gpu`. |

**`dwe-game --check-system`** (`engine/rhi/include/dwe/rhi/system_check.hpp`) crea una instancia de Vulkan y elige
GPU con la misma versión (1.3) y las mismas funciones que `Device::create` (`renderer_requirements.hpp`), sin ventana,
sin contenido y sin crear el dispositivo; tarda menos de un segundo con un controlador normal. Escribe en la salida
estándar líneas `clave=valor` bajo la cabecera `dwe-check-system 1`: `result` (`ok`, `no-vulkan`, `no-device`,
`unsuitable`), `detail`, `devices` y, si hay GPU, `gpu`, `vendor` (id PCI), `kind` (`discrete`, `integrated`,
`virtual`, `software`, `other`), `api` y `driver`; sale con 0 solo si es `ok`. Cuando ninguna sirve nombra la mejor de
las que hay, para saber de qué fabricante es el controlador. La rama que limita las opciones de las compilaciones de
distribución tiene que dejar pasar `--check-system`. El lanzador le da 20 s; sin respuesta (un juego anterior a la
opción, que dice «bad argument») juega como antes.

**Modo de compatibilidad (Linux).** El paquete de Linux trae el cargador de Vulkan (`lib/libvulkan.so.1`, que el juego
busca primero) y el controlador de Vulkan por software de Mesa, lavapipe, en `lib/lavapipe/` ([guía de la beta en
Linux](../../guides/beta-linux.md)). Si la primera comprobación no encuentra GPU que sirva, el lanzador repite
`--check-system` con `VK_DRIVER_FILES` (y `VK_ICD_FILENAMES`) apuntando a `lib/lavapipe/lvp_icd.json` y
`LD_LIBRARY_PATH` a esa carpeta; si así sirve, arranca el juego con ese mismo entorno. Va más lento (lo dibuja el
procesador), pero el juego arranca en una máquina virtual o con un controlador viejo o ausente. El script `dwe` del
paquete hace lo mismo para quien juega sin lanzador. Un equipo cuyo único Vulkan es uno por software del sistema
(llvmpipe) también ve el aviso, sin cambiar de controlador.

**AppImage.** El arranque de la AppImage pone sus carpetas delante de `XDG_DATA_DIRS`, donde el cargador de Vulkan
busca los controladores del equipo; el lanzador las quita del entorno de `--check-system` y del juego
(`outside_appimage`).

**.deb.** Recomienda `mesa-vulkan-drivers | vulkan-icd`, que apt instala por defecto: en un equipo con tarjeta AMD o
Intel sin el controlador de Vulkan de Mesa, así se juega con la tarjeta y no en modo de compatibilidad. La AppImage no
puede instalar nada y se apoya en el modo de compatibilidad.

**Windows.** El instalador trae WebView2 y el paquete del juego `vulkan-1.dll`; los controladores de las tarjetas no
se pueden instalar con seguridad desde el lanzador, así que sin una tarjeta que sirva se ve la pantalla de arriba.
Un lavapipe para Windows (para tener allí también modo de compatibilidad) está pendiente de decidir (abajo).

`DWE_LAUNCHER_FREE_BYTES=<bytes>` finge el espacio libre, para ver las pantallas de espacio sin llenar un disco; con
`VK_DRIVER_FILES=/no/existe.json` en el entorno del lanzador la primera comprobación no encuentra GPU (modo de
compatibilidad si el juego trae `lib/lavapipe`, pantalla de bloqueo si no).

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

Nadie elige versiones ni lanza flujos para una versión de prueba: cada cambio que entra en `main` (un pull request
fusionado) se compila y se publica solo como **prerelease**, el canal «Pruebas». Una versión de prueba ya probada se
convierte en la **estable** a mano, sin compilar nada otra vez. Las piezas:
`scripts/package_release.sh --platform <ordenador> --out <carpeta del repositorio>` compila el paquete (o toma uno con
`--tarball`), lo firma y lo añade a `game-manifest.json` (uno por versión, con todos los ordenadores);
`scripts/package/release_manifest.py launcher` escribe `latest.json`. El repositorio es público, así que los minutos de
Actions (también los de macOS) no cuestan nada. Si Actions no está disponible, `scripts/release_local.sh` es el
respaldo para Linux y Windows ([Publicar desde este PC](#publicar-desde-este-pc)).

### Las versiones

- La versión del juego es `<versión de CMakeLists.txt>-beta.<N>`, con `N` el número de commits de `main`
  (`scripts/ci/release.py version`). Cada pull request fusionado añade un commit (fusión *squash*), así que cada
  versión de `main` tiene un `N` mayor que la anterior. La etiqueta es `v<versión>`, por ejemplo `v0.1.0-beta.1031`.
- La versión de `CMakeLists.txt` (`project(dwe VERSION 0.1.0 ...)`) solo cambia a mano, en un pull request, para
  empezar otra serie (`0.2.0-beta.<N>` va después de todas las `0.1.0-beta.*`).
- Los programas no llevan dentro el canal: la versión estable es la misma prerelease ya probada, con los mismos
  archivos, firmas y manifiestos. Por eso un jugador del canal «Pruebas» no vuelve a descargar nada cuando esa versión
  pasa a estable, y uno del canal «Estable» recibe exactamente lo que se probó.
- El lanzador tiene su propia versión (`tauri.conf.json`), que se sube a mano cuando el lanzador cambia (ver abajo).

### Versión de prueba: cada fusión en `main`

`.github/workflows/release.yml` corre en cada push a `main` que toque algo más que documentación, y también a mano
(*Actions → Release → Run workflow*). Si llega una fusión nueva mientras otra se compila, la nueva cancela la vieja:
solo el commit más nuevo sale como versión. Lo que hace, por orden:

1. **Plan** (segundos): están el secreto `DWE_SIGNING_KEY`, la variable `DWE_SIGNING_PUBKEY` y `DWE_BETA_GATEWAY` con
   una URL `https`; calcula la versión y la etiqueta; se niega si ese commit ya tiene versión publicada, si la versión
   del juego no es mayor que la de la versión más nueva, o si la **versión del lanzador** no vale (ver abajo).
2. **Un trabajo por ordenador**, todos en paralelo e independientes: el juego para Linux, Windows, macOS Apple silicon
   y macOS Intel, y el lanzador para Linux, Windows y macOS. Si uno falla, los demás terminan igual. El trabajo del
   lanzador de Linux prueba primero que la clave firma y que la clave pública verifica esa firma. Cada lanzador
   comprueba que su programa lleva dentro la pasarela y la clave pública.
3. **Publicar**: si falta algún ordenador, el resumen de la ejecución lo marca como `MISSING` y **no se publica
   nada**; lo compilado queda en los artefactos de la ejecución y *Re-run failed jobs* repite solo lo que falló. Si
   están todos: firma los paquetes del juego, el `.deb` y el `.dmg`; escribe `game-manifest.json`; comprueba cada
   firma, los tamaños, los ordenadores, el commit y que ningún paquete lleva datos del disco; escribe las notas
   (los títulos de los pull requests desde la versión anterior, con una cabecera en español); crea la versión como
   borrador, sube los archivos, escribe y comprueba `latest.json`, y solo entonces la publica como prerelease. Un
   lanzador nunca ve una versión a medio subir (la pasarela no enseña borradores); el borrador de una ejecución
   cancelada lo borra la siguiente.

Con las cachés llenas tarda unos 10–12 min de reloj: los trabajos de Mac son los más largos. En frío (cachés
vacías, o tras cambiar `vcpkg.json`, los triplets o las imágenes), unos 25 min: el juego para Mac Intel tarda ~22.

### Versión estable: promover una prerelease

Cuando una versión de prueba está probada: *Actions → Promote → Run workflow* con su etiqueta
(`.github/workflows/promote.yml`). En un par de minutos:

1. Comprueba que es una prerelease publicada y más nueva que la estable actual.
2. Descarga sus archivos y comprueba, como haría un lanzador, cada firma, `game-manifest.json` y las URL de
   `latest.json`.
3. La marca como versión normal y como la última del repositorio (`releases/latest`), le pone el título
   `<etiqueta> (estable)` y notas con los cambios desde la estable anterior.

No compila ni firma nada: los archivos, las firmas y los manifiestos son los mismos que se probaron. Un arreglo
urgente sigue el mismo camino: un pull request a `main`, su versión de prueba, y promoverla.

**Cachés.** Cada trabajo guarda en la caché de Actions lo que la siguiente ejecución puede reutilizar (las de `main`
las leen también los pull requests):

| Caché | Clave | Qué ahorra |
|---|---|---|
| Dependencias de vcpkg, por ordenador | `vcpkg.json`, `vcpkg-ports/`, `triplets/`, toolchains y la imagen | 6–8 min por ordenador; se guarda aunque el empaquetado falle. Las de Linux y Windows las leen también las comprobaciones `linux` y `windows` de los pull requests |
| Objetos del juego (ccache), por ordenador | una por ejecución; se borran las anteriores | 12–20 min de compilación, salvo los archivos que cambiaron |
| Bibliotecas de MSVC y SDK de Windows (xwin) | el Dockerfile de la imagen | ~1 min de descarga |
| Objetos de las comprobaciones de los pull requests (`ccache-ci-linux`, `ccache-ci-windows`) | una por ejecución; las guarda el trabajo `pr-caches` | que `linux` y `windows` compilen solo lo que cambió el pull request |
| Dependencias compiladas del lanzador y descargas de cargo, por sistema | `Cargo.lock` y el Dockerfile del lanzador | casi toda la compilación del lanzador; su propio crate se compila siempre |

*Publicar* firma con el `tauri` de npm en vez de construir la imagen del lanzador (2–3 min menos). Las imágenes de
Docker (la de desarrollo y la del lanzador) se guardan en el registro de contenedores de GitHub
(`ghcr.io/indie-modding-games/dwe-dev` y `dwe-launcher-dev`, con la etiqueta del hash de `.devcontainer/`):
`scripts/ci/image.sh` las baja en vez de construirlas, y las construye y sube cuando cambia `.devcontainer/`.

**La versión del lanzador.** Un lanzador instalado solo se actualiza a una versión **mayor** que la suya (la de
`apps/launcher/src-tauri/tauri.conf.json`, que va en `latest.json`). Por eso, si algo cambió en `apps/launcher/ui` o
`apps/launcher/src-tauri` (sin contar `tests/`) desde la versión publicada más nueva, hay que subir la versión en
`tauri.conf.json`, `Cargo.toml` y `Cargo.lock` en el mismo pull request. `scripts/ci/launcher_version.sh` lo comprueba
en cada pull request (la comprobación `launcher-version`) y otra vez antes de publicar: falla si el lanzador cambió y
su versión es la misma que la del `latest.json` de esa versión, si es menor, o si los tres archivos no coinciden.
`scripts/release_local.sh` no lo comprueba: hay que mirarlo a mano.

## Publicar desde este PC

El respaldo del flujo. `scripts/release_local.sh` hace en el PC lo mismo que `release.yml` para Linux y Windows (macOS
necesita un Mac), con los mismos scripts e imágenes de Docker: los paquetes del juego (`package_linux.sh`,
`package_windows.sh`) firmados con `package_release.sh` en un solo `game-manifest.json`; el AppImage, el `.deb` y el
instalador de Windows del lanzador (`launcher.sh bundle` y `bundle-windows`) con la pasarela y la clave pública
dentro, y sus firmas; y `latest.json`. Necesita Docker, `gh` con sesión iniciada y, una vez, las bibliotecas de
Windows (`scripts/dev.sh windows-sdk`, que pide aceptar su licencia).

Desde una copia limpia de `origin/main`:

```bash
git fetch origin && git switch --detach origin/main
scripts/release_local.sh --tag v0.1.0-beta.1 --notes-file notas.md --upload
```

- Pide **una vez** la contraseña de la clave, en la terminal y sin mostrarla. La clave privada
  (`~/.dwe-signing/dwe-signing.key` por defecto, `--key` para otra) y la contraseña solo llegan a los contenedores
  que firman, por las variables `TAURI_SIGNING_PRIVATE_KEY` y `TAURI_SIGNING_PRIVATE_KEY_PASSWORD`: nunca en una
  línea de órdenes, un archivo ni el registro, y se borran al terminar.
- Antes de compilar comprueba que la clave firma y que su `.pub` (`--pubkey`, por defecto `<clave>.pub`) es la
  variable `DWE_SIGNING_PUBKEY` del repositorio: una contraseña equivocada falla en segundos, no tras la compilación.
  La pasarela es la variable `DWE_BETA_GATEWAY` (`--gateway` para otra) y los enlaces de la pantalla principal la
  variable opcional `DWE_LAUNCHER_LINKS` (`--links` para otros; sin ella, ninguno), como en el flujo.
- Todo va a `build/release-local/<tag>/` (`--out`, siempre dentro de `build/`): lo que se sube en `dist/`, el
  registro con los tiempos en `logs/`.
- Antes de subir comprueba que cada archivo tiene su `.sig` y que la clave pública la verifica (también el `.deb`),
  tamaños razonables, que los dos manifiestos se leen y nombran solo `linux-x86_64` y
  `windows-x86_64`, que ningún paquete lleva datos del disco (la misma comprobación que los empaquetadores) y que los
  dos lanzadores llevan la pasarela y la clave pública.
- Con `--upload` crea la versión **borrador** y prerelease `<tag>` en `Indie-Modding-Games/dw-engine` sobre el commit
  de `origin/main` (se niega si la copia tiene cambios o no está en `origin/main`), sube todo `dist/` (paquetes del
  juego y sus `.sig`, `game-manifest.json`, AppImage y su `.sig`, `.deb` y su `.sig`, instalador de Windows y su
  `.sig`) y por último `latest.json`, con las URL de la API de los archivos ya subidos, como el flujo (la pasarela
  las cambia por las suyas). Nada se publica: el responsable revisa el borrador y lo publica a mano.
- Si la etiqueta ya existe, o hay una versión publicada con ese nombre, se niega siempre; un borrador con ese nombre
  solo se borra con `--replace-draft`. Sin `--upload` solo compila y comprueba (`latest.preview.json` lleva URL de
  ejemplo); `--upload --no-build` sube, tras comprobarlo otra vez, lo que dejó una ejecución anterior.

Medido en este PC con la clave de prueba (2026-11-04, copia nueva con las cachés de vcpkg y ccache ya llenas):
33 min en total; imágenes 12 min (la de desarrollo se rehízo entera; con caché, segundos), juego Linux 3 min,
juego Windows 11 min, lanzador Linux 4 min, lanzador Windows 3 min, firmas y comprobaciones 7 s. Ocupa unos 10 GB
en `build/`. `--dry-run-key <carpeta>` es solo para probar el script: crea allí una clave de usar y tirar y compila
con ella sin preguntar nada (nunca sube).

## Puntos de montaje de la interfaz

La interfaz se organiza en cuatro ranuras con nombre (`ui/js/slots.js`): `sidebar` (la barra lateral), `tabs` (las
páginas que ocupan el centro: hoy, las de los complementos), `news` (las tarjetas del panel de novedades) y `settings` (las secciones de ajustes). Las piezas del lanzador se
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

- Modo de compatibilidad en Windows: el puerto `mesa` de vcpkg tiene la opción `lavapipe`, pero necesita bajar y
  compilar Mesa y LLVM enteros (fuentes de unos 50 MB y 150 MB, varias horas y más de 20 GB de compilación) y no
  admite el runtime de C estático del triplete de Windows. La alternativa es una compilación ya hecha de Mesa para
  Windows (unos 30 MB). Las dos son descargas nuevas: decide el responsable.

- Probar el lanzador en un Windows de verdad y todo en un Mac (el llavero, rutas y el instalador de cada sistema).
- La clave de firma real y la primera versión publicada (decisión del responsable).
- Desplegar la pasarela y fijar `DWE_BETA_GATEWAY` (el responsable, desde los paneles).
- Probar los complementos en Windows (allí el esquema es `http://dweaddon.localhost/`) y macOS.
