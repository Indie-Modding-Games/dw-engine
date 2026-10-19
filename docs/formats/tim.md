# TIM, TIM apilados y packs de paso fijo

Referencia de las imágenes del original tal como las lee `formats/image` (`dwe::formats::image`). El plan general
está en [formatos del original](../architecture/formats.md). Las citas son al decomp (`dw_decomp`, `src/`); aquí no
hay datos del disco.

Todos los enteros son little-endian. Los lectores reciben un `std::span<const std::byte>`, comprueban cada offset y
devuelven un error si el fichero está mal formado; nunca leen fuera del buffer.

## TIM

El formato de imagen estándar de PsyQ: lo que el original pasa a `GsGetTimInfo` y sube a la VRAM con `LoadImage`.

| Offset | Tipo | Campo |
|---|---|---|
| 0 | `u32` | Magia `0x10` |
| 4 | `u32` | Flags: bits 0-2 modo de píxel (0 = 4 bits, 1 = 8 bits, 2 = 16 bits, 3 = 24 bits), bit 3 hay CLUT |
| 8 | bloque | CLUT, solo si el bit 3 está activo |
| … | bloque | Imagen |

Cada bloque es `{u32 longitud; u16 x, y, ancho, alto; datos}`. La longitud incluye los 12 bytes de la cabecera. `x` y
el ancho cuentan unidades de 16 bits de la VRAM, no píxeles: una unidad son 4 píxeles a 4 bits, 2 a 8 bits, 1 a 16
bits y 2/3 a 24 bits (lo que sobra de cada fila es relleno). Los datos ocupan `ancho × 2 × alto` bytes.

- **CLUT:** `ancho × alto` colores de 16 bits. Un TIM puede tener varias paletas: una por fila o, si la fila es más
  ancha que la paleta (16 colores a 4 bits, 256 a 8 bits), varias seguidas en la misma fila. `timPaletteCount` las
  cuenta y se numeran de izquierda a derecha y de arriba abajo; `decodeTim(tim, n)` usa la paleta `n`. Si la fila es
  más corta que la paleta completa, la paleta es la fila entera, y un índice que se sale de ella es un error.
- **Píxeles:** a 4 bits el primer píxel de cada byte está en el nibble bajo. A 24 bits cada píxel son tres bytes
  R, G, B.
- **Lo que se rechaza:** magia distinta de `0x10`, modo 4 (mixto) o mayor, bloques vacíos o más grandes que la VRAM
  (1024 × 512 unidades), una longitud menor que lo que piden ancho × alto, y cualquier bloque que pase del final.
  Lo que haya después del último bloque se ignora; `Tim::byteSize` dice cuántos bytes ocupó según sus longitudes.
- **El bloque de imagen se busca con la longitud del bloque CLUT**, como hace `GsGetTimInfo`.
- **Un TIM de 4 u 8 bits sin CLUT** es válido (su paleta está en otro sitio de la VRAM): se leen sus índices, pero no
  se puede pasar a RGBA.

### Colores y transparencia

Un color de la consola (`Color15`) tiene 5 bits por canal (rojo en los bits 0-4, verde en 5-9, azul en 10-14) y el
bit STP (15). Al pasar a RGBA8 cada canal se amplía repitiendo sus bits altos (`v << 3 | v >> 2`, así 31 da 255).

La regla de alfa (`toRgba8`):

- `0x0000` es el único color transparente (alfa 0): la consola no dibuja esos texels.
- Todo lo demás es opaco (alfa 255), también `0x8000`, que es negro opaco.
- El bit STP solo importa cuando la primitiva se dibuja semitransparente, y eso lo decide quien dibuja, no la imagen.
  Por eso no cambia el alfa, pero no se pierde: `Tim` guarda la CLUT y los píxeles en bruto.
- A 24 bits no hay transparencia.

## TIM apilados

`ETCDAT\ETCTIM.BIN` y `STDDAT\STDTIM.BIN` son TIM seguidos sin índice. `readStackedTims` los recorre como
`utils2.c: loadStackedTIMFile`:

