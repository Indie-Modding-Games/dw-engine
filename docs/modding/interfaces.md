# Interfaces de la comunidad

El juego enseña sus pantallas (el HUD, el menú de pausa, la ficha del compañero...) con su propia interfaz. Una
**interfaz de la comunidad** es una carpeta que cambia cómo se ven algunas de esas pantallas, o todas, sin tocar el
juego: se copia la carpeta y se elige en el menú.

Hay dos cosas distintas que se pueden cambiar:

- El **aspecto** (colores, imágenes, tipografía) de la interfaz del juego: eso es un [aspecto](aspectos.md).
- Las **pantallas** en sí (qué enseñan, dónde y cómo): eso es una interfaz, lo que explica esta página.

## Instalar una interfaz

1. Copia la carpeta de la interfaz (por ejemplo `minimal/`) en la carpeta de interfaces del usuario:
   - Linux: `~/.local/share/indie-modding-games/dwe/ui/addons/`
   - Windows: `%APPDATA%\indie-modding-games\dwe\ui\addons\`
   - macOS: `~/Library/Application Support/indie-modding-games/dwe/ui/addons/`

   Si viene en un zip, descomprímelo ahí. Debe quedar `ui/addons/minimal/addon.yaml`.
2. Arranca el juego y abre **Opciones → Interfaz → Interfaz**. Elige la nueva con izquierda y derecha.
3. Cierra el menú: la interfaz se aplica al cerrarlo.

Un paquete (un mod) también puede traer interfaces en su carpeta `ui/addons/`.

### Si algo sale mal

- Si la interfaz elegida no se puede usar (está rota, es para otra versión del juego o le falta un archivo), vuelve
  la del juego y un aviso arriba de la pantalla dice por qué durante unos segundos.
- **Modo seguro:** mantén pulsada **Mayús** mientras arranca el juego, o arráncalo con `--safe-ui`. Se usa solo la
  interfaz del juego y no se lee la carpeta de interfaces del usuario. Sirve para volver a un juego que funciona y
  elegir otra interfaz.

## Hacer una interfaz

Una interfaz es una carpeta con un `addon.yaml` y los documentos de las pantallas que cambia. El ejemplo más pequeño
está en `examples/ui-addons/minimal/`: cambia solo el recuadro de pausa.

```text
minimal/
  addon.yaml
  pause_box/
    document.rml
    style.rcss
    logic.lua
```

### addon.yaml

```yaml
id: minimal              # minúsculas, cifras y guiones bajos; el nombre de la carpeta
name: Pausa mínima       # lo que se ve en Opciones → Interfaz
version: 1.0.0           # la versión de tu interfaz
ui_api: "0.10"           # la versión de la API de la interfaz con la que funciona
type: ui                 # una interfaz entera
screens:                 # el documento que enseña cada pantalla que cambias
  pause_box: pause_box/document
```

- `id` tiene que ser igual al nombre de la carpeta; si no, la interfaz no se ofrece.
- `ui_api` dice con qué versiones de la [API de la interfaz](ui-api.md) funciona. `"0.10"` quiere decir «0.10 o
  cualquier versión posterior antes de la 1.0». También se puede dar un rango: `">=0.10 <0.14"`. Si el juego tiene
  una versión fuera del rango, la interfaz no se usa y el aviso lo dice.
- `screens` nombra, para cada pantalla que cambias, su documento sin la extensión `.rml`, relativo a la carpeta. Las
  pantallas que no nombras siguen con el documento del juego. La lista de pantallas está en
  `content/packs/game/ui/addon.yaml` y en la [API de la interfaz](ui-api.md).
- El esquema del archivo está en `content/schemas/ui_addon.schema.json`.

### Los documentos

Cada documento es un `.rml` de [RmlUi](https://mikke89.github.io/RmlUiDoc/) con su estilo `.rcss` y su código Lua,
como los del juego (`content/packs/game/ui/`). Pueden enlazar los estilos comunes del juego (`ui/common/base.rcss`)
con una ruta relativa.

El código Lua de la pantalla lee el juego con la [API de la interfaz](ui-api.md): `ui.setting(...)`, las funciones
`partner`, `clock`, `bag`, `player`, `field` y `options`, los eventos de `hooks.on` y los manejadores `on_show`,
`on_tick`, `on_action`... Para pedirle algo al juego usa `ui.send(nombre, valor)`.

Para que tu editor complete los nombres, apunta el servidor de Lua (LuaLS) a
`content/schemas/ui_api/ui_api.d.lua`.

### Lo que una interfaz puede hacer

El código de una interfaz corre aislado:

- Lee todo lo que la API de la interfaz publica.
- Solo puede mandar al juego los comandos y opciones que la API declara (`ui.send`). Cualquier otro nombre lanza un
  error de Lua y el juego no lo recibe.
- No toca los archivos, la red ni el resto del juego: solo su propia carpeta.

### Probar mientras la haces

- Cambia los archivos y vuelve a elegir la interfaz en el menú (pasa a otra y vuelve): se vuelve a leer de sus
  archivos.
- Los errores de Lua y los avisos de carga salen en el registro del juego.
- Si la interfaz deja el juego en mal estado, arranca en modo seguro (Mayús).

## Ver también

- [API de la interfaz](ui-api.md): todo lo que una pantalla puede leer y mandar.
- [Aspectos](aspectos.md): cambiar solo el estilo de la interfaz del juego.
- [Paquetes](paquetes.md): repartir una interfaz dentro de un mod.
