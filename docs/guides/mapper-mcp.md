# Servidor MCP del editor de mapas

`dwe-mapper mcp` es el editor de mapas sin ventana, como **servidor MCP para herramientas externas**: cualquier
programa que hable MCP (un asistente, un script, un test) puede leer los mapas, el mundo y la historia, editarlos y
guardarlos en un paquete de mod, con las mismas reglas que la ventana. La lista de herramientas está en
[mapper → Servidor MCP](../modulos/apps/mapper.md#servidor-mcp).

## Arrancarlo

```sh
scripts/mapper.sh build    # build/mapper/release/dwe-mapper
build/mapper/release/dwe-mapper mcp --content <carpeta de contenido> --mods <carpeta de mods> --target <carpeta de mods>/mi_mod
```

| Opción | Qué hace |
|---|---|
| `--content <dir>` | La carpeta de contenido, como en el juego (obligatoria) |
| `--team-packs <dir>` | Los packs del equipo; dentro del repositorio se encuentran solos |
| `--mods <dir>` | Una carpeta de mods o un paquete suelto (se repite) |
| `--target <paquete>` | Dónde se guarda: el id o la carpeta de un paquete en el que se puede escribir, o una carpeta que todavía no existe dentro de una carpeta de mods, que se crea como paquete de mod. Sin destino, las herramientas que escriben responden un error |
| `--live <endpoint>` | Conecta ya con un `dwe-game` en marcha (`tcp:<puerto>` o `unix:<socket>`); también se puede con `live_connect` |

El servidor lee mensajes JSON-RPC 2.0 por la entrada estándar, uno por línea, y responde por la salida estándar. Lo que
no es protocolo va a la salida de errores.

## Configurar un cliente

Casi todos los clientes MCP aceptan un bloque como este (el nombre de la clave y del fichero cambian de un cliente a
otro; las rutas tienen que ser absolutas):

```json
{
  "mcpServers": {
    "dwe-mapper": {
      "command": "/ruta/al/repo/build/mapper/release/dwe-mapper",
      "args": [
        "mcp",
        "--content", "/ruta/al/contenido",
        "--mods", "/ruta/a/los/mods",
        "--target", "/ruta/a/los/mods/mi_mod"
      ]
    }
  }
}
```

## Cómo se trabaja

- **Leer:** `list_maps`, `map_info`, `world_graph`, `path`, `section`, `find` y `validate` no cambian nada.
- **Editar:** `set_exit`, `set_collision_rect`, `add_npc`, `move_npc`, `edit_section` y `set_text` abren el mapa que
  nombran y hacen el cambio como un paso de deshacer. Quedan **sin guardar** hasta `save` (o se tiran con `discard`).
  Se trabaja sobre un mapa cada vez: si otro mapa tiene cambios sin guardar, la herramienta lo dice y no hace nada.
- **Probar antes:** todas las ediciones y `new_map` aceptan `"dry_run": true`. Responden el parche (o los ficheros) que
  darían y no dejan nada hecho.
- **Guardar:** `save` escribe el parche del mapa abierto en el paquete destino (o el mapa entero con `"whole": true`; un
  mapa que el propio paquete añadió se guarda siempre entero). `new_map` y la vuelta de `set_exit` con `back` escriben
  en ese momento, como en la ventana.
- **En vivo:** con el juego arrancado con `--control tcp:<puerto>`, `live_connect` lo enlaza; `live_reload` le hace
  volver a leer los paquetes y el mapa tras guardar, `probar_evento` prepara y lanza un evento y `play_from` corre una
  sección desde una orden.

Cada respuesta de herramienta trae el resultado como texto y como `structuredContent`. Un fallo vuelve con
`isError: true` y la explicación del editor; los mensajes informativos del editor van en `messages`.

## Ejemplo de sesión

Las líneas que manda el cliente (las respuestas, una por línea, se omiten):

```json
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"script","version":"1"}}}
{"jsonrpc":"2.0","method":"notifications/initialized"}
{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"map_info","arguments":{"map":"area_a"}}}
{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"set_collision_rect","arguments":{"map":"area_a","column0":30,"row0":30,"column1":32,"row1":31,"value":128,"dry_run":true}}}
{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"set_collision_rect","arguments":{"map":"area_a","column0":30,"row0":30,"column1":32,"row1":31,"value":128}}}
{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"save","arguments":{}}}
```

La respuesta de la 3 trae `"kept": false` y el parche (`collision: cells: …`); la de la 5, `"saved_in": "mi_mod"`. El
test `apps/mapper/crates/mapper-mcp/tests/stdio.rs` hace esta misma sesión.
