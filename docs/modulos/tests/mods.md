# Mods de ejemplo de los tests

Paquetes **sintéticos** que usan los tests y las rutas de autotest para comprobar que un mod añade y sustituye
contenido por ID sin código propio ([tablas por entradas](../../architecture/vfs.md#tablas-por-entradas)). No
llevan nada del juego original: todos los valores, la imagen y el modelo están inventados.

## `example_mod`

Un mod de la capa `mods` que depende de `base` y del paquete `game`. Muestra las tres cosas que puede hacer un mod:

| Qué | Ficheros |
|---|---|
| **Añade un Digimon** `example_mod:pebblemon` | Su fila en `data/digimon.yaml` (sin `index`: lo pone el motor) y en `data/raise.yaml`, su modelo en `digimon/example_mod/pebblemon/model.glb` y el contrato del modelo en `contract.yaml`, obligatorio para un Digimon nuevo |
| **Añade un mapa** `example_mod:meadow` | Su fila en `data/maps.yaml` y su carpeta `map/example_mod/meadow/` con `data.yaml` y el fondo `background_day.png`. La salida 0 lleva a File City (`twna01`, salida 1) |
| **Sustituye un asset** | `field/warps.yaml` sustituye entero el del paquete `game`: el campo empieza en el mapa nuevo. Es una copia del de `content/packs/game` con otro `start`, y solo vale para los tests |
| **Nombra lo que añade** | `text/en/example_mod.yaml` y `text/es/example_mod.yaml`, con las claves `digimon_names.pebblemon` y `map_names.meadow` |

Las IDs nuevas llevan el id del paquete como espacio de nombres (`example_mod:`), y las carpetas de sus assets lo
llevan como una carpeta más (`map/example_mod/meadow/`), así que las rutas siguen siendo `[a-z0-9_/]`.

### Los binarios

Los dos ficheros binarios los escribe [`make_example_mod.py`](https://github.com/Indie-Modding-Games/dw-engine/blob/main/tests/mods/make_example_mod.py) (Python 3, sin dependencias):
`python3 tests/mods/make_example_mod.py tests/mods/example_mod`. Escribe siempre los mismos bytes.

- `background_day.png`: PNG indexado de 256x256 con cinco colores: hierba a cuadros, un camino horizontal y agua
  arriba.
- `model.glb`: un tetraedro de 4 vértices sobre un solo hueso (`bone_00`), en las unidades y ejes de los modelos de
  `dwx` (Y hacia abajo), con una textura PNG de 8x8 incrustada. Tiene las animaciones `anim_00` a `anim_04` que el
  campo pide a un compañero, cada una un balanceo de 2 ticks a 20 ticks por segundo que acaba en un bucle (los
  `extras` que lee `engine/assets/src/model_data.cpp`). Cumple `contract.yaml` (un hueso raíz y esas animaciones).

Para cambiarlos basta con otro PNG o glTF que cumpla lo mismo; `dwx validate --with <base> --with content/packs/game
tests/mods/example_mod` dice si el paquete sigue valiendo.

### Dónde se usa

- `apps/session/tests/example_mod_tests.cpp`: las tablas componen, el campo entra en el mapa nuevo por un warp de un
  mapa original y sale por el suyo, una partida nueva empieza con el Digimon nuevo de compañero y los textos nombran
  lo nuevo en los dos idiomas. Con `DWE_CONTENT`, lo mismo sobre la base extraída con todos los sistemas del juego.
- `apps/dwx/tests/validate_tests.cpp`: con `DWE_CONTENT`, `dwx validate` del mod sobre la base y el paquete `game`.
- `tests/autotest/route-mod-map.yaml`: el juego con `--mods tests/mods/example_mod` empieza en el mapa nuevo y sale a
  File City ([prueba vertical](../../guides/prueba-vertical.md)).

## `ui_skin/test_contrast`

Un aspecto de la UI sintético en un mod de la capa `mods` que depende del paquete `game`: `skin.yaml` extiende el
aspecto `modern` y su `theme.rcss` cambia los colores en todas las pantallas, con el nombre del aspecto en los dos
idiomas (`localization/<idioma>/test_contrast.yaml`). Prueba que un mod añade un aspecto sin tocar el motor. Lo usan
`engine/ui/tests/skin_switch_tests.cpp`, los tests de los ajustes de aspecto de `apps/game` y la ruta
`tests/autotest/route-skins.yaml`.
