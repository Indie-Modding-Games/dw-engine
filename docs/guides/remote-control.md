# Remote control of the game

With `--control <socket>` or `--control tcp:<port>`, the game opens a debugging channel: an outside tool (an MCP
server, a test, a script, the map editor) reads the state and moves the game while it runs, without routes or reading
logs. Without the option nothing of the channel exists and the game does nothing for it.

```sh
dwe-game --content <content> --start twna01 --new-game-chain seeded --seed 1 \
  --control /tmp/dwe.sock --control-paused
```

- `--control <socket>` listens on that Unix socket (Linux and macOS). It serves several clients at once (see
  [Several clients](#several-clients)). On Windows there are no Unix sockets: the game says so and does not start.
- `--control tcp:<port>` (also `tcp:127.0.0.1:<port>` or `tcp:localhost:<port>`) listens on that TCP port of
  127.0.0.1, with the same protocol, on any system. Only on loopback: another host (`tcp:0.0.0.0:...`) is rejected at
  startup, and a client that does not come from 127.x is closed, because the channel drives the whole game without
  asking. `tcp:0` lets the system pick a free port; the log says it (`control: listening at
  tcp:127.0.0.1:<port>`).
- `--control-paused` leaves the logic stopped before its first tick, so that a session can be repeated tick by tick.
- A game with `--control` neither reads nor writes the player's settings, like an autotest. Saved games go to the
  `--saves` folder.
- With `--autotest`, the route runs first and the game stays open when it ends, for the channel.

## Protocol

One JSON request per line, one JSON response per line:

```json
{"id": 1, "cmd": "press", "keys": ["right"], "ticks": 30}
{"id": 1, "ok": true, "keys": ["right"], "from": 90, "ticks": 30, "tick": 120}
```

The response repeats the `id` (any JSON value) with `"ok": true` and what was requested, or with `"ok": false` and an
`"error"` that says what fails. Responses to `press`, `advance` and `screenshot` arrive when the ticks have run or
the image has been written.

| Command | Fields | What it does |
|---|---|---|
| `status` | | Answers `tick`, `paused`, `mode` (`title`, `movie`, `interview`, `field`, `battle`), `map`, `held` (the held keys), `pending` (the ticks left to advance), `textures` (the occupied slots of the bindless table), `gpu_bytes` (the GPU memory the allocator holds), `overlay` (whether the map debug layer is on), `clients` (the connected clients) and `preview` (the map with a [preview](#patch-preview), or `null`). `textures` and `gpu_bytes` are for spotting leaks in a long session; `overlay` lets the mapper show the real state on connecting |
| `state` | `path` | The line of the [state trace](state-trace.md) for now, or the part the path names: `player`, `player.x`, `battle.fighters.0.hp`, `npcs.id=6` (the NPC with that id) |
| `press` | `keys`, `ticks` (6) | The keys down exactly that many ticks and then up. With the logic stopped, it runs those ticks itself. Two `press` in a row chain tick by tick |
| `hold` | `keys` | The keys down from the next tick until `release` |
| `release` | `keys` | Releases those keys, or all without `keys` |
| `pause` | `alpha` (optional, 0 to 1) | Stops the logic: no tick runs, but the game keeps drawing. With `alpha`, the images are drawn that fraction of a tick after the last one (the smooth motion between ticks) instead of where the clock leaves it, so captures between two ticks always come out the same; `resume` or a `pause` without `alpha` removes it |
| `resume` | | The logic goes back to its pace |
| `advance` | `ticks` | Runs those ticks. With the logic stopped, exactly those and no more, at 4 per frame; otherwise, it waits for them to pass |
| `screenshot` | `path` | Saves the next drawn frame as a PNG at that path |
| `script` | `code` | Runs a line of the game's scripts, like the console (`give_item`, `evolve`, `train`...) |
| `place` | `text` | `place("<who> <x> <z> <rotation>")`, in original units |
| `battle` | `target` | `battle("<target>")`, for example `story:2` |
| `load` | `save` or `scene` | Loads a saved-game file, or starts a new game on a map (`twna01` or `twna01,2` with the exit) |
| `trace` | `path` | Writes the state trace to that file, tick by tick; without `path` it stops writing it |
| `quit` | | Closes the game |
| `rescan` | | Lists the files of the mounted packs again, to see those a tool created or deleted while the game runs |
| `reload_map` | `exit` (0-9) | `rescan` and reads and enters the current map again (see [Live map editing](#live-map-editing)). Answers `map` and `exit` (`null` without `exit`) |
| `story_state` | `path` | The story state: `map`, `triggers` (the active ones, from lowest to highest), `variables` (those other than 0, by number: `{"12": 3}`), `script` (`busy`, `map_script`, `sections` that run: `script`, `section` and `lines`, the lines of the section's code it is at, from the outermost to that of the order that is waiting), `stage` (the story lock's stage) and `camera` (`eye`, `target`, `focal_length`, `background`, `viewport`, `scale`, `scroll`, in world space; `null` without a map). With `path`, only that part: `variables.12`, `script.sections.0` |
| `overlay` | `on` (`true`/`false`) | Turns the [map debug layer](#map-debug-layer) on or off; without `on` it toggles it. Answers `overlay` |
| `live` | | Where everyone is, in a short line to query several times a second (see [Live state](#live-state)) |
| `preview_map` | `map`, `patch`, `pack` | Shows the map with a patch on top without saving it (see [Patch preview](#patch-preview)). Answers `map`, `applied` (`collision` or `reentered`) and `tick` |
| `preview_clear` | | Removes the preview. Answers `map` (empty without a game) and `cleared` (`false` if there was none); it is never an error |
| `log_tail` | `since` | The log's warnings and errors since number `since` (without it, all it keeps): `next` (the number of the last one) and `entries` (`seq`, `level` (`warning` or `error`), `text`). It keeps the last 256, only with `--control` |
| `story_set` | `kind` (`trigger` or `var`), `number`, `value` | Writes a story trigger (`value` `true`/`false` or 1/0) or a variable (0-255), like `story_set` in the console. Not while a story script runs. Answers `kind`, `number` and `value` |
| `story_run` | `script`, `section`, `code`, `pack` | Starts a story section on the next frame, as if the map requested it. With `code` (the text of a story patch of that script with that section) it runs that code once instead of the packs', without saving it; `pack` says which pack it is loaded with (without it, the one the script gives). Not while a story script runs. Answers `script`, `section` and `sent` (if it carried `code`) |

Keys go by their name (`keyNamed`): `a` to `z`, `0` to `9`, `return`, `escape`, `backspace`, `tab`, `space`, `left`,
`right`, `up`, `down`, `left_shift`, `right_shift`; in a list or as `"right+return"`. They enter the input state at
the start of each tick, like a route's `hold`, so they last the requested ticks even if the frames are slow.

For example:

```json
{"id": 7, "cmd": "story_set", "kind": "trigger", "number": 12, "value": true}
{"id": 7, "ok": true, "kind": "trigger", "number": 12, "value": true}
{"id": 8, "cmd": "log_tail", "since": 40}
{"id": 8, "ok": true, "next": 42, "entries": [{"seq": 41, "level": "warning", "text": "..."}, {"seq": 42, "level": "error", "text": "..."}]}
```

The number in `story_set` goes in `number` and not in `id`, which is the request's.

`story_run` is what the [mapper](../modules/apps/mapper.md#sequences)'s "Play from here" uses: it sends a patch that
places everyone where they would be and runs the rest of the section. While it runs, `story_state` says which line it
is at (`lines`), and the mapper turns it into the order of the patch that wrote it:

```json
{"id": 8, "cmd": "story_run", "script": 12, "section": 81,
 "code": "return { script = 12, sections = { [81] = function() story.delay(5) end } }"}
```

## Several clients

The channel serves up to 4 clients at once (the map editor and a test, for example), on the Unix socket and on TCP.
To the fifth it writes `{"id":null,"ok":false,"error":"control: too many clients (4 at most)"}` and closes it.

- Each client receives only the responses to its own requests, including those that arrive later (`press`,
  `advance`, `screenshot`). If it leaves earlier, those responses are lost, but their ticks run anyway.
- Each client carries a number that is never given again, so a late response never reaches another client that
  connected afterward.
- Requests run on the game's thread in the order they arrive. The held keys, the `press` calls and the pause belong
  to the game, shared by everyone: a `release` without keys also releases another client's, and a `pause` stops the
  logic for everyone.
- A client that leaves in the middle of a line, or that sends a line of more than 1 MiB, is closed on its own.

## Pause and advance

With the logic stopped, the game only runs the ticks `advance` or `press` ask for, and each frame lasts as long as
those ticks: what moves with the frame's time (animations, fades) moves with them. This way, the same sequence of
requests from `--control-paused` with the same `--seed` leaves the same state.

## Live map editing

The map editor writes to the packs (the user's mod layer, or `content/packs/game` for the team) and asks for
`reload_map` to see the change in the running game:

1. `rescan`: the list of files of the mounted packs is rebuilt, so a new file (a `map/<id>/data` that before only the
   base pack had) wins right away. The packs and their order do not change: a new pack asks for a restart. If a file
   does not give a valid id (a temporary half-written one), it fails and the previous list stays.
2. The current map's document with its patches (`map/<id>/patch`, see [Maps](../formats/maps.md)),
   `field/script_warps`, `field/rules` with the `field/triggers` bands and the map table (`data/maps`) are read again
   from the packs. The story modules (`story/<n>`) and their patches (`story/patches/*`) are discarded and read again
   from the packs in the next section that runs, so an edited `story/<n>.lua` or a new patch takes effect from the
   reload ([Story](../modding/story.md)). The text tables (`text/<language>`) of the language in use are read again,
   so a line that a story patch brings or changes shows up right away. If the map no longer loads, or does not have
   the requested exit, it fails before touching anything.
3. The map is entered again: without `exit`, where the player and the partner are, without a fade; with `exit`,
   through that exit, as when crossing a door. The story, the inventory, the clock and the session stay the same: the
   game does the same as when entering a map (it runs the map script's header, which places its NPCs).

It is not reloaded during a battle, in the middle of a map change or while a story section runs (its coroutine runs
in the modules already loaded; a section that is only waiting to start, like a step tile's, starts right away with the
new ones): the response is an error and it can be requested again when it ends. The
`field: entered ...` line in the log says which packs patched the map (`patched by <pack>, ...`). Of `field/rules`,
map changes, fades and script tiles use the new values from the reload; what the field built once with them (the
NPCs, the partner's distance) waits for a new game. The other parts that read the map table or the script jumps on
their own (the music, the island map) read them when opened.

If a step fails (a file's YAML, a patch that does not apply, the requested exit), the response's error carries the
loader's text with the file and the line.

## Live state

`live` answers, in one line:

```json
{"id": 3, "ok": true, "tick": 5120, "paused": false, "mode": "field", "map": "twna01", "busy": false,
 "preview": null, "log": 12, "player": {"x": 256, "y": 0, "z": 1285.5, "yaw": 1.5708},
 "partner": {"x": 180, "y": 0, "z": 1300, "yaw": 3.1416}, "npcs": [{"id": 11, "x": -839, "y": 0, "z": -2210, "yaw": 0}], "players": [],
 "view": {"x": 0, "y": 16, "w": 320, "h": 240}}
```

- `mode` and `map` as in `status`; `busy`, whether a story script or section runs (or is about to start); `preview`,
  the map with a preview or `null`; `log`, the number of the last warning or error `log_tail` keeps (0 if there is
  none), to request `log_tail` only when it changes.
- `player`, `partner` and the `npcs` (by their script id) are in [world space](../architecture/world-space.md), as
  the field keeps them (not in the original's axes of the state trace): the floor is the XZ plane, and a collision
  tile is `column = floor(x / 100) + 50`, `row = floor(z / 100) + 50`. Without a game they are `null` and `[]`; during
  a battle they are still the field's, where the fight takes place.
- `players`: in a co-op game, the other players this game draws on its map, by player id (`id`, 0 the host), at the
  place they are drawn this frame (`[]` outside co-op, and a player on another map is not listed).
- `yaw` in radians, as the engine keeps it: the character looks toward `(sin yaw, cos yaw)` in `(x, z)`, that is,
  `yaw = 0` looks toward world +Z (increasing grid rows) and a positive `yaw` turns from +Z toward +X
  (`assets::forwardOfYaw`). For an arrow on the grid: `dir = (sin yaw, cos yaw)` in `(column, row)`.
- `view`: the background pixels the window shows: `x`, `y` is the top left (`MapCamera::scroll`) and `w`, `h` the size
  (`viewportSize / scale`); `null` without a map camera.

It is built directly from the field, without the trace or an intermediate JSON. With 64 NPCs the response takes about
3.9 KB and reading the request and writing it costs about 0.3 ms in the tests' debug build with sanitizers
(`live stays cheap with a crowded map`); the release one is several times faster. At 5-10 queries per second it is
not noticeable in the frame.

## Patch preview

`preview_map` shows an editor change without saving it:

```json
{"id": 4, "cmd": "preview_map", "map": "twna01", "patch": "collision:\n  cells:\n  - {column: 30, row: 28, value: 128}\n", "pack": "my_mod"}
{"id": 4, "ok": true, "map": "twna01", "applied": "collision", "tick": 5200}
```

- `patch` is the text of a map patch (the same format as `map/<id>/patch`, see
  [Maps](../formats/maps.md#patches)); empty changes nothing. It is applied in memory on top of the data and the
  patches of the map's packs, as one more patch, the last; in its errors it is called `preview`. With `pack`, if it is
  a mounted pack, the patch has its rights (those of the base for a base pack); if not, those of a mod.
- The map is entered again where the player and the partner are, like `reload_map` without `exit`, but without
  listing the packs again or reading `field/rules` or the script jumps.
- If the patch only touches collision (or touches nothing) and the preview it replaces (if any) did not touch
  anything else either, the map is not entered again: the field's collision grid is changed in place, along with the
  minimap. This undoes the tiles a script closed (`setImpassableRect`) until the next entry into the map, and a
  trigger tile under the player does not fire until they step on it again. `applied` says `collision` or
  `reentered`.
- It is rejected without a game, if `map` is not the map being shown (`the game shows <map>`), during a battle, in
  the middle of a map change and while a story script runs. The patch is checked before applying it; an error in the
  YAML or in what it names comes back with its line (`patch preview: line 3, column 5: ...`). If it fails, the
  previous preview stays.
- The preview belongs to its map: entering any other map removes it (the next entry uses the packs), and
  `reload_map` too, because it reads the packs again. `preview_clear` removes it and, if the game is on that map,
  enters again from the packs (or puts the collision back in place if it was only collision).
- Nothing of it is ever written to disk: saved games do not keep map documents, only the map you are on.
- `reload_map` is also rejected while a story script runs, with the same text.

## Map debug layer

It draws over the real field, with the map's camera:

- The collision grid: walls in red and trigger tiles by band, with the `field/triggers` bands the field uses (the
  original's if no pack changes it): step (51-79) in yellow, confirm (80-109) in cyan, exits (110-119) in green, the
  toilet (120) in purple and other values in gray. Each value carries a label in the center of its tiles: `step 60`,
  `confirm 85`, `exit 2 -> <map>:<exit>`, `toilet`.
- The map's exits: where each one leaves the player (`in <n>`) and where they face.
- The NPCs of the map document: their spot and orientation with `npc <slot> id <script id>`, and their route points
  joined by lines.

The collision is the one the field uses now, with the scripts' changes. It is turned on and off with F3 (action
`debug_map_overlay`), with `map_overlay("on")`, `map_overlay("off")` or `map_overlay("toggle")` in the console, and
with the channel's `overlay` command. It is drawn with the ImGui layer, on top of the game's interface, and appears in
`screenshot` captures.

## The MCP server

An MCP server outside the repository starts the game with this channel in its own Xvfb and offers the same tools as
the one that drives the original game in an emulator: `start`, `stop`, `status`, `actors`, `read`, `press`, `hold`,
`release`, `pause`, `resume`, `advance`, `screenshot`, and also `place`, `battle`, `script`, `load`, `trace`,
`reports` (the F12 reports) and `compare`, which plays an oracle scenario in both games and returns only the
differences.

## Code

- `apps/game/src/remote_control.*`: the requests and the dispatcher, without a socket or a game; its tests are in
  `apps/game/tests/remote_control_tests.cpp`.
- `apps/game/src/control_json.*`: the JSON that reads the requests and picks parts of the state.
- `apps/game/src/control_socket.*`: the Unix or TCP socket with its clients, never blocking the game; its tests are in
  `apps/game/tests/control_socket_tests.cpp`.
- `apps/game/src/control_log.*`: the warnings and errors `log_tail` gives.
- `apps/game/src/story_state_json.*`: the JSON of `story_state`.
- `apps/game/src/map_debug_overlay.*`: the shapes of the map debug layer, which `debug::DebugUi::setShapes` draws
  (`packages/engine/debug/include/dwe/debug/overlay_shapes.hpp`).
- `PlaySession::reloadMap`, `FieldScene::reloadMap`, `game::field::Field::reloadData`,
  `session::StoryLuaHost::reloadScripts` and `assets::VirtualFileSystem::rescan`: the reload.
- `PlaySession::previewMap`, `FieldScene::previewMap`, `game::field::Field::setCollision`,
  `assets::mapPatchSections` and `assets::loadMapDocument` with extra patches: the preview.
- `PlaySession::live`: the live state.
- `apps/game/src/game.cpp`: joins the channel to the game loop.
