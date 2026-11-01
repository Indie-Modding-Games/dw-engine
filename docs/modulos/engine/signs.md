# signs

Los carteles pintados en las imágenes de los mapas, escritos otra vez en el idioma del jugador. La versión
americana dejó muchos carteles en japonés (la arena, los postes de Freezeland, las tiendas...) y otros en inglés
(el felpudo de la casa, el cartel de la tienda de herramientas). `signs` borra el texto viejo y dibuja el nuevo
**al cargar el mapa**, a partir de las texturas que el jugador extrajo de su disco y de datos propios: el repositorio
no lleva ni un píxel del original, ni recortado ni editado.

Cómo encontrar un cartel y escribir su overlay: [guía de carteles](../../guides/carteles.md).

## Piezas

| Pieza | Qué hace |
|---|---|
| `sign_overlay.hpp` | El formato: `Sign` (imagen, región, borrado, texto, fuente, efectos), `parseSignOverlay`, `mergeSignOverlays` |
| `sign_font.hpp` | `SignFont`: una fuente TrueType del paquete rasterizada con FreeType, sin hinting y con kerning |
| `sign_painter.hpp` | `paintSign` / `paintSigns`: borra y dibuja en una copia de las imágenes y da parches; `applySignPatches` los aplica |
| `map_signs.hpp` | `MapSigns`: lee los overlays del VFS, carga las fuentes y guarda los parches por mapa, idioma y revisión |

## Overlays

- `signs/<mapa>.yaml` del paquete `game`: los carteles de un mapa en todos los idiomas (sus textos, en inglés y en
  español, en `localization/<idioma>/signs.yaml`).
- `signs/<idioma>/<mapa>.yaml` de cualquier paquete (el de español es `es-translation`): reemplaza carteles del mismo
  `id`, añade los que solo ese idioma necesita (el felpudo, que en inglés ya se lee bien) y quita con `remove` los
  que ese idioma deja como en el original.
- El esquema es `content/schemas/sign_overlay.schema.json`.

Un cartel dice:

| Campo | Qué es |
|---|---|
| `target` | `background` (por defecto) o una página de textura de los sprites: `{page, bits, clut}` como la nombra el sprite |
| `region` | `[x, y, ancho, alto]` en píxeles de la imagen: lo único que el cartel puede cambiar, y la caja del texto si no hay `corners` |
| `corners` | Las cuatro esquinas del texto (arriba izquierda, arriba derecha, abajo derecha, abajo izquierda), para un cartel en perspectiva |
| `rotation`, `skew` | Grados de giro (horario) e inclinación de las letras, cuando no hay `corners` |
| `clear` | Cómo se borra el texto viejo (abajo) |
| `text` | La clave del texto, como `signs.trai00_title_top`; `\n` parte la línea |
| `font`, `size` | Una fuente de `ui/fonts/` (`noto_sans_bold` por defecto) y su alto en píxeles de la imagen |
| `letter_spacing`, `line_spacing` | Píxeles extra entre letras y factor sobre el alto de línea |
| `align`, `valign` | `left`/`center`/`right` y `top`/`middle`/`bottom` |
| `vertical` | Una letra debajo de otra (una pancarta vertical) |
| `fit` | Encoge el texto hasta que cabe (por defecto sí) |
| `color`, `outline`, `glow` | El color de las letras, un contorno `{color, width}` y un halo de neón `{color, radius, strength}` |
| `colors` | `all` (cualquier color opaco de la paleta) o `region` (solo los que el original usa alrededor) |

Los modos de `clear`:

| Modo | Qué hace |
|---|---|
| `inpaint` | Cada píxel mezcla los de fuera del rectángulo en su fila y su columna, por distancia |
| `fill` | Un color: `color`, o la media del borde del rectángulo |
| `diffuse` | Quita solo los píxeles que se alejan del color del tablero (`color`, o el más común) más de `tolerance`, los crece `grow` píxeles y los rellena desde el tablero de alrededor, anillo a anillo, con el grano del tablero; `quad` limita el borrado a un cuadrilátero |
| `none` | No borra: el texto se dibuja encima (la segunda línea de un cartel ya borrado) |

## Cómo se pinta

1. El texto se rasteriza con FreeType a 4x, con el contorno (dilatación) y el halo (desenfoque de tres pasadas).
2. La caja del texto se lleva a la imagen con una homografía (cuadrado a cuadrilátero): las `corners`, o la región
   con su giro e inclinación.
