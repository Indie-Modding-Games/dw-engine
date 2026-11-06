# Control remoto del juego

Con `--control <socket>` o `--control tcp:<puerto>`, el juego abre un canal de depuración: una herramienta de fuera
(un servidor MCP, un test, un script, el editor de mapas) lee el estado y mueve el juego mientras corre, sin rutas
ni lectura de logs. Sin la opción no existe nada del canal y el juego no hace nada por él.

```sh
dwe-game --content <contenido> --start twna01 --new-game-chain seeded --seed 1 \
  --control /tmp/dwe.sock --control-paused
```

- `--control <socket>` escucha en ese socket Unix (Linux y macOS). Atiende a un cliente a la vez; si se va, el
  siguiente puede conectarse. En Windows no hay sockets Unix: el juego lo dice y no arranca.
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
| `status` | | Responde `tick`, `paused`, `mode` (`title`, `movie`, `interview`, `field`, `battle`), `map`, `held` (las teclas mantenidas), `pending` (los ticks que quedan por avanzar), `textures` (las ranuras ocupadas de la tabla bindless) y `gpu_bytes` (la memoria de GPU que tiene el asignador): las dos últimas sirven para ver fugas en una sesión larga |
| `state` | `path` | La línea de la [traza de estado](traza-de-estado.md) de ahora, o la parte que nombra el camino: `player`, `player.x`, `battle.fighters.0.hp`, `npcs.id=6` (el NPC de ese id) |
| `press` | `keys`, `ticks` (6) | Las teclas abajo exactamente esos ticks y luego arriba. Con la lógica parada, corre esos ticks ella misma. Dos `press` seguidos se encadenan tick a tick |
| `hold` | `keys` | Las teclas abajo desde el siguiente tick hasta `release` |
| `release` | `keys` | Suelta esas teclas, o todas sin `keys` |
| `pause` | | Para la lógica: no corre ningún tick, pero el juego sigue dibujando |
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
| `story_state` | `path` | El estado de la historia: `map`, `triggers` (los activos, de menor a mayor), `variables` (las distintas de 0, por número: `{"12": 3}`), `script` (`busy`, `map_script`, `sections` que corren: `script` y `section`), `stage` (la etapa del candado de historia) y `camera` (`eye`, `target`, `focal_length`, `background`, `viewport`, `scale`, `scroll`, en espacio de mundo; `null` sin mapa). Con `path`, solo esa parte: `variables.12`, `script.sections.0` |
| `overlay` | `on` (`true`/`false`) | Enciende o apaga la [capa de depuración del mapa](#capa-de-depuración-del-mapa); sin `on` la cambia. Responde `overlay` |

Las teclas van por su nombre (`keyNamed`): `a` a `z`, `0` a `9`, `return`, `escape`, `backspace`, `tab`, `space`,
`left`, `right`, `up`, `down`, `left_shift`, `right_shift`; en una lista o como `"right+return"`. Entran en el
estado de la entrada al principio de cada tick, como los `hold` de una ruta, así que duran los ticks pedidos
aunque los frames vayan lentos.

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
2. Se vuelven a leer de los packs el documento del mapa actual, `field/script_warps`, `field/rules` y la tabla de
   mapas (`data/maps`). Si el mapa ya no carga, o no tiene la salida pedida, falla antes de tocar nada.
3. Se vuelve a entrar en el mapa: sin `exit`, donde están el jugador y el compañero, sin fundido; con `exit`, por
   esa salida, como al cruzar una puerta. La historia, el inventario, el reloj y la sesión siguen igual: el juego
   hace lo mismo que al entrar en un mapa (corre la cabecera del script del mapa, que coloca sus NPCs).

No se recarga durante una batalla ni en mitad de un cambio de mapa. De `field/rules`, los cambios de mapa, los
fundidos y las casillas de script usan los nuevos valores desde la recarga; lo que el campo construyó una vez con
ellos (los NPCs, la distancia del compañero) espera a una partida nueva. Las otras partes que leen la tabla de
mapas o los saltos de script por su cuenta (la música, el mapa de la isla) los leen al abrirse.

## Capa de depuración del mapa

Dibuja sobre el campo real, con la cámara del mapa:

- La rejilla de colisión: paredes en rojo y casillas de disparo por banda: pisar (51-79) en amarillo, confirmar
  (80-109) en cian, salidas (110-119) en verde, el retrete (120) en morado y otros valores en gris. Cada valor lleva
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
- `apps/game/src/control_socket.*`: el socket Unix o TCP, sin bloquear nunca el juego; sus pruebas están en
  `apps/game/tests/control_socket_tests.cpp`.
- `apps/game/src/story_state_json.*`: el JSON de `story_state`.
- `apps/game/src/map_debug_overlay.*`: las formas de la capa de depuración del mapa, que dibuja
  `debug::DebugUi::setShapes` (`packages/engine/debug/include/dwe/debug/overlay_shapes.hpp`).
- `FieldScene::reloadMap`, `game::field::Field::reloadData` y `assets::VirtualFileSystem::rescan`: la recarga.
- `apps/game/src/game.cpp`: une el canal al bucle del juego.
