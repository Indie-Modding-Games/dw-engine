# Archivo `.pak`

Un `.pak` guarda un paquete entero (su `pack.yaml` y todos sus ficheros) en un solo fichero, para distribuirlo y
cargarlo sin miles de ficheros sueltos. Es un formato propio del motor, no del original. Lo escribe `dwx pack` y lo
lee `packages/engine/assets` (`dwe::assets::PakFileSystem`). La decisión está en el
[ADR 0017](../adr/0017-cocinado-y-archivos-pak.md).

Todos los enteros son little-endian. Los offsets se cuentan desde el principio del fichero.

## Estructura

```text
cabecera (64 bytes)
datos de cada fichero, en orden de id, cada uno alineado a 16 bytes
tabla de contenidos (64 bytes por entrada), alineada a 16 bytes
nombres
manifiesto (pack.yaml, tal cual)
```

El índice (tabla, nombres y manifiesto) va al final para que `dwx pack` escriba el archivo de una pasada, leyendo cada
fichero una sola vez. Quien lo abre lee la cabecera y después el índice de un tirón; los datos se leen cuando se piden.

### Cabecera

| Offset | Tipo | Campo |
|---|---|---|
| 0x00 | `u8[8]` | Magia `89 50 41 4B 0D 0A 1A 0A` (`\x89PAK\r\n\x1A\n`) |
| 0x08 | `u32` | Versión del formato: 2 (el lector acepta también 1) |
| 0x0C | `u32` | Flags: 0 (no hay ninguno definido) |
| 0x10 | `u32` | Número de entradas |
| 0x14 | `u32` | Alineación de los datos y de la tabla: 16 |
| 0x18 | `u64` | Offset de la tabla de contenidos |
| 0x20 | `u64` | Offset de los nombres |
| 0x28 | `u64` | Tamaño de los nombres |
| 0x30 | `u64` | Offset del manifiesto |
| 0x38 | `u64` | Tamaño del manifiesto |

La magia sigue la idea de PNG: el byte `0x89` y los dos finales de línea hacen que una transferencia en modo texto la
rompa, y así se detecta.

### Entrada de la tabla de contenidos

| Offset | Tipo | Campo |
|---|---|---|
| 0x00 | `u64` | Offset de los bytes guardados |
| 0x08 | `u64` | Tamaño guardado |
| 0x10 | `u64` | Tamaño del fichero sin comprimir |
| 0x18 | `u32` | Offset del nombre dentro de los nombres |
| 0x1C | `u32` | Longitud del nombre |
| 0x20 | `u32` | Flags: bits 0-7 compresión (0 = sin comprimir, 1 = zstd), el resto a 0 |
| 0x24 | `u8[20]` | SHA-1 del fichero sin comprimir |
| 0x38 | `u64` | Reservado: 0 |

- El **nombre** es la ruta del fichero dentro del paquete, con `/` entre carpetas y sin NUL: `map/start/background.png`.
  El id del asset sale de él como en una carpeta (la ruta sin la extensión, ver [VFS](../architecture/vfs.md)).
- Las entradas están **ordenadas por id de asset**, que es también el orden de sus rutas porque `.` va antes que
  cualquier carácter de un id. Así se busca una entrada con una búsqueda binaria.
- **Compresión** (versión 2): con 1, los bytes guardados son **un único frame de zstd** que se descomprime en
  exactamente el tamaño del fichero, sin nada detrás; el frame lleva ese tamaño en su cabecera. Con 0, los bytes se
  guardan tal cual y los dos tamaños son iguales. `dwx pack` comprime cada fichero y lo guarda tal cual cuando
  comprimido no ocupa menos (los PNG, que ya van comprimidos, suelen quedarse así).
- Un fichero comprimido no puede pasar de 2 GiB sin comprimir (`PakMaxEntrySize`).
- El **SHA-1** sirve para comprobar cada fichero al leerlo y como clave de contenido; no es una medida de seguridad.

## Determinismo

