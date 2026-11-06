# API de historia

Los scripts de eventos del juego original llegan al motor como módulos Lua que `dwx extract` escribe en
`story/<n>.lua` ([formato](../formats/scripts.md#lua)). Cada orden del script es una llamada a la tabla `story`. Esta
página dice cómo se llaman esas funciones y las claves de sus textos, que es lo que un mod ve y puede reemplazar.

> Estado: la partida publica `story` en su VM de historia y `LuaApiVersion` es **1.3.0**. Que el paquete base declare
> `api:` y lo que un mod puede hacer con la historia son de la fase 5.
>
> **CAMBIO DE CONTRATO (2026-10-23).** `story.play_sound(ranura, sonido)` toca el sonido de la ranura (0 y 1, los
> bancos del sistema; 8, el del mapa) y sigue sin esperar; `story.set_bgm(fuente)` pone la música de la fuente (la
> reinicia aunque sea la misma; `0xFF` la para) y `story.stop_bgm(operando)` la para
> ([audio](../architecture/audio.md#64-el-tramo-2-en-detalle)). Antes eran marcadores que no sonaban; con ellas
> `LuaApiVersion` sube de 1.2.0 a **1.3.0**.

## Nombres

Una función de `story` se llama como la orden del listado (`story.show_textbox`, `story.play_sound`), salvo las de esta
tabla, que la API renombra con el vocabulario del motor (especie, compañero, NPC, variable). El listado YAML
(`script/dg/<n>.yaml`) conserva los nombres originales: es el contrato con `dw1fe` y no cambia.

| Orden del listado | Función de `story` | Qué hace |
|---|---|---|
| `store_digimon_type` | `store_partner_species` | Guarda la especie del compañero en una variable |
| `store_digimon_value` | `store_species_type` | Guarda en una variable el tipo (campo `type` de `data/digimon`) de la especie que hay en otra |
| `load_digimon` | `load_species_model` | Carga el modelo de una especie |
| `set_digimon` | `spawn_npc` | Aparece el NPC de un hueco del mapa, si es de esa especie |
| `call_digimon_routine` | `call_partner_routine` | Ejecuta una rutina fija del compañero |
| `unload_digimon` | `unload_species_model` | Descarga el modelo de una especie |
| `set_pstat` | `set_var` | Da un valor a una variable de la historia |
| `add_to_pstat` | `add_to_var` | Suma a una variable de la historia |
| `reduce_pstat` | `reduce_var` | Resta a una variable de la historia |
| `copy_pstat` | `copy_var` | Copia una variable de la historia en otra |

Los operandos se renombran igual: el operando `digimon` se llama `species` y el operando `pstat`, `var`, en los
comentarios del Lua.

Las entidades que no son NPC son constantes de `story`: `story.PLAYER` (el personaje del jugador, `tamer` en el
listado) y `story.PARTNER` (el compañero).

La tabla vive en un solo sitio, `packages/formats/src/script/story_api_names.cpp`, y las palabras que ningún nombre generado
puede contener, en `ForbiddenNameWords` (`packages/formats/include/dwe/formats/script/story_api_names.hpp`). Un test recorre
todas las órdenes y sus operandos, y los tests del conversor y del disco miran cada módulo generado.

## Claves de texto

Un `show_textbox` o un `set_selection` no lleva su texto en el Lua: lleva una clave, y el texto está en
`text/<idioma>/story_<n>.yaml`.

- La clave es `story_<n>.text.<offset>`: `<n>` es el id del script con tres cifras (`000` para `MAPHEAD.SCN`, que el
  juego carga como script 0; `001`–`221` para los de `DG.SCN`, el mismo número que el `script_id` de los NPC de un
  mapa) y `<offset>` el de la orden en el listado, con cuatro cifras hexadecimales. Ejemplo: `story_049.text.04c0`.
- En el fichero de textos la clave va sin el prefijo del fichero: `text.04c0: "..."`.
- `dwx extract` escribe el inglés (`text/en/`). Un idioma sin la clave usa el inglés.
