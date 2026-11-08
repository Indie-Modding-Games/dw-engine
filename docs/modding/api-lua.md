# API de Lua

Lo que un mod puede hacer desde Lua: su script de entrada, los eventos del juego que escucha y las funciones que el
juego le publica. Cómo está hecho por dentro (sandbox, presupuestos, registro de funciones y eventos) está en
[script](../modulos/engine/script.md).

> **Versión 1.4.0 (2026-11-07).** Llegan el script de entrada de los mods, los eventos del juego y sus tablas
> versionadas (abajo), y `api` pasa a ser obligatoria en los paquetes de las capas `mods` y `user`. Nada de lo que ya
> existía cambia: un mod escrito para la 1.3 sigue funcionando si declara `api: "1.x"` o `api: "1.3"`.

## El script de entrada

Si un paquete trae `scripts/main.lua`, el juego lo ejecuta una vez por partida, al empezar una nueva o al cargar una,
en el entorno del paquete y antes de lanzar `game.new_game` o `game.loaded`. Es el sitio para registrar los
manejadores de eventos:

```lua
-- scripts/main.lua de mi_mod
local dias = 0

hooks.on("clock.new_day", function(t)
  dias = dias + 1
  print("día " .. t.day .. " del año " .. t.year .. "; llevo " .. dias)
end)
```

- Los scripts de entrada corren en el orden de la pila, del paquete más bajo al más alto.
- El entorno de cada paquete es suyo: sus globales son estrictas (todo con `local`), `print` escribe en el log como
  `mod <paquete>: ...` y las tablas de la API son de solo lectura ([entornos](../modulos/engine/script.md#entornos-de-los-mods)).
- Si el script falla, el error va al log con el nombre del paquete y la partida sigue.
- Lo que el script guarda en variables de Lua no pasa a la partida guardada: para eso están los disparadores y las
  variables de la historia ([capacidad](historia.md#capacidad)).

## Eventos del juego

`hooks.on(evento, función)` registra un manejador. Cada manejador recibe **una tabla** con los campos del evento y
`version`, la versión de la tabla: un campo nuevo sube la versión menor de la API y no cambia `version`; quitar o
cambiar uno sube las dos. Cada manejador recibe su propia copia de la tabla, así que cambiarla no afecta a los demás.
Un evento que el juego no publica es un error de `hooks.on`.

| Evento | Cuándo |
|---|---|
| `field.map_entered` | El jugador está en un mapa: al empezar la partida (`previous` es nil) y tras cada cambio de mapa |
| `field.map_left` | El jugador deja un mapa por otro (`next`), justo antes de `field.map_entered` del nuevo |
| `battle.started` | Empieza un combate. `kind`: `normal`, `tournament`, `arena` o `versus`; `enemies`, cuántos rivales; `first_enemy`, el id de la especie del primero; `scripted`, si lo abre la historia |
| `battle.ended` | Termina un combate y ya se dieron sus premios. `outcome`: `victory`, `fled` o `defeat`; `recruit`, el residente que se une a la ciudad, si hay uno |
| `evolution.evolved` | El compañero evoluciona de una especie (`from`) a otra (`to`), por id |
| `clock.new_day` | Empieza un día nuevo (la medianoche del compañero) |
| `inventory.item_used` | El jugador usa un objeto con el compañero; `context` es `field` (en combate todavía no se lanza) |
| `game.new_game` | Empieza una partida nueva con el compañero `partner` |
| `game.loaded` | Se cargó una partida y el juego sigue |

Los manejadores de un paquete tienen un presupuesto por evento y otro por fotograma; uno que falla va al log y los
demás siguen ([presupuestos](../modulos/engine/script.md#presupuestos-de-un-script-para-quien-hace-mods)).

## Funciones publicadas

El juego publica en la VM de la historia la tabla `story`, la de los scripts de eventos: un mod la usa con los mismos
nombres ([API de historia](historia.md)). Las que esperan (mostrar un texto, mover un personaje) solo se pueden
llamar desde una sección de historia, que el motor avanza fotograma a fotograma; desde un manejador de evento, la
llamada falla. Leer y cambiar disparadores y variables (`story.trigger`, `story.set_trigger`, `story.var`,
`story.set_var`…) funciona en cualquier sitio:

```lua
hooks.on("battle.ended", function(t)
  if t.won then
    -- 300: una variable extra que el ruleset del mod añade (historia.md, «Capacidad»)
    story.set_var(300, story.var(300) + 1)
  end
end)
```

## Referencia generada

Esta parte la escribe el test `the Lua API page lists every game event and story function the game publishes`
(`packages/session/tests/game_hooks_tests.cpp`) a partir del código, y falla si no coincide: con
`DWE_WRITE_API_DOC=1` la reescribe.

<!-- api-lua:generated -->
Versión de la API: **1.4.0**. Tabla de cada evento: versión 1.

| Evento | Campos de la tabla, además de `version` |
|---|---|
| `field.map_entered` | `map` (string), `previous` (string o nil) |
| `field.map_left` | `map` (string), `next` (string) |
| `battle.started` | `kind` (string), `map` (string), `enemies` (integer), `first_enemy` (string o nil), `scripted` (boolean) |
| `battle.ended` | `kind` (string), `outcome` (string), `won` (boolean), `recruit` (string o nil) |
| `evolution.evolved` | `from` (string), `to` (string) |
| `clock.new_day` | `year` (integer), `day` (integer) |
| `inventory.item_used` | `item` (string), `context` (string) |
| `game.new_game` | `partner` (string) |
| `game.loaded` | solo `version` |

Funciones de `story` (53):

- `story.ENDED`
- `story.PARTNER`
- `story.PLAYER`
- `story.add_to_var`
- `story.cancel_closes_choice`
- `story.copy_var`
- `story.delay`
- `story.entity_look_at_entity`
- `story.entity_move_to`
- `story.entity_move_to_axis`
- `story.entity_move_to_axis_with_camera`
- `story.entity_move_to_entity`
- `story.entity_move_to_entity_with_camera`
- `story.entity_move_to_with_camera`
- `story.entity_set_rotation`
- `story.entity_walk_to`
- `story.entity_walk_to_entity`
- `story.entity_walk_to_entity_with_camera`
- `story.entity_walk_to_with_camera`
- `story.fadeout_hud`
- `story.jump_to_file`
- `story.load_species_model`
- `story.move_camera_to`
- `story.move_camera_to_entity`
- `story.move_object_to`
- `story.play_animation`
- `story.play_sound`
- `story.reduce_var`
- `story.reset_entity_origin`
- `story.rotate_3d_object`
- `story.section_on_exit`
- `story.set_autotalk`
- `story.set_bgm`
- `story.set_dialog_owner`
- `story.set_script`
- `story.set_selection`
- `story.set_textbox_mode`
- `story.set_textbox_origin`
- `story.set_textbox_size`
- `story.set_trigger`
- `story.set_var`
- `story.show_textbox`
- `story.spawn_npc`
- `story.stop_bgm`
- `story.store_random`
- `story.teleport`
- `story.trigger`
- `story.unload_entity`
- `story.unload_species_model`
- `story.unset_trigger`
- `story.var`
- `story.wait_for_entity`
- `story.warp_to`

<!-- /api-lua:generated -->
