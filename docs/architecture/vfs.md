# Paquetes y sistema de ficheros virtual

Cómo encuentra el motor cada asset entre los paquetes montados. Es la parte de "capas de contenido" de la
[tubería de assets](assets-pipeline.md), y el código está en `packages/engine/assets`.

## Un paquete

Un paquete es una carpeta con un `pack.yaml` en la raíz y los ficheros de sus assets debajo:

```text
hd_backgrounds/
├─ pack.yaml
└─ map/
   └─ file_city_01/
      └─ background.ktx2      → map/file_city_01/background
```

### El manifiesto

```yaml
id: hd_backgrounds
version: 1.2.0
layer: remaster
priority: 0
name: Fondos en HD
description: Fondos de mapa escalados.
dependencies:
  - id: base
    min_version: 1.0.0
```

| Campo | Obligatorio | Qué es |
|---|---|---|
| `id` | Sí | De 1 a 64 caracteres `a-z`, `0-9` y `_`. No puede haber dos paquetes montados con el mismo |
| `version` | Sí | Versión semántica `MAJOR.MINOR.PATCH`, sin sufijos (`-beta`, `+build`) |
| `layer` | Sí | `base`, `remaster`, `mods` o `user`. Un mod (de una carpeta de mods o de `--mods`) solo puede declarar `mods` o `user`; si no, se deja fuera |
| `priority` | No (0) | Número entero. Ordena los paquetes de una misma capa: gana el más alto |
| `name`, `description` | No | Texto para las personas |
| `dependencies` | No | Lista de `id` y `min_version` |
| `schemas` | No | Qué esquema sigue cada documento YAML del paquete (ver abajo) |
| `api` | No | Versión de la API de Lua que piden sus scripts: `1.x` (cualquier 1.y) o `1.3` (la 1.3 o posterior) ([script](../modulos/engine/script.md)). Prevista como obligatoria para `mods` y `user`; hoy sigue siendo opcional |

Una clave desconocida es un error, para que una errata no pase desapercibida. El formato también está como JSON
Schema en [`content/schemas/pack.schema.json`](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/pack.schema.json), para editores y herramientas.

### Esquemas de los documentos

`schemas` dice qué JSON Schema de `content/schemas/` sigue cada documento YAML. Cada clave es el nombre de un esquema
(`map` es `content/schemas/map.schema.json`) y su valor, la lista de **patrones de ID** de los documentos que lo
siguen. En un patrón, `*` es cualquier texto dentro de un segmento y `**`, cualquier texto aunque cruce `/`:

```yaml
schemas:
  digimon: [data/digimon]
  map: [map/*/data]
  image: ["map/*/texture*/*_meta", "texture/**_meta"]
```

