# Control remoto del juego

Con `--control <socket>` o `--control tcp:<puerto>`, el juego abre un canal de depuración: una herramienta de fuera
(un servidor MCP, un test, un script, el editor de mapas) lee el estado y mueve el juego mientras corre, sin rutas
ni lectura de logs. Sin la opción no existe nada del canal y el juego no hace nada por él.

```sh
dwe-game --content <contenido> --start twna01 --new-game-chain seeded --seed 1 \
  --control /tmp/dwe.sock --control-paused
```

- `--control <socket>` escucha en ese socket Unix (Linux y macOS). Atiende a varios clientes a la vez (ver
  [Varios clientes](#varios-clientes)). En Windows no hay sockets Unix: el juego lo dice y no arranca.
- `--control tcp:<puerto>` (también `tcp:127.0.0.1:<puerto>` o `tcp:localhost:<puerto>`) escucha en ese puerto TCP
  de 127.0.0.1, con el mismo protocolo, en cualquier sistema. Solo en el loopback: otro host (`tcp:0.0.0.0:...`)
  se rechaza al arrancar, y un cliente que no venga de 127.x se cierra, porque el canal maneja el juego entero sin
  preguntar. `tcp:0` deja que el sistema elija un puerto libre; el log lo dice (`control: listening at
  tcp:127.0.0.1:<puerto>`).
- `--control-paused` deja la lógica parada antes de su primer tick, para que una sesión se repita tick a tick.
- Un juego con `--control` no lee ni escribe los ajustes del jugador, como un autotest. Las partidas van a la
  carpeta de `--saves`.
- Con `--autotest`, la ruta corre primero y el juego sigue abierto cuando acaba, para el canal.

## Protocolo

Una petición JSON por línea, una respuesta JSON por línea:

```json
{"id": 1, "cmd": "press", "keys": ["right"], "ticks": 30}
{"id": 1, "ok": true, "keys": ["right"], "from": 90, "ticks": 30, "tick": 120}
```

La respuesta repite el `id` (cualquier valor JSON) con `"ok": true` y lo pedido, o con `"ok": false` y un
`"error"` que dice qué falla. Las respuestas a `press`, `advance` y `screenshot` llegan cuando han corrido los
ticks o se ha escrito la imagen.

| Orden | Campos | Qué hace |
|---|---|---|
| `status` | | Responde `tick`, `paused`, `mode` (`title`, `movie`, `interview`, `field`, `battle`), `map`, `held` (las teclas mantenidas), `pending` (los ticks que quedan por avanzar), `textures` (las ranuras ocupadas de la tabla bindless), `gpu_bytes` (la memoria de GPU que tiene el asignador), `overlay` (si la capa de depuración del mapa está encendida), `clients` (los clientes conectados) y `preview` (el mapa con una [vista previa](#vista-previa-de-un-parche), o `null`). `textures` y `gpu_bytes` sirven para ver fugas en una sesión larga; `overlay` deja que el mapper muestre el estado real al conectar |
| `state` | `path` | La línea de la [traza de estado](traza-de-estado.md) de ahora, o la parte que nombra el camino: `player`, `player.x`, `battle.fighters.0.hp`, `npcs.id=6` (el NPC de ese id) |
| `press` | `keys`, `ticks` (6) | Las teclas abajo exactamente esos ticks y luego arriba. Con la lógica parada, corre esos ticks ella misma. Dos `press` seguidos se encadenan tick a tick |
| `hold` | `keys` | Las teclas abajo desde el siguiente tick hasta `release` |
| `release` | `keys` | Suelta esas teclas, o todas sin `keys` |
| `pause` | `alpha` (opcional, de 0 a 1) | Para la lógica: no corre ningún tick, pero el juego sigue dibujando. Con `alpha`, las imágenes se dibujan esa fracción de tick después del último (el movimiento suave entre ticks) en vez de donde lo deja el reloj, así que las capturas entre dos ticks salen siempre iguales; `resume` o un `pause` sin `alpha` lo quitan |
| `resume` | | La lógica vuelve a su ritmo |
| `advance` | `ticks` | Corre esos ticks. Con la lógica parada, exactamente esos y ninguno más, a 4 por frame; si no, espera a que pasen |
| `screenshot` | `path` | Guarda el siguiente frame dibujado como PNG en esa ruta |
| `script` | `code` | Corre una línea de los scripts del juego, como la consola (`give_item`, `evolve`, `train`...) |
| `place` | `text` | `place("<quién> <x> <z> <rotación>")`, en unidades del original |
| `battle` | `target` | `battle("<objetivo>")`, por ejemplo `story:2` |
| `load` | `save` o `scene` | Carga un fichero de partida, o empieza una partida nueva en un mapa (`twna01` o `twna01,2` con la salida) |
| `trace` | `path` | Escribe la traza de estado en ese fichero, tick a tick; sin `path` deja de escribirla |
| `quit` | | Cierra el juego |
| `rescan` | | Vuelve a listar los ficheros de los packs montados, para ver los que una herramienta creó o borró con el juego en marcha |
| `reload_map` | `exit` (0-9) | `rescan` y vuelve a leer y entrar en el mapa actual (ver [Edición de mapas en vivo](#edición-de-mapas-en-vivo)). Responde `map` y `exit` (`null` sin `exit`) |
| `story_state` | `path` | El estado de la historia: `map`, `triggers` (los activos, de menor a mayor), `variables` (las distintas de 0, por número: `{"12": 3}`), `script` (`busy`, `map_script`, `sections` que corren: `script`, `section` y `lines`, las líneas del código de la sección por las que va, de la más externa a la de la orden que espera), `stage` (la etapa del candado de historia) y `camera` (`eye`, `target`, `focal_length`, `background`, `viewport`, `scale`, `scroll`, en espacio de mundo; `null` sin mapa). Con `path`, solo esa parte: `variables.12`, `script.sections.0` |
| `overlay` | `on` (`true`/`false`) | Enciende o apaga la [capa de depuración del mapa](#capa-de-depuración-del-mapa); sin `on` la cambia. Responde `overlay` |
| `live` | | Dónde está cada uno, en una línea corta para consultarla varias veces por segundo (ver [Estado en vivo](#estado-en-vivo)) |
| `preview_map` | `map`, `patch`, `pack` | Enseña el mapa con un parche encima sin guardarlo (ver [Vista previa de un parche](#vista-previa-de-un-parche)). Responde `map`, `applied` (`collision` o `reentered`) y `tick` |
| `preview_clear` | | Quita la vista previa. Responde `map` (vacío sin partida) y `cleared` (`false` si no había ninguna); nunca es un error |
| `log_tail` | `since` | Los avisos y errores del log desde el número `since` (sin él, todos los que guarda): `next` (el número del último) y `entries` (`seq`, `level` (`warning` o `error`), `text`). Guarda los 256 últimos, solo con `--control` |
| `story_set` | `kind` (`trigger` o `var`), `number`, `value` | Escribe un disparador (`value` `true`/`false` o 1/0) o una variable (0-255) de la historia, como `story_set` en la consola. No mientras corre un script de historia. Responde `kind`, `number` y `value` |
| `story_run` | `script`, `section`, `code`, `pack` | Arranca una sección de historia en el frame siguiente, como si la pidiera el mapa. Con `code` (el texto de un parche de historia de ese script con esa sección) corre ese código una vez en lugar del de los packs, sin guardarlo; `pack` dice con qué pack se carga (sin él, el que da el script). No mientras corre un script de historia. Responde `script`, `section` y `sent` (si llevaba `code`) |

Las teclas van por su nombre (`keyNamed`): `a` a `z`, `0` a `9`, `return`, `escape`, `backspace`, `tab`, `space`,
`left`, `right`, `up`, `down`, `left_shift`, `right_shift`; en una lista o como `"right+return"`. Entran en el
estado de la entrada al principio de cada tick, como los `hold` de una ruta, así que duran los ticks pedidos
aunque los frames vayan lentos.

Por ejemplo:

```json
{"id": 7, "cmd": "story_set", "kind": "trigger", "number": 12, "value": true}
{"id": 7, "ok": true, "kind": "trigger", "number": 12, "value": true}
{"id": 8, "cmd": "log_tail", "since": 40}
{"id": 8, "ok": true, "next": 42, "entries": [{"seq": 41, "level": "warning", "text": "..."}, {"seq": 42, "level": "error", "text": "..."}]}
```

El número de `story_set` va en `number` y no en `id`, que es el de la petición.

`story_run` es lo que usa «Reproducir desde aquí» del [mapper](../modulos/apps/mapper.md#secuencias): manda un parche
que coloca a cada uno donde estaría y corre el resto de la sección. Mientras corre, `story_state` dice por qué
línea va (`lines`), y el mapper la convierte en la orden del parche que la escribió:

```json
{"id": 8, "cmd": "story_run", "script": 12, "section": 81,
 "code": "return { script = 12, sections = { [81] = function() story.delay(5) end } }"}
```

## Varios clientes

El canal atiende hasta 4 clientes a la vez (el editor de mapas y un test, por ejemplo), en el socket Unix y en TCP.
Al quinto le escribe `{"id":null,"ok":false,"error":"control: too many clients (4 at most)"}` y lo cierra.

- Cada cliente recibe solo las respuestas a sus peticiones, también las que llegan más tarde (`press`, `advance`,
  `screenshot`). Si se va antes, esas respuestas se pierden, pero sus ticks corren igual.
- Cada cliente lleva un número que no se vuelve a dar, así que una respuesta tardía nunca llega a otro cliente que
  se conectó después.
- Las peticiones corren en el hilo del juego en el orden en que llegan. Las teclas mantenidas, los `press` y la
  pausa son del juego, compartidos por todos: un `release` sin teclas suelta también las de otro cliente, y un
  `pause` para la lógica para todos.
- Un cliente que se va a mitad de una línea, o que manda una línea de más de 1 MiB, se cierra solo él.

## Pausa y avance

Con la lógica parada, el juego solo corre los ticks que pide `advance` o `press`, y cada frame dura lo que esos
ticks: lo que se mueve con el tiempo del frame (animaciones, fundidos) se mueve con ellos. Así, la misma secuencia
de peticiones desde `--control-paused` con la misma `--seed` deja el mismo estado.

## Edición de mapas en vivo

El editor de mapas escribe en los packs (la capa de mods del usuario, o `content/packs/game` para el equipo) y
pide `reload_map` para ver el cambio en el juego que corre:

1. `rescan`: la lista de ficheros de los packs montados se rehace, así que un fichero nuevo (un `map/<id>/data`
   que antes solo tenía el pack base) gana ya. Los packs y su orden no cambian: un pack nuevo pide reiniciar. Si
   un fichero no da un id válido (un temporal a medio escribir), falla y queda la lista anterior.
2. Se vuelven a leer de los packs el documento del mapa actual con sus parches (`map/<id>/patch`, ver
   [Mapas](../formats/mapas.md)), `field/script_warps`, `field/rules` con las bandas de `field/triggers` y la tabla
   de mapas (`data/maps`). Los módulos de historia (`story/<n>`) y sus parches (`story/patches/*`) se descartan y se
   vuelven a leer de los packs en la siguiente sección que corra, así que un `story/<n>.lua` editado o un parche
   nuevo valen desde la recarga ([Historia](../modding/historia.md)). Las tablas de texto (`text/<idioma>`) del
   idioma en uso se vuelven a leer, así que una línea que trae o cambia un parche de historia se ve ya. Si el mapa
   ya no carga, o no tiene la salida pedida, falla antes de tocar nada.
3. Se vuelve a entrar en el mapa: sin `exit`, donde están el jugador y el compañero, sin fundido; con `exit`, por
   esa salida, como al cruzar una puerta. La historia, el inventario, el reloj y la sesión siguen igual: el juego
   hace lo mismo que al entrar en un mapa (corre la cabecera del script del mapa, que coloca sus NPCs).

No se recarga durante una batalla, en mitad de un cambio de mapa ni mientras corre una sección de historia (su
corrutina corre en los módulos ya cargados; una sección que solo espera a empezar, como la de una casilla de pisar,
empieza ya con los nuevos): la respuesta es un error y se puede pedir otra vez cuando acabe. La
línea `field: entered ...` del log dice qué packs parchearon el mapa (`patched by <pack>, ...`). De `field/rules`, los cambios de mapa, los
fundidos y las casillas de script usan los nuevos valores desde la recarga; lo que el campo construyó una vez con
ellos (los NPCs, la distancia del compañero) espera a una partida nueva. Las otras partes que leen la tabla de
mapas o los saltos de script por su cuenta (la música, el mapa de la isla) los leen al abrirse.

Si un paso falla (el YAML de un fichero, un parche que no aplica, la salida pedida), el error de la respuesta lleva
el texto del cargador con el fichero y la línea.

## Estado en vivo

`live` responde, en una línea:

```json
{"id": 3, "ok": true, "tick": 5120, "paused": false, "mode": "field", "map": "twna01", "busy": false,
 "preview": null, "log": 12, "player": {"x": 256, "y": 0, "z": 1285.5, "yaw": 1.5708},
 "partner": {"x": 180, "y": 0, "z": 1300, "yaw": 3.1416}, "npcs": [{"id": 11, "x": -839, "y": 0, "z": -2210, "yaw": 0}],
 "view": {"x": 0, "y": 16, "w": 320, "h": 240}}
```

- `mode` y `map` como en `status`; `busy`, si corre (o va a empezar) un script o una sección de historia; `preview`,
  el mapa con vista previa o `null`; `log`, el número del último aviso o error que guarda `log_tail` (0 si no hay
  ninguno), para pedir `log_tail` solo cuando cambia.
- `player`, `partner` y los `npcs` (por su id de script) están en [espacio de mundo](../architecture/world-space.md),
  como los guarda el campo (no en los ejes del original de la traza de estado): el suelo es el plano XZ, y una
  casilla de colisión es `columna = floor(x / 100) + 50`, `fila = floor(z / 100) + 50`. Sin partida son `null` y
  `[]`; durante una batalla siguen siendo los del campo, donde se pelea.
- `yaw` en radianes, como lo guarda el motor: el personaje mira hacia `(sin yaw, cos yaw)` en `(x, z)`, es decir,
  `yaw = 0` mira hacia +Z del mundo (filas crecientes de la rejilla) y un `yaw` positivo gira de +Z hacia +X
  (`assets::forwardOfYaw`). Para una flecha en la rejilla: `dir = (sin yaw, cos yaw)` en `(columna, fila)`.
- `view`: los píxeles del fondo que enseña la ventana: `x`, `y` es el de arriba a la izquierda
  (`MapCamera::scroll`) y `w`, `h` el tamaño (`viewportSize / scale`); `null` sin cámara de mapa.

Se construye directamente del campo, sin la traza ni JSON intermedio. Con 64 NPCs la respuesta ocupa unos 3,9 KB y
leer la petición y escribirla cuesta unos 0,3 ms en la compilación de depuración con sanitizadores de las pruebas
(`live stays cheap with a crowded map`); la de release es varias veces más rápida. A 5-10 consultas por segundo no
se nota en el frame.

## Vista previa de un parche

`preview_map` enseña un cambio del editor sin guardarlo:

```json
{"id": 4, "cmd": "preview_map", "map": "twna01", "patch": "collision:\n  cells:\n  - {column: 30, row: 28, value: 128}\n", "pack": "mi_mod"}
{"id": 4, "ok": true, "map": "twna01", "applied": "collision", "tick": 5200}
```

- `patch` es el texto de un parche de mapa (el mismo formato que `map/<id>/patch`, ver
  [Mapas](../formats/mapas.md#parches)); vacío no cambia nada. Se aplica en memoria encima de los datos y los
  parches de los packs del mapa, como un parche más, el último; en sus errores se llama `preview`. Con `pack`, si es
  un pack montado, el parche tiene sus derechos (los de la base para un pack base); si no, los de un mod.
- Se vuelve a entrar en el mapa donde están el jugador y el compañero, como `reload_map` sin `exit`, pero sin
  volver a listar los packs ni leer `field/rules` ni los saltos de script.
- Si el parche solo toca la colisión (o no toca nada) y la vista previa que sustituye (si hay) tampoco tocaba otra
  cosa, no se vuelve a entrar: la rejilla de colisión del campo se cambia en el sitio, con el minimapa. Esto
  deshace las casillas que un script cerró (`setImpassableRect`) hasta la siguiente entrada en el mapa, y una
  casilla de disparo bajo el jugador no salta hasta que la vuelva a pisar. `applied` dice `collision` o
  `reentered`.
- Se rechaza sin partida, si `map` no es el mapa que se ve (`the game shows <mapa>`), durante una batalla, en
  mitad de un cambio de mapa y mientras corre un script de historia. El parche se comprueba antes de aplicarlo; un
  error del YAML o de lo que nombra vuelve con su línea (`patch preview: line 3, column 5: ...`). Si falla, la
  vista previa anterior sigue.
- La vista previa es de su mapa: entrar en cualquier otro mapa la quita (la siguiente entrada usa los packs), y
  `reload_map` también, porque vuelve a leer los packs. `preview_clear` la quita y, si el juego está en ese mapa,
  vuelve a entrar desde los packs (o devuelve la colisión en el sitio si solo era de colisión).
- Nada de ella se escribe nunca en disco: las partidas no guardan documentos de mapa, solo el mapa en que se está.
- `reload_map` también se rechaza mientras corre un script de historia, con el mismo texto.

## Capa de depuración del mapa

Dibuja sobre el campo real, con la cámara del mapa:

- La rejilla de colisión: paredes en rojo y casillas de disparo por banda, con las bandas de `field/triggers` que usa
  el campo (las del original si ningún pack lo cambia): pisar (51-79) en amarillo, confirmar (80-109) en cian,
  salidas (110-119) en verde, el retrete (120) en morado y otros valores en gris. Cada valor lleva
  una etiqueta en el centro de sus casillas: `step 60`, `confirm 85`, `exit 2 -> <mapa>:<salida>`, `toilet`.
- Las salidas del mapa: dónde deja cada una al jugador (`in <n>`) y hacia dónde mira.
- Los NPCs del documento del mapa: su sitio y orientación con `npc <ranura> id <id de script>`, y sus puntos de ruta
  unidos por líneas.

La colisión es la que usa el campo ahora, con los cambios de los scripts. Se enciende y se apaga con F3 (acción
`debug_map_overlay`), con `map_overlay("on")`, `map_overlay("off")` o `map_overlay("toggle")` en la consola, y con
la orden `overlay` del canal. Se dibuja con la capa de ImGui, encima de la interfaz del juego, y sale en las
capturas de `screenshot`.

## El servidor MCP

Un servidor MCP de fuera del repositorio arranca el juego con este canal en su propio Xvfb y ofrece las mismas
herramientas que el que maneja el juego original en un emulador: `start`, `stop`, `status`, `actors`, `read`,
`press`, `hold`, `release`, `pause`, `resume`, `advance`, `screenshot`, y además `place`, `battle`, `script`,
`load`, `trace`, `reports` (los reportes F12) y `compare`, que juega un escenario del oráculo en los dos juegos y
devuelve solo las diferencias.

## Código

- `apps/game/src/remote_control.*`: las peticiones y el despachador, sin socket ni juego; sus pruebas están en
  `apps/game/tests/remote_control_tests.cpp`.
- `apps/game/src/control_json.*`: el JSON que lee las peticiones y elige partes del estado.
- `apps/game/src/control_socket.*`: el socket Unix o TCP con sus clientes, sin bloquear nunca el juego; sus
  pruebas están en `apps/game/tests/control_socket_tests.cpp`.
- `apps/game/src/control_log.*`: los avisos y errores que da `log_tail`.
- `apps/game/src/story_state_json.*`: el JSON de `story_state`.
- `apps/game/src/map_debug_overlay.*`: las formas de la capa de depuración del mapa, que dibuja
  `debug::DebugUi::setShapes` (`packages/engine/debug/include/dwe/debug/overlay_shapes.hpp`).
- `PlaySession::reloadMap`, `FieldScene::reloadMap`, `game::field::Field::reloadData`,
  `session::StoryLuaHost::reloadScripts` y `assets::VirtualFileSystem::rescan`: la recarga.
- `PlaySession::previewMap`, `FieldScene::previewMap`, `game::field::Field::setCollision`,
  `assets::mapPatchSections` y `assets::loadMapDocument` con parches de más: la vista previa.
- `PlaySession::live`: el estado en vivo.
- `apps/game/src/game.cpp`: une el canal al bucle del juego.
