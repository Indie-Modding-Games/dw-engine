# Map editor MCP server

`dwe-mapper mcp` is the map editor without a window, as an **MCP server for external tools**: any program that speaks
MCP (an assistant, a script, a test) can read the maps, the world and the story, edit them and save them in a mod
pack, with the same rules as the window. The list of tools is in
[mapper → MCP server](../modules/apps/mapper.md#mcp-server).

## Starting it

```sh
scripts/mapper.sh build    # build/mapper/release/dwe-mapper
build/mapper/release/dwe-mapper mcp --content <content folder> --mods <mods folder> --target <mods folder>/my_mod
```

| Option | What it does |
|---|---|
| `--content <dir>` | The content folder, as in the game (required) |
| `--team-packs <dir>` | The team's packs; inside the repository they are found on their own |
| `--mods <dir>` | A mods folder or a single pack (can be repeated) |
| `--target <pack>` | Where it saves: the id or folder of a pack that can be written to, or a folder that does not exist yet inside a mods folder, which is created as a mod pack. With no target, the tools that write return an error |
| `--live <endpoint>` | Connects right away to a running `dwe-game` (`tcp:<port>` or `unix:<socket>`); it can also be done with `live_connect` |

The server reads JSON-RPC 2.0 messages from standard input, one per line, and answers on standard output. Anything
that is not protocol goes to the error output.

## Configuring a client

Almost all MCP clients accept a block like this (the name of the key and of the file change from one client to
another; the paths must be absolute):

```json
{
  "mcpServers": {
    "dwe-mapper": {
      "command": "/path/to/repo/build/mapper/release/dwe-mapper",
      "args": [
        "mcp",
        "--content", "/path/to/content",
        "--mods", "/path/to/mods",
        "--target", "/path/to/mods/my_mod"
      ]
    }
  }
}
```

## How to work with it

- **Read:** `list_maps`, `map_info`, `world_graph`, `path`, `section`, `find` and `validate` change nothing.
- **Edit:** `set_exit`, `set_collision_rect`, `add_npc`, `move_npc`, `edit_section` and `set_text` open the map they
  name and make the change as one undo step. They stay **unsaved** until `save` (or are thrown away with `discard`).
  You work on one map at a time: if another map has unsaved changes, the tool says so and does nothing.
- **Try first:** all the edits and `new_map` accept `"dry_run": true`. They answer with the patch (or the files) they
  would produce and leave nothing done.
- **Save:** `save` writes the open map's patch into the target pack (or the whole map with `"whole": true`; a map that
  the pack itself added is always saved whole). `new_map` and the return trip of `set_exit` with `back` write at that
  moment, as in the window.
- **Live:** with the game started with `--control tcp:<port>`, `live_connect` links it; `live_reload` makes it read
  the packs and the map again after saving, `probar_evento` prepares and launches an event and `play_from` runs a
  section from an order.

Each tool response carries the result as text and as `structuredContent`. A failure comes back with
`isError: true` and the editor's explanation; the editor's informational messages go in `messages`.

## Example session

The lines the client sends (the responses, one per line, are omitted):

```json
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"script","version":"1"}}}
{"jsonrpc":"2.0","method":"notifications/initialized"}
{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"map_info","arguments":{"map":"area_a"}}}
{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"set_collision_rect","arguments":{"map":"area_a","column0":30,"row0":30,"column1":32,"row1":31,"value":128,"dry_run":true}}}
{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"set_collision_rect","arguments":{"map":"area_a","column0":30,"row0":30,"column1":32,"row1":31,"value":128}}}
{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"save","arguments":{}}}
```

The response to 3 carries `"kept": false` and the patch (`collision: cells: …`); the one to 5, `"saved_in": "my_mod"`. The
test `apps/mapper/crates/mapper-mcp/tests/stdio.rs` runs this same session.
