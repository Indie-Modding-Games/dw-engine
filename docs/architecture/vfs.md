# Paquetes y sistema de ficheros virtual

Cómo encuentra el motor cada asset entre los paquetes montados. Es la parte de "capas de contenido" de la
[tubería de assets](assets-pipeline.md), y el código está en `engine/assets`.

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
| `layer` | Sí | `base`, `remaster`, `mods` o `user` |
| `priority` | No (0) | Número entero. Ordena los paquetes de una misma capa: gana el más alto |
| `name`, `description` | No | Texto para las personas |
| `dependencies` | No | Lista de `id` y `min_version` |
| `schemas` | No | Qué esquema sigue cada documento YAML del paquete (ver abajo) |

Una clave desconocida es un error, para que una errata no pase desapercibida. El formato también está como JSON
Schema en [`content/schemas/pack.schema.json`](../../content/schemas/pack.schema.json), para editores y herramientas.

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
y no se adivina por la ruta, para que un tipo de documento nuevo nunca pase sin comprobar: `dwx validate` da error si
un documento no encaja en ninguna regla o encaja en reglas de dos esquemas distintos.

## IDs de asset

- Son segmentos de `a-z`, `0-9` y `_` separados por `/`, de 200 caracteres como mucho: `digimon/agumon/model`.
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

1. Por **capa**: base (1), remaster (2), mods (3), usuario (4).
2. Dentro de una capa, por **prioridad**, de menor a mayor.
3. Si empatan en capa y prioridad, cada paquete va encima de los paquetes de ese empate de los que depende, y si no,
   por **id** en orden alfabético.

El orden en que se pasan las carpetas no importa: los mismos paquetes dan siempre la misma pila.

**Sobrescribir y añadir:** si varios paquetes tienen el mismo ID, gana el que está más arriba en la pila, aunque las
extensiones sean distintas (un `background.ktx2` del remaster sustituye al `background.png` de la base). Un ID que
solo tiene un paquete simplemente se añade.

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

Para saber qué montar, `findPacks(carpeta)` devuelve la propia carpeta si tiene `pack.yaml`, y si no, cada subcarpeta
inmediata que lo tenga. Así se puede montar `content/base` o todo `content/`.

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
