# mapper

The `dwe` map editor (`apps/mapper`, program `dwe-mapper`). It opens the content folder like the game, lists
the maps and draws over the map background its collisions, exits, characters, doors and bathroom, with the packs'
[patches](../../formats/maps.md#patches) already applied. It **edits** the collision, the exits, the NPCs, the doors, the bathroom and the
map's story events, and **saves** what changed as patches in a mod pack ([Edit](#edit),
[Events](#events), [Save](#save)). It shows the whole world as a graph of maps with its checks
([World](#world)), **adds new maps** to a pack ([New map](#new-map)) and serves all of that without a window to
external tools ([MCP server](#mcp-server)). It is written in Rust
with [egui](https://github.com/emilk/egui) (wgpu), separate from CMake, and is not part of the packs that get published.

## What it shows

- **Map list** (left): the maps from `data/maps.yaml` that have data, grouped by the zone name from
  the table or by the id prefix, with a filter by id or name. The group of the open map expands on its own.
- **Canvas** (center): the map background (day, evening, night or whatever palettes it has) and the layers on top,
  projected with the same camera as the game ([how a map is drawn](../../formats/maps.md)). Dragging
  pans (right or middle button), the wheel zooms in and out toward the cursor and a double click with "Select" fits the
  map to the window. The left button does what the chosen tool does ([Edit](#edit)).
- **Layers** (top bar), each one can be turned on and off:
  - **Walls:** the blocked cells (bit `0x80`).
  - **Triggers:** the trigger cells colored by band: step, confirm, exit and bathroom. The bands are
    those of the content's `field/triggers` (the file that wins in the stack, as in the game); without it, those of the
    original (51–79, 80–109, 110–119 and 120). A `field/triggers` that the game would reject (overlapping bands, outside
    1–127, more than ten exits, extra or missing keys) comes out as an error in the status bar and those of the
    original are used.
  - **Exits:** each exit with its index and its destination (`map:exit`) next to its cells, and the arrival arrow
    (where the player appears when entering through it and which way they face). A destination that does not exist or that contradicts the
    original's index is marked.
  - **NPCs:** the position and orientation of each character slot, with its type and its script.
  - **Waypoints:** the points each character walks or runs to, in order; the route returns to the first. Waits
    are not drawn.
  - **Doors** and **Bathroom:** the doors from `data/maps.yaml` and the map's bathroom points.
  - **Grid:** the 100×100 cell collision grid.
  - **Events:** where each event of the map starts: each trigger cell with its section (step or button), the NPCs
    with the section that runs when talked to and, in a corner, the events on entering and on loading the map. Labels
    that overlap are stacked ([Events](#events)).
- **Details** (right): the map (index, name, background size and "patched by: …" with the packs whose patches
  were applied, from bottom to top), its exits with "Go" (hovering over a patched exit shows which
  pack the patch is from), its characters and what is under the cursor: the cell, its value and its band, and the world
  and original coordinates.
- **Ctrl+click on an exit** opens its destination map centered on the arrival; "Back" returns to the previous map.
- **Patches:** the map is read as `loadMapDocument` does: the `map/<id>/patch` files are applied from the pack whose `data` wins and from
  all those above it, from bottom to top, with the same rules and the same errors as the engine (`dwe_map::patch`
  copies `map_patch.cpp`). A patch that cannot be applied leaves the map unopened and the error names the file.
- **Live:** it connects to the remote control of a running `dwe-game` ([remote control](../../guides/remote-control.md))
  and follows it while editing ([Live](#live)).

## Live

The "Live" panel (right) connects with `--live` or by typing `tcp:<port>` or `unix:<socket>` and pressing
"Connect". The connection lives in its own thread (`mapper_live::LiveLink`): if the game closes or restarts, the
editor says so in the status bar and reconnects on its own every second. The game accepts several clients at once
([several clients](../../guides/remote-control.md#several-clients)), so the editor can coexist with the MCP server
or another tool.

- **Status:** the status bar shows the game mode (title, field, battle…), its map, the tick, whether it is paused
  or busy with a story script, whether it is showing a preview and the round-trip time of the last query.
  In yellow while there is no connection.
- **Follow the game** (on by default): when the game enters another map, the editor opens it. If the open map
  has unsaved changes, it asks first as when opening a map by hand; if you choose to stay, it does not ask again
  until the game changes map again. "Go to the game's map" opens it by hand.
- **"Game" layer:** over the map the game is playing, the player, their partner and the NPCs with an arrow toward where
  they look, and the rectangle of the background that the game window shows. The editor queries `live` every 150 ms; the game's
  log (`log_tail`) is only requested when `live` says there is something new.
- **Live preview** (off by default): the game shows the unsaved change of the open map. The editor
  sends the difference between the map the packs give and the edited one (the same patch it would save) with
  `preview_map`, at most every 300 ms and never in the middle of a stroke. If only the collision changes, the game changes it
  in place; otherwise it re-enters the map with the player where they were. The preview lives only in the game's
  memory: it is not written to disk or to saved games. It is removed when the checkbox is turned off, when all the changes are undone,
  on saving (then "Reload on save" brings in what was saved), on disconnecting and on closing the editor. The game
  rejects it in battle, with a map change pending or while a story script runs; the editor waits for
  the game to return to the free field.
- **Take the player there:** with the right button on the canvas, "Take the player here" puts them at that point (`place`).
  On an exit, the same menu and the exit inspector offer "Try this exit" (puts the player on a
  cell of the exit, the clicked one or the first, and lets the game run, which takes it as if stepped on) and "Take the
  player to the arrival" (puts them where the exit leaves them, facing where it faces).
- **Pause, Step and +1 s:** stop the game, advance a tick or a second.
- **Reload on save** (on by default): each save requests `reload_map`. "Reload the map in the game" requests it
  by hand and "Game debug layer" turns on its
  [map debug layer](../../guides/remote-control.md#map-debug-layer).
- **Story:** the game's story state (`story_state`): the stage, the map script, the sections in
  progress, the active triggers and the variables other than 0, with the labels from `story/names` when they have them
  (`12 door_open`). "Auto refresh" reads it again every second and the filter searches by number or name. To test
  events you can turn a trigger on or off and set a variable (`story_set`); the game rejects it while a script
  runs.
- **Game log:** the game's warnings and errors (the last 300). What the game rejects (a patch that cannot
  be applied, with the file and the line the loader gives) also comes out in "Problems".

## Edit

Each change is an action that can be undone (Ctrl+Z) and redone (Ctrl+Y or Ctrl+Shift+Z); a brush stroke, a
drag or a field dragged in the inspector is **a single step**. The status bar marks the map with unsaved
changes (`* twna01 (unsaved)`) and says which pack it saves to.

**Tools** (bar under the menu):

| Tool | Key | What it does |
|---|---|---|
| Select | V | Picks and drags the arrival of an exit, an NPC or a point of its route; with Shift, dragging orients them toward the cursor; a click on the cells of an exit picks it. If there is nothing underneath, dragging pans the view |
| Brush | B | Paints the chosen value in a square of 1, 3 or 5 cells (`[` and `]`); the stroke joins the cells with lines |
| Rectangle | R | Paints the dragged rectangle (shown before releasing) |
| Fill | F | Paints the cells connected to the clicked one that have the same value |
| Eyedropper | I | Takes the value of the cell (or Alt+click with any paint tool) |

The **value** that is painted is chosen by groups from the `field/triggers` bands (wall, free, on step, on
confirm, each exit and bathroom) or as a raw number; X toggles wall and free.

**Inspector** (right, top), depending on what is chosen:

- **Exit:** the destination map (list of ids with a filter; choosing one also sets the original's index if the map
  has one, or -1), the destination exit (-1 to 9), the arrival (x, y, z) and the orientation (0–4095). "Paint its cells"
  sets the brush with the exit's trigger and "Erase cells" with the free value; "Open the destination" goes to the map.
- **NPC:** `type_id`, `follow_mode`, `tracking_range`, `script_id`, the position and the orientation, and under "Fields" the
  other numeric fields of the record. Text ones are not shown and are saved as they were.
- **Route:** each part with its type (wait, walk, run), its point (or, for a wait, its frames) and "Move up",
  "Move down" and "Remove"; "Add point" (or W under the cursor) adds a stretch on foot. `ai_sections` and `waypoints` are
  kept together: 8 parts at most, the rest with -1.
- **Door:** the model (or "no model"), the position and the turn; it is also dragged and turned on the canvas.
  "Add door" gives a model to the first free slot of the six in the set.
- **Bathroom:** its two points (the partner runs to the first and then to the second); each point is dragged on the canvas.
  "Add bathroom" and "Remove bathroom".
- **Camera, lights and objects:** shown, read-only.

**Adding and removing NPCs.** "Add NPC" (or "Add an NPC here" with the right button on the canvas) copies the chosen NPC,
or the last one on the map, or the first one in the content, into a new slot at the end, and makes it appear: it adds
`story.load_species_model(t)` and `story.spawn_npc(t, slot, 1)` to the map's entry section (script 0, section =
map index), right after its `story.set_script` and before any `if`, so that it always appears in the story draft ([Events](#events)). It is all a single
undo step. A map has at most 8 NPCs. "Remove the last NPC" removes only the last slot and its `spawn_npc`: the
story names the NPCs by their number, so removing one from the middle would change who each section talks to. A map
patch names the NPCs by position and cannot add or remove them, so with NPCs added or removed the map is
saved whole and the editor says so. Exits are not added or removed: the map always has its 10 records and
a new exit is one without cells that is edited and painted.

The doors and the bathroom are not in the map but in `data/maps` ([doors and bathroom](../../formats/maps.md#patches)); on
saving, the destination's `data/maps` is written ([Save](#save)).

### Shortcuts

| Keys | What it does |
|---|---|
| Ctrl+S | Save (patch) |
| Ctrl+Q | Quit |
| M | Switch between the map and the world |
| + / − | Zoom in / out |
| Home | Frame the map |
| N | Next time of day (background) |
| Ctrl+G | Go where the selected exit leads |
| Ctrl+D | Add an NPC under the cursor |
| Shift+drag in the world | From one map to another: connect their exits |
| Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z | Undo / redo |
| Ctrl+F | Search for a map in the list |
| PgUp / PgDn | Previous / next map in the list |
| 1 … 9, 0 | Show or hide a layer: walls, triggers, exits, NPCs, waypoints, doors, bathroom, grid, game and events |
| Alt+0 … Alt+9 | Pick the exit with that number and center it |
| C | Center the view on the selection |
| V, B, R, F, I | Select, Brush, Rectangle, Fill, Eyedropper |
| `[` / `]` | Smaller / larger brush |
| X | Value: wall / free |
| Q / E | Rotate the selection 45° one way or the other |
| Del | Remove the chosen waypoint |
| W | Add a waypoint to the chosen NPC under the cursor |
| Esc | Deselect |
| Ctrl+click | Go where the exit leads |
| Shift+drag | Orient the selection toward the cursor |
| Click on an event | With the Events layer and Select: opens its section |

"Edit → Keyboard shortcuts" shows the same list (`SHORTCUT_LIST` in `text.rs`). While a text field has
focus, the keys belong to the field: Ctrl+Z undoes what was typed in it and not in the map.

## Events

An event is a section of a story script that the map starts: on entering (the entry section of
script 0), on loading, on stepping or pressing the button on a trigger cell and on talking to an NPC. The editor reads the
`story/<n>.lua` files and applies the [section patches](../../modding/story.md#patches-per-section) like the game,
so it shows the section that really runs and who provides it.

- **"Events" panel** (left, under the maps): each event of the map with its type, its section, "patched by …"
  when a patch provides it and the map jumps it makes (`→ map exit`, with the condition if it has one; a click opens
  the destination). It filters by type or by number. A click opens the section and centers the view on the event. A cell that
  points to a section that the script does not have is marked: nothing happens when stepping on it, and editing it creates it.
- **From the canvas to the section:** with the Events layer, a click on a trigger cell or on an NPC opens its section. In
  the inspector, the cell under the cursor says which section runs (`Step → section 0x51 (script 12)`) with "Open
  section"; when painting a trigger, "Section…" says which sections exist for that value.
- **Section editor** ("Section" window): at the top, the script, the section and its origin (module or patch).
  - **Structure:** the lines of the section as sentences in the interface language (dialogs with their text, changes of triggers
    and variables with their names from `story/names`, movements, map jumps…), with the `if` blocks and their branches. Choosing
    a line edits its fields (operands); in a dialog, the text line: changing it changes the key in all
    the places that use it, and "New line" gives that command its own key with the current text. A simple
    condition (trigger on or off, variable equal to a value) is changed with fields; any other, as Lua text.
    "Delete", "Move up" and "Move down" move the lines and "Open routine" enters a routine of the script (with "Back to
    section").
  - **Palette:** the `story` API commands by groups, with a filter; a new one goes after the chosen line (or
    inside the chosen block). "If…" adds a block and the ones that change map or script end the section.
  - **Lua:** the section's (or routine's) code as text; "Apply" changes it if it compiles and says the line of the
    error if not.
  - "Revert section" removes the section from the draft: it runs again as the packs give it.

  Each change is an undo step, along with those of the map. What is edited lives in a **draft** per script of the
  destination pack and shows up right away in the panel and on the canvas; with no destination chosen it shows the same, but to save it
  you have to choose one.
- **Test event** ("Test" in the panel or in the editor, with a game connected in [Live](#live)): the editor
  reads the conditions the section checks at the start (the `if` blocks that only end, following `return
  routines.x()`) and shows what it will write into the game's story for it to happen (triggers and variables) and the ones it
  does not know how to set (they have to be met by hand). With a line chosen in the editor, it adds the conditions of the `if` blocks
  that lead to it, and in a talk event, the ones the map's entry section needs to make
  that NPC appear (its `spawn_npc`). When testing it sends `story_set` with those values, puts the player on the
  trigger cell or in front of the NPC and requests `reload_map`, which reads the saved patches and texts again and enters
  the map where the player is (the entry section runs again and makes the NPCs appear). In a button or talk event, when that section ends, it presses confirm. At the end it compares the story state from before and
  after and shows it ("What the test changed"). While the game runs a story script it does not allow writing
  to it: the editor says so and does not test. The game runs what is **saved**: with unsaved changes, the dialog
  warns about it.

## Sequences

A scene is a story section seen over time: who walks where, when the camera moves, what text
comes out and what waits for what. "Timeline" in the header of the section editor (or `--timeline` with `--section`)
opens a panel under the map with the section as tracks.

### The model

`dwe-story::sequence` runs the section like the [interpreter](../../architecture/story-interpreter.md), in frames of the
original (20 per second; the game gives each one 3 ticks): commands run one after another in the same frame until
one waits.

| Track | What it carries |
|---|---|
| Player, Partner, NPC n | The movements of that character (walk, slide, turn) and its animations |
| Camera | The camera movements |
| Text | Dialogs and choices |
| Audio | Sounds and music |
| Waits | `delay`, `wait_for_entity` and the other commands that stop the section |
| State | Triggers, variables, models and the rest of the commands without duration |

Each command says how the section continues (the bar on hover):

- **Does not block:** walk, slide, turn, animations and camera fill the movement slot of their character
  (or of the camera) and the next command starts at the same time. A new movement on an occupied slot replaces the one
  before: the old block is cut off with a red edge. `entity_move_to` and the camera last their operand's number of frames.
- **Blocks for some frames:** `delay` (and `fadeout_hud`, one).
- **Waits for the player:** a dialog, a choice, a battle or an engine routine (white bar).
- **Waits for the movement to finish:** `wait_for_entity`, until the slot it names is empty.
- **Ends the section:** a map change.

How long a step on foot lasts comes from the character's distance and speed (world units per frame, from the
clips in `field/rules.yaml`): the player walks at 20 and runs at 50, the partner at 22.6 and 43.5. What the editor cannot
know it **estimates** and marks with stripes: an NPC goes like the player, a dialog lasts 40 frames (how long it takes
the player to get through it), an animation 20 and a battle or an engine routine 20. An `if` follows the branch of the chosen
command (or the longest one, going through the `if` blocks that only end the section), a loop runs once and a routine from the
file runs inside, with its commands under the line that calls it.

### The panel

- **Ruler and cursor:** dragging on the ruler or on a track moves the cursor; the map shows each character and the
  camera where the model has them at that frame.
- **Blocks:** a click chooses the command (and its line in the section editor); dragging the right edge changes the
  frames of a `delay` or of a movement with duration. "Before" and "After" swap the command's place with its neighbor
  in the same block; the final `return` does not move.
- **On the map:** each movement is drawn with its path, an arrow and its command number, turns with a dashed
  arrow and the camera with its field of view. The diamond at the end of the chosen command is dragged to change the
  destination. Right click on the map: "Walk here", "Turn toward here" and "Camera here" at that point.
- **Add at the cursor:** walk, turn, wait, camera here or at the character, animation and text (with a new line
  "…" to write it in the editor) after the command that runs at the cursor, for the character of the chosen command.

Each change is an action with its undo step and ends up in the same section draft: it is saved as a
[story patch](../../modding/story.md#patches-per-section) with Ctrl+S.

### Playing in the game

With a game connected ([Live](#live)):

- **Play** prepares the game like [Test event](#events) (writes the story the section needs, puts the
  player and reloads the map) and then starts the section with `story_run`. It runs what is saved.
- **Play from here** starts at the chosen command. The editor writes a one-use patch: first it places
  each character and the camera where the model has them at the start of that command (one-frame movements and turns),
  then the earlier commands that only change state (triggers, variables, models, NPCs; without their waits
  or movements) and then the chosen command with everything that follows it. It sends it with `story_run` and `code`: the game runs it
  once and it is not saved anywhere, so it includes what is not saved yet.
- **Pause / Resume**, **+1 frame** (3 ticks) and **+1 s** stop and advance the game.
- **Follow the game:** the editor reads `story_state` four times per second; the section gives the lines it is on
  (`lines`) and the editor converts them into the command of the patch sent or of the section. That command is marked in orange,
  the cursor follows it and the header says "The game is at #n".
- **Game-model deviation:** how far each character is in the game from where the model puts it at the cursor (100 =
  one cell); on the map, a dashed line joins the two. It is useful to see where the model estimates badly.

Map jumps that land in the same place are grouped in one label ("destination ×n" or "first (+n more)") with the
whole list on hover.

## Save

**Destination** ("File → Destination"): the pack where it writes. It can be a mounted mod pack (the folders from
`--mods`), a new one ("New mod pack…": a folder with its `pack.yaml` in the `mods` layer, which is mounted when
created) or, only in a repository checkout, the team's `game` pack (`content/packs/game`). Never the base
pack or an extracted or generated one. The editor remembers the last destination in `dwe-mapper/settings.yaml` in the user's
configuration folder (`$XDG_CONFIG_HOME`, `%APPDATA%` or `~/.config`).

**Save (patch)**, Ctrl+S, writes `map/<id>/patch.yaml` in the destination with **only the differences** from the
floor beneath it (the `data` that wins plus the patches of the packs below the destination):

- the exits by their `exit` and the NPCs by their `index`, with only the changed fields (each field is replaced whole:
  a changed route carries whole `ai_sections` and `waypoints`);
- the camera, the lights (by `index`) and the objects if they changed;
- the collision as rectangles of cells (`cells`) when they fit in **64 rectangles** or fewer
  (`dwe_map::diff::MOST_RECTANGLES`), and otherwise the whole grid (`grid`). The rectangles are searched first across and then
  down by whole rows; 64 rectangles are about 380 lines, more or less what the whole grid takes.

The **edited events** are saved in the same Ctrl+S as a story patch of the destination,
`story/patches/<pack>_<script>.lua` (with `<script>` in three digits), with only the changed sections and the routines
they need copied as private routines ([section patches](../../modding/story.md#patches-per-section)).
If the pack already had a patch for that script, that one is edited (and merged with it if it changed on disk since the
draft was opened); a patch left without sections is deleted. The new or changed text lines go to the
destination's `text/<language>`. A story patch from a pack above that provides the same section is a warning: the
game will see its own. If what is saved changes map jumps (`story.warp_to`), `field/script_warps` has to be extracted
again ([script warps](../../architecture/script-warps.md)): with the `dwx` program set in
"File → Settings…", the editor runs `dwx script-warps <content> --with <mods> -o <destination>/field/script_warps.yaml`
itself; without it, it gives the command in the status bar.

The changed **doors and bathroom** are saved in the destination's `data/maps.yaml`: the whole `doors` or `toilets`
sections (as composed) and, if the map's set is shared by other maps or it had none, a new one at the end with the
map's entry pointing to it. The other maps do not change.

If the destination already had a map patch, the new one is computed again against the floor with the old and the new together, so
the file is still the minimum. A patch that ends up empty is deleted. **Save whole map** instead writes
the whole `map/<id>/data.yaml` in the destination (and removes its patch), in the `dwx` format.

Before writing, the result is loaded with the whole stack and checked; what fails comes out in the "Problems on
saving" panel and nothing is written:

- the destination is below the pack whose `data` wins (its patch would not be applied);
- a touched exit leads to a map that does not exist, to an exit outside 0–9, to nothing while having cells (dead warp) or
  with an id and an index that name different maps;
- the route of a touched NPC does not fit (8 parts);
- what is written does not load.

A patch from a pack **above** the destination that changes the same thing is only a warning: the game will see its own.

The files are written atomically (a temporary file and a rename). Then the content is mounted again
(the same as "File → Reload content"), the map is left clean and the undo history starts over.
"File → Revert" discards the unsaved changes of the map and its events. Changing maps, reopening the content or
quitting with unsaved changes asks first: Save, Discard or Cancel.

How a map is saved step by step: [Editing a map with the mapper](../../modding/maps.md).

The interface comes in English and Spanish, and all its texts live in `crates/mapper-core/src/text.rs`, each with both languages. The
language is `--lang en|es`, else the system's (`LC_ALL`, `LC_MESSAGES`, `LANG`), else English. The MCP server answers in
English unless it is given `--lang`.

## World

"World" (top bar, key M or `--world`) swaps the canvas for the **world graph**: each map with data is a
node with its id and its name, in the color of its zone (the zone name from `data/maps`), and each path between two maps is
an arrow:

- **Exits:** a line with the exit and the arrival one (`map:exit → map:exit`). If the two maps lead to
  each other, the two exits are drawn as a single line with an arrowhead at both ends.
- **Script warps:** a dashed line for each `warp_to` of a story section that runs on the map
  (`script:section`), with its conditions (`if …`) in the label and in the tooltip. They come from the same events
  as the Events layer ([Events](#events)).
- An exit with a destination but without trigger cells is not drawn: the original leaves unused exits like that.

The graph is read with the stack's patches applied and with the unsaved changes of the open map, so an
edited exit shows up in the world before saving. The label at the top counts maps, exits and script warps.

**Placement.** The zones are laid out on a grid in the order they are reached from the start map
(`field/warps`, `start`), the maps of each zone in a circle and then a few steps of fixed forces (the maps move
apart, the paths pull and each map goes back toward its zone). There is nothing random: the same content always gives the
same drawing. Dragging a map leaves it where it is dropped and the position is saved in the settings, per content folder
(`world_layouts`); "Rearrange" forgets the saved positions.

**View.** Wheel to zoom in and out, drag the background to pan, "Fit" to see everything. The search box
finds a map by id or name and Enter centers it; "Exits" and "Script warps" show or hide each type of
path and "Zones" hides whole zones. `--world --map <id>` opens the world centered on that map.

**Checks.** The panel on the right lists what is wrong, and each map carries a dot in the color of the worst thing it
has, with its count:

| Severity | What |
|---|---|
| Error | A path leads to a map that does not exist or to an exit that the destination map does not have |
| Warning | The arrival is inside a wall (the arrival falls on a blocked cell); the map cannot be reached from the start map by any path; an id that is not any map next to an original index that does lead to one |
| Note | A one-way exit: the destination map has no path back (off by default: the original has many on purpose) |

A click on a check centers its map. A map whose `data` cannot be read does not enter the graph; `validate` in the
[MCP server](#mcp-server) lists them with their error.

**Gestures.** Double click on a map opens it in the map view. A click on a path opens its origin: an exit is
selected on its map and a script warp opens its section in the event editor. **Shift+drag** (or drag
with the right button) from one map to another opens "Connect maps": you choose the origin exit, the arrival one and whether the
way back is connected too. The exit of the open map is left as one more unsaved change (it is undone with Ctrl+Z); that
of any other map is written at that moment as a patch in the destination pack, the same as on saving. An exit
that had no cells gets a door: two free floor cells that the camera shows, at the bottom and toward the center of the
background, and the arrival three cells inward.

**Live.** With the editor connected to the game, the map the player is on carries a ring and the maps they have
gone through in the session are highlighted with the path between them.

## New map

"Maps → New map…" adds a map to the **destination pack** (never to the base; without a destination it cannot be done):

| Field | What it is |
|---|---|
| Id | The name of the map within the pack (`a-z`, `0-9` and `_`); its full id is `<pack>:<id>` |
| Name | The text the game shows on entering; it is written to `text/es/<pack>.yaml` and, until someone translates it, the same in `text/en` |
| Zone | The zone name from `data/maps` it is grouped with (and the one the game uses while there is no other) |
| Template | **Copy of a map**: its camera, its collision, its NPCs and its `data/maps` entry. The images are not copied: the new map takes them from the original's folder with `assets_from`, unless copying them is checked. Its exits are emptied unless keeping them is checked. **Blank**: a free floor of the requested size (128 px tiles, up to 35) under an overhead camera, with a PNG background from the user (it is copied into the pack; it has to measure whole tiles and have few colors, since the game reads indexed PNGs) or with generated gray squares |
| Connect with | An exit of another map and the exit of the new map to come back by. Both are connected: the new map's in its data, the other's as a patch in the destination pack. If either had no cells it gets a door ([World](#world)) |

The editor writes `map/<pack>/<id>/data.yaml` (and the background if there is one), the entry without an index in `data/maps.yaml`
of the pack and the name, reads the content again and opens the new map. The format is in
[Maps that a pack adds](../../formats/maps.md#maps-a-package-adds) and the step by step in
[Adding a new map](../../modding/new-maps.md).

A map that the destination pack added is always saved whole in its own `data.yaml`, never as a patch next to it.
"Rename map…" and "Delete map…" only work for those maps. Renaming moves the folder, the entry and the name and
changes the exits that led to it in the maps the pack added or patches; it refuses if a path leads to it
that the pack cannot change (the exit of another map or a script warp). Deleting refuses while any path
leads to the map: they have to be changed first. Deleting cannot be undone.

## MCP server

`dwe-mapper mcp` starts the editor without a window as an **MCP server for external tools**: it speaks JSON-RPC 2.0 over
standard input and output, one message per line, and offers the same model as the window (`mapper-core`, without
egui). How to connect it and examples: [guide to the map editor's MCP server](../../guides/mapper-mcp.md).

```sh
dwe-mapper mcp --content <folder> [--team-packs <dir>] [--mods <dir>] [--target <pack>] [--live tcp:<port>] [--lang en|es]
```

`--target` is the id or the folder of a pack that can be written to, or a new folder inside a mods folder,
which is created as a mod pack. The `dwe-mapper-mcp` binary does the same without the `mcp` in front.

| Tool | What it does |
|---|---|
| `list_maps` | The maps with data: id, zone, whether a pack added them and their name |
| `map_info` | A map: zone, tiles, where it takes its images from, exits with destination, cells and arrival, NPCs, events and a collision summary |
| `world_graph` | The world: counts, zones and checks; with `map`, its incoming and outgoing paths; with `edges`, the list of paths |
| `path` | The shortest path between two maps by exits and script warps |
| `section` | A story section: one sentence per command (like the event editor), its Lua and a summary of its timeline |
| `find` | Searches the story code and sentences and the map ids |
| `validate` | The world checks, the maps that cannot be read and whether the open map has changes |
| `set_exit` | Takes an exit to another (or to nowhere); with `back`, also the way back |
| `set_collision_rect` | Sets a rectangle of cells to a value |
| `add_npc`, `move_npc` | Adds an NPC (copy of the last one) or moves one |
| `edit_section` | Changes the code of a section |
| `set_text` | Changes a sentence by its key |
| `new_map` | Creates a map in the destination pack, like "New map" |
| `save`, `discard` | Saves the open map's changes to the destination pack (patch, or whole with `whole`) or discards them |
| `live_connect`, `live_status`, `live_reload` | Connects to the game, asks for its state or makes it read the map again |
| `probar_evento` | Tests an event in the game, like "Test" |
| `play_from` | Runs a section from a top-level command, like "Play from here" |

Each tool declares its arguments with a JSON schema. The edits open their map, go through the same
actions as the window (with its undo) and stay unsaved until `save`; with `dry_run: true` they answer with the patch
they would give and are undone. It works on one map at a time: opening another with unsaved changes is an error that asks to
save or discard. A tool error comes back as a result with `isError` and the editor's text; a
malformed message or a tool that does not exist, as a JSON-RPC error.

## Parity with the previous editor

The Qt editor (`_work/ref-dw-map-editor`, reference only; its code is not copied) worked on the files
on disk. What it had and where it is here:

| Qt editor feature | Here |
|---|---|
| Map tree by zone, search box | Map list with groups and filter |
| Painting collision (brush, rectangle, fill, eyedropper) | The same ([Edit](#edit)) |
| Properties of exits, NPCs, doors and bathroom | Details panel and selection |
| "Locations": which exits lead to this map | In the world graph (the map's incoming paths) and in `world_graph` of the MCP server |
| Dialog and script editor | Event editor with sentences, conditions and timeline |
| Shortcuts: quit (Ctrl+Q), zoom in and out (+/−), fit (Home), time of day (N), go to the exit (Ctrl+G), add NPC (Ctrl+D) | Added, with the same keys |
| Windowless capture (`--shot`) | The same |
| MCP server (stdio and HTTP) | Stdio only: it is what local clients use; HTTP would open a port needlessly |
| Export and import of open formats (glTF, PNG per layer) | No: the content is already YAML and PNG in the packs, readable by any tool |
| Export a pack | No: the editor writes directly to the destination pack |
| Pieces and contents of the `.MAP` (sprites, TIM images) | No: they are disc data that `dwx` already converts; the editor works on the YAML |
| Keyboard shortcut editor | No: the list is fixed and is shown in "Edit → Keyboard shortcuts" |
| Built-in self-check | No: the `cargo test` tests cover it |
| Built-in assistant panel | No: the MCP server lets any external tool be used |

What the Qt editor did not have: the world graph, creating new maps, the live connection with the game, event
testing and the timeline.

## How it is built

| Crate | Folder | What it does |
|---|---|---|
| `dwe-pack` | `packages/rust/dwe-pack` | The packs like the engine ([virtual file system](../../architecture/vfs.md)): `pack.yaml`, the stack by layer, priority and dependencies, the asset ids, the composed tables (`data/maps`) and the mounting of the game content with the team packs and the mods |
| `dwe-map` | `packages/rust/dwe-map` | A map document read from the YAML with its patches and written back (`emit`, `diff`, `save`), the collision with its legend or in hexadecimal, the trigger bands from `field/triggers`, the names from `story/names`, the coordinates ([world space](../../architecture/world-space.md)), the camera with its projection onto the background and the doors and bathroom from `data/maps` (`places`) |
| `dwe-story` | `packages/rust/dwe-story` | The story scripts in Lua: a reader and writer of the subset that the modules and patches use (`parse`, `write`), the scripts with their patches applied like the game (`script`), the events of a map (`events`), the sentences and command labels in English or Spanish (`describe`, `commands`), editing by lines and conditions (`edit`), the conditions of a section for testing it (`needs`), the section as a timeline and the "Play from here" patch (`sequence`) and saving as a patch (`save`) |
| `mapper-core` | `apps/mapper/crates/mapper-core` | The editor model without a window: the open content, the map, the view, the layers, the map list, what is drawn (`MapOverlay`), the tools (`tools`), the undo history (`edit`) and the save destinations (`targets`). Every change enters through the `Action` enum |
| `mapper-live` | `apps/mapper/crates/mapper-live` | The remote control client (JSON lines over a Unix socket or local TCP) and `LiveLink`, the thread that queries the game, reconnects and sends it the editor's commands without stopping the window |
| `mapper-mcp` | `apps/mapper/crates/mapper-mcp` | The windowless MCP server: the tools, their schemas and the JSON-RPC loop over stdio on top of `mapper-core` |
| `dwe-mapper` | `apps/mapper` | The window: panels, canvas, world graph and command line |

`dwe-pack` and `dwe-map` copy the engine's rules in C++ and do not depend on anything graphical; any
Rust tool will be able to use them. Their tests check those rules with synthetic packs with neutral ids (`area_a`, `pack_a`):

- **Camera:** `dwe-map/tests/camera.rs` compares the projection, the offset and `pick_ground` with the values that
  the engine's `MapCamera` gives on the same inputs (`tests/data/camera_vectors.txt` → `.expected`).
  `scripts/mapper.sh camera-vectors` generates them again by compiling `tests/cpp/camera_vectors.cpp` with the engine.
- **Collision:** reading and rewriting the section gives the same text as `dwx` (legend and hexadecimal).
- **Patches:** `dwe-map/src/patch.rs` repeats the cases of `packages/engine/assets/tests/map_patch_tests.cpp` on the
  same test map (an exit field by field, the stack order, rectangles after a whole grid, objects,
  the errors that name the file, the stack floor that leaves out the patches below the `data`).
- **Bands and names:** the same cases as `trigger_bands_tests.cpp` and `story_names_tests.cpp` (what is rejected and
  with what message).
- **Writing:** an untouched document is written the same as `dwx` wrote it (the emitter imitates libyaml: blocks with
  indent 2, single-key lists not indented, texts in double quotes); the keys the model does not
  know are kept in place. Diffing and then applying the patch gives the edited map, and without changes there is no
  patch.
- **Against the engine:** `dwe-map/tests/written.rs` edits `base.yaml` from `packages/engine/assets/tests/data/written_maps` and saves for each case the
  patch the editor writes and the whole map it expects (`cells`: rectangles, exits and NPC fields; `grid`: the
  whole grid). `packages/engine/assets/tests/map_written_tests.cpp` applies that patch to the same base with
  `patchMapDocument` and compares field by field with `expected.yaml` read by the engine: both sides read, patch and
  write the same. `DWE_MAPPER_BLESS=1 cargo test --test written` generates them again after an intentional change.
- **Windowless editor:** `mapper-core/tests/session.rs` paints, fills, drags and turns NPCs and exits, edits routes,
  undoes and redoes, asks about unsaved changes and saves into a new mod pack; also the preview
  patch and where the exit tests put the player. `mapper-core/src/live.rs` tests the map
  following and when the preview is sent or removed.
- **Story:** `dwe-story` reads and rewrites each form of the generated Lua, applies patches like `StoryLuaHost`,
  and saves: a new patch, one that changed on disk, one from above that wins, one that ends up empty. The patch it
  writes is checked against the engine: `packages/session/tests/data/written_story` carries a module and the patch the editor
  produces (with a private routine) and `packages/session/tests/story_patch_tests.cpp` loads it with the
  game's reader and runs the section. `mapper-core/tests/story.rs` edits a line and a condition, undoes, saves the patch and
  the text, adds and removes NPCs, saves doors and bathroom and prepares an event test. With `DWE_MAPPER_CONTENT`,
  `dwe-story/tests/real_content.rs` reads and rewrites all the modules, turns each section into a patch and lists
  the events of all the maps, and also produces the timeline of each section and the "Play from here"
  patch of each top-level command, which has to be readable again.
- **Sequences:** `dwe-story/tests/sequence.rs` checks the durations, the waits, the cutting of a movement by
  the next one, the routines, the positions at the cursor, the game's lines and the "Play from here"
  patch. `mapper-core/tests/timeline.rs` changes frames, drags a destination, moves and adds commands with undo and
  redo, saves the patch and prepares a playback from a command.
- **World and new maps:** `mapper-core/tests/world.rs` mounts synthetic content with round-trip exits,
  a script warp with a condition, broken exits, an arrival in a wall and a zone that cannot be reached, and
  checks the paths, each check, that the placement is the same every time and respects moved maps, that
  unsaved changes show in the graph, and creating a connected map in a mod, renaming it and deleting it (only those
  of the pack itself and without paths leading to it). `dwe-map/src/new_map.rs` tests the files that are written
  for each template, the door and the arrival of a connected exit and the PNG backgrounds. With `DWE_MAPPER_CONTENT`, the
  whole game's graph classifies each exit.
- **MCP server:** `mapper-mcp/tests/stdio.rs` starts the binary and talks to it over stdio like a client: the greeting, the
  tool list, reads, an edit with `dry_run` that changes nothing, the same edit saved in a new mod
  and read back by another server, the errors, and a new map planned, created and connected.
- **Live:** `mapper-live` talks to a fake game (a server in a thread that answers like
  `remote_control.cpp`): the answers, the rejections, the fifth client rejected, that the link reconnects
  when the game returns, that it only requests the log when it changes and that on closing it removes the preview.
- **Real content** (optional): with `DWE_MAPPER_CONTENT=<content folder>`, `tests/real_content.rs` reads all the
  maps, checks the collision round trip and that each map without patches is written byte for byte the same. With `DWE_CAMERA_VECTORS=<path without extension>` it also compares other
  camera vectors (`.txt` and `.expected`) generated locally.

The document keeps whole what it reads (`MapDocument::source`), also the regions, the images, the ambient color
and the NPC fields that have no type in Rust, and writes them back in their order. The lights and objects are read with the engine's keys
so that a patch that breaks them is rejected as in the game. Two differences from the engine's reader, which is stricter:
errors in a value do not say the patch's line (Rust's YAML tree does not keep it) and for
characters only the fields the editor uses are checked. `.pak` files are not read: only folders. The editor lists the
files when opening the content and again after each save or with "File → Reload content".

## Using it

```sh
scripts/mapper.sh build                    # build/mapper/release/dwe-mapper
scripts/mapper.sh run --content <folder>   # builds it and opens it on this machine
```

Without `--content` it opens empty and the folder is chosen in "File → Open content folder". Inside the repository it finds
only `content/packs` (the team packs). Options (`dwe-mapper --help`):

| Option | What it does |
|---|---|
| `--content <dir>` | The content folder: the base pack and the packs next to it |
| `--team-packs <dir>` | The team packs, if they are not found on their own |
| `--mods <dir>` | A mods folder or a single pack (repeatable); their map patches are applied |
| `--map <id>` and `--exit <n>` | The map that opens first, centered on the arrival through that exit |
| `--section <script>:<section>` | With `--map`: opens that section in the editor (decimal or `0x…`) |
| `--timeline` | With `--section`: shows its timeline |
| `--variant <name>` | The background: `day`, `dusk`, `night`… |
| `--layers <list>` | The layers turned on: `walls,triggers,exits,npcs,waypoints,doors,toilet,grid,live,events` |
| `--group-by name\|prefix` and `--filter <text>` | The map list |
| `--live unix:<socket>\|tcp:<port>` | Connects to a running `dwe-game` |
| `--size <width>x<height>` | The window size |
| `--lang en\|es` | The language of the interface; without it, the system's, else English |
| `--world` | Opens the world graph; with `--map`, centered on that map |
| `--shot <file.png>` | Saves an image of the window and exits (for the documentation or a virtual X) |

`dwe-mapper mcp …` opens no window: it is the [MCP server](#mcp-server) (`dwe-mapper mcp --help`).

## Building and testing

All in Docker with the launcher image (`dwe-launcher-dev`, which already brings Rust, clippy and rustfmt); no GPU is needed.

```sh
scripts/dev.sh test --filter mapper   # or scripts/mapper.sh test
```

`scripts/mapper.sh test` passes `cargo fmt --check`, `cargo test --locked` and `cargo clippy -D warnings` across the whole
workspace (`apps/mapper/Cargo.toml` is its root and includes the crates in `packages/rust`). In CI the `mapper` job runs
when `apps/mapper/`, `packages/rust/` or `scripts/mapper.sh` changes; it is not required and a change only there
does not build the C++.
