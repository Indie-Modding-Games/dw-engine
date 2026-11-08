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
  - ui/skins/mi_aspecto/theme   # hojas para todas las pantallas
screens:
  pause_menu:
    styles: [ui/skins/mi_aspecto/pause_menu/style]   # y por pantalla
```

El paquete de los tests [`tests/mods/ui_skin/test_contrast`](../modulos/tests/mods.md) es un ejemplo completo.

## Reglas

- **El estilo, no el comportamiento.** Un aspecto no escribe `nav-*` ni scripts propios: la navegación y la lógica de
  cada pantalla son del juego. Un documento de reemplazo conserva los ids y enlaza el script de su pantalla.
- **Imágenes por id.** Las hojas de sprites y las imágenes nombran assets por su id; un mod no reparte imágenes del
  original.
- **Si algo falla**, el aspecto se deja fuera con el motivo en el log y el juego sigue con el que había.

## Probar mientras se edita

Con `--tuning`, el juego vigila los ficheros de la interfaz cada medio segundo:

- al guardar una hoja RCSS, un RML o un script de una pantalla, esa pantalla se carga otra vez;
- al guardar un `skin.yaml` (o `ui/ui_skin.yaml`), el juego lee otra vez todos los aspectos y carga de nuevo cada
  pantalla con el aspecto en uso. Si la edición deja el aspecto en uso roto (un `extends` que no existe, una hoja que
  falta), no se aplica: el log dice por qué y el aspecto anterior sigue hasta la siguiente edición.

Las imágenes de las hojas de sprites y un aspecto en una carpeta nueva siguen necesitando reiniciar el juego.
