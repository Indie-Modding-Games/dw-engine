# Assets de desarrollo

En desarrollo, el juego y las herramientas pueden usar **todos** los assets locales: la imagen del disco, las carpetas
que extrajo `dwx` y los datos del decomp. Es explícito y opcional: nunca es lo que recibe un jugador. El camino del
producto no cambia: `dwx extract` coloca el paquete `base` en la máquina del usuario y el juego lo monta como
cualquier otro paquete ([tubería de assets](assets-pipeline.md)). Ningún dato del juego entra en git por ninguno de
los dos caminos.

## Cómo se activa

Con la opción `--dev-assets` de `dwe-game` y `dwe-sim` (decisión del usuario del 2026-10-21): existe en todos los
builds, está apagada por defecto y, mientras está activa, el juego muestra un aviso en pantalla, encima de cualquier
otro documento (`ui/dev_assets/document` del paquete `game`, clave de texto `dev.assets_active` en inglés y español),
y lo escribe en el log. Las raíces se leen de un fichero `dev-assets.yaml`:

```yaml
disc_image: /home/yo/juegos/mi-disco.cue
extracted_content:
  - ../content-f4
  - ../content-f3
decomp_root: /home/yo/code/dw_decomp
```

| Clave | Qué es | Qué se hace con ella hoy |
|---|---|---|
| `disc_image` | La imagen del disco (`.cue`, `.bin` o `.iso`) | `dwx` la extrae a la caché de desarrollo y esa carpeta se monta como la última de `extracted_content` (ver «Leer el disco en el juego») |
| `extracted_content` | Carpetas extraídas con `dwx` (con paquetes como `base/`), en orden de preferencia | Sus paquetes se montan con los normales |
| `decomp_root` | Una copia del decomp (con `include/dw` y `src`) | Se comprueba y se registra; es referencia de comportamiento, nunca se copia nada |

Todas las claves son opcionales, pero el fichero tiene que nombrar al menos una raíz; una clave desconocida es un
error. Una ruta relativa es relativa a la carpeta del fichero. Si falta el fichero, o una raíz no existe o no es de su
tipo, el juego no arranca y el error dice qué raíz y por qué: con `--dev-assets` nunca se juega a medias.

**Dónde se busca el fichero** (`devAssetsConfigFolders`): primero en la carpeta actual (así cada worktree tiene el
suyo) y después en la carpeta de datos del usuario, la de `settings.yaml` y las partidas. `dwe-sim` usa la misma carpeta de datos que el juego
(`content/user_data.hpp`), así que ambos comparten el fichero y la caché. `--dev-assets` es solo un interruptor: no
recibe la ruta del fichero.
`dev-assets.yaml` está en el `.gitignore`: tiene rutas de una máquina y no se versiona.

## Precedencia

Los assets de desarrollo **solo rellenan lo que falta** (decisión del usuario del 2026-10-21). `GameContent::mount`
([vfs](vfs.md)) junta primero los paquetes normales (la carpeta de contenido, su `packs/`, los del equipo si no hay
paquete `game`, y los de `--mods`) y después los de cada carpeta extraída:

1. Un paquete normal gana al paquete de desarrollo con su **id** (el del `pack.yaml`, no el nombre de la carpeta): con
   `--content` apuntando a una carpeta con `base/`, un `base/` o un `base_viejo/` con id `base` de las carpetas de
   desarrollo se queda fuera. Los paquetes de desarrollo se eligen con los del juego solos, sin los mods, y el juego
   tiene que montar con ellos: un mod no puede cambiar ni romper esa base. Un mod con el id de un paquete de
   desarrollo elegido se deja fuera (`skippedMods()`) porque el id ya está montado, igual que sin assets de desarrollo
   un mod con el id de un paquete del juego, y el paquete de desarrollo se queda.
2. Entre carpetas de desarrollo, la primera de la lista gana a las siguientes. La carpeta que sale de `disc_image` va
   la última: las de `extracted_content`, que se nombran a mano, le ganan por id.
3. Los que entran van **debajo de todos los paquetes normales**, también de `base` y de los de `--mods`, digan lo que
   digan su capa y su prioridad (`PackLocation::fillsGaps`): solo aportan los IDs que ningún paquete normal tiene.
   Entre ellos se apilan por capa y prioridad. El log los marca «from the dev assets, under every other pack», y los
   que quedan fuera también salen, con el paquete que les gana.

Así un desarrollador puede jugar con `--content content --dev-assets` (los paquetes del equipo del árbol de fuentes y
el `base` de una carpeta extraída) sin copiar ni empaquetar nada.

## Código

