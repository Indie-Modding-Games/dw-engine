# Textos y fuente

Cómo lee `formats/text` los textos del original y su fuente, y cómo los escribe `dwx extract`. Los textos pasan a
UTF-8 con etiquetas legibles para lo que no es un carácter, y vuelven a los mismos bytes: la conversión no pierde
nada y las pruebas lo comprueban en los dos sentidos.

## Dos codificaciones

| | Cadenas de bytes | Texto de script |
|---|---|---|
| Dónde | Ejecutable y overlays (nombres, menús, descripciones) | Instrucciones `SHOW_TEXTBOX` y `SET_SELECTION` de `SCN` |
| Unidad | Un byte; desde `0x80`, un código Shift-JIS de dos bytes | Celdas de dos bytes (cuatro la `03`) |
| Lo dibuja | `font.c: drawString` y `sjis.c: convertAsciiToJis` | `script_textbox.c: showTextbox` y `drawString2` |
| Fin | El NUL de la cadena | Una celda `0D` seguida de un byte `00` |

En el US las cadenas del ejecutable son ASCII de un byte y el texto de los scripts es todo Shift-JIS de ancho
completo: `showTextbox` copia pares de bytes, así que en un script no cabe ASCII de un byte.

## Juego de caracteres

`charset.hpp` traduce entre códigos Shift-JIS y Unicode con el **juego del juego**: las filas `0x81`–`0x84` de cp932
(signos, números, letras latinas, kana, griego y cirílico; 524 caracteres) con dos cambios que pide la fuente:

| Código | cp932 | Juego del juego |
|---|---|---|
| `0x8175` | `「` | `’` (U+2019): la fuente dibuja ahí el apóstrofo |
| `0x8176` | `」` | `”` (U+201D): la fuente dibuja ahí las comillas |
| `0x8166`, `0x8168` | `’` y `”` | sin carácter: sus caracteres pasan a los códigos de arriba |

Quedan 522 caracteres y la traducción es biyectiva: cada carácter tiene un código y cada código un carácter como
mucho. `「` y `」` no tienen código, porque ningún glifo del US los dibuja.

**Por qué solo esas filas y no cp932 entero.** Cubren todos los glifos de la fuente del US y todos los códigos que
aparecen en los datos del US. Los kanji (solo en la versión japonesa) y cualquier otro código no se pierden: salen
como etiqueta (`{sjis:889F}`, `{cell:889F}`) y vuelven igual. Una tabla de cp932 entero tendría unos 7 900
caracteres que ningún texto del US usa.

La tabla son 752 celdas `u16` (4 filas × 188 bytes de cola, de `0x40` a `0xFC` sin `0x7F`), escritas en
`charset.cpp` como literales. Se genera de forma determinista desde la tabla estándar de la página de códigos
(`CP932.TXT` de Microsoft, los mismos datos que el índice Shift_JIS de WHATWG en estas filas): para cada celda, el
carácter del par o 0 si no tiene; las sustituciones se aplican en el código, no en la tabla. La tabla inversa se
construye al vuelo ordenando la directa.

En el texto de script las letras del US son de ancho completo; para que se lean, el decodificador las escribe como
ASCII (`fullWidthOf` / `asciiOf`): `Ａ` (`0x8260`) es `A`, `0x8140` es el espacio, `0x8142` es `.`, `0x8175` es `'` y
`0x8176` es `"`. El resto del ASCII imprimible va a U+FF01 y siguientes. El otro punto, `0x8144`, se queda como
`．` para que la vuelta sea exacta.

## Etiquetas

`{` y `}` se escriben `{{` y `}}`. Lo que no es un carácter va entre llaves.

Cadenas de bytes:

| Etiqueta | Bytes |
|---|---|
| `{byte:XX}` | un byte de control (`< 0x20`), `0x7F`, o un primer byte de Shift-JIS sin segundo al final de la cadena |
| `{sjis:XXXX}` | un código de dos bytes sin carácter en el juego del juego |

Texto de script: cada celda de control es `código, parámetro` (`showTextbox` expande los nombres y números;
`drawString2` dibuja colores, tabulaciones y columnas):

