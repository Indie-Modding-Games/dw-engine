# Add a new map

A mod pack can bring maps that the original game does not have and connect them to the others through their exits. The
map editor (`dwe-mapper`, [module](../modules/apps/mapper.md#new-map)) creates them in the target pack: the base
pack is not touched. Which files they are and what the game asks for: [Maps that a pack
adds](../formats/maps.md#maps-a-package-adds).

## 1. Open the content and choose the target

```sh
scripts/mapper.sh run --content <content folder> --mods <mods folder>
```

In "File → Target" choose the mod pack where the map will go, or create one with "New mod pack…" (same as in
[Edit a map with the mapper](maps.md#2-choose-where-it-is-saved)).

## 2. Create the map

"Maps → New map…":

- **Id:** the name inside the pack (`a-z`, `0-9` and `_`). The map will be `<pack>:<id>`.
- **Name:** what the game shows on entering. It is written in Spanish and, until someone translates it, the same in
  English.
- **Zone:** the zone name it is grouped under in the list.
- **Template:**
  - **Copy of a map:** starts from a map that already exists (its camera, its collision and its NPCs). Its images are
    not copied: the new data say `assets_from: <map>` and the game takes them from there. Check "copy its images" if
    you are going to change the background. The original's exits are emptied, unless you check keeping them.
  - **Blank:** a free floor of the size you choose (in 128 px tiles, up to 35 tiles) seen from above. The background
    is a PNG of yours, which is copied into the pack (it must measure whole tiles and have at most 256 colors), or
    some gray squares to start.
- **Connect with:** an exit of another map (for example, one the town does not use) and the new map's exit to come
  back through. The two end up connected. If an exit had no cells, the editor gives it a two-cell door and the arrival
  a few cells inward; you can move them afterwards.

"Create" writes the files, re-reads the content and opens the new map. The other map's exit is written as a patch in
the same pack.

## 3. Adjust it

Edit the new map like any other ([Edit](maps.md#3-edit)): the collision, the exits, the NPCs. Ctrl+S saves it whole
in its own `data.yaml` (it belongs to the pack; no patch needed). To connect more exits open the **World** (M key) and
drag with Shift from one map to another: the graph also shows whether any path ended up broken, one-way only or with
the arrival inside a wall.

"Maps → Rename map…" and "Maps → Delete map…" only work for the maps the pack added. Deleting asks that no path leads
to the map beforehand.

## 4. Check it and play it

```sh
dwx validate --with <content folder>/base <mods folder>/<pack id>
```

It gives an error for a map with no entry, an entry with no data or a map with no background. Then start the game with
the same `--mods`; to go straight to the new map:

```sh
dwe-game --mods <mods folder> --new-game --start <pack>:<id>,<exit>
```

What the game does not do yet with an added map: it has no music of its own, the bird transport does not lead to it,
the story does not place its NPCs (it has no story sections) and a game saved in it does not load if the mod is not
mounted.
