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
| `disc_image` | La imagen del disco (`.cue` o `.bin`) | Se comprueba que existe y se registra; solo `dwx` lee el disco (ver «Leer el disco en el juego») |
| `extracted_content` | Carpetas extraídas con `dwx` (con paquetes como `base/`), en orden de preferencia | Sus paquetes se montan con los normales |
| `decomp_root` | Una copia del decomp (con `include/dw` y `src`) | Se comprueba y se registra; es referencia de comportamiento, nunca se copia nada |

Todas las claves son opcionales, pero el fichero tiene que nombrar al menos una raíz; una clave desconocida es un
error. Una ruta relativa es relativa a la carpeta del fichero. Si falta el fichero, o una raíz no existe o no es de su
tipo, el juego no arranca y el error dice qué raíz y por qué: con `--dev-assets` nunca se juega a medias.

**Dónde se busca el fichero** (`devAssetsConfigFolders`): primero en la carpeta actual (así cada worktree tiene el
suyo) y después en la carpeta de datos del usuario, la de `settings.yaml` y las partidas. `dwe-sim` no tiene carpeta
de datos y solo mira la carpeta actual. `--dev-assets` es solo un interruptor: no recibe la ruta del fichero.
`dev-assets.yaml` está en el `.gitignore`: tiene rutas de una máquina y no se versiona.

## Precedencia

Los assets de desarrollo **solo rellenan lo que falta** (decisión del usuario del 2026-10-21). `GameContent::mount`
([vfs](vfs.md)) junta primero los paquetes normales (la carpeta de contenido, su `packs/`, los del equipo si no hay
paquete `game`, y los de `--mods`) y después los de cada carpeta extraída:

1. Un paquete normal gana al paquete de desarrollo con su **id** (el del `pack.yaml`, no el nombre de la carpeta): con
   `--content` apuntando a una carpeta con `base/`, un `base/` o un `base_viejo/` con id `base` de las carpetas de
   desarrollo se queda fuera.
2. Entre carpetas de desarrollo, la primera de la lista gana a las siguientes.
3. Los que entran van **debajo de todos los paquetes normales**, también de `base` y de los de `--mods`, digan lo que
   digan su capa y su prioridad (`PackLocation::fillsGaps`): solo aportan los IDs que ningún paquete normal tiene.
   Entre ellos se apilan por capa y prioridad. El log los marca «from the dev assets, under every other pack», y los
   que quedan fuera también salen, con el paquete que les gana.

Así un desarrollador puede jugar con `--content content --dev-assets` (los paquetes del equipo del árbol de fuentes y
el `base` de una carpeta extraída) sin copiar ni empaquetar nada.

## Código

| Dónde | Qué |
|---|---|
| `engine/assets/dev_asset_sources.hpp` | `DevAssetSources` (las raíces por tipo) y `validateDevAssetSources` |
| `engine/assets/dev_pack_selection.hpp` | `findDevPacks` (los paquetes de las carpetas extraídas) y `selectDevPacks` (cuáles entran, por id, y cuáles quedan fuera) |
| `engine/assets/virtual_file_system.hpp` | `PackLocation::fillsGaps`: un paquete que se apila debajo de todos; `readPackManifest` |
| `apps/content/dev_assets.hpp` | La opción, el nombre del fichero, la clave del aviso, `devAssetsConfigFolders`, `parseDevAssetsConfig` y `loadDevAssets` (busca, lee, comprueba y registra) |
| `apps/content/game_content.hpp` | `GameContent::mount(..., devAssets)` monta y etiqueta los paquetes de desarrollo |
| `apps/game` | `content_mount` (monta con la carpeta de datos del usuario) y `dev_assets_banner` (el aviso, y el ajuste `dev_assets_banner` que comprueba `tests/autotest/route-dev-assets.yaml`) |
| `tests/support` | El único dueño de `DWE_DISC`, `DWE_CONTENT` y `DWE_DECOMP` en los tests, y la carpeta temporal `TempFolder` ([README](../../tests/support/README.md)) |

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
la imagen del disco. Con `--dev-assets` el juego ya usa las carpetas extraídas, pero `disc_image` solo se comprueba.
**Decidido (usuario, 2026-10-21): la opción 1**, que llega en un cambio aparte. Las opciones que se valoraron:

1. **(Elegida) Extracción bajo demanda a una caché de desarrollo.** Con `disc_image` y sin carpeta extraída al
   día, el juego pide a `dwx extract` (otro proceso) que extraiga a una caché de desarrollo (por ejemplo
   `<datos del usuario>/dev-cache/<hash del disco>-<versión de dwx>/`) y la monta como una carpeta extraída más. Se
   mantiene la regla 2 y el producto usa exactamente el mismo código que el desarrollo. Coste: la primera vez tarda lo
   que una extracción completa (minutos) y ocupa lo mismo que una carpeta extraída (≈1,5 GB), y hay que decidir cuándo
   la caché está vieja (hash del disco y versión de `dwx`).
2. **Un módulo solo de desarrollo que enlaza `formats` en los builds de desarrollo.** El juego lee el disco al vuelo,
   sin extracción previa. Necesita un ADR de excepción a la regla 2, los builds de desarrollo y de producto dejan de
   ser iguales (contradice «en todos los builds») y las limitaciones del formato original pueden colarse en el
   runtime, justo lo que la regla evita.
3. **Solo carpetas extraídas y decomp en el juego; el disco solo a través de `dwx` a mano.** Es lo que hay hoy: lo
   más simple y sin riesgo para las reglas, pero tras un cambio de `dwx` hay que volver a extraer a mano.