3. Cada píxel mezcla las capas (halo, contorno, letras) sobre el tablero ya borrado y se escribe como el **índice
   de la paleta** más cercano (RGB ponderado). No entra ningún color nuevo en la textura y las paletas de la hora
   del día siguen sombreando el cartel.
4. `paintSigns` da un parche por cartel (el rectángulo de píxeles que cambió); `applySignPatches` los escribe en
   las imágenes y, si cambió una página de textura, vuelve a montar las texturas de los sprites.
5. Muchos mapas dibujan encima del fondo sprites recortados del mismo fondo (un mostrador delante de una pancarta,
   un toldo): con los objetos del mapa, cada píxel que un cartel del fondo cambió se copia también en los sprites de
   8 bits con la paleta del fondo (`clut` -1) que lo muestran, si su texel tenía el mismo índice que el fondo (algunos
   sprites se recortaron un par de píxeles desplazados: se busca el desplazamiento, hasta 4 píxeles, en el que más
   texels coinciden con el fondo). Un
   sprite con paleta propia (un cartel que parpadea con una copia iluminada) necesita su propio cartel con
   `target: {page, bits, clut}`, como `shop_lit` en `gcan09`.

## En el juego

`FieldScene` guarda una copia de las imágenes del mapa tal como las leyó, pinta los parches antes de
`MapRenderer::setMap` y, si cambian el idioma (`Catalog::revision`) o la opción, vuelve a subir el fondo y las
texturas de los sprites con `MapRenderer::updateImages`. `MapSigns` guarda los parches por mapa, idioma y revisión, así
que volver a un mapa no pinta nada otra vez.

La opción **Carteles traducidos** del menú de pausa (opciones; `translated_signs` en los ajustes del jugador) está
activada por defecto; desactivada, el mapa muestra las texturas originales.

## Herramientas y pruebas

- `dwe-game --dump-signs <mapa>[,<mapa>...] --shots <carpeta> [--lang <idioma>]`: escribe el fondo y las páginas de
  textura con rejilla, la lista de sprites, los carteles pintados y un `place(...)` para ir a ver cada cartel
  (`apps/game/src/sign_dump.cpp`). Las imágenes salen del disco del jugador: van a `_work/tmp`, nunca al repositorio.
- `engine/signs/tests`: lectura de overlays, borrado, texto en una textura sintética (sin datos del juego), giro,
  perspectiva, efectos, parches y el cambio de idioma de `MapSigns`.
- `tests/autotest/route-signs.yaml`: captura un mapa en el idioma de la partida y otra con la opción apagada.

## Carteles hechos

| Mapa | Cartel | en | es |
|---|---|---|---|
| `betl02` | Neón de la arena | ARENA | ARENA |
| `betl02` | Pancarta de la recepción | RECEPTION | RECEPCIÓN |
| `frzl06` | Postes del cruce | ICEBERG BEACH / MISTY TREES | PLAYA ICEBERG / BOSQUE BRUMOSO |
| `gcan09` | Cartel de la tienda | MONOCHROME SHOP | TIENDA MONOCROMO |
| `gias06b` | Título de la tienda de reciclaje | RECYCLED GOODS & CARDS | RECICLAJE Y CARTAS |
| `gias06b` | Subtítulo y pancarta SALE | (ya en inglés) | TIENDA DE RECICLAJE Y CARTAS / REBAJAS |
| `mayo01_2`, `mayo02_2` | Flechas al monte | MT. PANORAMA | MTE. PANORAMA |
| `mayo10`, `mayo11` | Tablero de los pescadores (los garabatos azules se quedan) | ANGLER'S CODE y sus dos reglas | CÓDIGO DEL PESCADOR |
| `room02` | Felpudo | (ya en inglés) | Bienvenido |
| `trai00` | Tablero del curso de combate | BATTLE COURSE | CURSO DE COMBATE |
| `twnb10` | Cartel de abierto | (ya en inglés) | ESTAMOS ABIERTOS |

Pendientes: el grafiti de `omoc08` (sobre ladrillos con relieve, que el borrado aún no rehace) y, fuera de la lista,
la placa de la Green Gym de `trai00` (una página de sprites), el cartel de la tienda de herramientas de `twnb10` y el de
Drill Tunnel de `mayo11`.
