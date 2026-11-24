# Glossary

Project terms and their names in the code. If a term is not here, it is added in the PR that introduces it.

| Term | Meaning | In the code |
|---|---|---|
| Action | What the player wants to do (`confirm`, `move`…), independent of the device ([ADR 0014](adr/0014-gamepads-and-input.md)) | `dwe::input` |
| ADR | A record of an architecture decision ([adr/](adr/index.md)) | — |
| Asset | Any piece of content: model, texture, map, text, script, sound | `dwe::assets` |
| Base | The content layer generated from the user's disc | `content/base/` |
| Cook | Convert a source asset into the format the GPU loads | `dwx cook` |
| Digimon contract | The skeleton and animation list every Digimon must have | `dwx validate` |
| Decomp | The project that rebuilt the C code of the original game. Here it is the reference | — |
| Delta / recipe | What is shipped instead of an asset derived from the original; it is applied on top of the user's base | — |
| ECS | Entities, components and systems: the world model ([ADR 0012](adr/0012-ecs-with-entt.md)) | `dwe::scene` |
| Reference sheet | A summary of how a system of the original worked ([reference/](reference/index.md)) | `docs/reference/` |
| Source (state) | An asset in an open, editable format | — |
| Asset ID | A stable name for an asset, like `map/file_city_01/background` | — |
| Game mode | A way of playing chosen for a new game: a preset of rulesets and, optionally, fixed options ([game variants](architecture/game-variants.md)) | `dwe::game::modes` |
| Game variant | A game mode that offers one of the two main experiences ([game variants](architecture/game-variants.md)) | `dwe::game::modes::ClassicMode`, `dwe::game::modes::FolderMode` |
| Save type | The variant chosen when a new save is created and stored in that save; it replaces the old hidden-content activation ([game variants](architecture/game-variants.md#choosing-a-variant)) | `PlayStart::modeId`, `mode.id` |
| Pack | A content layer with its manifest (`pack.yaml`) | `dwe::assets::PackManifest` |
| Partner | The Digimon the player raises | `dwe::game::partner` |
| Design pillar | A GDD sentence that says what part of the game's essence is not touched | — |
| High definition | How the engine sharpens the field's backgrounds and the models' textures at run time ([high definition](architecture/high-definition.md)) | `dwe::render` |
| Remaster | The content layer above the base: images enlarged by `dwx upscale`, and any HD art or music a pack brings | — |
| Ruleset | A set of game rules and values, as data ([ADR 0010](adr/0010-own-rules-with-rulesets.md)) | `content/packs/game/rules/<id>/` |
| Tamer | The character the player controls | — |
| VFS | Virtual file system that stacks the packs | `dwe::assets` |
| Warp | The point on a map that leads to another | `dwe::game::field` |
