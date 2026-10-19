# Texturas KTX2 de `dwx cook`

`dwx cook` convierte cada PNG de un paquete en una textura [KTX 2.0](https://registry.khronos.org/KTX/specs/2.0/ktxspec.v2.html)
con el mismo id (`map/start/background.png` → `map/start/background.ktx2`). Es el formato estándar de Khronos para
Vulkan. El contenedor se escribe a mano, sin librerías (`apps/dwx/src/ktx2.cpp`), y esta página dice qué campos
rellena. La decisión está en el [ADR 0017](../adr/0017-cocinado-y-archivos-pak.md).

## Qué escribe

- Una textura 2D, sin array ni cubo, con la **cadena de mipmaps completa** hasta 1×1.
- Texels **RGBA8 sin comprimir** y sin supercompresión: `VK_FORMAT_R8G8B8A8_SRGB` (43) para el color y
  `VK_FORMAT_R8G8B8A8_UNORM` (37) para los datos.
- **Color o datos:** lo dice el YAML lateral de la imagen (`<id>_meta.yaml`, el que escribe `dwx extract` para cada
  TIM) con la clave `color_space`: `srgb` (color) o `linear` (profundidad, máscaras, normales). Sin la clave, o sin
  YAML lateral, es color. Otro valor es un error. El extractor no escribe la clave porque todo lo que extrae es color;
  está para las máscaras y mapas de normales que vengan. El nombre del id ya no cuenta.
- **No todo PNG se convierte.** Un PNG que guarda datos de búsqueda (índices y paleta) se copia tal cual, como PNG,
  porque un KTX2 de colores RGBA perdería los índices y el motor ya no podría cambiar de paleta. Se copia si:
  - el propio PNG es indexado (tipo de color 3 en su cabecera `IHDR`): `dwx extract` escribe indexado todo lo que
    tiene CLUT, como los TIM de 4 y 8 bits, los fondos de mapa (`background_day`, `_dusk`, `_night` y
    `background_paletteN`, que son el mismo fondo dibujado con otra paleta) y el atlas de la fuente; o
  - su YAML lateral dice `png: indexed` o tiene `clut_rect`, es decir, viene de un TIM con CLUT.

  Es provisional, hasta decidir cómo dibuja el motor las paletas. Se mira el contenido y los metadatos, nunca el
  nombre: los fondos de mapa no tienen YAML lateral, pero su PNG es indexado, y eso basta.

Todos los enteros son little-endian.

### Cabecera (80 bytes)

| Offset | Campo | Valor |
|---|---|---|
| 0 | `identifier` | `AB 4B 54 58 20 32 30 BB 0D 0A 1A 0A` (`«KTX 20»\r\n\x1A\n`) |
| 12 | `vkFormat` | 43 o 37 |
| 16 | `typeSize` | 1 (bytes por componente) |
| 20 | `pixelWidth`, `pixelHeight` | Medidas del PNG |
| 28 | `pixelDepth` | 0 (2D) |
| 32 | `layerCount` | 0 (no es un array) |
| 36 | `faceCount` | 1 (no es un cubo) |
| 40 | `levelCount` | `floor(log2(max(ancho, alto))) + 1` |
| 44 | `supercompressionScheme` | 0 (ninguna) |
| 48 | `dfdByteOffset`, `dfdByteLength` | Justo después del índice de niveles; 92 bytes |
| 56 | `kvdByteOffset`, `kvdByteLength` | Justo después del DFD |
| 64 | `sgdByteOffset`, `sgdByteLength` | 0 y 0 (sin datos globales de supercompresión) |

### Índice de niveles

Un registro de 24 bytes por nivel, del 0 (el más grande) al último: `byteOffset`, `byteLength` y
`uncompressedByteLength` (`u64`), los dos tamaños iguales porque no hay supercompresión.

### Descriptor de formato (DFD)

`dfdTotalSize` (92) y un bloque básico de la Khronos Data Format Specification 1.3 con cuatro muestras:

| Campo | Valor |
|---|---|
| `vendorId`, `descriptorType` | 0, 0 (Khronos, básico) |
| `versionNumber`, `descriptorBlockSize` | 2 (KDFS 1.3), 88 |
| `colorModel` | 1 (RGBSDA) |
| `colorPrimaries` | 1 (BT.709) |
| `transferFunction` | 2 (sRGB) o 1 (lineal) |
| `flags` | 0 (alfa sin premultiplicar) |
| `texelBlockDimension0..3` | 0 (bloques de 1×1×1×1) |
| `bytesPlane0..7` | 4, y el resto 0 |
| Muestras | R, G, B y A: bits 0, 8, 16 y 24, 8 bits cada una, `sampleLower` 0, `sampleUpper` 255 |

En el formato sRGB la muestra de alfa lleva el flag `LINEAR` (canal `0x1F`): el alfa nunca va codificado en sRGB.

### Pares clave/valor

Ordenados por clave, cada uno con su longitud (`u32`), la clave y el valor terminados en NUL y relleno hasta 4 bytes:

- `KTXorientation` = `rd`: las filas van de arriba abajo, como en el PNG.
- `KTXwriter` = `dwx`.

### Datos

Empiezan alineados a 4 bytes y van **del nivel más pequeño al más grande**, como pide la especificación. Cada nivel
son sus filas de arriba abajo, 4 bytes por texel, sin relleno.

## Cómo se calculan los mipmaps

- Cada nivel mide la mitad del anterior, redondeando hacia abajo y nunca menos de 1.
- Cada texel es la media del área que cubre en el nivel anterior (un filtro de caja), con el peso de la parte de cada
  texel que cae dentro. Así un lado impar no pierde la última columna. Los pesos se calculan con enteros, así que
  son exactos.
- El color se promedia en **luz lineal** (se decodifica el sRGB, se promedia y se vuelve a codificar) y **ponderado
  por el alfa**: los texels transparentes, que en las texturas del original son negro con alfa 0, no oscurecen a sus
  vecinos. El alfa se promedia tal cual.
- Cada nivel sale del anterior, en coma flotante, y se redondea a 8 bits al final. El nivel 0 es el PNG sin tocar.

La misma imagen da siempre los mismos bytes con el mismo compilador y la misma librería estándar; entre plataformas,
`pow` podría diferir en el último bit y cambiar algún texel de los mipmaps en una unidad.
