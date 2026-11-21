# Aspectos

Un aspecto (skin) es el estilo de toda la interfaz: colores, fuentes, tamaños, marcos e imágenes de cada pantalla. El
juego trae dos, el original y el moderno, y el jugador los cambia en «Aspecto de la interfaz». Un mod añade otro con
una carpeta `ui/skins/<id>/`. El contrato completo está en [UI, aspectos](../architecture/ui.md#aspectos); dónde está
cada pantalla y cómo se prueba, en [editar menús](../guides/edit-menus.md).

## Un aspecto mínimo

```text
mi_aspecto/
├─ pack.yaml                         (layer: mods, api: "1.4", depende de game)
├─ localization/es/mi_aspecto.yaml   skin_name: "Mi aspecto"
└─ ui/skins/mi_aspecto/
   ├─ skin.yaml
   └─ theme.rcss
```

```yaml
# ui/skins/mi_aspecto/skin.yaml
id: mi_aspecto
name: mi_aspecto.skin_name
extends: modern                 # parte del moderno y cambia solo lo suyo
theme:
  - theme                       # relativo a la carpeta del skin → ui/skins/mi_aspecto/theme
cursor: cursor                  # opcional: PNG con alfa recto, se instala como cursor del sistema
cursor_hotspot: [1, 1]          # opcional: el punto que hace clic (x, y en píxeles desde el rincón sup-izq.)
screens:
  pause_menu:
    styles: [pause_menu/style]  # → ui/skins/mi_aspecto/pause_menu/style
```

Las hojas de `theme`, de `screens.<pantalla>.styles`, el `document` y el `cursor` se escriben **relativos a la carpeta
del skin** (`parseSkin` les pone `ui/skins/<id>/` delante); también vale la ruta absoluta completa, así que los
aspectos escritos antes de esta forma siguen funcionando. Las imágenes de las `sprite_sheets` **no** se relativizan:
nombran assets de las capas base.

Para no partir de cero, `python scripts/new_skin.py fork <id> --from <skin>` copia un aspecto existente a una carpeta
nueva, reescribe sus rutas y deja **una hoja `style.rcss` vacía por cada pantalla** que el origen no estilice;
`--flatten` disuelve la cadena `extends:` en un skin autocontenido.

El paquete de los tests [`tests/mods/ui_skin/test_contrast`](../modules/tests/mods.md) es un ejemplo completo.

## Maquetación

Algunas pantallas tienen dos disposiciones en el mismo documento y el mismo script: la del aspecto moderno y la del
original. Un aspecto elige con `layout:` en su `skin.yaml`, por pantalla; si extiende otro, hereda lo que no ponga:

```yaml
layout:
  pause_menu: {menu: original, box: original}
  main_menu: {menu: original}
```

`menu` es la disposición de la pantalla y `box`, si sus cajas se abren de golpe (`modern`) o crecen desde un contorno
gris como en el original (`original`). Otras son más finas, para que un aspecto que extiende el original se quede con
parte: `pages` (las páginas del compañero como pantallas completas, o `compact`, una sola página con el estado y las
técnicas), `picture` (la portada ensanchada con las cajas en sus sitios del original) y `heading` (el título de la
máquina de guardar también sobre su pregunta). Y algunas solo las usa por ahora el aspecto DevVago: `tabs: minimal` y
`medals: profile` en el menú de pausa, `pool: current` en el del compañero. La lista completa de pantallas y opciones está en
[UI, aspectos](../architecture/ui.md#aspectos); el documento recibe además la clase `layout-<opción>-<valor>` en su
`<body>`, para escribir reglas que solo valgan con una disposición.

## Reglas

- **El estilo, no el comportamiento.** Un aspecto no escribe `nav-*` ni scripts propios: la navegación y la lógica de
  cada pantalla son del juego. Un documento de reemplazo conserva los ids y enlaza el script de su pantalla.
- **Imágenes por id.** Las hojas de sprites y las imágenes nombran assets por su id; un mod no reparte imágenes del
  original.
- **El cursor del ratón es del sistema, no de RmlUi.** Con `cursor:` y `cursor_hotspot:` el aspecto instala una
  imagen como **cursor del SO** mientras esté puesto: queda siempre arriba (sobre menús modales incluidos) sin pelear
  con el apilado. Sin `cursor:`, la flecha del sistema se queda; cambiar de aspecto la restaura.
- **Cada pantalla en cada aspecto del juego.** La base oculta `modern` no es un aspecto propio: cada aspecto que el
  jugador puede elegir (original, pixel, devvago) estiliza cada pantalla que estiliza otro, él mismo o a través de un
  aspecto elegible que extiende, para que ninguna pantalla caiga en la tarjeta simple de la base. `dwe_ui_tests` lo
  comprueba.
- **Textos largos encogidos, no cortados.** Los valores cuyo largo depende del idioma o del jugador (una técnica, la
  línea de acceso, el lugar de una partida) se dibujan con `common/fit_text.lua`, así un aspecto con una columna
  estrecha los muestra enteros, medidos de nuevo cuando el jugador cambia de aspecto o de tamaño de interfaz. Un texto
  que un aspecto puede encoger va en su propio span dentro de su columna (`player-record-text` en `player-record`,
  `label-menu-text` en `label-menu`), así la columna conserva su ancho y se mide el texto, no la caja. `fit_text.fit`
  acepta un origen opcional para el encogido (`"100% 50%"` deja un texto alineado a la derecha pegado a su borde
  derecho, como las teclas del gráfico junto a las etapas). Un valor en una caja con relleno (la edad y el peso del
  compañero) se ajusta al sitio dentro de su relleno, leído de donde empieza su texto, así un valor alineado a la
  derecha que cabe nunca se encoge. Una caja cuyo contenido salta de línea parte también una palabra más larga que la
  caja (`word-break: break-word` en el nombre del lugar del minimapa), y un menú emergente de opciones crece con la
  más larga lejos de la caja de al lado (las opciones de objeto del compañero).
- **Cada letrero dentro de su caja.** En cada aspecto e idioma un letrero queda dentro de la caja dibujada a su
  alrededor y sin tocar los textos de al lado, igual en todas partes: los textos que dependen del idioma con
  `fit_text`, las filas con columnas fijas y un hueco entre ellas (el nombre en la tienda y su precio en una columna
  propia 24dp después), y las líneas de teclas encogidas al sitio que les deja su pie. Un aspecto mueve las cajas para
  que no se tapen (la descripción de la tienda se abre bajo una página llena de filas, las teclas de la caja del nombre
  van sobre la caja del nombre, lejos de Jijimon), y las etiquetas de nombre del cooperativo de jugadores que están
  juntos se apilan: la del jugador más cercano se queda en su sitio y cada otra sube sobre las que taparía, a 4dp.
  `dwe_ui_tests` carga cada pantalla en cada aspecto e idioma y falla
  con un texto fuera de su caja (`label_fit_tests.cpp`), y comprueba las pantallas más cargadas con sus textos más
  largos: las filas de la tienda, el minimapa, la caja del nombre, las etiquetas de nombre del cooperativo, el aviso y
  los valores de la sala online, el pie de las opciones, las teclas en el
  centro de la barra enmarcada de la búsqueda.
- **`image-color` tiñe también los decoradores.** RmlUi multiplica los decoradores de imagen de un elemento (y su
  ninepatch) por su `image-color`, no solo la imagen de un `<img>`: un `image-color` transparente que oculta la imagen
  del original oculta también un decorador dibujado en el mismo elemento. Oculta la imagen con `display: none` y dibuja
  en un elemento que la envuelva (la caja de vidas envuelve la imagen de cada vida en su `slot-N`, que marca una vida
  perdida con `empty`; los cursores del gráfico, las medallas y las cartas del menú de pausa envuelven la suya en un
  `.cursor-frame`).
- **Si algo falla**, el aspecto se deja fuera con el motivo en el log y el juego sigue con el que había.

## Probar mientras se edita

Con `--tuning`, el juego vigila los ficheros de la interfaz cada medio segundo:

- al guardar una hoja RCSS, un RML o un script de una pantalla, esa pantalla se carga otra vez;
- al guardar un `skin.yaml` (o `ui/ui_skin.yaml`), el juego lee otra vez todos los aspectos y carga de nuevo cada
  pantalla con el aspecto en uso. Si la edición deja el aspecto en uso roto (un `extends` que no existe, una hoja que
  falta), no se aplica: el log dice por qué y el aspecto anterior sigue hasta la siguiente edición.

Las imágenes de las hojas de sprites y un aspecto en una carpeta nueva siguen necesitando reiniciar el juego.
