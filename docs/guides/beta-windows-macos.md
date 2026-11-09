# Beta para Windows y macOS

El jugador de Windows o de Mac hace lo mismo que el de Linux: instala el lanzador, elige su disco y juega. No instala
nada más a mano: ni librerías, ni Vulkan, ni un runtime de C++. Solo hace falta el controlador normal de su tarjeta
gráfica (en Windows trae Vulkan; en macOS Vulkan va dentro del juego, sobre Metal). Antes de jugar, el lanzador
pregunta al juego si alguna tarjeta le sirve (`dwe-game --check-system`); si ninguna sirve, muestra qué hacer con un
botón a la página de controladores del fabricante y, en Windows, a Windows Update
([requisitos del equipo](../modulos/apps/launcher.md#requisitos-del-equipo)).

| | Windows | macOS |
|---|---|---|
| Lanzador | `dwe-launcher_<versión>_x64-setup.exe` (NSIS, por usuario, sin administrador) | `dwe-launcher_<versión>_universal.dmg` (Apple silicon e Intel) |
| Paquete del juego | `dwe-game-<versión>-windows-x86_64.tar.xz` | `dwe-game-<versión>-darwin-aarch64.tar.xz` y `-darwin-x86_64.tar.xz` |
| Datos del lanzador | `%LOCALAPPDATA%\indie-modding-games\dwe` | `~/Library/Application Support/indie-modding-games/dwe` |
| Partidas y ajustes del juego | `%APPDATA%\indie-modding-games\dwe` | La misma carpeta que los datos |
| Probado | Con Wine: el paquete del juego (arranca hasta el título, `dwx` identifica el disco), la instalación del lanzador y sus pruebas | Nada todavía: falta un Mac |

El lanzador elige el paquete de su ordenador en `game-manifest.json` por la clave `<sistema>-<procesador>`
(`linux-x86_64`, `windows-x86_64`, `darwin-aarch64`, `darwin-x86_64`, los mismos nombres que `latest.json` del
actualizador). En Windows los datos van a la carpeta local y no a la itinerante, porque las versiones instaladas y el
contenido ocupan cientos de megas; el juego sigue guardando partidas y ajustes en su carpeta de siempre, así que
`dwe-game.exe` arrancado a mano las encuentra (el contenido no: se le pasa con `--content` o se vuelve a leer el
disco).

## Windows

### El paquete del juego

```bash
scripts/dev.sh windows-sdk --accept-license   # una vez: las librerías de MSVC y el SDK de Windows (xwin)
scripts/package_windows.sh --out <carpeta>
```

Compila en el contenedor de desarrollo, desde Linux, el preset `windows-cross-package` (clang-cl con el CRT estático,
release, `DWE_PACKAGE`) y deja `dwe-beta-<versión>-<commit>-windows-x86_64.tar.xz` con la misma forma que el de Linux:
`bin/dwe-game.exe`, `bin/dwx.exe`, `bin/vulkan-1.dll`, `packs/`, `licenses/`, `LICENSE`, `THIRD_PARTY_NOTICES.txt`,
`README.md`, `VERSION`, `EXTRACTOR_VERSION` y `dwe.cmd` para arrancarlo sin lanzador. Los `.pdb` quedan aparte, para
los informes de fallos.

- `packs/` lleva los paquetes del equipo cocinados y empaquetados en `.pak`. Como `dwx.exe` no corre en Linux, el
  script los cocina con el `dwx` del preset `linux-clang-package` (lo compila si hace falta); cocinar da los mismos
  bytes en los dos sistemas dentro del alcance de la [ADR 0017](../adr/0017-cocinado-y-archivos-pak.md#determinismo).
- El CRT y la librería estándar de C++ van dentro de los programas (`/MT`), así que no hace falta instalar ningún
  redistribuible. La única DLL que se reparte es el cargador de Vulkan; el script lee las importaciones de cada
  programa (`llvm-readobj --coff-imports`) y se niega a empaquetar si alguna DLL no está en `bin/` ni es de Windows.
- El juego del paquete es un programa de ventana (`/SUBSYSTEM:WINDOWS`): no abre una consola al lado. Su salida va al
  registro que le da el lanzador (`<datos>\logs\game.log`). `dwx.exe` sigue siendo de consola, y el lanzador lo
  ejecuta sin ventana.
- Como en Linux, el script se niega si un programa nombra una carpeta de la máquina que compila o si hay un paquete
  `base`.

### Probarlo con Wine

```bash
Xvfb :125 -screen 0 1280x720x24 -nolisten tcp &
scripts/wine_smoke.sh <tarball o carpeta> --display :125 --out <carpeta> \
  --disc <imagen .cue> --content <contenido extraído>
```

Corre el paquete en la imagen `dwe-wine-smoke` (`.devcontainer/wine.Dockerfile`: Ubuntu con Wine, Mesa y lavapipe,
sin red): `dwx.exe identify` con el disco y `dwe-game.exe --content` en el Xvfb que se le da, con capturas antes y
después de pulsar Intro. Vulkan pasa por `winevulkan` hasta lavapipe, así que no hace falta GPU. Bajo Wine se usa el
`vulkan-1` propio de Wine (`WINEDLLOVERRIDES=vulkan-1=b`): Wine solo registra su controlador para su cargador, y el
`vulkan-1.dll` del paquete no encontraría ninguno; en Windows el del paquete lee los controladores del registro como
cualquier juego.

Wine no es Windows: el instalador, el llavero y SmartScreen no se prueban así.

### El lanzador

```bash
scripts/launcher.sh bundle-windows
```

Compila en la imagen del lanzador (`.devcontainer/launcher.Dockerfile`, con el destino MSVC de Rust, `cargo-xwin`,
clang y NSIS) el instalador `build/launcher/x86_64-pc-windows-msvc/release/bundle/nsis/dwe-launcher_<versión>_x64-setup.exe`
y, con la clave de firma, su `.sig` para el actualizador. `tauri.windows.conf.json` lo configura:

- **Por usuario** (`installMode: currentUser`): se instala en `%LOCALAPPDATA%\dwe-launcher` y se registra en
  «Aplicaciones instaladas» del usuario, sin pedir permisos de administrador.
- **WebView2 dentro** (`embedBootstrapper`): si el Windows del jugador no tiene el motor web que usa la interfaz
  (un Windows 10 sin actualizar), el instalador lleva su instalador pequeño y lo ejecuta; este descarga el motor, así
  que esa primera instalación necesita internet (el lanzador la necesita igual para descargar el juego). Windows 11
  ya lo trae. Si no se puede instalar, el instalador se detiene sin dejar nada a medias.
- Con Wine se comprobó la instalación silenciosa (`/S`): el programa queda en `%LOCALAPPDATA%\dwe-launcher` con su
  desinstalador y la entrada del usuario. La interfaz del lanzador no se puede probar con Wine (WebView2 no funciona
  ahí).
- En español o inglés, según el idioma de Windows.

### Lo que ve el jugador: SmartScreen

El instalador y el juego no están firmados con un certificado de editor, así que la primera vez Windows muestra
«Windows protegió su PC». El jugador pulsa **Más información → Ejecutar de todas formas**. El antivirus puede
analizar `dwe-game.exe` la primera vez que arranca; tarda unos segundos.

Con un certificado de firma de código (de una autoridad reconocida, o el servicio de firma en la nube de Windows) el
aviso cambia a mostrar el nombre del editor y, con reputación acumulada, desaparece. Hace falta firmar el instalador,
`dwe-game.exe` y `dwx.exe` en el flujo de publicación; el actualizador de Tauri no cambia.

## macOS

No hay ningún Mac en la máquina del equipo y no se descarga el SDK de Apple, así que todo lo de macOS está preparado
pero **sin probar**: lo compilan y empaquetan los ejecutores macOS de GitHub cuando se lance el flujo de publicación.

### El compilador

Clang de Apple y la libc++ del sistema no tienen todo el C++26 que usa el código (`std::print`, `std::ranges::to`,
`std::views::zip`, `std::jthread` sin `-fexperimental-library`, `std::from_chars` de coma flotante) y además atan
partes de la librería a la versión del sistema. Por eso los presets `macos-arm64-*` y `macos-x64-*` usan el LLVM de
Homebrew (`tooling/cmake/toolchains/macos-llvm-libcxx.cmake`) y enlazan su libc++ y libc++abi de forma estática, como en
Linux. Mínimo: macOS 13.3 (la libc++ formatea números con coma flotante, `std::to_chars`, desde esa versión). Homebrew
vive en `/opt/homebrew` en Apple silicon y en `/usr/local` en los Mac Intel; la toolchain usa `DWE_LLVM_ROOT` si está
(el flujo de publicación la fija con `brew --prefix llvm`), si no `HOMEBREW_PREFIX`, y si no la primera de esas dos
carpetas que tenga el LLVM. Sin LLVM se para con un mensaje claro en vez del error engañoso de vcpkg («unable to find
a build program corresponding to Ninja») que tumbó la primera ejecución en Intel.

### El paquete del juego

```bash
scripts/package_macos.sh --arch arm64 --out <carpeta>   # o --arch x64 en un Mac Intel
```

Necesita `brew install llvm molten-vk ninja`, CMake y `VCPKG_ROOT`. Deja `dwe-beta-<versión>-<commit>-darwin-aarch64.tar.xz`
con:

| Ruta | Qué es |
|---|---|
| `dwe-game.app/Contents/MacOS/` | `dwe-game` y `dwx`. |
| `dwe-game.app/Contents/Frameworks/` | El cargador de Vulkan (`libvulkan.1.dylib`) y MoltenVK, que implementa Vulkan sobre Metal. |
| `dwe-game.app/Contents/Resources/packs/` | Los paquetes del equipo en `.pak` (`game.pak`, `es-translation.pak`); `content::teamPacksFolder` los busca ahí. |
| `dwe-game.app/Contents/Resources/vulkan/icd.d/MoltenVK_icd.json` | Donde el cargador busca los controladores dentro de un paquete de aplicación. |
| `README.md`, `VERSION`, `EXTRACTOR_VERSION`, `licenses/`, `LICENSE`, `THIRD_PARTY_NOTICES.txt` | Como en Linux. |

El lanzador ejecuta `dwe-game.app/Contents/MacOS/dwe-game` cuando el paquete trae la aplicación, y `bin/dwe-game`
si no. El juego encuentra `dwx` y los paquetes a partir de la ruta real de su programa (`_NSGetExecutablePath`),
porque SDL da `Contents/Resources` dentro de una aplicación.

Riesgos sin medir: el juego pide Vulkan 1.3 con `bufferDeviceAddress` e indexado de descriptores; MoltenVK lo da en
Apple silicon con Metal 3, pero los Mac Intel con gráficas antiguas pueden quedarse fuera.

### El lanzador

Un solo `.dmg` universal (`tauri build --target universal-apple-darwin`) para Apple silicon e Intel, con su
`.app.tar.gz` y `.sig` para el actualizador. `tauri.macos.conf.json` pide macOS 13.3, como el juego, y la firma ad hoc
(`signingIdentity: "-"`).

### Lo que ve el jugador: Gatekeeper

El lanzador lleva una firma **ad hoc** (`codesign -s -`), sin identidad de desarrollador, y no está notarizado. Apple
silicon exige alguna firma para ejecutar un programa; la ad hoc basta, pero Gatekeeper avisa de que «no se puede
verificar el desarrollador» la primera vez que se abre algo descargado de internet. El jugador:

1. Arrastra `dwe-launcher` desde el `.dmg` a Aplicaciones.
2. Hace **clic derecho (o Control+clic) → Abrir** y confirma con **Abrir**. En macOS 15 y posteriores ese atajo ya
   no basta: abre el lanzador una vez, cierra el aviso y va a **Ajustes del Sistema → Privacidad y seguridad →
   Abrir igualmente**, y confirma con su contraseña.

Solo hace falta una vez. El juego no lo pide: lo descarga el propio lanzador, y lo que descarga un programa así no
queda marcado en cuarentena.

Con una cuenta de Apple Developer (de pago, anual) se firma con un certificado «Developer ID Application» y se
notariza (`notarytool`) en el flujo de publicación: Gatekeeper abre entonces el lanzador sin aviso. Tauri lo hace
con las variables `APPLE_CERTIFICATE`, `APPLE_SIGNING_IDENTITY`, `APPLE_ID`, `APPLE_PASSWORD` y `APPLE_TEAM_ID`, y
el paquete del juego se firmaría con la misma identidad en `package_macos.sh`.

## Publicación y su coste

`.github/workflows/release.yml` corre en cada fusión en `main` que cambia código (no con cambios solo de
documentación) y compila, en paralelo, el juego para Linux y
Windows en Ubuntu (Windows por compilación cruzada), el juego para Apple silicon (`macos-15`) e Intel
(`macos-15-intel`), y el lanzador para Linux, Windows (en Ubuntu) y macOS (el `.dmg` universal, `macos-15`). Los
manifiestos nombran siempre los cuatro ordenadores. Cada ordenador se compila por su cuenta: si uno falla, los demás
terminan, el resumen de la ejecución dice cuál falta y no se publica nada hasta que *Re-run failed jobs* lo complete.
Qué comprueba y cómo una versión de prueba pasa a estable: [Publicar una
versión](../modulos/apps/launcher.md#publicar-una-versión). `scripts/release_local.sh` sigue siendo el respaldo para
Linux y Windows desde el PC del responsable: [Publicar desde este
PC](../modulos/apps/launcher.md#publicar-desde-este-pc).

Los pull requests no prueban en Windows de verdad: su comprobación `windows` solo compila por compilación cruzada.
`.github/workflows/platform-check.yml` compila y pasa las pruebas unitarias del motor y del lanzador en
`windows-latest` cada lunes y a mano, y también en `macos-latest` si se marca la opción `macos`.

El repositorio es público, así que los minutos de los ejecutores estándar de GitHub (Linux, Windows y macOS) no se
cobran. Duración aproximada (sin cachés → con cachés llenas y un cambio pequeño):

| Flujo | Linux y Windows | macOS |
|---|---|---|
| `release.yml` | ~30 → ~5 min | ~22 → ~8 min (Intel es el más lento) |
| `platform-check.yml` (por defecto: Windows) | ~70 → ~35 min | — |
| `platform-check.yml` con `macos` | ~70 → ~35 min | ~60 → ~30 min |

## Pendiente

- Probar el lanzador en un Windows de verdad (instalador, llavero, actualización) y todo en un Mac.
- Firmas de editor (decisión del responsable: tienen coste).
