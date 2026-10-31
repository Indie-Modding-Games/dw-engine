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
| `{color:N}` | `#C` y una cifra: el color del texto que sigue (`battle_hud.c: BTL_drawBattleEndText`) |
| `{br}` | `#R`: salto de línea (ídem) |
| `{wait}` | `#W`: espera y salto de línea (ídem) |
| `{0}`, `{1}`... | cada `%d` por orden: un número que el juego escribe con `sprintf` (mensajes de la pesca). Una traducción puede cambiarlos de orden, porque cada uno lleva su número |

Los códigos con `#` solo los lee el texto del final de la batalla (`btl.battle_effect_messages`), y `%d` los mensajes
de la pesca (`fish.messages`), pero se convierten en todas las cadenas de bytes: ninguna otra los tiene. Un `#` o un
`%` que no forman uno de estos códigos se quedan como texto.

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
Al dibujarla, el original para: `drawString2` acaba en el byte 0 y `drawTextboxStrings` no dibuja más filas de la
página, aunque `showTextbox` siga leyendo hasta el final y la página pase igual. El inglés tiene once
`{cell:0000}` delante de restos de una línea más larga (`story_054.text.0c02` se ve «fades away, he'll» y nada más);
dwe deja vacías las filas desde ahí (`appendPageRows`), medido en el original con un `00 00` escrito en la página
de la máquina de guardado: solo sale «Oh that?» y la página siguiente sale entera. El español pone la etiqueta al
final cuando traduce la intención de la línea.

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

`fontFromAtlas` reconstruye la fuente a partir de los dos ficheros. El juego no usa `formats`: la UI lee esos dos
ficheros con su propio lector (`ui::BitmapFont`) para el texto con aspecto original
([UI](../architecture/ui.md#aspecto-original)).

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

Las cadenas que el decomp nombra sin dirección conocida (tres bloques de `BTL` y `VS`) salen como notas del informe.

## Tablas de texto

`dwx extract` escribe una tabla por fuente, `text/<idioma>/<fuente>.yaml` (`text/en/main.yaml`, `text/en/btl.yaml`...):
un mapping plano de clave a texto, sin más campos.

```yaml
map_names.0003: "File City"
move_names.0012: "{color:2}Spit Fire"
```

- **Clave:** `<bloque>.<índice>`, con el índice en cuatro cifras (`map_names.0003`). Dentro del juego la clave
  completa lleva delante la fuente: `main.map_names.0003`. Los nombres de bloque y de fuente salen del manifiesto de
  cadenas y no cambian entre versiones del extractor, así que la clave es estable: la usan las traducciones.
- **Huecos:** un puntero nulo no tiene cadena y no tiene clave; el índice de las demás no cambia.
- **Texto:** UTF-8 con las etiquetas de [arriba](#etiquetas). Para las tablas de idiomas, cada etiqueta es un marcador
  (`{color:2}`, `{digimon:pstat=3}`, `{byte:0A}`): `dwx validate` comprueba que una traducción tenga el mismo
  conjunto de marcadores que el inglés en cada clave. `{{` y `}}` son llaves literales.
- **Traducciones:** otro idioma va en `text/<idioma>/<fuente>.yaml` de un paquete de traducción, con las mismas
  claves y nada más; puede dejar fuera cadenas (se ven en inglés) y puede ser parcial. Los nombres que se quedan en
  inglés no se repiten. Un paquete más alto en la pila cambia claves sueltas sin copiar la tabla. Ver
  [localización](../architecture/localizacion.md).

### Lock de una traducción

Un paquete de traducción puede llevar `text/<idioma>/.sources.lock` junto a sus tablas. No es un asset (los ficheros
con punto no entran en la VFS); lo lee `dwx validate` de la carpeta del paquete.

```yaml
sources:
  main.map_names.0003: 0a1b...   # SHA-1 del texto inglés del que se tradujo
kept_english: [main.digimon_names.0001, main.digimon_names.0002]
kept_original: [main.tournament_grades.0000]
```

- `sources`: clave completa de cada cadena traducida → SHA-1, en 40 cifras hexadecimales en minúscula, de su texto
  inglés tal como lo escribe `dwx extract`: el valor de la tabla en UTF-8, con sus etiquetas (`{color:1}`, `{br}`),
  sin comillas de YAML.
- `kept_english`: las claves que se quedan en inglés y no están en la traducción (nombres de Digimon).
- `kept_original`: opcional; las claves que no son palabras inglesas y se quedan tal cual (huecos, nombres de quien
  habla, letras de rango, restos en japonés, HP/MP/Bits). Separarlas de `kept_english` deja honesto el recuento de lo
  que queda en inglés.
- `sources` y `kept_english` están siempre; vacías son `{}` y `[]`.

`dwx validate` avisa de una cadena cuyo inglés ha cambiado desde que se tradujo (otro SHA-1), de una cadena traducida
sin SHA-1 en el lock, de una clave de `kept_english` o `kept_original` que también está traducida y de una clave que
el inglés no tiene.
Una traducción sin lock no se comprueba así.

El esquema es `content/schemas/text.schema.json`. El motor todavía lee el formato anterior (`source` y `strings`, un
mapping de bloque a lista de cadenas o `null`) con las mismas claves, para el contenido extraído antes de este cambio;
`dwx extract` ya no lo escribe.

El texto de los scripts no va a estas tablas: está en el listado de cada script (ver [Scripts de eventos](scripts.md)),
y su id estable es el script más el offset de su instrucción.
