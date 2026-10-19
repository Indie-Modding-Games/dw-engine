# `dwx extract`: el paquete base y el informe de cobertura

`dwx extract` lee el disco del usuario y escribe el **paquete base**: los assets en formato fuente que monta el
motor ([tubería de assets](../architecture/assets-pipeline.md)). Junto a ellos deja un informe de cobertura con lo que
se extrajo de cada fichero del disco y lo que falta. El código está en `apps/dwx`; los lectores, en `formats`.

## Uso

```sh
build/<preset>/apps/dwx/dwx extract [--out <carpeta>] [--force] <imagen.cue|imagen.bin|imagen.iso>
```

- `--out`: dónde se escribe el paquete. Por defecto `content/base`, que git ignora.
- La carpeta de salida tiene que estar vacía o haberla escrito `dwx extract` antes (lleva el fichero oculto
  `.dwx-extract`). En ese caso se vacía primero, para que no queden ficheros de una extracción anterior.
- Una carpeta con otros ficheros se rechaza. Con `--force` se escribe encima **sin borrar nada**; si lo que ya había
  rompe el paquete, la comprobación del final lo dice.
- Nada se escribe fuera de `--out`: las rutas de salida salen solo de ids de asset ya validados (minúsculas, cifras y
  `_`), nunca de nombres leídos del disco.
- Códigos de salida: 0 si el paquete se escribió y el motor lo monta, 2 si hubo un error.

Con la imagen fuera del repositorio, desde el contenedor:

```sh
DWE_DISC="/ruta/al/disco.bin" scripts/dev.sh test -R dwx   # incluye la prueba con el disco real
```

## Qué hace

1. Abre la imagen y el volumen ISO9660, e identifica la versión por el SHA-1 del ejecutable (como `--list`).
2. Calcula el SHA-1 de cada fichero del volumen y se lo da al primer **extractor** del registro que lo acepta.
3. Prepara la carpeta de salida y escribe el paquete: `pack.yaml`, las salidas, `coverage.yaml` y `.dwx-extract`.
4. Monta el paquete con el `VirtualFileSystem` del motor (`engine/assets`, sobre `DiskFileSystem`) y comprueba que
   cada salida se resuelve a su fichero. Es la primera prueba de que el motor carga lo extraído.
5. Imprime el resumen: ficheros totales, extraídos, parciales y pendientes, bytes cubiertos y número de salidas.

La salida es **determinista**: dos extracciones del mismo disco dan los mismos bytes en todos los ficheros. No hay
fechas ni rutas absolutas en lo que se escribe.

## Extractores

Un extractor es una entrada del registro (`apps/dwx/src/extractors.cpp`) con un nombre y dos funciones: `handles`,
que dice si acepta un fichero por su ruta en el volumen, y `extract`, que lee el fichero del disco y devuelve sus
salidas (id de asset, extensión y bytes) y lo que no entendió. Gana el primero que acepta el fichero, así que los
específicos van antes que los patrones. Añadir un tipo de fichero (modelos, mapas, tablas, textos, audio, vídeo,
scripts) es una entrada más y sus dos funciones; el extractor recibe el volumen entero y la versión, por si necesita
otros ficheros o el ejecutable.

| Extractor | Ficheros | Salida |
|---|---|---|
| `tim_pack` | Los packs de paso fijo conocidos: `CHDAT/ALLTIM.TIM` y `CARD/CARD.ALL` | Un PNG y un YAML por entrada |
| `stacked_tim` | Los TIM apilados: `ETCDAT/ETCTIM.BIN` y `STDDAT/STDTIM.BIN` | Un PNG y un YAML por TIM |
| `tim_file` | Cualquier otro `.TIM`: un TIM, o varios seguidos | Un PNG y un YAML por TIM |

Los formatos están en [TIM y packs](tim.md).

## El paquete base

```text
content/base/
├─ pack.yaml            id base, capa base, versión 0.1.0
├─ coverage.yaml        informe de cobertura (también es el asset `coverage`)
├─ .dwx-extract         marca de que lo escribió dwx; oculto, el motor no lo ve
├─ digimon/<tipo>/texture.png + texture_meta.yaml
├─ card/<id>.png + <id>_meta.yaml
└─ texture/<carpetas>/<nombre>.png + <nombre>_meta.yaml
```

### Ids de asset

Todos los ids salen de `apps/dwx/src/asset_names.cpp`, para que el esquema se cambie en un solo sitio. Cada fichero
cumple las reglas de `engine/assets`: segmentos de `[a-z0-9_]` y un solo punto en el nombre.