| Dónde | Qué |
|---|---|
| `packages/engine/assets/dev_asset_sources.hpp` | `DevAssetSources` (las raíces por tipo) y `validateDevAssetSources` |
| `packages/engine/assets/dev_pack_selection.hpp` | `findDevPacks` (los paquetes de las carpetas extraídas) y `selectDevPacks` (cuáles entran, por id, y cuáles quedan fuera) |
| `packages/engine/assets/virtual_file_system.hpp` | `PackLocation::fillsGaps`: un paquete que se apila debajo de todos; `readPackManifest` |
| `packages/content/dev_assets.hpp` | La opción, el nombre del fichero, la clave del aviso, `devAssetsConfigFolders`, `parseDevAssetsConfig`, `loadDevAssets` (busca, lee, comprueba y registra) y `openDevAssets` (todo eso más el disco, para el juego y `dwe-sim`) |
| `packages/content/dev_disc.hpp` | La imagen del disco a través de `dwx`: dónde se busca `dwx` (`dwxCandidates`, `findDwx`), `extractedDiscContent` (ejecuta `dwx extract --cache`) y `withDiscContent` |
| `packages/content/user_data.hpp` | La organización y la aplicación de la carpeta de datos del usuario, comunes al juego y a `dwe-sim` |
| `apps/dwx/disc_cache.hpp` | La caché: `ExtractorVersion`, la clave, la entrada que se escribe aparte y se renombra, las entradas viejas y `dwx extract --cache` ([página del módulo](../modulos/apps/dwx.md)) |
| `packages/engine/platform/process.hpp` | `runProcess` (ejecuta un programa sin shell y recoge su salida), `executableFolder` y `executableFileName` |
| `packages/content/game_content.hpp` | `GameContent::mount(..., devAssets)` monta y etiqueta los paquetes de desarrollo |
| `apps/game` | `content_mount` (monta con `openDevAssets`) y `dev_assets_banner` (el aviso, y el ajuste `dev_assets_banner` que comprueba `tests/autotest/route-dev-assets.yaml`) |
| `tests/support` | El único dueño de `DWE_DISC`, `DWE_CONTENT` y `DWE_DECOMP` en los tests, y la carpeta temporal `TempFolder` ([página del módulo](../modulos/tests/support.md)) |

Los tests usan las variables de `scripts/dev.sh`, no `--dev-assets`. En el contenedor solo se ven las rutas que
`dev.sh` monta (`DWE_DISC`, `DWE_CONTENT`, `DWE_DECOMP`), así que un `dev-assets.yaml` con otras rutas falla dentro de
él; `scripts/dev.sh run` corre en el host y no tiene ese límite.

## Qué hay en el decomp

Una copia del decomp (`~/code/dw_decomp`) tiene, para la versión `us`:

| Ruta | Qué es | Uso posible |
|---|---|---|
| `include/dw/*.h` | Tipos, enums y nombres de opcodes del original (`script.h`, `pstat.h`, `trigger.h`...) | Referencia para nombrar y comprobar opcodes y estado en las fichas y los tests de paridad |
| `src/<overlay>/*.c` | El C decompilado | Referencia de comportamiento (`archivo.c: función` en un comentario) |
| `text/<overlay>/*.yaml` | Los textos que el decomp genera, por bloque y versión | Comparar el texto que extrae `dwx` |
| `config/us/*.yaml`, `symbols_*.txt` | La división del ejecutable en segmentos y los símbolos | Localizar tablas y funciones al verificar un extractor |
| `disks/us/` | Los ficheros del disco volcados por el usuario (fuera del git del decomp) | Leer un fichero del disco sin abrir la imagen (solo herramientas) |
| `build/`, `asm/` | Salida de su build (fuera de su git) | Ninguno previsto |

El motor solo comprueba que la raíz tiene `include/dw` y `src`; no lee ni copia nada de ella. Los tests de paridad que
comparen con el decomp la encuentran con `test_support::userDecomp()` y conocen las rutas que necesitan.

## Leer el disco en el juego

La regla 2 de [dependencias](dependency-rules.md) dice que el runtime nunca depende de `formats`: solo `dwx` sabe leer
la imagen del disco. Por eso, con `disc_image`, el juego (y `dwe-sim`) le piden a `dwx`, en otro proceso, el contenido
extraído del disco en una **caché de desarrollo** y montan esa carpeta como una carpeta extraída más (decisión del
usuario del 2026-10-21, opción 1 de las de abajo). Sin `--dev-assets`, o sin `disc_image`, no se ejecuta nada.

**Cómo funciona.** Al arrancar con `--dev-assets`, `openDevAssets` lee el fichero y, si nombra una imagen, ejecuta

```sh
dwx extract --cache <datos del usuario>/dev-cache <imagen>
```

con los argumentos tal cual, sin shell (`platform::runProcess`). `dwx` calcula la clave del disco, `<sha1>-v<versión>`:
el SHA-1 del fichero que tiene los sectores (el `.bin` de un `.cue`) y `ExtractorVersion`, la versión de lo que escribe
`dwx extract`.