- La lista termina en la primera palabra que no es `0x10` (`StackedTims::end`), o al final del fichero.
- Cada TIM avanza lo que dan su ancho × alto, no sus longitudes: `4 + ((ancho × alto / 2) + 4) × 4` por la magia, los
  flags y la imagen, más `((ancho × alto / 2) + 3) × 4` por la CLUT si la hay, con las medidas en unidades de 16
  bits. La división redondea hacia abajo, como en el original.
- Si eso no coincide con lo que dicen las longitudes de los bloques, se usa el tamaño del original y el resultado
  trae un aviso en `warnings` (el lector no escribe en el log).
- Cada TIM se lee con el lector normal desde su offset hasta el final del fichero, así que sus datos salen igual que
  los sube el original. Si un TIM está mal formado o el paso se sale del fichero, es un error.

## Packs de paso fijo

Algunos ficheros son entradas concatenadas a un paso fijo de sectores (2048 bytes), sin cabecera. El paso y el número
de entradas están en el código del original, así que `knownPacks()` los declara. `splitPack` parte cualquier pack por
su paso y `splitKnownPack` comprueba además el número de entradas.

| Fichero | Paso | Entradas | Índice | Código |
|---|---|---|---|---|
| `CHDAT\ALLTIM.TIM` | 9 sectores (`0x4800`) | 180 | tipo de Digimon | `model.c: loadDigimonTexture, loadMMDAsync, uploadModelTexture` |
| `CARD\CARD.ALL` | 14 sectores (`0x7000`) | 66 | id de carta | `game_menu.c: loadCardImage`, `file.c: loadStackedTIMEntry` |

- **Qué hay en cada entrada:** en los dos packs, un TIM al principio de la entrada y relleno hasta el paso. El
  original lee los sectores de la entrada (`readFileSectors(fichero, buffer, índice × paso, paso)`) y pasa al
  `GsGetTimInfo` la palabra que sigue a la magia, sin comprobarla.
- **ALLTIM:** el original no usa la posición de VRAM del TIM: `uploadModelTexture` la sustituye por la página de
  textura y la CLUT del modelo que lo usa. Las 180 entradas son los tipos de Digimon (`loadMMDAsync` rechaza
  `digimonType >= 0xB4`).
- **CARD.ALL:** el TIM se sube a su propia posición. Las 66 entradas son las cartas (`CARD_DATA` ocupa `0x108`
  bytes de entradas de 4).
- **Tamaños:** en la tabla de ficheros de todas las versiones (`src/main/file_table_*.c`) ALLTIM.TIM ocupa
  `0x32A000` bytes (180 × `0x4800`) y CARD.ALL `0x1CE000` (66 × `0x7000`).

## PNG

`encodePngRgba` y `encodePngIndexed` escriben los PNG del extractor con [lodepng](https://github.com/lvandeve/lodepng)
(paquete de vcpkg, enlazado en privado). La salida es determinista: los mismos píxeles dan los mismos bytes con la
misma versión de la librería, porque no se escribe fecha ni texto y la conversión automática de tipo de color está
apagada.

- **RGBA:** PNG de 8 bits por canal.
- **Indexado:** PNG con paleta de 8 bits; la paleta va en `PLTE` y, si algún color no es opaco, su alfa en `tRNS`.
  Los TIM de 4 bits también se escriben a 8 bits por píxel.

**Qué usará el extractor por defecto:** los TIM de 4 y 8 bits con CLUT se escribirán como PNG indexado con la paleta 0,
porque así no se pierde nada: los índices y la paleta siguen en el fichero, el RGBA se puede sacar de ellos (al revés
no, porque una paleta suele repetir colores) y cualquier editor de imágenes lo abre. Las demás paletas de un TIM con
varias irán como metadato, igual que la CLUT en bruto con su bit STP. Los TIM de 16 y 24 bits irán en RGBA.
