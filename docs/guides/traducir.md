# Traducir los textos del original

El proyecto tiene su propia traducción al español de los textos del juego original. Esta guía dice dónde vive, cómo
se escribe y cómo se mantiene al día cuando cambia el inglés extraído. Los términos que se repiten están en el
[glosario de la traducción](glosario-es.md).

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
cifras. El id completo lleva delante la fuente (`main.item_names.0013`, ver [Textos y fuente](../formats/textos.md)).
En las tablas de la historia la clave es `text.<offset>`, el offset de la orden del script en cuatro cifras
hexadecimales (`"text.3b66"`, id completo `story_000.text.3b66`, ver [Scripts de eventos](../formats/scripts.md#lua)).
Una clave que falta se muestra en inglés.

## Qué no se traduce

No se escriben, porque serían copias del original. El `.sources.lock` los lista, para que la cobertura distinga «se
queda así a propósito» de «falta traducir»:

- `kept_english`: los nombres de los Digimon (`main.digimon_names`), que se quedan en inglés. Los nombres de las
  técnicas (`main.move_names`) sí se traducen (decisión del responsable de diseño, 2026-10-21). Los títulos en
  palabras inglesas de ese bloque, que no son nombres, sí se traducen: `Market Manager` (`translated_name_ids`).
- `kept_original`: lo que no son palabras inglesas y se queda tal cual: las cadenas en blanco, las que, sin los
  espacios de los lados, son el nombre de un Digimon (la línea de quien habla en el curling), las letras sueltas, los
  restos en japonés que el US no muestra, las siglas `HP`, `MP` y `Bits` y el nombre propio `Overdell`.

Las reglas están al principio de `scripts/translation.sh` (`kept_english_blocks`, `kept_original_blocks`,
`kept_name_block` y `kept_original_ids`).

## Cómo se traduce

1. Extraer el disco (`dwx extract`) y apuntar `DWE_CONTENT` a la carpeta de contenido.
2. Ver qué falta: `scripts/translation.sh missing main` escribe cada id con su inglés **en la terminal**. Nunca se
   pega ese inglés en el repositorio. `missing story_049` da una tabla de la historia y `missing story`, todas.
3. Escribir el español en `text/es/<fuente>.yaml` siguiendo el [glosario](glosario-es.md) y las reglas de abajo.
4. `scripts/translation.sh lock` apunta en el `.sources.lock` el SHA-1 del inglés de cada id nuevo.
5. `scripts/translation.sh check` tiene que acabar con `0 problems`.

`status` da la cobertura por fuente (las tablas de la historia suman una fila; `status story` da una por tabla) y
`long` lista las traducciones más largas que su inglés, para revisar que caben. En la historia, `long` lista en su
lugar las líneas de más de 30 caracteres (o más anchas que la línea más ancha de su inglés) y los textos con más
líneas que su inglés y que el cuadro (4 filas); las etiquetas no cuentan.

### Reglas del texto

- Español latinoamericano neutro, tuteando al jugador.
- Cuando el inglés del original está roto, se traduce la intención y se conservan todos los códigos de control
  (decisión del responsable de diseño, 2026-10-20; las demás decisiones están en el [glosario](glosario-es.md)).
- Las etiquetas y marcadores se copian tal cual y no cambian de número: las etiquetas de
  [Textos y fuente](../formats/textos.md#etiquetas) (`{color:N}`, `{br}`, `{wait}`...) y los números `{0}`, `{1}`
  que el original escribía con `%d`, que sí pueden cambiar de orden. `check` y `dwx validate` lo comprueban.
- Las cadenas con relleno de espacios (los nombres de mapa centrados en 24 caracteres) mantienen el ancho.
- Las cadenas que son una línea de un cuadro (las de la pesca o el curling) no pasan del largo de las líneas del
  original, unos 29 caracteres.
- El diálogo de la historia se parte en líneas con `\n` como el original: hasta 30 caracteres por línea sin contar
  las etiquetas, y a ser posible las mismas líneas que el inglés (el cuadro tiene 4 filas). La primera línea que nombra
  a quien habla (`{color:7}Coelamon {color:1}\n`) se copia igual, con el espacio antes de la etiqueta.

### Comprobar que cabe

El motor dibuja estos textos con sus propias fuentes (Noto Sans en RmlUi), que tienen tildes, `ñ`, `¿` y `¡`; la
fuente del original no los tiene, así que ningún texto traducido puede pasar por ella. Con `--lang es` se revisa con
capturas que caben:

- los nombres de objeto en el inventario y en el cuadro de «objeto encontrado» (unos 96 px en el original);
- las descripciones de objeto (una línea);
- el texto de fin de batalla (dos líneas de unos 156 px: «soltó», «se lastimó», «¡aprendida!» y las órdenes);
- los gritos y nombres de las órdenes de batalla;
- los nombres de técnica en el menú de órdenes de batalla y en el cuadro de fin de batalla;
- los nombres de mapa del transporte de Birdramon y los temas de la máquina de música (25 caracteres en el original);
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
overlay: [guía de carteles](carteles.md).