Las reglas de todos los paquetes montados valen para todos: un mod que sobrescribe `data/digimon` no necesita
repetirla, y su documento se comprueba con el mismo esquema que el de la base. Se declara el esquema en el manifiesto
y no se adivina por la ruta, para que un tipo de documento nuevo nunca pase sin comprobar:
[`dwx validate`](assets-pipeline.md#dwx-validate) da error si un documento no encaja en ninguna regla o encaja en
reglas de dos esquemas distintos. `dwx extract` escribe en el `pack.yaml` de la base una regla para cada tipo de
documento que genera (`names::documentSchemas` en `apps/dwx/src/asset_names.cpp`).

## IDs de asset

- Son segmentos de `a-z`, `0-9` y `_` separados por `/`, de 200 caracteres como mucho: `map/file_city_01/background`.
- Sin segmentos vacíos, sin mayúsculas y sin puntos, así que un ID nunca sale de la carpeta del paquete ni depende de
  si el disco distingue mayúsculas.
- **El ID de un fichero es su ruta dentro del paquete sin la extensión.** `map/file_city_01/background.png` es
  `map/file_city_01/background`. El nombre del fichero lleva exactamente un punto y una extensión en minúsculas
  (`model.lod1.gltf` no vale).
- Dos ficheros de un mismo paquete con el mismo ID (`model.gltf` y `model.glb`) son un error.
- Se ignoran `pack.yaml` y los ficheros y carpetas que empiezan por punto (`.git`, `.gitkeep`). Cualquier otro
  fichero que no dé un ID válido es un error.
- En disco, los enlaces simbólicos son un error: un paquete no puede leer fuera de su carpeta.

## La pila

Al montar un conjunto de paquetes, el motor los apila de abajo arriba:

0. Los paquetes que **rellenan huecos** (`PackLocation::fillsGaps`, los de los assets de desarrollo) van debajo de
   todos los demás; entre ellos valen las reglas siguientes.
1. Por **capa**: base (1), remaster (2), mods (3), usuario (4).
2. Dentro de una capa, por **prioridad**, de menor a mayor.
3. Si empatan en capa y prioridad, cada paquete va encima de los paquetes de ese empate de los que depende, y si no,
   por **id** en orden alfabético.

El orden en que se pasan las carpetas no importa: los mismos paquetes dan siempre la misma pila.

**Sobrescribir y añadir:** si varios paquetes tienen el mismo ID, gana el que está más arriba en la pila, aunque las
extensiones sean distintas (un `background.ktx2` del remaster sustituye al `background.png` de la base). Un ID que
solo tiene un paquete simplemente se añade.

## Tablas por entradas

Las tablas de contenido no se sustituyen enteras: se **componen entrada a entrada**, para que un mod añada un Digimon
o un mapa sin copiar la tabla (`readComposedTable`, `packages/engine/assets/include/dwe/assets/table_composition.hpp`). Hay seis
clases de tabla: Digimon, raise, mapas, objetos, técnicas y evolución (`composedTableKinds`,
`packages/engine/assets/include/dwe/assets/table_kinds.hpp`). De cada clase se componen su tabla de siempre (`data/digimon`,
`data/raise`, `data/maps`, `data/items`, `data/moves` y `data/evolution`) y cualquier otra que un ruleset nombre con
el rol de la clase en sus `tables` (`raise: data/hard_raise`). El juego y `dwx validate` componen las mismas
(`composedTablesOf`). El resto de assets sigue la regla de la pila: gana el de más arriba.

### IDs de contenido

Cada entrada se nombra por su id de contenido (`ContentId`, `packages/engine/assets/include/dwe/assets/content_id.hpp`):

- **Originales:** su nombre estable de siempre, sin espacio de nombres (`species_a`, `twna01`). Es el que ya escribe
  `dwx extract` y leen las herramientas, así que no cambia.
- **Lo que añade un paquete:** `<id del paquete>:<nombre>`, como `example_mod:meadow`. Cada parte es `[a-z0-9_]`; el
  nombre tiene 64 caracteres como mucho.
- **Sus assets** van en una carpeta por parte: el mapa `example_mod:meadow` está en `map/example_mod/meadow/` y el
  modelo del Digimon `example_mod:pebblemon` en `digimon/example_mod/pebblemon/model`. Así los
  [IDs de asset](#ids-de-asset) siguen siendo `[a-z0-9_/]`.
- **El número (`index`) es interno:** el del original, que usan sus datos (los warps, los guardados), o el que pone el
  motor a una entrada nueva. Un paquete se refiere a las entradas por su id.

### Cómo se componen

Los ficheros de la tabla se leen de abajo arriba en la pila. Cada tabla dice qué secciones son **listas de entradas**
y con qué clave se nombra cada entrada (`id`, o `digimon` en `data/raise`):

1. Una entrada con un id que ya está **sustituye** a esa entrada en su sitio y se queda con su `index`. Si da un
   `index` distinto, es un error, y fuera de la base tampoco puede dar `index` a una entrada que añadió otro paquete:
   ese número lo pone el motor y depende de los mods montados. «La base» son los paquetes del motor
   (`PackOrigin::Engine`) de la capa base: un paquete que solo declara `layer: base` no tiene sus derechos.
2. Una entrada nueva se **añade al final**, en el orden de la pila y, dentro de un paquete, en el de su fichero, con
   el siguiente `index` libre (el mayor más uno). Solo la base da el `index` de sus entradas, y dos entradas de la
   base no pueden tener el mismo; fuera de la base se deja fuera.
3. Fuera de la capa base, una entrada nueva se llama `<id del paquete>:<nombre>`: un nombre sin espacio de nombres o
   con el de otro paquete es un error.
4. Las secciones que van por el id de otra tabla (las filas de `data/raise` y las de `data/evolution`, por Digimon u
   objeto) aceptan filas de cualquier id y **no numeran** las nuevas: el `index` de una fila de la base es el sitio de
   la entrada a la que pertenece en el original, se conserva cuando un paquete la sustituye y una fila nueva no tiene.
5. Una lista de entradas solo se puede dar como lista: `maps: null` o `maps: 1` es un error, no una forma de vaciarla.
6. Cualquier otra sección (las puertas y los baños de `data/maps`, por ejemplo) se **sustituye entera** por la del
   paquete de más arriba que la tiene, como las secciones de un ruleset con `extends`. Una sección nueva se añade.
7. La composición se hace siempre, aunque solo un paquete tenga la tabla: así se comprueban sus ids y se numeran sus
   entradas. Si no cambia nada, se lee el fichero tal cual, para que los errores de los lectores digan sus líneas.

El resultado es el mismo YAML que leería el juego de un solo fichero, así que los lectores estrictos de cada tabla no
cambian. **Un paquete que trae la tabla entera** (una copia de la de la base, editada) da la misma tabla que antes:
cada entrada sustituye a la suya con el mismo `index`. Un mod **no puede quitar** entradas: una entrada que falta en
su fichero sigue estando.

### Un Digimon nuevo trae su contrato

Un Digimon que añade un paquete (un id con espacio de nombres) tiene que traer el contrato de su modelo,
`digimon/<paquete>/<nombre>/contract.yaml` ([esquema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/model_contract.schema.json)): es contra lo
que se comprueba su modelo, porque no hay un original con el que compararlo. Sin él, el juego no carga la tabla
(`loadDigimonTable`) y `dwx validate` da un problema (`requireNewSpeciesContracts`).

### Los guardados, los rulesets y el `index`

El `index` de una entrada nueva depende de qué mods hay montados y en qué orden, así que nada que sale de la sesión lo
usa: los guardados (la especie del compañero, el mapa, las técnicas aprendidas, las especies criadas y, desde la
versión 2 de su sección, los objetos de la bolsa), los rulesets, los scripts, las claves de texto y los registros
nombran cada entrada por su id. Dentro de la sesión, el motor usa asas de la composición
([IDs en la sesión](../modulos/engine/assets.md#ids-en-la-sesión)). Los rangos de números de un ruleset solo cubren
las entradas del original ([rangos e ids](rulesets.md#rangos-e-ids)), y un warp llega a un mapa de un paquete por su
id ([destino de una salida](../formats/mapas.md#destino-de-una-salida)).

Quitar un mod no deja la partida sin cargar: un montón de la bolsa cuyo objeto ya no está en ninguna tabla se tira,
con un aviso en el registro que nombra su id y la cantidad ([inventario](../modulos/game/inventory.md)).

### Nombres de lo añadido

Lo que se ve de una entrada nueva sale de la tabla de textos del paquete, `text/<idioma>/<id del paquete>.yaml`, con
la clave `<bloque>.<nombre>`: `digimon_names.pebblemon`, `map_names.meadow` (`addedEntryTextKey`,
`packages/engine/localization/include/dwe/localization/catalog.hpp`). Los originales siguen con sus claves numeradas.

Un ejemplo completo está en [`tests/mods/example_mod`](../modulos/tests/mods.md).

## Dependencias

Cada dependencia tiene que:

- estar montada;
- tener al menos la versión `min_version`;
- estar en la misma capa o en una más baja (la base no puede necesitar un mod);
- no formar un ciclo.

Las dependencias no cambian la capa ni la prioridad: si un mod depende de otro con más prioridad, el otro sigue
encima.

## Qué se puede preguntar

| Función | Qué devuelve |
|---|---|
| `resolve(id)` | El paquete que gana y la ruta de su fichero, o nada |
| `read(id)` | Los bytes del fichero que gana |
| `ids()` | Todos los IDs, ordenados |
| `sources(id)` | Todos los paquetes que tienen ese ID, de abajo arriba |
| `overriddenIds()` | Los IDs que tiene más de un paquete, para depurar qué sobrescribe a qué |

Un paquete también puede ser un [archivo `.pak`](../formats/pak.md), que se abre con `PakFileSystem`. Cada paquete
se monta con su propio sistema de ficheros (`PackLocation`: el sistema de ficheros y la raíz del paquete en él), así
que una pila puede mezclar carpetas y archivos: `mount(sistemaDeFicheros, carpetas)` es el caso de todos en el mismo.
`listPackFiles` lista los ficheros de un paquete con su ID y comprueba las mismas reglas que el montaje.

Para saber qué montar, `findPacks(carpeta)` devuelve la propia carpeta si tiene `pack.yaml` (y nada más). Si no,
devuelve cada subcarpeta inmediata que lo tenga y cada fichero `.pak` inmediato, ordenados por ruta. Así se puede
montar `content/base`, todo `content/` o un `content/` que mezcle carpetas y archivos. Las reglas de precedencia:

- Una carpeta gana al `.pak` con su nombre: con `base/` y `base.pak` se monta `base/`, y `base.pak` sale en
  `hidden`. La carpeta es lo que se edita y el archivo se construye a partir de ella (`dwx pack`), así que la carpeta
  es la versión más reciente.
- Solo cuenta el nombre del fichero, no el `id` de su `pack.yaml`: dos paquetes con nombres distintos y el mismo `id`
  se devuelven los dos y el montaje falla con «mounted twice», que dice las dos rutas.
- Un `.pak` dentro de una subcarpeta no cuenta (igual que un `pack.yaml` dos niveles más abajo), y un `.pak` dentro
  de una carpeta que ya es un paquete es un asset de ese paquete.
- El orden de la pila no lo decide `findPacks`: lo deciden la capa y la prioridad de cada manifiesto al montar.

`openPacks(disco, encontrados)` abre cada archivo con `PakFileSystem` y devuelve las `PackLocation` para `mount`
junto con los archivos abiertos, que deben vivir tanto como el sistema de ficheros virtual. El visor de assets del
overlay de depuración lo usa así y dice en su estado qué archivos quedaron ocultos.

## Assets de desarrollo

Con `--dev-assets` (solo en desarrollo, nunca por defecto) `GameContent` monta también los paquetes de las carpetas
extraídas que nombra el fichero de configuración de desarrollo, solo para rellenar lo que falta. Un paquete normal gana
al paquete de desarrollo con su id, y una carpeta anterior a una posterior; los que quedan fuera se registran en el
log. Los que entran rellenan huecos (`PackLocation::fillsGaps`): van debajo de todos los paquetes normales, digan lo
que digan su capa y su prioridad, y el log los marca. Una imagen del disco en ese fichero llega como una carpeta
extraída más, la última: `dwx` la extrae a una caché de desarrollo. Detalle en
[assets de desarrollo](assets-de-desarrollo.md).

## Errores

Montar falla, con un mensaje que dice el paquete o el fichero, si:

- falta `pack.yaml` o tiene un error (con línea y columna);
- hay dos paquetes con el mismo `id`;
- falta una dependencia, su versión es menor que `min_version`, está en una capa más alta o hay un ciclo;
- un fichero no da un ID válido, dos ficheros de un paquete dan el mismo ID o hay un enlace simbólico.

`read` falla si ningún paquete tiene el ID o si el fichero no se puede leer.

## YAML

Los manifiestos y el resto del formato fuente se leen con `core::parseYaml` ([ADR 0016](../adr/0016-yaml-con-libyaml.md)).
Solo se acepta un documento por fichero, sin anclas, alias ni etiquetas, con claves escalares y sin repetir. Hay un
límite de 16 MiB y de 64 niveles de anidamiento.
