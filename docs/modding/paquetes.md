# Paquetes e ids

La referencia completa está en [paquetes y VFS](../architecture/vfs.md); esta página es lo que necesita quien hace un
mod.

## El `pack.yaml`

```yaml
id: mi_mod                 # a-z, 0-9 y _; único entre los paquetes montados
version: 1.0.0             # MAJOR.MINOR.PATCH
layer: mods                # mods o user para un mod
api: "1.4"                 # la API de Lua para la que está escrito: obligatoria en mods y user
priority: 0                # dentro de la capa, gana el más alto
name: "Mi mod"
description: "Lo que hace, para las personas."
dependencies:
  - id: game
    min_version: 0.1.0
schemas:                   # qué esquema sigue cada YAML nuevo del paquete (ver abajo)
  mode: [modes/*/mode]
```

- **`layer`.** Un mod (lo que se monta con `--mods`) solo puede ser `mods` o `user`; `base` y `remaster` son del
  motor y un mod que las declara se deja fuera. `user` va encima de `mods`: es para los retoques del propio jugador.
- **`api`.** `"1.x"` acepta cualquier 1.y; `"1.4"` pide la 1.4 o una posterior. Un mod con otra versión mayor o
  con una menor más nueva que la del motor se deja fuera con el motivo, y también uno con ficheros de Lua sin `api`.
  Por ahora, un mod sin `api` que no trae ficheros de Lua (solo datos, como los paquetes que escribían el lanzador y el mapper antes de este cambio) todavía se monta, con un aviso en el log. La versión actual está en la
  [API de Lua](api-lua.md).
- **`dependencies`.** Cada una tiene que estar montada, con su versión mínima, en la misma capa o una más baja. Casi
  todo mod depende de `game` (las reglas, los menús y los textos del juego) y, si toca contenido extraído, de `base`.
- **`schemas`.** Las reglas de todos los paquetes valen para todos: un YAML que sustituye uno del juego ya tiene su
  esquema. Solo hace falta declarar los tipos de documento nuevos.

## Ids de asset

- El id de un fichero es su ruta dentro del paquete sin la extensión: `ui/hud/style.rcss` es `ui/hud/style`.
- Segmentos de `a-z`, `0-9` y `_` separados por `/`, sin mayúsculas ni puntos; un nombre de fichero lleva un solo
  punto.
- **Sustituir:** un fichero con el mismo id que uno de un paquete de debajo lo reemplaza entero, aunque cambie la
  extensión. **Añadir:** un id nuevo simplemente se suma.
- Excepciones: los parches de mapa (`map/<id>/patch`) y los parches de historia se aplican encima en vez de
  sustituir ([mapas](../formats/mapas.md#parches), [historia](historia.md#parches-por-seccion)), y las tablas de
  contenido se componen por entradas (abajo).

## Ids de contenido

Las tablas (`data/items`, los mapas…) se componen por entradas: un mod que trae `data/items` no
sustituye la tabla entera, sino que cambia las entradas con un id que ya existe y añade las nuevas al final.

- Lo del original se nombra por su nombre estable; lo que añade un paquete, por `<id del paquete>:<nombre>`
  (`mi_mod:espada`). En disco, `mi_mod:prado` vive en `map/mi_mod/prado/`.
- El número interno de una entrada nueva lo pone el motor al componer y cambia con los paquetes montados: un mod no
  debe guardarlo ni compararlo.
- El nombre visible de lo añadido va en la tabla de textos del paquete, `text/<idioma>/<id del paquete>.yaml`, con la
  clave `<bloque>.<nombre>` (`digimon_names.pebblemon`).

Un ejemplo completo, con una especie y un mapa nuevos, es el paquete de los tests
[`tests/mods/example_mod`](../modulos/tests/mods.md).

## Textos

Los textos de la interfaz salen de `localization/<idioma>/<área>.yaml` de todos los paquetes, y cada clave lleva
delante el nombre de su fichero: la clave `title` de `localization/es/modes.yaml` es `modes.title`. Un mod pone las
suyas en un fichero con su id (`localization/es/mi_mod.yaml` → `mi_mod.titulo`), en cada idioma que ofrece. Para
cambiar un texto del juego trae el mismo fichero con solo las claves que cambia: las tablas de textos se mezclan
clave a clave, al contrario que el resto de los assets ([localización](../architecture/localizacion.md#paquetes-idiomas-nuevos-y-mods)).
