# Packs and ids

The full reference is in [packs and VFS](../architecture/vfs.md); this page is what someone making a mod needs.

## The `pack.yaml`

```yaml
id: my_mod                 # a-z, 0-9 and _; unique among the mounted packs
version: 1.0.0             # MAJOR.MINOR.PATCH
layer: mods                # mods or user for a mod
api: "1.4"                 # the Lua API it is written for: required in mods and user
priority: 0                # within the layer, the highest wins
name: "My mod"             # one text, or one per language with en: { en: "My mod", es: "Mi mod" }
description: "What it does, for people."
dependencies:
  - id: game
    min_version: 0.1.0
schemas:                   # which schema each new YAML in the pack follows (see below)
  mode: [modes/*/mode]
```

- **`layer`.** A mod (what is mounted with `--mods`) can only be `mods` or `user`; `base` and `remaster` belong to the
  engine and a mod that declares them is left out. `user` goes on top of `mods`: it is for the player's own tweaks.
- **`api`.** `"1.x"` accepts any 1.y; `"1.4"` asks for 1.4 or a later one. A mod with another major version or with a
  minor newer than the engine's is left out with the reason, and so is one with Lua files and no `api`. For now, a mod
  without `api` that brings no Lua files (data only, like the packs the launcher and the mapper wrote before this
  change) is still mounted, with a warning in the log. The current version is in the [Lua API](lua-api.md).
- **`dependencies`.** Each one must be mounted, with its minimum version, in the same layer or a lower one. Almost
  every mod depends on `game` (the game's rules, menus and texts) and, if it touches extracted content, on `base`.
- **`schemas`.** The rules of all packs apply to all: a YAML that replaces one of the game's already has its schema.
  Only the new document types need to be declared.

## Asset ids

- A file's id is its path inside the pack without the extension: `ui/hud/style.rcss` is `ui/hud/style`.
- Segments of `a-z`, `0-9` and `_` separated by `/`, with no capitals or dots; a file name carries a single dot.
- **Replace:** a file with the same id as one from a pack below replaces it whole, even if the extension changes.
  **Add:** a new id is simply added.
- Exceptions: map patches (`map/<id>/patch`) and story patches are applied on top instead of replacing
  ([maps](../formats/maps.md#patches), [story](story.md#patches-per-section)), and the content tables are composed
  by entries (below).

## Content ids

The tables (`data/items`, the maps…) are composed by entries: a mod that brings `data/items` does not replace the
whole table, it changes the entries with an id that already exists and adds the new ones at the end.

- What is from the original is named by its stable name; what a pack adds, by `<pack id>:<name>`
  (`my_mod:sword`). On disk, `my_mod:meadow` lives in `map/my_mod/meadow/`.
- The internal number of a new entry is set by the engine when composing and changes with the mounted packs: a mod
  must not store it or compare it.
- The visible name of what is added goes in the pack's text table, `text/<language>/<pack id>.yaml`, with the key
  `<block>.<name>` (`digimon_names.pebblemon`).

A complete example, with a new species and a new map, is the tests' pack
[`tests/mods/example_mod`](../modules/tests/mods.md).

## Texts

The interface texts come from `localization/<language>/<area>.yaml` of all the packs, and each key is prefixed with
its file's name: the `title` key of `localization/es/modes.yaml` is `modes.title`. A mod puts its own in a file with
its id (`localization/es/my_mod.yaml` → `my_mod.title`), in each language it offers. To change a game text, bring the
same file with only the keys it changes: the text tables are merged key by key, unlike the rest of the assets
([localization](../architecture/localization.md#packages-new-languages-and-mods)).
