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
| `transparent` | Deja la zona transparente (el color de la paleta con alfa 0), para que se vea lo que hay detrás del sprite: las letras de una página de sprites que se pinta encima del mapa (los postes luminosos). Las letras nuevas se dibujan sin bordes suaves, porque no hay con qué mezclarlas |

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
texturas de los sprites con `MapRenderer::updateImages`. Lo mira en cada fotograma (`FieldScene::update`), también con
el juego en pausa: el idioma elegido en el menú de pausa cambia los carteles que se ven detrás del menú al momento. `MapSigns` guarda los parches por mapa, idioma y revisión, así
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
- `tests/autotest/route-signs-language.yaml`: cambia el idioma en el menú de pausa de `gcan09` y captura el cartel detrás
  del menú abierto (ya en el idioma nuevo) y después en el campo.

## Carteles hechos

| Mapa | Cartel | en | es |
|---|---|---|---|
| `betl02` | Neón de la arena | ARENA | ARENA |
| `betl02` | Pancarta de la recepción | RECEPTION | RECEPCIÓN |
| `frzl01` | Aviso junto a la estufa (letra ilegible en el original: se escribe el sentido) | NOTICE / Beware of the cold | AVISO / Cuidado con el frío |
| `frzl06` | Postes del cruce | ICEBERG BEACH / MISTY TREES | PLAYA ICEBERG / BOSQUE BRUMOSO |
| `frzl12` | Poste de la cresta, de lado (el mismo cartel que el este de `frzl06`) | ICEBERG BEACH | PLAYA ICEBERG |
| `gias00` | Poste luminoso (página de sprites, `clear: transparent`) | (ya en inglés) | PUEBLO FACTORIAL / MTE. INFINITO / MTE. BASURA |
| `gias08` | Cartel de la caseta del váter (juego de palabras) | PUBLIC POO | POPÓ PÚBLICO |
| `gcan09` | Cartel de la tienda | MONOCHROME SHOP | TIENDA MONOCROMO |
| `gias06b` | Título de la tienda de reciclaje | RECYCLED GOODS & CARDS | RECICLAJE Y CARTAS |
| `gias06b` | Subtítulo y pancarta SALE | (ya en inglés) | TIENDA DE RECICLAJE Y CARTAS / REBAJAS |
| `mayo01_2`, `mayo02_2` | Flechas al monte | MT. PANORAMA | MTE. PANORAMA |
| `mayo00`, `mayo02_2` | Postes luminosos (página de sprites) | (ya en inglés) | CASA DE JIJIMON / CIUDAD FILE / GIMNASIO VERDE / BOSQUE NATIVO / MTE. PANORAMA / TÚNEL TALADRO |
| `mayo10`, `mayo11` | Tablero de los pescadores (los garabatos azules se quedan) | ANGLER'S CODE y sus dos reglas | CÓDIGO DEL PESCADOR |
| `mayo11` | Nombre del túnel sobre la boca | DRILL TUNNEL | TÚNEL TALADRO |
| `omoc08` | Pintada roja en la pared de ladrillo | THE GANG / WAS HERE | LA BANDA / ESTUVO AQUÍ |
| `room02` | Felpudo | (ya en inglés) | Bienvenido |
| `room13`, `room19` | Pancarta de la sala de espera y letrero de la recepción de la arena | WAITING ROOM / RECEPTION | SALA DE ESPERA / RECEPCIÓN |
| `trai00` | Tablero del curso de combate | BATTLE COURSE | CURSO DE COMBATE |
| `trai00` | Placa del soporte de abajo | GREEN GYM | GIMNASIO VERDE |
| `twnb10`…`twnb17` | Placa de la tienda de herramientas y cartel de abierto (cada fase de la plaza) | (ya en inglés) | TIENDA DE OBJETOS / ESTAMOS ABIERTOS |
| `twnb03`…`twnb06`, `twnb11`…`twnb14`, `twnb18`…`twnb21` | Nombre del restaurante en el mostrador | (ya en inglés) | RESTAURANTE |
| `twna02`…`twna13`, `topn01`, `tend01`, `tend02` | Placa del banco de objetos junto a la granja | (ya en inglés) | BANCO DE OBJETOS |

Pendientes y huecos: el inventario completo de carteles por mapa (qué queda y por qué) está en el seguimiento del
trabajo de traducción; las palabras del mapa luminoso de `frzl03` están cortadas en muchos trozos de sprite y el overlay
aún no sabe escribir un texto repartido así.