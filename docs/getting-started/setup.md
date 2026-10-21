# Preparar el entorno

Todo se compila dentro de un contenedor Docker ([ADR 0015](../adr/0015-desarrollo-en-contenedor.md)). En tu máquina
solo necesitas Docker, git y el driver de tu GPU con soporte de Vulkan 1.3.

## 1. Instalar Docker (Ubuntu)

```bash
sudo apt update && sudo apt install -y docker.io docker-buildx
sudo usermod -aG docker $USER
```

Cierra sesión y vuelve a entrar para que el grupo `docker` se aplique. Comprueba que funciona:

```bash
docker run --rm hello-world
```

> Estar en el grupo `docker` equivale a tener permisos de root en la máquina. Es lo habitual en una máquina de
> desarrollo.

## 2. Compilar

```bash
scripts/dev.sh image       # crea la imagen de desarrollo (una vez, o cuando cambie el Dockerfile)
scripts/dev.sh configure   # la primera vez vcpkg compila las dependencias: tarda un rato
scripts/dev.sh build
scripts/dev.sh test
```

El preset por defecto es `linux-clang-debug`, con ASan y UBSan. Para otro: `PRESET=linux-clang-release
scripts/dev.sh configure`.

Los objetos compilados se guardan en una caché de ccache (el volumen `dwe-ccache`), así que volver a compilar algo
que ya se compiló, aunque sea en otra carpeta o worktree del repositorio, sale de la caché. El build, los tests,
clang-tidy y clang-format usan todos los núcleos; `DWE_JOBS=6` los limita.

### Comprobar antes de un PR

```bash
scripts/dev.sh check-commit   # lo de cada commit: build con warnings como errores y los tests rápidos
scripts/dev.sh check-full     # lo de la punta de la rama, como la CI: además formato, tests lentos y clang-tidy
```

Las dos usan el preset `ci`, guardan el log de cada paso en `build/logs/` (o en `DWE_LOG_DIR`) y terminan con error
si falla algo. Los tests lentos son los que leen el disco del usuario o el contenido extraído de él (la etiqueta
`slow` de ctest): sin `DWE_DISC` ni `DWE_CONTENT` pasan al momento. Detalle en el
[ADR 0015](../adr/0015-desarrollo-en-contenedor.md).

### Con tu disco

El contenedor no ve nada fuera del repositorio. Para que las herramientas y los tests del disco lean tu copia, pon en
`DWE_DISC` la ruta de la imagen (`.bin` o `.cue`):

```bash
DWE_DISC="$HOME/juegos/Mi disco (USA).bin" scripts/dev.sh test
```

`dev.sh` monta la carpeta de la imagen **solo lectura** en la misma ruta dentro del contenedor y le pasa `DWE_DISC`.
Pon la ruta entre comillas si tiene espacios o paréntesis. Sin `DWE_DISC`, los tests que necesitan el disco se saltan
(la CI nunca lo tiene). Igual con `DWE_CONTENT` (una carpeta extraída con `dwx`) y `DWE_DECOMP` (una copia del
decomp, para los tests que comparan con él); los tests los leen solo a través de `tests/support`
([README](../../tests/support/README.md)). Para jugar con tus assets locales sin empaquetar nada, mira
[assets de desarrollo](../architecture/assets-de-desarrollo.md).

## 3. Jugar

```bash
scripts/dev.sh run
```

El juego se ejecuta en tu máquina, no en el contenedor. En debug activa las validation layers de Vulkan; si no están
instaladas (un build sin la feature `validation`), avisa en el registro (`validation layer unavailable`) y sigue sin ellas.

