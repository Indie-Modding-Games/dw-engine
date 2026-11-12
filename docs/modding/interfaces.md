# Interfaces de la comunidad

El juego enseña sus pantallas (el HUD, el menú de pausa, la ficha del compañero...) con su propia interfaz. Una
**interfaz de la comunidad** es una carpeta que cambia cómo se ven algunas de esas pantallas, o todas, sin tocar el
juego: se copia la carpeta y se elige en el menú.

Hay tres cosas distintas que se pueden cambiar:

- El **aspecto** (colores, imágenes, tipografía) de la interfaz del juego: eso es un [aspecto](aspectos.md).
- Las **pantallas** en sí (qué enseñan, dónde y cómo): eso es una **interfaz** (`type: ui`).
- Algo **más** encima de cualquier interfaz, como un reloj arriba de la pantalla: eso es un **complemento** (`type: plugin`).

Interfaces y complementos se instalan igual y se escriben igual; esta página explica los dos.

## Instalar una interfaz

1. Copia la carpeta de la interfaz (por ejemplo `minimal/`) en la carpeta de interfaces del usuario:
   - Linux: `~/.local/share/indie-modding-games/dwe/ui/addons/`
   - Windows: `%APPDATA%\indie-modding-games\dwe\ui\addons\`
   - macOS: `~/Library/Application Support/indie-modding-games/dwe/ui/addons/`

   Si viene en un zip, descomprímelo ahí. Debe quedar `ui/addons/minimal/addon.yaml`.
2. Arranca el juego y abre **Opciones → Interfaz → Interfaz**. Elige la nueva con izquierda y derecha. La fila solo
   sale cuando hay alguna interfaz que elegir.
3. Cierra el menú: la interfaz se aplica al cerrarlo.

Los complementos se encienden solos. La fila **Opciones → Interfaz → Complementos** los apaga o enciende todos juntos.

Un paquete (un mod) también puede traer interfaces en su carpeta `ui/addons/`.

### Si algo sale mal

- Si la interfaz elegida o un complemento no se pueden usar (está rota, es para otra versión del juego o le falta un archivo), vuelve
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
ui_api: "0.12"           # la versión de la API de la interfaz con la que funciona
type: ui                 # una interfaz entera
screens:                 # el documento que enseña cada pantalla que cambias
  pause_box: pause_box/document
```

Un complemento cambia `type` y, en lugar de `screens`, da la lista de sus documentos (`examples/ui-addons/clock/`):

```yaml
id: clock
name: Reloj arriba
version: 1.0.0
ui_api: "0.11"
type: plugin
documents:              # los documentos que enseña encima de la interfaz
  - document
```

- `id` tiene que ser igual al nombre de la carpeta; si no, la interfaz no se ofrece.
- `ui_api` dice con qué versiones de la [API de la interfaz](ui-api.md) funciona. `"0.12"` quiere decir «0.12 o
  cualquier versión posterior antes de la 1.0». También se puede dar un rango: `">=0.12 <0.14"`. Si el juego tiene
  una versión fuera del rango, la interfaz no se usa y el aviso lo dice.
- `screens` nombra, para cada pantalla que cambias, su documento sin la extensión `.rml`, relativo a la carpeta. Las
  pantallas que no nombras siguen con el documento del juego. La lista de pantallas está en
  `content/packs/game/ui/addon.yaml` y en la [API de la interfaz](ui-api.md).
- `documents` (solo complementos) nombra sus documentos igual. Se enseñan siempre, encima de la interfaz y sin
  quitarle el foco al menú; el propio documento decide cuándo esconder lo suyo (el reloj se esconde cuando el tiempo
  del juego está parado). Su `.rcss` lo coloca: `position: absolute` donde quieras, con `pointer-events: none` para
  que el ratón siga llegando al juego. El minimapa se esconde mientras algo le queda encima, así que deja libres las
  esquinas que usa el HUD de cada aspecto (el reloj de ejemplo va arriba en el centro).
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
- No toca los archivos, la red ni el resto del juego: el Lua de la interfaz no tiene `io`, `os`, `require` ni
  `load`, y solo se cargan los documentos, estilos y scripts de su propia carpeta.

### Las pantallas que se pueden cambiar

Esta versión cubre los menús, el HUD y los avisos. Estas pantallas tienen en la [API de la interfaz](ui-api.md) todo
lo que leen y mandan:

| Pantalla | Qué es |
|---|---|
| `hud` | El HUD del campo: reloj, medidores, necesidades, minimapa |
| `pause_menu` | El menú de pausa y las opciones |
| `partner_menu` | La ficha del compañero y la bolsa |
| `skin_picker` | La pregunta del aspecto en el primer arranque |
| `pause_box` | El recuadro de pausa |
| `field_item_notice` | El aviso de un objeto en el suelo o en un cofre |
| `award_notice` | El aviso de un premio |
| `fishing_notice` | El aviso de lo pescado |
| `save_indicator` | El indicador de guardado |
| `money_box` | El cuadro del dinero |
| `lives_box` | El cuadro de vidas |
| `ui_notice` | El aviso de que una interfaz no se pudo usar |

El resto de pantallas de `content/packs/game/ui/addon.yaml` también se pueden nombrar en `screens`, pero lo que leen
todavía no está en el contrato y puede cambiar entre versiones del juego.

### Versiones

La API de la interfaz lleva su propia versión (la tabla está en la [API de la interfaz](ui-api.md#versiones)). Una
versión menor (0.12 → 0.13) solo añade nombres: una interfaz hecha para la 0.12 sigue funcionando. Una mayor puede
quitar o cambiar nombres. Pon en `ui_api` la versión con la que la probaste: `"0.12"` acepta esa y las menores
siguientes, y el juego no la usará con una versión mayor que no conoce.

### Probar mientras la haces

- Cambia los archivos y vuelve a elegir la interfaz en el menú (pasa a otra y vuelve): se vuelve a leer de sus
  archivos. Desde la consola de desarrollo, `ui_addon("minimal")` la pone sin abrir el menú.
- Los errores de Lua y los avisos de carga salen en el registro del juego.
- Si la interfaz deja el juego en mal estado, arranca en modo seguro (Mayús).

## Repartir una interfaz

Comparte la carpeta tal cual, o un zip con la carpeta dentro (`minimal.zip` → `minimal/addon.yaml`). Quien la use la
copia en su carpeta de interfaces, como en [Instalar una interfaz](#instalar-una-interfaz). También puede ir dentro
de un mod, en su carpeta `ui/addons/<id>/`: se ofrece cuando el mod está puesto.

Antes de repartirla:

- Que el `id` sea el nombre de la carpeta y no choque con el de otra interfaz conocida.
- Que `ui_api` sea la versión con la que la probaste.
- Que funcione con los tres aspectos del juego (Original, Moderno y Pixel art): la interfaz trae sus propios estilos,
  pero los documentos del juego que no cambias siguen con el aspecto elegido.
- Que no lleve material de otros juegos (imágenes, textos o música) que no puedas repartir.

## Ver también

- [API de la interfaz](ui-api.md): todo lo que una pantalla puede leer y mandar.
- [Aspectos](aspectos.md): cambiar solo el estilo de la interfaz del juego.
- [Paquetes](paquetes.md): repartir una interfaz dentro de un mod.
