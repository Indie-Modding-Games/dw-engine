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

## Parches por sección

Para cambiar un evento no hace falta copiar el `story/<n>.lua` entero. Un paquete trae `story/patches/<nombre>.lua`
(el nombre es libre) que devuelve el script que parchea y sus secciones:

```lua
-- story/patches/puerta_abierta.lua de un mod
return {
  script = 49,
  sections = {
    -- Sustituye la sección 0x51 del script 49.
    [0x51] = function()
      story.set_trigger(203)
      return story.ENDED
    end,
    -- Envuelve la 0x52: hace algo antes y deja que la original siga.
    [0x52] = function(original)
      story.set_var(5, 1)
      return original()
    end,
  },
}
```

- Cada función recibe `original`, la sección que sustituye (o `nil` si el script no la tiene: un parche también puede
  añadir secciones nuevas). Las secciones que el parche no nombra siguen siendo las del script.
- `return original()` deja que la original corra entera, esperas y saltos incluidos. Para hacer algo **después**,
  guarda lo que devuelve y devuélvelo al final: `local r = original(); story.set_var(5, 2); return r`. Así un
  `return story.ENDED` de la original sigue acabando el script.
- Una sección devuelve `story.ENDED` (acaba el script) o nada (vuelve a quien la llamó), como las del script.
- El parche corre en el entorno de su paquete (sandbox y cuota de memoria del paquete) con la misma API `story`.
- Si dos paquetes parchean la misma sección, gana el que está más arriba en la pila (capa y prioridad del
  `pack.yaml`) y el log lo dice. `original` es siempre la sección del script, no la del parche que perdió.
- Un parche que no carga (Lua que no compila, sin `script`, una sección fuera de 0–65535) se deja fuera con un error
  en el log y el juego usa la sección del script. `dwx validate` comprueba que el Lua compila.
- Los números de sección son los del script: en el Lua generado aparecen como `[0x0051] = routines.at_...`.
- Con el juego abierto y el [canal de control](../guides/control-remoto.md#edición-de-mapas-en-vivo), `reload_map`
  hace que los módulos y los parches se vuelvan a leer de los paquetes en la siguiente sección que corra: un parche
  nuevo o un `story/<n>.lua` editado valen sin reiniciar, y los disparadores, las variables y la sesión siguen igual.
  No se recarga mientras corre una sección.
- `dwx script-warps` ([warps de script](../architecture/warps-de-script.md#desde-el-lua)) aplica los parches igual
  que el juego antes de sacar los cambios de mapa, así que un `story.warp_to` de un parche llega a
  `field/script_warps`. El recorrido solo sigue lo que sabe leer: `original()` como en una subrutina
  (`return original()`, o `if original() == ENDED then return ENDED end` y seguir), no guardado en una variable
  (`local r = original()`). Un parche así corre bien en el juego, pero `dwx script-warps` falla y nombra el fichero
  del parche y su línea.
- Un parche no ve las `routines` del módulo: corre en su propio entorno. Si una sección necesita código compartido del
  script (el `return routines.at_0100()` del Lua generado), el parche trae su copia como rutina privada: arriba,
  `local routines = {}` y después `function routines.<nombre>() ... end` (sin parámetros y sin `original`). Dentro del
  parche, `routines.<nombre>` es siempre la suya; una que el parche no define es un error de `dwx script-warps` con
  su línea (en el juego fallaría al correr). Las secciones que el parche no toca siguen usando las del módulo. Así
  guarda el editor de mapas una sección editada:

  ```lua
  local routines = {}

  function routines.at_0100()
    story.set_var(5, 2)
    return story.ENDED
  end

  return {
    script = 900,
    sections = {
      [0x51] = function(original)
        story.set_trigger(73)
        return routines.at_0100()
      end,
    },
  }
  ```

## Editar eventos con el mapper

El [editor de mapas](../modulos/apps/mapper.md#eventos) escribe estos parches por ti:

1. Abre el contenido y el mapa, elige el paquete de mod en «Archivo → Destino» y enciende la capa **Eventos** (tecla
   0). El panel «Eventos» lista lo que hace el mapa al entrar, al pisar o pulsar en cada celda de disparo y al hablar
   con cada NPC, con la sección que corre y el parche que la da, si lo hay.
2. Un clic en un evento (en el panel o en el lienzo) abre su sección. En «Estructura», elige una línea y cambia sus
   campos: el texto de un diálogo, el número de un disparador, la condición de un `if`. La paleta añade comandos y
   la pestaña «Lua» deja escribir la sección a mano. Ctrl+Z deshace.
3. Ctrl+S guarda `story/patches/<paquete>_<script>.lua` con las secciones cambiadas (y las rutinas que usan, como
   rutinas privadas) y las líneas de texto en el `text/<idioma>` del paquete. Si ya había un parche del paquete para
   ese script, se edita ese.
4. Con el juego abierto y conectado ([En vivo](../modulos/apps/mapper.md#en-vivo)), «Probar» pone la historia del
   juego en el estado que la sección necesita, recarga el mapa, lleva al jugador al evento y enseña lo que cambió.

Si el cambio añade o mueve un `story.warp_to`, vuelve a sacar `field/script_warps` con `dwx script-warps` (el editor
lo hace solo si sabe dónde está `dwx`, en «Archivo → Ajustes…»). Antes de publicar, `dwx validate` sobre el mod.

## Nombres de disparadores y variables

La historia trabaja con números (disparadores 0–799, variables 0–255). `story/names.yaml` del paquete `game` da una
etiqueta a los que el motor usa para algo (medallas, objetos de la página del jugador, música…), para que las
herramientas los muestren por nombre ([esquema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/story_names.schema.json)):

```yaml
triggers:
  364: medal_00
variables:
  245: music_mode
```

Un mod puede traer su propio `story/names.yaml` (gana por ID, entero). Los nombres no cambian nada del juego.
