# launcher

El lanzador de escritorio de `dwe` (`apps/launcher`): lee el disco del jugador, mantiene el juego al día con las
versiones publicadas del repositorio y lo arranca. Es un programa aparte del motor, hecho con
[Tauri 2](https://v2.tauri.app/): el backend en Rust (`src-tauri/`) y la interfaz en HTML, CSS y JavaScript sin
framework (`ui/`). Por ahora se prueba en Linux; Windows y macOS compilan con el mismo código, pero nadie los ha
probado todavía.

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
- **Ajustes:** carpeta de datos, idioma (español por defecto, inglés), token de GitHub, buscar actualizaciones al
  abrir y ahora, volver a la versión anterior del juego, volver a leer el disco y abrir la carpeta de registros.

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

Cada versión del repositorio (GitHub Releases de `Indie-Modding-Games/dw-engine`, prereleases incluidas) lleva:

| Archivo | Qué es |
|---|---|
| `dwe-game-<versión>-linux-x86_64.tar.xz` | El paquete de `scripts/package_linux.sh` (juego, `dwx`, paquetes del equipo; ningún dato del disco). |
| `game-manifest.json` | Versión semántica, commit, `extractor_version` y, por ordenador (`linux-x86_64`...), el nombre, tamaño, SHA-256 y firma del paquete. |
| `latest.json`, `*.AppImage`, `*.AppImage.sig` | El lanzador mismo, para el actualizador de Tauri. |

El lanzador pide `GET <api>/repos/<repo>/releases`, toma la versión más nueva que tenga `game-manifest.json` y lo
compara con la instalada (semver). Para actualizar:

1. Descarga el paquete por su URL de la API (`Accept: application/octet-stream`), a `<datos>/downloads/*.part`.
2. Comprueba el tamaño, el SHA-256 y la firma minisign con la clave pública del lanzador. Si algo no cuadra, lo
   borra y el juego se queda como estaba.
3. Lo descomprime en `<datos>/game/versions/.staging-<versión>`, comprueba que trae `bin/dwe-game` y `bin/dwx`, lo
   renombra a `versions/<versión>` y reescribe `<datos>/game/state.json` (actual y anterior) con un solo `rename`.
   Se conservan solo la versión actual y la anterior; «Volver a la versión anterior» las intercambia.
4. Si la `ExtractorVersion` del juego nuevo es otra (el archivo `EXTRACTOR_VERSION` del paquete o el manifiesto),
   vuelve a leer el disco recordado sin preguntar. Si el disco ya no está, lo dice y pide elegirlo de nuevo.
   Volver a la versión anterior hace lo mismo.

El lanzador se actualiza a sí mismo con el actualizador de Tauri (`latest.json`, la misma clave); en Linux solo
funciona cuando corre como AppImage.

El servidor es configurable: `api_base` en los ajustes del lanzador o `DWE_LAUNCHER_API` para una ejecución. Solo
se acepta `https`, o `http` a este mismo ordenador (para las pruebas). El token nunca se envía a otra dirección que la
de la API: un archivo cuya URL apunte a otro sitio se rechaza, y al redirigir una descarga a otro servidor
`reqwest` quita la cabecera.

### El token de GitHub (beta cerrada)

Mientras el repositorio sea privado, cada jugador necesita un token de GitHub con lectura del repositorio: un
*fine-grained personal access token* con acceso solo a `dw-engine` y el permiso **Contents: Read-only** (o un token
clásico con `repo`). Se pega en Ajustes → Acceso a GitHub. El lanzador lo guarda en el llavero del sistema (Secret
Service en Linux, como GNOME Keyring o KWallet; el administrador de credenciales en Windows; el llavero en macOS)
con el servicio `dwe-launcher`, y nunca en un archivo ni en un registro (el registro tapa cualquier cosa con forma de
token). Sin llavero disponible, el lanzador lo dice y no guarda nada. Cuando el repositorio sea público, el token
deja de hacer falta.

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
sus paquetes en `packs/` junto a `bin/`), con la salida en `<datos>/logs/game.log`, y oculta el lanzador. Si la
carpeta de datos no es la del juego, añade `--saves <datos>/saves --reports <datos>/reports`. Cuando el juego
termina, el lanzador vuelve; si terminó con error, muestra las últimas 30 líneas de su registro.

## Carpetas

| Ruta | Qué es |
|---|---|
| `~/.config/indie-modding-games/dwe-launcher/settings.json` | Ajustes del lanzador (carpeta de datos, idioma, servidor, disco recordado). Nunca el token. |
| `<datos>` | Por defecto la del juego, `~/.local/share/indie-modding-games/dwe` (`$XDG_DATA_HOME`), así que el juego arrancado sin lanzador ve lo mismo. |
| `<datos>/game/versions/`, `state.json` | Las versiones instaladas del juego. |
| `<datos>/content/` | Lo leído del disco, como lo deja la pantalla de primer arranque. |
| `<datos>/logs/launcher.log`, `game.log` | El registro del lanzador y la salida de la última partida. |

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
```

Con `DWE_BUILD_SLOTS=<carpeta de cerrojos>` cada compilación espera uno de los dos huecos de compilación de la
máquina, como `dbuild.sh`. Las fuentes se copian del paquete del juego (`content/packs/game/ui/fonts`) al compilar.

Los tests de Rust cubren el manifiesto, la comparación de versiones, el SHA-256 y la firma, la instalación con
cambio atómico y vuelta atrás, el desempaquetado de `.zip` y `.7z`, la hoja CUE, el registro `install.yaml` (con el
formato del juego) y una extracción con un `dwx` falso; `tests/fake_release.rs` levanta un servidor que contesta como
la API de un repositorio privado y prueba la descarga sin token, con token, con un archivo alterado y con otra firma.

Para probar a mano, `apps/launcher/tools/fake_release_server.py <carpeta> --port 8765` sirve como versiones las
subcarpetas de `<carpeta>` (sus archivos y `notes.md`); se arranca el lanzador con
`DWE_LAUNCHER_API=http://127.0.0.1:8765`.

## Publicar una versión

`scripts/package_release.sh --out <carpeta del repositorio>` compila el paquete (o toma uno con `--tarball`), lo firma y escribe
`game-manifest.json`; `scripts/package/release_manifest.py launcher` escribe `latest.json`. El flujo
`.github/workflows/release.yml` hace todo eso y lo sube a una versión **borrador**; solo se ejecuta a mano y nadie lo
ha ejecutado todavía: la beta no se publica hasta que el responsable la haya jugado.

## Puntos de montaje de la interfaz

La interfaz se organiza en cuatro ranuras con nombre (`ui/js/slots.js`): `sidebar` (el riel), `tabs` (las pestañas
de inicio), `news` (las tarjetas de novedades) y `settings` (las secciones de ajustes). Las piezas del lanzador se
registran ahí igual que lo hará un complemento. Las órdenes con privilegios (disco, llavero, descargas, extracción,
arrancar programas) están solo en `ui/js/api.js` y en los comandos de Rust (`src-tauri/src/commands.rs`); lo que una
pieza recibe para pintarse (`context()`) no lleva ni el token ni la ruta del disco.

## Pendiente

- Probar Windows y macOS (el llavero, rutas y el instalador de cada sistema).
- La clave de firma real y la primera versión publicada (decisión del responsable).
- Complementos de la comunidad sobre las ranuras de la interfaz.
