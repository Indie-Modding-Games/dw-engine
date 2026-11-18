# Edit a map with the mapper

The map editor (`dwe-mapper`, [module](../modules/apps/mapper.md)) changes a map's collision, exits and NPCs and
saves only what changed as a **patch** (`map/<id>/patch.yaml`) in a mod pack. The base pack is never touched: the game
applies the patch on top when it loads the map, the same as the editor
([map patches](../formats/maps.md#patches)).

## 1. Open the content with your mods

```sh
scripts/mapper.sh run --content <content folder> --mods <mods folder>
```

`--mods` is the same folder (or single pack) given to the game. Without it the editor has nowhere to save until you
create a new pack. Choose the map in the list on the left.

## 2. Choose where it is saved

"File → Target" lists the packs the editor can write to: the mounted mods and, in a repository checkout, the team's
`game` pack. "New mod pack…" creates one: a mods folder and an id (`a-z`, `0-9` and `_`). The editor writes its
`pack.yaml` in the `mods` layer, mounts it and leaves it as the target; next time it remembers it. The status bar at
the bottom always says the target.

## 3. Edit

- **Collision:** choose the value (wall, free, a walk-over or confirm trigger, an exit, the toilet) and paint with the
  brush (B), the rectangle (R) or the fill (F). The eyedropper (I, or Alt+click) takes the value of a cell.
- **Exits:** with "Select" (V) click the cells of an exit or its arrival arrow. In the inspector change the
  destination map (with a filter), the destination exit, the arrival and the facing; drag the arrow to move the
  arrival and with Shift to rotate it. "Paint its cells" leaves the brush ready for that exit's cells.
- **NPCs:** drag an NPC to move it, with Shift to rotate it (or Q and E). The inspector has its fields and its route:
  add points ("Add point" or W under the cursor), drag them, change their type (wait, walk, run), move them up, down
  or remove them (Del).

Ctrl+Z undoes and Ctrl+Y redoes; a stroke or a drag is a single step. "Edit → Keyboard shortcuts" has the list.

## 4. Save

Ctrl+S ("Save (patch)") checks the map and writes the patch. If something is not valid (an exit to a map that does not
exist, an exit with cells that leads nowhere, a route of more than 8 parts, a target below the pack that brings the
map), it shows up in "Problems when saving" and nothing is written. A patch looks like this:

```yaml
warps:
- exit: 0
  target_exit: 3
npcs:
- index: 1
  position:
    x: 700
    y: 0
    z: -1800
collision:
  cells:
  - column: 29
    row: 39
    width: 3
    height: 3
    value: 128
```

Only the exits and NPCs that changed are there, with their changed fields; collision goes as rectangles of cells or,
if the change is large, as the whole grid. Saving again redoes the whole patch against the map underneath, so undoing
a change and saving removes it from the file. "Save full map" instead writes the whole map (`map/<id>/data.yaml`) in
the pack.

## 5. See it in the game

Start the game with the same `--mods` and with remote control (`--control tcp:<port>`, see
[remote control](../guides/remote-control.md#live-map-editing)). In the editor's "Live" panel type
`tcp:<port>` and press "Connect". With "Reload on save" on, each Ctrl+S makes the game re-read the packs and re-enter
the map where the player is. "Game debug layer" shows in the game the cells and exits as the engine sees them.

Without saving: with "Live preview" the game shows the change as soon as you make it (in memory only; it touches
neither the pack nor the saved game). "Follow the game" opens the map the game enters, the "Game" layer shows where
the player and the NPCs are, and with the right button on the map you can take the player to a point or try an exit.
More in [mapper → Live](../modules/apps/mapper.md#live).

To check the pack without opening the game:

```sh
dwx validate <mods folder>/<pack id>
```

## Put a toilet on a map

With the "Extra toilets" option, any map can have a toilet booth: a line in the pack's `field/extra_toilets.yaml`
(map, position and facing) gives the drawn booth, its solid cells, the cells in front that take the partner to the
toilet and the usual toilet routine. The details and an example are in
[Extra toilets](../modules/apps/game.md#extra-toilets).
