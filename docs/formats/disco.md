# Disco

Cómo lee `formats/disc` la copia del usuario: la imagen, sus sectores, el sistema de ficheros ISO9660 y la tabla de
ficheros del ejecutable. Todo se puede comprobar con `dwx extract --list`.

## Lectura de bytes

Todos los lectores usan `ByteReader` (`formats/include/dwe/formats/byte_reader.hpp`): lee campos little-endian en un
offset y devuelve `std::nullopt` si el campo no cabe en el buffer. Nunca se hace `reinterpret_cast` de structs y nunca
se lee fuera de rango: un fichero malformado da un error con el motivo.

## Imagen

`DiscImage::open` acepta:

| Entrada | Cómo se lee |
|---|---|
| `.cue` | Se interpreta la hoja y se abre el BIN que nombra, relativo a la carpeta del `.cue` |
| `.bin` sin hoja | Sectores de 2352 bytes si el primer sector empieza por el patrón de sincronización |
| `.iso` (u otro) | Sectores de 2048 bytes si el tamaño es múltiplo de 2048 |

La imagen se lee **sector a sector** desde el fichero: una imagen de 700 MB nunca se carga entera. Si el tamaño no es
un número entero de sectores, es un error.

### Hoja CUE

Solo se soporta lo que usa un disco de datos de una pista:

- Un único `FILE "…" BINARY`.
- Una única `TRACK 01` en modo `MODE2/2352`, `MODE1/2352` o `MODE1/2048`.
- `INDEX 01 00:00:00`: la pista empieza al principio del fichero.
- Se ignoran `REM`, `CATALOG`, `CDTEXTFILE`, `TITLE`, `PERFORMER`, `SONGWRITER`, `FLAGS` e `ISRC`.

Varias pistas o ficheros, pistas de audio, `PREGAP`/`POSTGAP` o un `INDEX` distinto dan un error que dice la línea y
el motivo. Acepta finales de línea CRLF, el BOM de UTF-8 y nombres entre comillas con espacios y paréntesis.

## Sectores

Un sector en bruto mide 2352 bytes:

| Offset | Tamaño | Campo |
|---|---|---|
| 0 | 12 | Patrón de sincronización `00 FF×10 00` |
| 12 | 3 | Dirección (minuto, segundo, sector en BCD); no se comprueba |
| 15 | 1 | Modo: 1 o 2 |
| 16 | 8 | Solo modo 2: subcabecera (dos copias de fichero, canal, submodo, codificación) |
| 24 | 2048 o 2324 | Solo modo 2: datos de usuario |

- **Modo 1:** 2048 bytes de datos en el offset 16.
- **Modo 2 forma 1:** 2048 bytes en el offset 24. Es lo que usan los ficheros normales.
- **Modo 2 forma 2:** 2324 bytes en el offset 24, sin corrección de errores. El bit `0x20` del submodo marca la forma 2.
  La usan el vídeo y el audio XA intercalado.

Un sector sin patrón de sincronización o con otro modo es un error. En una ISO de 2048 bytes todos los sectores son de
forma 1.

## ISO9660

`readIsoVolume` lee el volumen con la norma ECMA-119:

- El **descriptor primario** está en el sector 16: tipo 1, `CD001`, versión 1. Se leen el nombre del volumen
  (offset 40), el número de sectores (80), el tamaño de bloque (128, tiene que ser 2048) y el registro de la raíz (156).
- Los **registros de directorio**: longitud (0), LBA (2), tamaño (10), flags (25; `0x02` = carpeta) y nombre (32 y
  33). Los números van en las dos endianidades; se lee la mitad little-endian. Un byte de longitud 0 indica el relleno
  hasta el final del sector: los registros no cruzan sectores.
- Del nombre se quitan la versión (`;1`) y el punto final de los nombres sin extensión. Las rutas se dan con `/`:
  `MAP/MAP1/NOMBRE.MAP`, ordenadas por ruta.

Se rechaza, con un error:

- Registros que no caben en su sector, más cortos que 33 bytes o con el nombre fuera del registro.
- Atributos extendidos, ficheros multiextensión o intercalados (ningún disco del juego los usa).
- Nombres vacíos, `.`, `..` o con `/`, `\` o caracteres de control: así una extracción nunca escribe fuera de su
  carpeta.
- Carpetas o ficheros que se salen de la imagen.
- **Bucles:** una carpeta que apunta a otra ya visitada. Más de 32 niveles de carpetas o más de 65536 ficheros.
- Dos ficheros con la misma ruta.

### Contenido y SHA-1 de un fichero

- `readIsoFile` lee un fichero de sectores de forma 1, cortado a su tamaño.
- `hashIsoFile` calcula el SHA-1 con los datos de cada sector según su forma: 2048 bytes por sector de forma 1 (el
  último, cortado al tamaño) y 2324 por sector de forma 2. Para un fichero normal es el SHA-1 de sus bytes; los STR,
  que mezclan vídeo (forma 1) y audio XA (forma 2), se hashean enteros, porque el tamaño ISO cuenta 2048 bytes por
  sector.

## Ejecutable

El disco arranca el ejecutable que dice `SYSTEM.CNF` (`BOOT = cdrom:\NOMBRE;1`); `findBootExecutable` lo busca en el
volumen sin distinguir mayúsculas.

`readPsxExeHeader` lee la cabecera PS-X EXE: la marca `PS-X EXE`, `pc0` (0x10), `gp0` (0x14), `t_addr` (0x18, dirección
de carga) y `t_size` (0x1C). El segmento de texto empieza en el offset 0x800 del fichero y tiene que caber en él. Una
dirección de memoria se convierte en offset del fichero con `0x800 + (dirección − t_addr)`, siempre dentro del
segmento. No se supone ninguna dirección de carga: se lee de la cabecera.

## Tabla de ficheros

El original abre casi todos los ficheros por su nombre sin carpeta, buscándolo en la tabla `FILE_OFFSET_TABLE` del
ejecutable (`src/main/file.c: lookupFileTable`, `readFile`; `include/dw/file_table.h: FileEntry`):

| Offset | Tipo | Campo |
|---|---|---|
| 0 | `s32` | `sector`: LBA del primer sector |
| 4 | `s32` | `size`: tamaño en bytes |
| 8 | `char*` | `filename`: puntero a un nombre dentro del ejecutable |

La tabla termina en la primera entrada con `size` 0. `readFileTable` la lee desde la dirección de cada versión y
sigue cada puntero hasta su NUL (64 caracteres como mucho), comprobando que todo está dentro del segmento de texto.

| Versión | Dirección | Entradas (con la final) | Fuente |
|---|---|---|---|
| US | `0x8011FE1C` | 819 | `config/us/symbols.txt`, `src/main/file_table_us.c` |
| EU | `0x80125F44` | 851 | `config/eu/symbols.txt`, `src/main/file_table_eu.c` |
| JP | `0x80125E18` | 816 | `config/jp/symbols.txt`, `src/main/file_table_jp.c` |
| JP rev. 1 | `0x80126730` | 816 | `config/jp_rev1/symbols.txt`, `src/main/file_table_jp_rev1.c` |

Solo la US está comprobada contra un disco.

### Comprobación contra el ISO9660

`checkFileTable` busca cada entrada en el volumen:

1. Por **nombre** (sin carpeta) con el mismo sector y tamaño.
2. Si ningún fichero tiene ese nombre, por **sector y tamaño**: es un alias. En el disco US, `PSX.EXE` es otro
   nombre del ejecutable `SLUS_010.32`.
3. Si nada coincide, la entrada apunta a datos que las carpetas esconden ("solo en la tabla").

Un tamaño también coincide si cuenta **2336 bytes por sector**: el original lee los vídeos STR como sectores de modo 2
enteros (subcabecera, datos y detección de errores), y la tabla guarda su tamaño así. Una entrada con el nombre de un
fichero pero otro sector u otro tamaño es una discrepancia.

### Ficheros que se abren por ruta

Algunos ficheros se abren con `CdSearchFile` y su ruta completa en vez de con la tabla. **En el US también están en
la tabla**:

| Fichero | Dónde |
|---|---|
| `SCN\DG.SCN` | `src/main/script_interp.c: readFileSection` |
| `ETCHI\EFEDAT.EFE` | `src/main/efe.c` |
| `ETCDAT\FEEL_EF.TMD`, `ETCDAT\EX.TIM`, `ETCDAT\EX.TMD` | `src/main/efe_table.c: EFEX_PATH_ARRAY` |
| `MOVIE\OP1.STR`, `OP2.STR`, `ED2.STR`, `EDR.STR` | `src/mov/mov.c: MOV_MOVIES` |

Además, la cola de lectura (`src/main/file_queue.c`) busca por ruta cualquier petición con tamaño negativo.

## Disco US comprobado

Con la imagen US (ejecutable `31ec666b987d5b007dec40eecc4092d27ed9085b`):

- 818 ficheros en el ISO9660 y 818 entradas en la tabla (819 con la final).
- Todos los ficheros del volumen están en la tabla: 817 por nombre y el ejecutable por su alias `PSX.EXE`.
- Ninguna entrada solo en la tabla y ninguna discrepancia. Los cuatro STR coinciden con la regla de 2336 bytes.

## `dwx extract --list`

```bash
build/<preset>/apps/dwx/dwx extract --list [--verify-image] <imagen.cue|imagen.bin|imagen.iso>
```

Abre la imagen, lee el volumen, identifica la versión por el SHA-1 del ejecutable (tabla `known_releases`) y lista
cada fichero **por orden de disco** (LBA y, a igualdad, ruta): LBA, tamaño, SHA-1, si está en la tabla (`yes`, `no`,
`hidden` si solo está en la tabla, `-` si la versión no se conoce) y ruta. Después resume los ficheros fuera de la
tabla, los que se abren por ruta, los alias y las discrepancias. La salida es determinista.

- `--verify-image` calcula además el SHA-1 del fichero de la imagen (lento; todavía no hay hashes de imágenes en la
  tabla, así que solo se muestra).
- Códigos de salida, como `dwx verify`: 0 versión conocida y tabla coherente, 1 versión desconocida o discrepancias,
  2 error.

No escribe nada: la extracción a `content/base/` llega después.

## Tests

- Unitarios (`formats/tests/`): fixtures sintéticos hechos por el propio test (`disc_fixtures.hpp`): sectores en bruto
  con su sincronización y subcabecera, un volumen ISO9660 pequeño con carpetas anidadas, hojas CUE y un ejecutable
  falso con su tabla. Cubren los casos malformados: imágenes truncadas, offsets fuera de la imagen, bucles de carpetas,
  nombres peligrosos, tablas sin final y punteros fuera del ejecutable.
- Con el disco (`real_disc_tests.cpp`): solo si `DWE_DISC` apunta a la imagen
  ([preparar el entorno](../getting-started/setup.md)); si no, se salta. Comprueba la versión, la tabla contra el
  ISO9660 y que el número de ficheros es razonable.