- Si la entrada `dev-cache/<clave>/` existe, la usa tal cual: es lo normal a partir de la segunda vez y tarda lo que
  tarda el SHA-1 de la imagen (medido con una imagen de 380 MB: unos 12 s en la build `ci`, de depuración con ASan;
  la primera extracción tardó unos 5 minutos en esa misma build).
- Si no existe, extrae el disco entero (**la primera vez tarda unos minutos y ocupa ≈1,5 GB** con el juego completo, en
  `~/.local/share/indie-modding-games/dwe/dev-cache/` en Linux —`$XDG_DATA_HOME` si está definida— y en
  `%APPDATA%\indie-modding-games\dwe\dev-cache\` en Windows; el log lo avisa y `dwx` escribe su
  progreso en la salida de errores) en una carpeta temporal oculta, `.<clave>.partial-<aleatorio>/base/`, añade
  `dwx-cache.yaml` (el SHA-1 y la versión) y la **renombra** a `dev-cache/<clave>/`. Una entrada solo aparece por ese
  renombrado, después de volcar al disco (`fsync`) todo lo que tiene, así que una entrada que existe está completa; si
  la extracción falla, la carpeta temporal se borra. Si dos procesos extraen a la vez el mismo disco, el que renombra
  segundo tira su copia y usa la del primero.
- Una entrada es una carpeta real con su `dwx-cache.yaml`. La caché nunca sigue un enlace, ni para usar una entrada ni
  para borrarla; lo que tenga el nombre de una entrada sin serlo (un enlace, un fichero, una carpeta sin registro) se
  borra (un enlace, sin lo que apunta) y se extrae de nuevo, con un aviso.
- En su salida estándar `dwx` escribe solo la carpeta de la entrada, en UTF-8: ese es el contrato con el juego.

Lo que no cambia: la caché solo escribe en la carpeta de datos del usuario, nunca en la imagen, en `~/code/assets` ni
en carpetas de contenido existentes; su carpeta se monta como las de `extracted_content` (después de ellas), debajo de
todos los paquetes normales.

**Cuándo está vieja.** Una entrada no se parchea nunca: otro disco o otra `ExtractorVersion` dan otra clave y otra
entrada nueva. `ExtractorVersion` se sube a mano cada vez que cambia lo que `dwx extract` escribe para el mismo disco;
el test de la huella (`apps/dwx/tests/disc_cache_tests.cpp`) extrae un disco sintético que llega a varios extractores y
falla, diciendo «bump ExtractorVersion and update the fingerprint», hasta que se sube y se añade la fila de la versión
nueva a su tabla de huellas, que solo crece (las versiones suben y cada una tiene su propia huella). El
test solo ve lo que ese disco sintético recorre: un cambio en un extractor que no toca (los que necesitan las tablas
del ejecutable, por ejemplo) también obliga a subir la versión, y eso lo recuerda el [página de dwx](../modulos/apps/dwx.md).

**Limpieza.** Tras escribir una entrada nueva, `dwx` borra las entradas del mismo disco de versiones **anteriores**
que escribió él (las que tienen la marca de `dwx extract` en `base/`). Las de versiones posteriores se quedan: son de
otro `dwx` más nuevo, por ejemplo el de otro worktree. Las carpetas temporales no se tocan, porque otra extracción
puede estar escribiéndolas; las de más de 24 horas salen en un aviso para borrarlas a mano cuando no haya ninguna
extracción en marcha. Toda la caché se puede borrar sin riesgo: se vuelve a extraer.

**Dónde se busca `dwx`.** Junto al ejecutable que corre y, si no está, en su carpeta del árbol de build
(`build/<preset>/apps/game/dwe-game` → `build/<preset>/apps/dwx/dwx`). Si no está en ninguno de los dos sitios, el
juego no arranca y el error nombra los dos.

Las opciones que se valoraron:

1. **(Elegida) Extracción bajo demanda a una caché de desarrollo.** Se mantiene la regla 2 y el producto usa
   exactamente el mismo código que el desarrollo. Coste: la primera vez tarda lo que una extracción completa (minutos)
   y ocupa lo mismo que una carpeta extraída (≈1,5 GB).
2. **Un módulo solo de desarrollo que enlaza `formats` en los builds de desarrollo.** El juego lee el disco al vuelo,
   sin extracción previa. Necesita un ADR de excepción a la regla 2, los builds de desarrollo y de producto dejan de
   ser iguales (contradice «en todos los builds») y las limitaciones del formato original pueden colarse en el
   runtime, justo lo que la regla evita.
3. **Solo carpetas extraídas y decomp en el juego; el disco solo a través de `dwx` a mano.** Lo más simple, pero tras
   un cambio de `dwx` hay que volver a extraer a mano.