- Muévete con WASD, las flechas, la cruceta o el stick izquierdo de cualquier mando.
- `º`/`` ` `` o `F1` abre la consola de desarrollo. Ahí puedes escribir Lua: `marker.x = 100`.
- Edita `apps/game/scripts/main.lua` con el juego abierto: se recarga al guardar.
- En la ventana Frame del overlay, **asset viewer** abre el [visor de assets](../guides/visor-de-assets.md). Monta
  `content/base`, o la carpeta que pases con `scripts/dev.sh run --content <carpeta>`.
- `scripts/dev.sh run --content <contenido extraído> --model-viewer` muestra, en vez de la escena de prueba, el
  jugador y el compañero con cada una de sus animaciones ([modelos](../architecture/models.md#visor-de-modelos)).
- Si tu mando no se reconoce, deja un `gamecontrollerdb.txt` de SDL en la carpeta desde la que lanzas el juego.
- `scripts/dev.sh run --check-content content/base` no abre ventana: monta los paquetes de la carpeta (carpetas y
  archivos `.pak`), abre cada asset con los lectores del motor (PNG, KTX2, glTF, YAML, Ogg Opus, WAV, SF2, MIDI,
  Y4M), lista los que fallan y termina con error si hay alguno.
- `scripts/dev.sh run --content <contenido extraído> --list-saves` no abre ventana: escribe en el log cada ranura de
  guardado (vacía, su cabecera o por qué no se lee). `--saves <carpeta>` usa otra carpeta de partidas en vez de la
  de los datos del usuario ([partidas guardadas](../architecture/save.md#dónde-están-los-ficheros)).

## Windows

> **Escrito, sin verificar hasta el primer push.** Los presets y el job de la CI de Windows siguen la documentación de
> CMake, vcpkg y clang-cl, pero todavía no se han ejecutado nunca.

En Windows no se usa el contenedor: se compila con **clang-cl** y la biblioteca estándar de MSVC.

1. Instala Visual Studio 2022 (o sus Build Tools) con "Desarrollo para el escritorio con C++", y LLVM 20 o posterior
   (`clang-cl` en el `PATH`).
2. Clona vcpkg en el commit del `builtin-baseline` de `vcpkg.json`, ejecuta `bootstrap-vcpkg.bat` y define
   `VCPKG_ROOT` apuntando a esa carpeta.
3. Desde una "Developer PowerShell for VS 2022" (trae CMake y Ninja):

```powershell
cmake --preset windows-clang-debug
cmake --build --preset windows-clang-debug
ctest --preset windows-clang-debug
```

Las dependencias se compilan con el triplet `x64-windows-static` (librerías estáticas y CRT estático, `/MT`), así
que el juego no necesita que el jugador instale el runtime de Visual C++. El cargador de Vulkan (`vulkan-1.dll`) se
copia junto al ejecutable. Para activar las validation layers en debug:
`$env:VK_LAYER_PATH = "build\windows-clang-debug\vcpkg_installed\x64-windows-static\bin"`. Los builds de Windows
no llevan sanitizers.

### El build de Windows desde Linux

El preset `windows-cross` compila y enlaza desde el contenedor lo mismo que `windows-ci` (clang-cl, debug, CRT
estático, warnings como errores), así que los errores de la biblioteca estándar de MSVC salen en local y no solo en la
CI. Usa el `clang-cl` y el `lld-link` de la imagen contra el CRT, la STL de MSVC y el Windows SDK que descarga
[xwin](https://github.com/Jake-Shadle/xwin):

```bash
scripts/dev.sh windows-sdk                       # una vez: descarga las librerías al volumen dwe-xwin
PRESET=windows-cross scripts/dev.sh configure
PRESET=windows-cross scripts/dev.sh build
```

- **Licencia:** las cabeceras y librerías de Microsoft van con la licencia de Visual Studio y del Windows SDK.
  `windows-sdk` la muestra y pide aceptarla a quien lo ejecuta; `--accept-license` la da por aceptada, solo si ya la
  has leído. Esos ficheros viven en el volumen `dwe-xwin` (o en `DWE_XWIN_DIR`) y **nunca** se suben al repositorio.
- Las dependencias se compilan con el triplet `x64-windows-static-cross`: el `x64-windows-static` de siempre, pero con
  clang-cl en lugar del `cl` de Visual Studio.
- Los tests no se ejecutan: son ejecutables de Windows.
- xwin trae las librerías de Visual Studio 2022 (MSVC 14.44); la CI de Windows usa las de Visual Studio 2026 (14.51).
  Los errores de portabilidad salen igual, pero un aviso nuevo de 14.51 solo saldría en la CI.
- La primera vez vcpkg compila las dependencias para Windows (unos 4 minutos); después el build en frío tarda minuto
  y medio y, con la caché de ccache llena, segundos.

## Otros comandos

| Comando | Qué hace |
|---|---|
| `scripts/dev.sh shell` | Abre una terminal dentro del contenedor |
| `scripts/dev.sh format` | Formatea el código con clang-format (`--check` solo comprueba) |
| `scripts/dev.sh tidy` | Pasa clang-tidy en paralelo (necesita un `configure` previo) |
| `scripts/dev.sh test -LE slow` | Pasa los tests sin los lentos, los que leen el disco o el contenido extraído |
| `build/<preset>/apps/dwx/dwx verify <fichero>` | Calcula el SHA-1 de una imagen o un ejecutable y dice de qué versión es |
| `build/<preset>/apps/dwx/dwx extract --list <imagen.cue>` | Lista los ficheros del disco con su SHA-1 y comprueba la tabla de ficheros del ejecutable ([Disco](../formats/disco.md)) |
| `build/<preset>/apps/dwx/dwx extract --cache <carpeta> <imagen.cue>` | Escribe la carpeta del contenido extraído del disco en una caché (la extrae la primera vez): lo que usa `--dev-assets` con `disc_image` ([assets de desarrollo](../architecture/assets-de-desarrollo.md#leer-el-disco-en-el-juego)) |
| `build/<preset>/apps/dwx/dwx validate content/base` | Comprueba un paquete o una carpeta de paquetes: esquemas, referencias, contratos de los modelos y que cada asset se abre ([detalle](../architecture/assets-pipeline.md#dwx-validate)) |

## Editor

Abre la carpeta con VS Code o CLion y elige "Reopen in Container": usa el mismo `.devcontainer/`.
