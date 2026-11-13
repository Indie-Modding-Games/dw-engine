# Interfaces de la comunidad

El juego enseña sus pantallas (el HUD, el menú de pausa, la ficha del compañero...) con su propia interfaz. Una
**interfaz de la comunidad** es una carpeta que cambia cómo se ven algunas de esas pantallas, o todas, sin tocar el
juego: se copia la carpeta (o su `.zip`) y se elige en el menú.

Hay tres cosas distintas que se pueden cambiar:

- El **aspecto** (colores, imágenes, tipografía) de la interfaz del juego: eso es un [aspecto](aspectos.md).
- Las **pantallas** en sí (qué enseñan, dónde y cómo): eso es una **interfaz** (`type: ui`).
- Algo **más** encima de cualquier interfaz, como un reloj arriba de la pantalla: eso es un **complemento** (`type: plugin`).

Interfaces y complementos se instalan igual y se escriben igual; esta página explica los dos.

## Instalar una interfaz

1. Copia la carpeta o el `.zip` de la interfaz (por ejemplo `minimal/` o `minimal.zip`) en la carpeta de interfaces
   del usuario:
   - Linux: `~/.local/share/indie-modding-games/dwe/ui/addons/`
   - Windows: `%APPDATA%\indie-modding-games\dwe\ui\addons\`
   - macOS: `~/Library/Application Support/indie-modding-games/dwe/ui/addons/`

   Con una carpeta debe quedar `ui/addons/minimal/addon.yaml`. Un `.zip` no hace falta descomprimirlo: el juego lo
   lee tal cual (ver [Interfaces en un .zip](#interfaces-en-un-zip)).
2. Arranca el juego y abre **Opciones → Interfaz → Interfaz**. Elige la nueva con izquierda y derecha. La fila solo
   sale cuando hay alguna interfaz que elegir.
3. Cierra el menú: la interfaz se aplica al cerrarlo.

Los complementos se encienden solos. La fila **Opciones → Interfaz → Complementos** los apaga o enciende todos juntos.

Un paquete (un mod) también puede traer interfaces en su carpeta `ui/addons/`, como carpetas (no como `.zip`).

### Interfaces en un .zip

Un `.zip` en `ui/addons/` (con cualquier nombre, por ejemplo `Reloj-1.2.zip`) se lee sin descomprimirlo, como si fuera
la carpeta de su interfaz:

- Lleva el `addon.yaml` en la raíz del zip o dentro de su única carpeta (`minimal.zip` → `minimal/addon.yaml`). Los
  extras que añade macOS (`__MACOSX/`, `.DS_Store`) no cuentan.
- Su id es el `id` de su `addon.yaml`; el nombre del zip y el de su carpeta no importan.
- Si una carpeta y un `.zip` dan el mismo id, se usa la carpeta. Si dos `.zip` dan el mismo id, se usa el primero por
  nombre. El que queda fuera sale en el registro del juego.
- La interfaz de un zip, igual que la de una carpeta, solo lee sus propios archivos.
- Para cambiarla, sustituye el `.zip` y vuelve a arrancar el juego: los zip se miran al arrancar.

Por seguridad, un `.zip` se rechaza entero, sin leer nada de él, si:

- tiene rutas absolutas, con `..`, con `\` o con `:`, o más de 12 carpetas de profundidad;
- tiene enlaces u otros archivos especiales, o archivos cifrados;
- tiene dos veces el mismo archivo, o algún archivo que no puede ser un asset (por ejemplo con mayúsculas);
- pesa más de 20 MB, tiene más de 1000 entradas, ocupa más de 40 MB descomprimido o algún archivo se expande más de
  100 veces su tamaño (una «bomba zip»);
- no tiene `addon.yaml` en la raíz ni en su única carpeta, o su `addon.yaml` no tiene un `id` válido.

Son los mismos límites que usa el lanzador para los complementos de contenido. Un zip rechazado no se ofrece y su
motivo sale en el registro; si era la interfaz elegida, vuelve la del juego y el aviso dice por qué.

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

- `id` tiene que ser igual al nombre de la carpeta; si no, la interfaz no se ofrece. En un `.zip` manda el `id`.
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
`partner`, `clock`, `bag`, `player`, `field`, `options`, `battle`, `versus`, `story`, `dialog` y `naming`,
los eventos de `hooks.on` y los manejadores `on_show`, `on_tick`, `on_action`... Para pedirle algo al juego
usa las acciones de esos espacios (`bag.use(3)`, `battle.command(2)`, `dialog.answer(0)`) o
`ui.send(nombre, valor)`.

Para que tu editor complete los nombres, apunta el servidor de Lua (LuaLS) a
`content/schemas/ui_api/ui_api.d.lua`.

### Lo que una interfaz puede hacer

El código de una interfaz corre aislado:

- Lee todo lo que la API de la interfaz publica.
- Solo puede mandar al juego los comandos y opciones que la API declara (`ui.send`). Cualquier otro nombre lanza un
  error de Lua y el juego no lo recibe. Las acciones de los espacios (`bag.use`, `battle.command`, `versus.step`...)
  mandan con el mismo filtro: lo que no pasaría con `ui.send` tampoco pasa por ellas.
- Las órdenes del combate solo pasan cuando el combate las atiende, igual que desde la interfaz del juego: las de la
  tira, la carga del golpe final y abrir la bolsa en la fase `fight`; cerrar la bolsa con la bolsa abierta; las de la
  caja del final en la fase `end`. Fuera de su momento, `ui.send` lanza un error y la acción (`battle.command`...)
  devuelve `false` y por qué.
- Las órdenes de las cajas de la historia solo pasan mientras su caja espera al jugador: `story_advance` y
  `story_cancel` con una página o una elección esperando, `story_choose` con una elección, las de `naming_*` con la
  caja del nombre abierta y `dialog_answer` con el diálogo abierto. Fuera de su momento, `ui.send` lanza un error y
  la acción (`story.advance`...) devuelve `false` y por qué.
- No toca los archivos, la red ni el resto del juego: el Lua de la interfaz no tiene `io`, `os`, `require` ni
  `load`, y solo se cargan los documentos, estilos y scripts de su propia carpeta (o de su `.zip`).

### Las pantallas que se pueden cambiar

Esta versión cubre los menús, el HUD, los avisos, el combate, el versus en línea y las cajas de la historia.
Estas pantallas tienen en la [API de la interfaz](ui-api.md) todo lo que leen y mandan:

| Pantalla | Qué es |
|---|---|
| `hud` | El HUD del campo: reloj, medidores, necesidades, minimapa |
| `pause_menu` | El menú de pausa y las opciones |
| `partner_menu` | La ficha del compañero y la bolsa |
| `skin_picker` | La pregunta del aspecto en cada arranque, tras la caja de idiomas |
| `pause_box` | El recuadro de pausa |
| `field_item_notice` | El aviso de un objeto en el suelo o en un cofre |
| `award_notice` | El aviso de un premio |
| `fishing_notice` | El aviso de lo pescado |
| `save_indicator` | El indicador de guardado |
| `money_box` | El cuadro del dinero |
| `lives_box` | El cuadro de vidas |
| `ui_notice` | El aviso de que una interfaz no se pudo usar |
| `battle` | El combate: luchadores, tira de órdenes, golpe final, apertura, números y caja del final |
| `tournament_match` | Lo que un combate de copa enseña encima: el nombre, los atributos, el fin de tiempo, el daño |
| `versus` | El versus en línea: la sala, el código, la partida y los compañeros, el combate, el marcador |
| `story_textbox` | El cuadro de texto de la historia: lo que dicen los personajes, las preguntas y sus respuestas |
| `dialog` | El diálogo de guardar: una pregunta de sí o no, o un aviso, sobre las partidas guardadas |
| `naming_box` | La caja que pide un nombre, del jugador o del compañero |

El resto de pantallas de `content/packs/game/ui/addon.yaml` también se pueden nombrar en `screens`, pero lo que leen
todavía no está en el contrato y puede cambiar entre versiones del juego.

### El combate

Todo el combate se lee con el espacio `battle` ([API de la interfaz](ui-api.md#battle)): `battle.state()` (la fase,
la bolsa, las cuentas, si es un combate de copa), `battle.fighters()`, `battle.commands()`, `battle.finisher()`,
`battle.intro()` (la apertura y el cartel), `battle.numbers()`, `battle.hp_bars()`, `battle.shout()`,
`battle.result()` (la caja del final) y `battle.match()` (lo de la copa). Los eventos dicen qué redibujar:
`battle.started` y `battle.ended` al empezar y acabar, `battle.phase` al cambiar de fase, `battle.changed` cuando
cambian los luchadores, la tira o el golpe final, y uno por cada parte que se mueve sola (`battle.intro`,
`battle.numbers`, `battle.overlays`, `battle.shout`, `battle.strip_moved`, `battle.match`).

Las reglas son del juego: qué órdenes hay, cuáles se pueden dar, cuánto cuestan, cuándo está listo el golpe final.
La interfaz solo las enseña y pide con las acciones:

- `battle.command(orden)` con el `command` de una entrada de `battle.commands()`; `battle.charge()` en cada pulsación
  mientras dura el aura del golpe final (`battle.finisher().charging`); `battle.open_bag()` abre la bolsa (la pantalla
  `partner_menu`) y `battle.close_bag()` la cierra.
- La caja del final la lleva la interfaz, al ritmo de `battle.state().frame_seconds`: cada punto o bit que cuenta
  pide su sonido con `battle.end_count()`, el último bit con `battle.end_counted()`, y cuando la caja ha terminado
  **tiene que** llamar a `battle.close()`, o el combate no se cierra.
- Los eventos llegan también con la pantalla oculta: dibuja solo si `ui.is_visible(document_name)`.

Un complemento puede enseñar algo más durante el combate sin cambiar su pantalla. `examples/ui-addons/battle_hp/`
pone abajo en el centro, donde ningún aspecto pone nada del combate, los PV de todos los luchadores mientras dura la
pelea, y se aparta mientras la bolsa está abierta: lee `battle.state()` y `battle.fighters()` y redibuja con
`battle.started`, `battle.ended`, `battle.phase` y `battle.changed`. No manda nada al juego.

### El versus

El versus en línea se lee con el espacio `versus` ([API de la interfaz](ui-api.md#versus)): `versus.state()` dice qué
vista se ve (`menu`, `host_setup`, `hosting`, `join_entry`, `joining`, `save_pick`, `lineup`, `waiting_lineup`,
`match`, `intermission`, `result`, `error`), dónde está el cursor y, en un combate, el reloj, el lado del jugador y
lo que enseña el escenario. Cada vista tiene su consulta: `versus.setup()`, `versus.connection()`, `versus.entry()`,
`versus.saves()`, `versus.lineup()`, `versus.fighters()`, `versus.log()`, `versus.results()`, `versus.score()` y
`versus.error()`. El cartel del principio de cada combate es el del combate (`battle.intro()`). Cada cambio llega
con el evento `versus.changed`.

El juego lleva cada vista: la interfaz pide los pasos con `versus.step(paso)` (`up`, `down`, `left`, `right`,
`confirm`, `cancel`, `menu`, `tab_previous`, `tab_next` y `finisher` para el golpe final) y `versus.pick(fila)`
para ir a una fila y confirmarla. El código de la sala se escribe con el teclado, no con la interfaz.

### La historia

Las cajas de la historia se leen con tres espacios ([API de la interfaz](ui-api.md#story)):

- `story.textbox()` es el cuadro de texto tal como lo publica el juego: si está abierto, si es el de las
  conversaciones o uno de tamaño propio, cuánto lleva abierto, quién habla (`speaker`), las filas ya traducidas y
  partidas en trozos de colores, dónde está el cursor y si enseña el aviso de pulsar. `story.choice()` da las
  opciones de una pregunta y `story.input()` lo que la caja espera del jugador: `"page"` (que pase la página),
  `"choice"` (que elija) o `""` (nada).
- `dialog.state()` y `dialog.options()` son el diálogo de guardar: su texto, la partida de la que habla y sus
  opciones (sí y no, o aceptar).
- `naming.state()` y `naming.cell(fila, columna)` son la caja del nombre: lo escrito, la rejilla de letras y el
  cursor.

Los eventos dicen qué redibujar: `story.changed` cuando cambia el cuadro, `story.text` con cada página nueva,
`story.input` cuando empieza o deja de esperar al jugador, `dialog.changed` y `naming.changed`. Llegan también con
la pantalla oculta: dibuja solo si `ui.is_visible(document_name)`.

Las reglas son del juego: cuándo espera una página, cuánto tarda en abrirse la caja, qué opciones tiene una pregunta,
qué letras admite un nombre. El juego también lee los botones y el teclado del jugador, así que una interfaz que solo
dibuja estas cajas no tiene que mandar nada. Si quiere llevarlas ella (con el ratón, con botones propios), pide con
las acciones: `story.advance()`, `story.choose(opción)` y `story.cancel()` mientras la caja espera;
`dialog.answer(opción)` y `dialog.cancel()`; `naming.write(letras)`, `naming.erase()` y `naming.accept()`.

Un complemento puede enseñar algo más junto a la historia sin cambiar sus cajas. `examples/ui-addons/story_log/`
guarda las últimas páginas que se han dicho, con quién las dijo, y las enseña a la derecha, entre el minimapa y las
barras del compañero, donde no tapa nada de ningún aspecto, mientras el cuadro de texto está abierto: lee `story.textbox()` y se redibuja con `story.text` y `story.changed`. No manda nada al juego.

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

Comparte la carpeta tal cual, o un zip con la carpeta dentro (`minimal.zip` → `minimal/addon.yaml`). Quien la use
copia la carpeta o el `.zip` en su carpeta de interfaces, como en [Instalar una interfaz](#instalar-una-interfaz). También puede ir dentro
de un mod, en su carpeta `ui/addons/<id>/`: se ofrece cuando el mod está puesto.

Antes de repartirla:

- Que el `id` sea el nombre de la carpeta y no choque con el de otra interfaz conocida.
- Si la repartes en `.zip`, que pase los límites de [Interfaces en un .zip](#interfaces-en-un-zip): sin enlaces ni
  cifrado, y con nombres de archivo en minúsculas.
- Que `ui_api` sea la versión con la que la probaste.
- Que funcione con todos los aspectos del juego (Original, Moderno, Pixel art...): la interfaz trae sus propios
  estilos, pero los documentos del juego que no cambias siguen con el aspecto elegido.
- Que no lleve material de otros juegos (imágenes, textos o música) que no puedas repartir.

## Ver también

- [API de la interfaz](ui-api.md): todo lo que una pantalla puede leer y mandar.
- [Aspectos](aspectos.md): cambiar solo el estilo de la interfaz del juego.
- [Paquetes](paquetes.md): repartir una interfaz dentro de un mod.
