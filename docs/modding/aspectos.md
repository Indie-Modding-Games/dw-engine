# Aspectos

Un aspecto (skin) es el estilo de toda la interfaz: colores, fuentes, tamaños, marcos e imágenes de cada pantalla. El
juego trae dos, el original y el moderno, y el jugador los cambia en «Aspecto de la interfaz». Un mod añade otro con
una carpeta `ui/skins/<id>/`. El contrato completo está en [UI, aspectos](../architecture/ui.md#aspectos); dónde está
cada pantalla y cómo se prueba, en [editar menús](../guides/editar-menus.md).

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

El paquete de los tests [`tests/mods/ui_skin/test_contrast`](../modulos/tests/mods.md) es un ejemplo completo.

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
parte: `pages` (las páginas del compañero como pantallas completas), `picture` (la portada ensanchada con las cajas
en sus sitios del original) y `heading` (el título de la máquina de guardar también sobre su pregunta). La lista completa de pantallas y opciones está en
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
- **Si algo falla**, el aspecto se deja fuera con el motivo en el log y el juego sigue con el que había.

## Probar mientras se edita

Con `--tuning`, el juego vigila los ficheros de la interfaz cada medio segundo:

- al guardar una hoja RCSS, un RML o un script de una pantalla, esa pantalla se carga otra vez;
- al guardar un `skin.yaml` (o `ui/ui_skin.yaml`), el juego lee otra vez todos los aspectos y carga de nuevo cada
  pantalla con el aspecto en uso. Si la edición deja el aspecto en uso roto (un `extends` que no existe, una hoja que
  falta), no se aplica: el log dice por qué y el aspecto anterior sigue hasta la siguiente edición.

Las imágenes de las hojas de sprites y un aspecto en una carpeta nueva siguen necesitando reiniciar el juego.