| Origen | Id | Ejemplo |
|---|---|---|
| Entrada de `ALLTIM.TIM` (tipo de Digimon) | `digimon/<tipo con 3 cifras>/texture` | `digimon/003/texture` |
| Entrada de `CARD.ALL` | `card/<id con 3 cifras>` | `card/065` |
| Fichero con un TIM | `texture/<carpetas>/<nombre>` | `STDDAT/GRADE/AN.TIM` → `texture/stddat/grade/an` |
| Fichero con varios TIM | `texture/<carpetas>/<nombre>/<n con 3 cifras>` | el tercero de `STDDAT/T_TOGI.TIM` → `texture/stddat/t_togi/002` |
| Metadatos de una imagen | `<id de la imagen>_meta` | `card/065_meta` |

Los nombres del disco se pasan a minúsculas y cualquier carácter que no sea letra, cifra o `_` se cambia por `_`. Los
números de Digimon y cartas son provisionales: cuando se extraigan las tablas del ejecutable pasarán a ser nombres
(`digimon/agumon/texture`). Dos salidas con el mismo id, o con un id reservado (`pack`, `coverage`), son un error de la
herramienta.

### Imágenes

- **PNG:** indexado de 8 bits con la paleta 0 (`PLTE` y `tRNS`) si el TIM es de 4 u 8 bits con CLUT; RGBA si es de 16
  o 24 bits. Así se conservan los índices y la paleta (ver [TIM y packs](tim.md#png)).
- **`<id>_meta.yaml`:** `source` (fichero del disco), `entry` (número en un pack o en una lista de varios), `offset`,
  `mode` (`indexed4`, `indexed8`, `direct16`, `direct24`), `width` y `height` en píxeles, `image_rect` y `clut_rect`
  (dónde lo sube el original a la VRAM, en unidades de 16 bits), `png` (`indexed` o `rgba`), `png_palette` y
  `palettes`: cada paleta como una cadena de colores de 4 cifras hex separados por espacios, en bruto (bit STP
  incluido). Si algo no se pudo escribir, va en `warnings`.
- **Texturas de Digimon:** el PNG usa la paleta 0, pero cada primitiva del modelo elige su propia fila de la CLUT; el
  extractor de modelos tendrá que usar las paletas del YAML para cada parte.

## Informe de cobertura

`coverage.yaml` tiene tres partes:

- `disc`: volumen, ruta y SHA-1 del ejecutable, y versión (vacía si no se conoce).
- `summary`: `files`, `extracted`, `partial`, `pending`, `bytes`, `bytes_covered` (los de ficheros extraídos o
  parciales) y `outputs`.
- `files`: por cada fichero del volumen, en orden de ruta, `path`, `size`, `sha1`, `status`, y si un extractor lo
  aceptó, `extractor`, `outputs` (rutas dentro del paquete) y `unknowns`.

Los estados:

| Estado | Significado |
|---|---|
| `extracted` | Un extractor lo aceptó y lo entendió entero |
| `partial` | Un extractor lo aceptó pero dejó algo en `unknowns`: entradas que no se leen, bytes que no son cero y nadie explica, tamaños que no cuadran, o el error si falló |
| `pending` | Ningún extractor lo acepta todavía |

La fase 1 se cierra cuando no quede ningún fichero pendiente ni parcial.

## Estado con el disco US

Con el disco US (`SLUS_010.32`): 818 ficheros, 55 extraídos (52 `.TIM`, los dos TIM apilados y `CARD.ALL`), ninguno
parcial y 763 pendientes, unos 6,9 MB de 331 MB. Salen 315 PNG (180 Digimon, 66 cartas, 15 de los TIM apilados y 54
de los `.TIM`) con sus 315 YAML. Ninguna imagen tiene índices fuera de su paleta ni diferencias de tamaño.

- `STDDAT/T_TOGI.TIM` tiene cuatro TIM seguidos: el original lee las texturas de las arenas con un número de TIM
  fijo (`std_setup.c: STD_loadArenaTIMToVRAM`). Por eso `tim_file` lee siempre una lista.
- Los nueve TIM de `ETCDAT/ETCTIM.BIN` también están como `.TIM` sueltos en `ETCDAT` (dan los mismos PNG), así que
  salen dos veces. Los de `STDDAT/STDTIM.BIN` no tienen copia suelta.