Los mismos ficheros dan el mismo `.pak`, byte a byte: las entradas van en orden de id, el relleno es de ceros y no se
guardan fechas, permisos ni nada de la máquina que lo escribe. El manifiesto se copia sin cambios.

La compresión también es fija: zstd 1.5.7 (la versión fijada por el baseline de vcpkg), nivel 9 (`PakZstdLevel`),
el tamaño del contenido en la cabecera del frame, sin checksum de zstd (el SHA-1 de la entrada ya lo cubre), sin id de
diccionario y sin los hilos de zstd. `dwx pack` comprime varios ficheros a la vez, cada uno con su propio contexto,
y los escribe en orden, así que el resultado no depende del número de hilos. Otra versión de zstd podría comprimir
distinto; el lector lee igual cualquier frame válido.

El nivel 9 sale de medir el paquete base de la versión US (4619 ficheros, 351 MB) en release:

| Nivel | `.pak` | Tiempo de `dwx pack` (con la relectura) |
|---|---|---|
| 3 | 242,1 MB | 2,5 s |
| 9 | 240,7 MB | 2,9 s |
| 19 | 238,8 MB | 19 a 27 s |

Del 9 al 19 se gana un 0,8 % a cambio de unas siete veces más tiempo, y `dwx pack` corre en la máquina de cada
usuario la primera vez que abre el juego.

## Versiones

- **1:** todos los ficheros sin comprimir.
- **2:** cada fichero con zstd o sin comprimir. Un archivo de versión 2 sin ningún fichero comprimido solo se
  diferencia de uno de versión 1 en el número de versión.

El lector acepta las dos: un archivo de versión 1 es un caso particular de la 2 (todo sin comprimir), así que leerlo
no cuesta nada. Una entrada comprimida en un archivo de versión 1 se rechaza.

## Qué comprueba el lector

`PakFileSystem::open` rechaza el archivo, con un mensaje que dice qué falla, si:

- es más corto que la cabecera, la magia no coincide o la versión no es 1 ni 2;
- los flags de la cabecera no son 0 o la alineación no es una potencia de dos de al menos 16;
- la tabla, los nombres y el manifiesto no van seguidos y sin huecos hasta el final del fichero (así el tamaño del
  fichero acota todo lo que se reserva);
- una entrada usa una compresión que su versión no tiene u otros flags, sus tamaños no coinciden sin compresión, su
  tamaño comprimido pasa de 2 GiB, sus datos no están alineados o salen de la zona de datos, o su nombre sale de los
  nombres;
- un nombre no da un id válido, es `pack.yaml`, o las entradas no están en orden estricto de id (lo que también
  rechaza ids repetidos).

Al leer una entrada comprimida se comprueba que sea un solo frame de zstd de su tamaño y se descomprime en un búfer
que empieza en 1 MiB y dobla a medida que el frame produce bytes, así que un tamaño falsificado no hace reservar la
memoria que dice: falla al acabarse el frame. zstd limita además su ventana a 128 MiB (su límite por defecto). Después,
con o sin compresión, se comprueba su SHA-1. Si algo falla, la lectura falla y dice que el archivo está dañado.

## Cómo se monta

`PakFileSystem` es un `FileSystem` con una sola carpeta, la ruta del propio archivo: al listarla da `pack.yaml` y cada
entrada, y `<archivo>/<entrada>` lee esa entrada. Cada paquete que se monta lleva su propio sistema de ficheros
(`PackLocation`), así que en una misma pila puede haber carpetas y archivos:

```cpp
const assets::DiskFileSystem disk;
auto base = assets::PakFileSystem::open("content/base.pak");
const std::array packs{assets::PackLocation{.fileSystem = &disk, .root = "content/packs/hd"},
                       assets::PackLocation{.fileSystem = base->get(), .root = "content/base.pak"}};
auto vfs = assets::VirtualFileSystem::mount(packs);
```

Las rutas de los assets que vienen de un archivo se ven como `content/base.pak/map/start/background.png`.
