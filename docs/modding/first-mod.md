# Your first mod

A small mod from start to finish: it counts the battles the player wins and writes it to the log, with its own text
in two languages. You need the built game and an extracted content folder ([play](../getting-started/play.md)); in the
examples, `<content>` is that folder and `<game>` the repository's.

## 1. The pack folder

Mods go in a separate folder, never inside the extracted content. Create `my_mods/counter/`:

```text
my_mods/
└─ counter/
   ├─ pack.yaml
   ├─ localization/
   │  ├─ es/counter.yaml
   │  └─ en/counter.yaml
   └─ scripts/
      └─ main.lua
```

## 2. The manifest

`pack.yaml`:

```yaml
id: counter
version: 0.1.0
layer: mods
api: "1.4"
name: "Victory counter"
description: "Counts the battles won in the game."
dependencies:
  - id: game
    min_version: 0.1.0
```

`api` says which version of the Lua API the mod is written for; without it the game does not mount a mod with scripts
([packs and ids](packs.md)).

## 3. The texts

`localization/es/counter.yaml`:

```yaml
victory: "Victoria número {count}"
```

`localization/en/counter.yaml`:

```yaml
victory: "Victory number {count}"
```

Keys are prefixed with the file name: this one is `counter.victory`.

## 4. The script

`scripts/main.lua` runs once when a game starts or loads and registers the handlers ([Lua API](lua-api.md)):

```lua
local victories = 0

hooks.on("battle.ended", function(t)
  if t.won then
    victories = victories + 1
    print("victory " .. victories .. " (" .. t.kind .. ")")
  end
end)

hooks.on("field.map_entered", function(t)
  print("map " .. t.map)
end)
```

Everything with `local`: creating a global is an error. The count lives as long as the game lasts; to save it with the
game you need a story variable (next step).

## 5. Try it

Check the pack without starting the game and then start it with your mods folder:

```sh
dwx validate --with <content>/base my_mods/counter
dwe-game --content <content> --mods my_mods
```

Start a game and win a battle: the log shows `mod counter: map ...` and `mod counter: victory 1 (normal)`. If the mod
does not appear, look in the log for the line that says why it was left out (a key missing in `pack.yaml`, another
API version) or for its script's error.

## 6. Save it with the game

Story variables travel with the saved game. The original uses the first 256; a mod asks for its own with a ruleset
that extends the original and enlarges the story capacity ([capacity](story.md#capacity)):

```yaml
# rules/counter/ruleset.yaml
id: counter
name: "Original with counter"
version: 0.1.0
extends: original
sections: [story]
```

```yaml
# rules/counter/story.yaml: the original's story section with the capacity enlarged
capacity:
  extra_variables: 1         # variable 256
```

With that ruleset, the script saves the count in variable 256:

```lua
hooks.on("battle.ended", function(t)
  if t.won then
    story.set_var(256, story.var(256) + 1)
  end
end)
```

A ruleset replaces whole sections, so `rules/counter/story.yaml` must also bring the rest of the original's section
(copy it from `rules/original/story.yaml` in the `game` pack). To play with it, add a [mode](modes.md) that uses it or
pass it with `--ruleset counter`.

## Next

- Change the look of the interface: [skins](skins.md).
- Touch the story events: [story API](story.md).
- Add a map: [new maps](new-maps.md).
