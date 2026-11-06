# Texturas KTX2 de `dwx cook`

`dwx cook` convierte los PNG de un paquete en texturas [KTX 2.0](https://registry.khronos.org/KTX/specs/2.0/ktxspec.v2.html)
con el mismo id (`ui/frame.png` → `ui/frame.ktx2`): **Basis Universal UASTC con
supercompresión zstd**, que el motor transcodifica al cargarlas a **BC7**, o a RGBA8 si la GPU no muestrea BC7. KTX2
es el contenedor estándar de Khronos para Vulkan y el que admite Basis Universal. El fichero lo escribe y lo lee
libktx (KTX-Software 4.4.2, por vcpkg), que trae el codificador y el transcodificador de Basis Universal. La
decisión está en el [ADR 0017](../adr/0017-cocinado-y-archivos-pak.md).

## Qué PNG se convierten

Un PNG se convierte salvo que el juego lo lea de su PNG o elija su paleta mientras corre: entonces una textura de
colores RGBA perdería los índices o los píxeles exactos y el lector del motor ya no podría usarla.

Primero, por el tipo de asset: los IDs de la lista `assets::PngOnlyImages`
(`packages/engine/assets/include/dwe/assets/png_only_images.hpp`) se copian siempre como PNG. La regla es una sola función,
`assets::pngReadingOf(id, meta)`, que comparten `dwx cook`, `dwx validate` y `dwe-game --check-content`: un tipo nuevo
es una entrada más de la tabla, y lo que declare el `_meta` de la imagen solo puede eximir una imagen que la tabla
nombra (hoy nada la exime), nunca añadirla. Son los fondos de mapa (`map/<id>/background_*`, sin YAML lateral: la hora
del día elige su paleta y los sprites con `clut` −1 la usan), las páginas de textura de los mapas
(`map/<id>/texture8/NNN`, `texture4/NNN`), también los de los mapas de un mod (`map/<paquete>/<nombre>/...`), y el atlas
de la fuente de mapa de bits (`font/<nombre>`, del que la UI lee el alfa exacto de cada píxel). `dwx validate` y
`dwe-game --check-content` rechazan para esos IDs cualquier fichero que no sea un PNG que su lector lea (indexado, para
los de mapa).

Después, por el YAML lateral (`<id>_meta.yaml`, el que escribe `dwx extract` para cada TIM). Un PNG **indexado**
(tipo de color 3) se copia tal cual, como PNG, si su YAML lateral:

- lista **más de una paleta** en `palettes`, es decir, la CLUT tiene varias filas para los mismos píxeles: las
  texturas de los Digimon (cada primitiva del modelo elige su fila), las de los efectos y los TIM de varias paletas;
  o
- es un **TIM de mapa con CLUT** (`source` acaba en `.MAP` y tiene `clut_rect`): cada sprite del mapa elige su fila
  (`clut` en `data.yaml`; −1 es la fila 480, que cambia con la hora del día), así que aunque la CLUT del TIM tenga
  una sola fila se puede dibujar con otra.

Todo lo demás se convierte: los PNG RGBA y los indexados con una sola paleta (cartas, la mayoría de las texturas
sueltas). Es la interpretación del responsable de la integración y se puede revisar (ver el ADR).

## Color o datos

Lo dice la clave `color_space` del YAML lateral: `srgb` (color) o `linear` (profundidad, máscaras, normales). Sin la
clave, o sin YAML lateral, es color. Otro valor es un error. El extractor no escribe la clave porque todo lo que
extrae es color. El espacio de color va en el `transferFunction` del descriptor de formato (2 = sRGB, 1 = lineal) y
el motor lo usa para elegir el formato de Vulkan (`BC7_SRGB` o `BC7_UNORM`, `R8G8B8A8_SRGB` o `_UNORM`).

## Qué escribe

1. Calcula la **cadena de mipmaps completa** hasta 1×1 (abajo).
2. Crea una textura 2D `VK_FORMAT_R8G8B8A8_SRGB` (43) o `_UNORM` (37), sin array ni cubo, con esos niveles.
3. La codifica a **UASTC** con `ktxTexture2_CompressBasisEx`: nivel 2 (`KTX_PACK_UASTC_LEVEL_DEFAULT`), sin RDO, un
   solo hilo por textura (`dwx cook` ya cocina varias a la vez).
4. La supercomprime con **zstd nivel 18** (`ktxTexture2_DeflateZstd`).
5. La escribe con dos metadatos fijos: `KTXorientation` = `rd` (filas de arriba abajo, como el PNG) y `KTXwriter` =
   `dwx cook / libktx v4.4.2…` (libktx añade su versión).

El resultado, en los campos del contenedor:

| Campo | Valor |
|---|---|
| `vkFormat` | 0 (`VK_FORMAT_UNDEFINED`: datos de Basis Universal, hay que transcodificarlos) |
| `typeSize` | 1 |
| `pixelWidth`, `pixelHeight` | Medidas del PNG, sin redondear a múltiplos de 4 |
| `pixelDepth`, `layerCount`, `faceCount` | 0, 0, 1 |
| `levelCount` | `floor(log2(max(ancho, alto))) + 1` |
| `supercompressionScheme` | 2 (zstd) |
| DFD | Bloque básico con `colorModel` 166 (UASTC), bloques de 4×4, 16 bytes por bloque, `transferFunction` 2 o 1 |
| Índice de niveles | Por nivel, el tamaño comprimido con zstd y el de los bloques UASTC (16 bytes por bloque de 4×4) |
| Datos | Un frame de zstd por nivel, del más pequeño al más grande |

Una imagen cuyo lado no es múltiplo de 4 se codifica en bloques enteros de 4×4; al transcodificar salen los bloques
enteros (BC7) o solo los texels de la imagen (RGBA8).

## Cómo se carga

`assets::decodeKtx2` abre el fichero con libktx sin cargar sus texels (`KTX_TEXTURE_CREATE_NO_FLAGS`) y lee la
cabecera y el índice de niveles (`inspectKtx2`). Antes de dárselo a libktx comprueba que el descriptor de formato
(DFD), los datos clave/valor (KVD) y los datos globales de supercompresión (SGD) que dice la cabecera caben en el
fichero. Con eso solo, `checkPlain2dKtx2` comprueba que sea una sola imagen
2D (sin capas ni caras de cubo, profundidad 1) con datos de Basis Universal, de 1 a 16384 texels de lado, con un
número de niveles posible, sin supercompresión o con BasisLZ o Zstd, que cada nivel esté dentro del fichero y que
ninguno ocupe al cargarse (`uncompressedByteLength` con Zstd, `byteLength` sin supercompresión) más de lo que cabe
en un nivel de su tamaño; los de BasisLZ se cargan tal cual, así que los acota el propio fichero. Una textura que
promete inflarse a gigas se rechaza sin reservar nada. Solo entonces carga e infla los texels
(`ktxTexture2_LoadImageData`) y transcodifica todos los niveles a la vez:

- a **BC7** (`KTX_TTF_BC7_RGBA`), 16 bytes por bloque de 4×4, si `rhi::Device::supportsBc7()` dice que la GPU lo
  muestrea en UNORM y en sRGB (todas las de escritorio);
- si no, a **RGBA8** (`KTX_TTF_RGBA32`), 4 bytes por texel.

Devuelve los niveles del más grande al más pequeño y si el color es sRGB. `rhi::Texture::create` sube BC7 solo si
el dispositivo lo admite; si no, lo rechaza. El visor de assets del overlay de depuración abre así los `.ktx2`.
UASTC a BC7 es casi sin pérdida (UASTC está pensado para eso), y a RGBA8 da los colores que guardó UASTC.

## Cómo se calculan los mipmaps

- Cada nivel mide la mitad del anterior, redondeando hacia abajo y nunca menos de 1.
- Cada texel es la media del área que cubre en el nivel anterior (un filtro de caja), con el peso de la parte de cada
  texel que cae dentro. Así un lado impar no pierde la última columna. Los pesos se calculan con enteros, así que
  son exactos.
- El color se promedia en **luz lineal** (se decodifica el sRGB, se promedia y se vuelve a codificar) y **ponderado
  por el alfa**: los texels transparentes, que en las texturas del original son negro con alfa 0, no oscurecen a sus
  vecinos. El alfa se promedia tal cual.
- Cada nivel sale del anterior, en coma flotante, y se redondea a 8 bits al final. El nivel 0 es el PNG sin tocar.

## Determinismo

La misma imagen con las mismas opciones da siempre los mismos bytes (hay un test que lo comprueba, también con
varios hilos), dentro de este alcance:

- las versiones fijadas por el baseline de vcpkg: libktx 4.4.2 (con su Basis Universal) y zstd 1.5.7;
- el mismo camino de CPU: el port de vcpkg compila Basis Universal con SSE 4.1 en x86-64 y lo usa si la CPU lo
  tiene; el camino con SSE y el escalar (u otra arquitectura) no prometen los mismos bloques;
- el mismo compilador y la misma librería estándar para los mipmaps: entre plataformas, `pow` podría diferir en el
  último bit y cambiar algún texel en una unidad.

Fuera de ese alcance el fichero puede cambiar, pero sigue siendo válido y el motor lo lee igual. La caché de
`dwx cook` guarda cada salida por el SHA-1 de la fuente y la versión del cocinero, así que una caché no mezcla
salidas de cocineros distintos.