| Código | Etiqueta | Parámetro |
|---|---|---|
| `01` | `{color:N}` | color |
| `02` | `{raw02:XX}` | sin consumidor conocido; se guarda tal cual |
| `03 a b c` | `{raw03:XXXXXX}` | la única celda de cuatro bytes; sin consumidor conocido |
| `04` | `{num:pstat=P}` | el valor de la variable de script `P` |
| `05`, `06` | `{tamer}`, `{partner}` | ignorado |
| `07`, `08`, `09` | `{digimon:pstat=P}`, `{move:pstat=P}`, `{item:pstat=P}` | nombre de la tabla en el índice que guarda `P` |
| `11`, `12` | `{bgm:pstat=P}`, `{tournament:pstat=P}` | ídem |
| `0A`, `0B` | `{bits}`, `{merit}` | ignorado |
| `0C`, `0E`, `0F` | `{tab96}`, `{tab132}`, `{half}` | ignorado |
| `10`, `13`, `14`, `15` | `{shopvar}`, `{tourn_won}`, `{tourn_wins}`, `{tourn_lost}` | ignorado |
| `16`–`1C` | `{col16}`... `{col1c}` | columnas fijas del US; ignorado |
| `0D 00` | salto de línea (`\n`) | |
| `0D xx` | `{newline:raw=XX}` | un salto con parámetro distinto de 0 |

Un parámetro ignorado que no es 0 se guarda: `{tamer:raw=05}`. Una celda que no es ni carácter ni control conocido
es `{cell:XXXX}`; una celda `00 xx` que no sigue a un salto de línea es un glifo más (`{cell:00XX}`).

**Fin del texto.** El texto acaba en la primera celda `0D` seguida de un byte `00`; esa celda y la `00 xx` que la
sigue son el final. Normalmente son `0D 00 00 00` y no se escriben; si llevan otros bytes se guardan con
`{end:XXYY}` al final. Que siga otra página depende de si la instrucción siguiente es otro `SHOW_TEXTBOX`
(`pageReady`), no del texto. El codificador rechaza lo que el original no podría leer igual: una celda que empieza
por `00` justo después de un salto de línea, `{end}` que no es la última etiqueta, `{cell}` con primer byte `03` o
`0D`.

## Fuente

Está en el ejecutable (`font.c`; direcciones del US en `findFontLocation`):

- `CHAR_TO_GLYPH_TABLE`: un código Shift-JIS `u16` por glifo; 79 en el US.
- `GLYPH_DATA`: por glifo, 11 filas `u16` y un ancho `u16` (24 bytes). En cada fila los bits 15–4 son las columnas
  0–11 y un bit a 0 es tinta; los bits 3–0 están siempre a 1.
- `FONT_CLUT`: 16 colores de 15 bits.
- El último glifo (el 78 del US, código `0x817E`) es el que se dibuja para un código que no está en la tabla.

`readFont` falla si los tamaños no cuadran, si un código se repite o si un glifo no tiene los bits 3–0 a 1 (el atlas
no los guardaría). `dwx extract` escribe:

- `font/main.png`: atlas indexado de celdas de 12 × 11 en filas de 16 (192 × 55 en el US); índice 1 = tinta,
  0 = fondo, con la `FONT_CLUT` como paleta.
- `font/main_meta.yaml`: `cell_width`, `cell_height`, `columns`, `fallback`, `colors` (los 16 colores en hex) y por
  glifo `index`, `sjis`, `character`, `ascii` (si lo tiene), `x`, `y` y `width`.

`fontFromAtlas` reconstruye la fuente a partir de los dos ficheros.

## Cadenas del ejecutable y los overlays

Las cadenas no están indexadas en ningún fichero, así que `string_manifest.cpp` lista dónde están para cada versión,
sacado del decomp (`config/us/symbols*.txt` y los bloques de `text/<dir>/<nombre>.yaml`). Guarda solo direcciones y
cuentas, nunca el texto. Cada bloque tiene un nombre único en su fuente y una forma:

| Forma | Contenido |
|---|---|
| `Pointers` | `char*[n]`; un puntero nulo no tiene cadena |
| `Records` | `n` registros de `stride` bytes que empiezan por `char nombre[length]` (`DIGIMON_DATA`, `ITEM_PARA`) |
| `Embedded` | `char[n][stride]` |
| `Named` | una cadena en cada dirección de la lista (las `STR_*` sueltas de un bloque) |

Los overlays (`BTL`, `STD`, `VS`, `KAR`, `FISH`, `TRN`) se leen en su dirección de carga; un puntero de un overlay
puede apuntar al ejecutable, y se busca ahí si no está en el overlay. Los códigos de modelo
(`DIGIMON_FILE_NAMES`) no son texto y no están.

`dwx extract` escribe `text/<idioma>/<fuente>.yaml` (`text/en/main.yaml`, `text/en/btl.yaml`...): `source` y
`strings`, un mapping de bloque a lista de cadenas (o `null`). El id estable de una cadena es
`<fuente>.<bloque>.<índice>`. Las cadenas que el decomp nombra sin dirección conocida (tres bloques de `BTL` y `VS`)
salen como notas del informe.

El texto de los scripts no va a estas tablas: está en el listado de cada script (ver [Scripts de eventos](scripts.md)),
y su id estable es el script más el offset de su instrucción.
