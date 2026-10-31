# Control remoto del juego

Con `--control <socket>`, el juego abre un canal de depuración: una herramienta de fuera (un servidor MCP, un
test, un script) lee el estado y mueve el juego mientras corre, sin rutas ni lectura de logs. Sin la opción no
existe nada del canal y el juego no hace nada por él.

```sh
dwe-game --content <contenido> --start twna01 --new-game-chain seeded --seed 1 \
  --control /tmp/dwe.sock --control-paused
```

- `--control <socket>` escucha en ese socket Unix. Atiende a un cliente a la vez; si se va, el siguiente puede
  conectarse. Solo en sistemas con sockets Unix; en los demás el juego dice que no puede y no arranca.
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

Las teclas van por su nombre (`keyNamed`): `a` a `z`, `0` a `9`, `return`, `escape`, `backspace`, `tab`, `space`,
`left`, `right`, `up`, `down`, `left_shift`, `right_shift`; en una lista o como `"right+return"`. Entran en el
estado de la entrada al principio de cada tick, como los `hold` de una ruta, así que duran los ticks pedidos
aunque los frames vayan lentos.

## Pausa y avance

Con la lógica parada, el juego solo corre los ticks que pide `advance` o `press`, y cada frame dura lo que esos
ticks: lo que se mueve con el tiempo del frame (animaciones, fundidos) se mueve con ellos. Así, la misma secuencia
de peticiones desde `--control-paused` con la misma `--seed` deja el mismo estado.

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
- `apps/game/src/control_socket.*`: el socket Unix, sin bloquear nunca el juego.
- `apps/game/src/game.cpp`: une el canal al bucle del juego.
