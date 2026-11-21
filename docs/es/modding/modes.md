# Modos de juego

Un modo es una forma de jugar que el jugador elige al empezar una partida nueva, con el ruleset con el que empieza.
El juego trae dos, las [variantes de juego](../architecture/game-variants.md) **Clásico Mejorado** y **Modo Folder**; un
mod añade otro con una carpeta `modes/<id>/` y su `mode.yaml`. Con un
solo modo montado el juego no pregunta nada; con dos o más, la partida nueva enseña la lista antes de las reglas. La
partida guarda su modo. Los detalles del módulo están en [modes](../modules/game/modes.md).

## El manifiesto

`modes/arena/mode.yaml` de un mod `mi_mod`:

```yaml
id: arena                         # igual que el nombre de la carpeta
name: mi_mod.arena_nombre         # claves de localization/<idioma>/mi_mod.yaml
description: mi_mod.arena_descripcion
ruleset: mi_mod_arena             # el ruleset con el que empieza (rules/mi_mod_arena/ruleset.yaml)
rulesets: [mi_mod_arena]          # los que admite el modo; incluye ruleset
order: 10                         # posición en la lista: Clásico Mejorado es 0, Modo Folder 1
options:                          # opcional: opciones que sus partidas fijan en un valor
  difficulty: normal
```

Todas las claves menos `options` son obligatorias y no se admiten otras. El esquema es `content/schemas/mode.schema.json`; el
paquete `game` ya lo declara para `modes/*/mode`, así que `dwx validate` comprueba también los de los mods.

- **Ruleset.** Cada ruleset de `rulesets` tiene que existir en los paquetes montados; si no, el modo se deja fuera con
  el motivo en el log. Un ruleset de mod suele extender el original y cambiar solo lo suyo
  ([rulesets](../architecture/rulesets.md)).
- **Reglas.** La pantalla de reglas de la partida nueva (contenido oculto y paso a Mega) sale solo para un modo que
  admite `original` y `oculto`; cualquier otro modo empieza directamente con su `ruleset`.
- **Opciones fijas.** `options` asocia nombres de opciones (las claves de la configuración) al valor con el que se
  juegan las partidas del modo, elija lo que elija el jugador; la página de opciones las muestra bloqueadas. Solo se
  pueden fijar las opciones que una partida aplica desde su propia configuración (las reglas y opciones de juego de la
  pestaña de juego); otra, o un valor que la opción no admite, se deja fuera con el motivo en el log
  ([opciones fijas](../architecture/game-variants.md#opciones-fijas-mientras-se-juega)).
- **Sustituir.** Un paquete de encima con la misma carpeta (`modes/folder/mode.yaml`) sustituye el modo, como
  cualquier asset; así un mod puede, por ejemplo, admitir su propio ruleset en el Modo Folder.
- **Guardar.** La partida guarda el id del modo; una partida de antes de las variantes (modo `original`, o ninguno) se
  carga como `folder`.

## Lo que aún no se puede

- Un modo no elige qué sistemas corren: la partida monta siempre los de la historia (campo, combate, cuidado…). Un
  modo sin campo, como una arena pura, espera al diseño del modo arena.
- La lista enseña los seis primeros modos (por `order`).
