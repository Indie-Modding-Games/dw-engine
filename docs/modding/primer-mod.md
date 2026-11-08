# Tu primer mod

Un mod pequeño de principio a fin: cuenta los combates que gana el jugador y lo escribe en el log, con un texto
propio en dos idiomas. Hace falta el juego compilado y una carpeta de contenido extraída
([jugar](../getting-started/jugar.md)); en los ejemplos, `<contenido>` es esa carpeta y `<juego>` la del repositorio.

## 1. La carpeta del paquete

Los mods van en una carpeta aparte, nunca dentro del contenido extraído. Crea `mis_mods/contador/`:

```text
mis_mods/
└─ contador/
   ├─ pack.yaml
   ├─ localization/
   │  ├─ es/contador.yaml
   │  └─ en/contador.yaml
   └─ scripts/
      └─ main.lua
```

## 2. El manifiesto

`pack.yaml`:

```yaml
id: contador
version: 0.1.0
layer: mods
api: "1.4"
name: "Contador de victorias"
description: "Cuenta los combates ganados en la partida."
dependencies:
  - id: game
    min_version: 0.1.0
```

`api` dice para qué versión de la API de Lua está escrito el mod; sin ella el juego no monta un mod con scripts
([paquetes e ids](paquetes.md)).

## 3. Los textos

`localization/es/contador.yaml`:

```yaml
victoria: "Victoria número {count}"
```

`localization/en/contador.yaml`:

```yaml
victoria: "Victory number {count}"
```

Las claves llevan delante el nombre del fichero: esta es `contador.victoria`.

## 4. El script

`scripts/main.lua` corre una vez al empezar o cargar una partida y registra los manejadores
([API de Lua](api-lua.md)):

```lua
local victorias = 0

hooks.on("battle.ended", function(t)
  if t.won then
    victorias = victorias + 1
    print("victoria " .. victorias .. " (" .. t.kind .. ")")
  end
end)

hooks.on("field.map_entered", function(t)
  print("mapa " .. t.map)
end)
```

Todo con `local`: crear una global es un error. La cuenta vive mientras dura la partida; para guardarla con ella hace
falta una variable de la historia (siguiente paso).

## 5. Probarlo

Comprueba el paquete sin arrancar el juego y luego arráncalo con tu carpeta de mods:

```sh
dwx validate --with <contenido>/base mis_mods/contador
dwe-game --content <contenido> --mods mis_mods
```

Empieza una partida y gana un combate: el log enseña `mod contador: mapa ...` y `mod contador: victoria 1 (normal)`.
Si el mod no aparece, busca en el log la línea que dice por qué se dejó fuera (una clave que falta en `pack.yaml`,
otra versión de la API) o el error de su script.

## 6. Guardarlo con la partida

Las variables de la historia viajan con la partida guardada. El original usa las 256 primeras; un mod pide las suyas
con un ruleset que extiende el original y amplía la capacidad de la historia ([capacidad](historia.md#capacidad)):

```yaml
# rules/contador/ruleset.yaml
id: contador
name: "Original con contador"
version: 0.1.0
extends: original
sections: [story]
```

```yaml
# rules/contador/story.yaml: la sección story del original con la capacidad ampliada
capacity:
  extra_variables: 1         # la variable 256
```

Con ese ruleset, el script guarda la cuenta en la variable 256:

```lua
hooks.on("battle.ended", function(t)
  if t.won then
    story.set_var(256, story.var(256) + 1)
  end
end)
```

Un ruleset sustituye secciones enteras, así que `rules/contador/story.yaml` tiene que traer también el resto de la
sección del original (cópiala de `rules/original/story.yaml` del paquete `game`). Para jugar con él, añade un
[modo](modos.md) que lo use o pásalo con `--ruleset contador`.

## Siguiente

- Cambiar el aspecto de la interfaz: [aspectos](aspectos.md).
- Tocar los eventos de la historia: [API de historia](historia.md).
- Añadir un mapa: [mapas nuevos](mapas-nuevos.md).
