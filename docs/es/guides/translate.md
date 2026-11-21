# Traducir los textos del original

El proyecto tiene su propia traducción al español de los textos del juego original. Esta guía dice dónde vive, cómo
se escribe y cómo se mantiene al día cuando cambia el inglés extraído. Los términos que se repiten están en el
[glosario de la traducción](spanish-glossary.md).

## Dónde vive

En el paquete [`content/packs/es-translation`](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/packs/es-translation/pack.yaml) (id `es_translation`),
que depende de `base`. Es un **delta**: solo lleva el español. El inglés sale del disco de cada usuario
(`dwx extract`), y ningún texto del original entra en git ([política legal](../legal/assets.md)).

```text
content/packs/es-translation/
├─ pack.yaml
└─ text/es/
   ├─ main.yaml       una por fuente de text/en: main, btl, std, vs, kar, fish, trn
   ├─ ...
   ├─ story_049.yaml  una por script de eventos: el diálogo de la historia (story_000 a story_221)
   └─ .sources.lock   generado: de qué inglés sale cada traducción y qué se deja en inglés
```

Cada fichero es un mapping plano de clave a texto, con las mismas claves que `text/en/<fuente>.yaml`:

```yaml
"item_names.0013": "Venda"
```

La clave es `<bloque>.<hueco>`: el bloque del manifiesto de cadenas y el índice del hueco dentro de él, con cuatro
cifras. El id completo lleva delante la fuente (`main.item_names.0013`, ver [Textos y fuente](../formats/texts.md)).
En las tablas de la historia la clave es `text.<offset>`, el offset de la orden del script en cuatro cifras
hexadecimales (`"text.3b66"`, id completo `story_000.text.3b66`, ver [Scripts de eventos](../formats/scripts.md#lua)).
Una clave que falta se muestra en inglés.

## Qué no se traduce

No se escriben, porque serían copias del original. El `.sources.lock` los lista, para que la cobertura distinga «se
queda así a propósito» de «falta traducir»:

- `kept_english`: los nombres de los Digimon (`main.digimon_names`), que se quedan en inglés. Los nombres de las
  técnicas (`main.move_names`) sí se traducen (decisión del responsable de diseño, 2026-10-21). Los títulos en
  palabras inglesas de ese bloque, que no son nombres, sí se traducen (`translated_name_ids`): `Market Manager` y
  `main character`, el nombre que sale en lugar del jugador mientras no tiene uno (una partida sin nombre).
- `kept_original`: lo que no son palabras inglesas y se queda tal cual: las cadenas en blanco, las que, sin los
  espacios de los lados, son el nombre de un Digimon (la línea de quien habla en el curling), las letras sueltas, los
  restos en japonés que el US no muestra, las siglas `HP`, `MP` y `Bits` y el nombre propio `Overdell`.

Las reglas están al principio de `scripts/translation.sh` (`kept_english_blocks`, `kept_original_blocks`,
`kept_name_block` y `kept_original_ids`). Un idioma con otra escritura puede escribir los nombres a su manera (el
coreano los escribe en hangul y el japonés en katakana; el chino los deja en inglés): en cuanto su `main.yaml` tiene un nombre de
`main.digimon_names` que no es un título, esos nombres y las cadenas que son solo un nombre cuentan como traducidos
para ese idioma y dejan de estar guardados (`own_names`).

## Cómo se traduce

1. Extraer el disco (`dwx extract`) y apuntar `DWE_CONTENT` a la carpeta de contenido.
2. Ver qué falta: `scripts/translation.sh missing main` escribe cada id con su inglés **en la terminal**. Nunca se
   pega ese inglés en el repositorio. `missing story_049` da una tabla de la historia y `missing story`, todas.
3. Escribir el español en `text/es/<fuente>.yaml` siguiendo el [glosario](spanish-glossary.md) y las reglas de abajo.
4. `scripts/translation.sh lock` apunta en el `.sources.lock` el SHA-1 del inglés de cada id nuevo.
5. `scripts/translation.sh check` tiene que acabar con `0 problems`.

`status` da la cobertura por fuente (las tablas de la historia suman una fila; `status story` da una por tabla) y
`long` lista las traducciones más largas que su inglés, para revisar que caben. En la historia, `long` lista en su
lugar las líneas de más de 30 columnas (o más anchas que la línea más ancha de su inglés) y los textos con más
líneas que su inglés y que el cuadro (4 filas); las etiquetas no cuentan. Los largos son columnas en pantalla: un
carácter chino, japonés o coreano cuenta dos. Otro idioma funciona igual con `--lang <código>`
([Un idioma nuevo](#un-idioma-nuevo)).

### Reglas del texto

- Español latinoamericano neutro, tuteando al jugador.
- Cuando el inglés del original está roto, se traduce la intención y se conservan todos los códigos de control
  (decisión del responsable de diseño, 2026-10-20; las demás decisiones están en el [glosario](spanish-glossary.md)).
- Las etiquetas y marcadores se copian tal cual y no cambian de número: las etiquetas de
  [Textos y fuente](../formats/texts.md#etiquetas) (`{color:N}`, `{br}`, `{wait}`...) y los números `{0}`, `{1}`
  que el original escribía con `%d`, que sí pueden cambiar de orden. `check` y `dwx validate` lo comprueban.
- Las cadenas con relleno de espacios (los nombres de mapa centrados en 24 caracteres) mantienen el ancho.
- Las cadenas que son una línea de un cuadro (las de la pesca o el curling) no pasan del largo de las líneas del
  original, unos 29 caracteres.
- El diálogo de la historia se parte en líneas con `\n` como el original: hasta 30 columnas por línea sin contar
  las etiquetas, y a ser posible las mismas líneas que el inglés (el cuadro tiene 4 filas). La primera línea que nombra
  a quien habla (`{color:7}<nombre> {color:1}\n`) se copia igual, con el espacio antes de la etiqueta.

### Comprobar que cabe

El motor dibuja estos textos con sus propias fuentes (Noto Sans en RmlUi), que tienen tildes, `ñ`, `¿` y `¡`; la
fuente del original no los tiene, así que ningún texto traducido puede pasar por ella. Con `--lang es` se revisa con
capturas que caben:

- los nombres de objeto en el inventario y en el cuadro de «objeto encontrado» (unos 96 px en el original);
- las descripciones de objeto (una línea);
- el texto de fin de batalla (dos líneas de unos 156 px: «soltó», «se lastimó», «¡aprendida!» y las órdenes);
- los gritos y nombres de las órdenes de batalla;
- los nombres de técnica en el menú de órdenes de batalla y en el cuadro de fin de batalla;
- los nombres de mapa del transporte de `species_e` y los temas de la máquina de música (25 caracteres en el original);
- las páginas de ayuda del curling y los cuadros de la pesca (líneas de unos 244 px);
- los nombres y descripciones de las medallas, los títulos del domador y los menús de la tarjeta de memoria.

Las cadenas que el original parte en columnas por posición de píxel (`kar.menu.0000`, `main.game_menu_year_day.0000`,
`main.main_menu_save_labels.0001`, `main.script_menu_trade_label.0000` y las etiquetas de las vistas de estado,
técnicas y jugador) se traducen
conservando el ancho en caracteres de cada columna, pero solo tienen sentido con el dibujo del original. La UI del
motor usa sus propias claves de localización para esas pantallas, y estas traducciones quedan solo como respaldo
(decisión del responsable de diseño, 2026-10-20).

## Cómo se actualiza

El `.sources.lock` guarda, por cada id traducido, el SHA-1 en hexadecimal del texto inglés del que salió (los bytes
UTF-8 del texto tal como está en `text/en`, con sus etiquetas). Si una extracción nueva cambia ese inglés, el hash ya no coincide y la
traducción queda **obsoleta**:

1. `scripts/translation.sh stale` lista los ids obsoletos con su inglés nuevo.
2. Se revisa el español de cada uno.
3. `scripts/translation.sh accept <id>...` apunta el hash nuevo.

`lock` nunca cambia el hash de un id que ya está en el lock, así que una traducción obsoleta solo deja de serlo con
`accept`. `dwx validate` lee el mismo lock y avisa de lo obsoleto.

## Texto de los scripts

El diálogo de los scripts de eventos (`show_textbox` y `set_selection`) sale en `text/en/story_<n>.yaml`, una tabla
por script, desde que los scripts pasaron a Lua; se traduce con el mismo flujo en `text/es/story_<n>.yaml`. Los
nombres de los personajes que hablan o se nombran se quedan en inglés, letra por letra.

## Carteles de los mapas

Los carteles pintados en los fondos (la arena, los postes, las tiendas...) también se traducen. Sus textos son
claves de `localization/<idioma>/signs.yaml` del paquete `game`, como el resto de textos del motor; lo que cambia es
dónde y cómo se dibujan, que va en un overlay por mapa. Un cartel que el original ya tiene en inglés y solo hace falta
en español va en `content/packs/es-translation/signs/es/<mapa>.yaml`. Cómo encontrar un cartel y escribir su
overlay: [guía de carteles](signs.md).

## Un idioma nuevo

Además del inglés y el español, el juego se está traduciendo al portugués de Brasil (`pt`), francés (`fr`), alemán
(`de`), ruso (`ru`), chino simplificado (`zh`), coreano (`ko`) y japonés (`ja`). Estos idiomas nuevos todavía no
tuvieron una revisión de hablantes nativos: si juegas en uno de ellos y un texto suena mal, no se entiende o no cabe,
tu revisión es bienvenida. Abre un issue en el repositorio del proyecto con el idioma, la pantalla o la línea, el texto
como está y cómo lo escribirías, o manda el arreglo como pull request a las tablas del idioma. Las decisiones de
términos de cada idioma están en su glosario:
[Deutsch](de-glossary.md), [Français](fr-glossary.md), [Português (Brasil)](pt-glossary.md),
[Русский](ru-glossary.md), [简体中文](zh-glossary.md), [한국어](ko-glossary.md), [日本語](ja-glossary.md).

Sumar un idioma con código `<código>` lleva estas piezas, todas siguiendo las del español.

1. **Los textos del motor**: `content/packs/game/localization/<código>/`, con una copia de cada tabla de
   `localization/en/` (`common.yaml`, `pause_menu.yaml`, `shop.yaml`, `first_run.yaml`...) con las mismas claves y
   los argumentos `{nombre}` tal cual. Una clave que el idioma todavía no tiene se muestra en inglés, clave por clave,
   así que una tabla se puede llenar de a poco.
2. **El nombre y la bandera del idioma**: `name` en su propio `language.yaml`, y `native.<código>` y `flag.<código>`
   en el `language.yaml` de **todos** los idiomas (la caja de idiomas los lista desde ahí). La imagen de la bandera va
   en `ui/flags/<código>`, que genera `scripts/art/make_flags.py`.
3. **Los textos del original**: un paquete delta `content/packs/<código>-translation` con id `<código>_translation`,
   como `es-translation`:

   ```text
   content/packs/<código>-translation/
   ├─ pack.yaml
   ├─ text/<código>/     main.yaml ... trn.yaml, story_NNN.yaml y el .sources.lock generado
   └─ signs/<código>/    los overlays de carteles por mapa, cuando un cartel necesita uno
   ```

   ```yaml
   id: <código>_translation
   version: 0.1.0
   layer: base
   priority: 2
   name:
     en: "<Language> translation"
     es: "Traducción al <idioma>"
   description:
     en: "The team's own <Language> translation of the original's texts, a delta over text/en. See docs/guides/translate.md."
     es: "Traducción propia del equipo al <idioma> de los textos del original, delta sobre text/en. Ver docs/guides/translate.md."
   dependencies:
     - id: base
       min_version: 0.1.0
   schemas:
     sign_overlay:
       - signs/*/*
   ```

   Nada registra el paquete: el juego monta cada carpeta de `content/packs` que tiene un `pack.yaml`, y un paquete de
   distribución cocina cada una en su propio `.pak` (`scripts/package/cook_team_packs.sh`). Sin el paquete, el diálogo
   del juego sale en inglés.
4. **El flujo de la traducción**: el mismo que para el español ([Cómo se traduce](#cómo-se-traduce) y
   [Cómo se actualiza](#cómo-se-actualiza)), con el idioma como opción:
   `scripts/translation.sh --lang <código> missing main`, `--lang <código> lock`, `--lang <código> check`... `--lang`
   elige `content/packs/<código>-translation` y `text/<código>/` (`DWE_TRANSLATION_LANGUAGE=<código>` hace lo mismo, y
   `--pack <carpeta>` apunta a otro paquete). `lock` necesita que exista el `pack.yaml` del paquete. Los ids que no se
   traducen ([Qué no se traduce](#qué-no-se-traduce)) son los mismos en todos los idiomas.
5. **La cobertura**: `scripts/translation.sh coverage` (con `DWE_CONTENT`) imprime, por idioma y por fuente, las
   cadenas, las traducidas, las que se dejan, las que faltan y las desactualizadas y la parte hecha; después, por
   idioma, su paquete y las claves que tiene de las del inglés en los textos del motor y en los del lanzador. Un idioma
   sin paquete sale con 0%. `coverage fr de` la limita a algunos idiomas.
6. **Los carteles**: las claves de `localization/<código>/signs.yaml` y, si hace falta, overlays por mapa en
   `content/packs/<código>-translation/signs/<código>/<mapa>.yaml` ([guía de carteles](signs.md)).
7. **El glosario**: `docs/guides/<código>-glossary.md`, con su copia en español en
   `docs/es/guides/<código>-glossary.md`, apunta las decisiones de términos del idioma como lo hace el
   [glosario del español](spanish-glossary.md): etapas, objetos, HP y MP, qué se hace con los nombres de los Digimon y
   con las técnicas.
8. **El lanzador**: `apps/launcher/ui/js/i18n/<código>.js` con las claves de `en.js`.
   `node apps/launcher/tools/i18n_check.mjs <código>` cuenta las claves que faltan (se muestran en inglés) y falla con
   claves que el inglés no tiene o con `{marcadores}` distintos.

En el primer arranque, la pantalla del disco pregunta el idioma si hay más de uno: los lista desde las carpetas de
`localization/` que tienen `language.yaml`, así que un idioma nuevo aparece en cuanto trae esa tabla. Con un solo
idioma no pregunta. Un idioma que solo tiene `language.yaml` muestra todos los demás textos en inglés y la revisión
lo deja pasar; en cuanto trae una segunda tabla, las debe todas.

### Largo de las líneas en chino, japonés y coreano

Todos los largos de arriba son columnas en pantalla, como las cuenta una terminal de ancho fijo: un carácter chino,
japonés o coreano (hanzi, kanji, kana, Hangul) y la puntuación de ancho completo (`。`, `、`, `！`, `？`) ocupan dos.
Una línea de la historia tiene 30 columnas, o sea 15 de esos caracteres, y el cuadro mantiene sus 4 filas. `long` mide
así, en la historia y en las tablas (una traducción más ancha que su inglés). El chino y el japonés no tienen
espacios: la línea se parte con `\n` donde un lector nativo haría una pausa, nunca dentro de una palabra.

### Fuentes

La fuente clásica compone las letras con acento agudo, grave o circunflejo, diéresis, cedilla y virgulilla, los
signos `¡ ¿ « » ‹ › „ ‚ ß º ª … ·` y las ligaduras `æ œ` ([Textos y fuente](../formats/texts.md#fuente)). El cirílico,
el chino, el japonés y el coreano los dibuja el motor con sus propias fuentes, que quien traduce no tiene que tocar:
[fuentes de la interfaz](../architecture/ui.md#fuentes). `uv run scripts/art/make_fonts.py check` lista cualquier
carácter de los textos que ninguna fuente dibuja; un carácter fuera de los juegos de las fuentes pide rehacerlas
(`make_fonts.py make`).

### Plurales y nombres

Una palabra que cambia con una cuenta toma las formas del idioma: el ruso escribe tres (`{n:день|дня|дней}`), el
francés y el portugués usan la primera con 0 y 1, y el chino, el japonés y el coreano tienen una (se escribe dos
veces, `{n:天|天}`); ver [Plurales](../architecture/localization.md#plurales). En la caja de nombres, el ruso, el
japonés y el coreano tienen páginas con sus propias letras antes de la latina
([nombres en todos los idiomas](../modules/apps/game.md#nombres-en-todos-los-idiomas)).
