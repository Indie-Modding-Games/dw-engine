# debug

Development tools with Dear ImGui: a console with the log and a Lua command line, frame statistics and the state of the gamepads and actions. It is opened with the `debug_console` action (`º` or F1). From the Frame window the [asset viewer](../../guides/asset-viewer.md) is opened, which mounts the packs of a folder and shows their textures. It is not the game's UI: that is `ui`, with RmlUi.

## Tuning panel

The **Tuning** panel changes the ruleset while the game is running. It is opened with the `tuning panel` checkbox of the Frame window,
with the `/tuning` command in the console (lines that start with `/` are overlay commands, not Lua) or by starting the
game with `--tuning`. The app gives it the packs, the `RulesetStore` and the `GameClock` with `DebugUi::attachTuning`.

- **Ruleset:** chooses the ruleset among those of the packs (`RulesetStore::select`) and shows its revision. Each section is
  a tree of its values: numbers and texts (strategy names included) are edited in their field and
  applied with Enter, and the `true`/`false` ones (the `quirks`) with a checkbox, all with `RulesetStore::setValue`. If the
  checks reject it, the error comes out in red and the value stays as before. *What would reloading change?* lists
  what a reload would change (the adjusted values that are lost and what was edited in the files); *reload from files*
  reloads. *Save to file* writes the section to the file of its pack (that of the ruleset that defines it, even if it is
  inherited), changing only the values in place: the comments with the decomp citations stay.
- **Clock:** the game time, running or stopped, the speed (slider and buttons 0, 1, 10, 60 and 3600) and the jump to
  a time as when sleeping (`GameClock::jumpTo`).
- **Systems:** the active sections, which ruleset each one comes from and its file, with the revision. A system adds its
  own tab with `DebugUi::addTuningTab({name, rows})`: each row is a name and a value, or a gauge from 0 to 1.

The logic without widgets is in `src/tuning_model.hpp` (`TuningModel`, `buildTuningTree`, `yamlOfEdit`) and is tested in
`tests/tuning_model_tests.cpp`; the widgets, in `src/tuning_panel.cpp`.

### Manual test

1. `dwe-game --tuning` (or `º`/F1 and `/tuning` in the console). The Ruleset tab shows `original`, revision 1, and its
   sections (`care`, `clock`, `battle`…).
2. In `care › hunger › window_minutes` type `45` and Enter: the revision goes to 2 and `care.hunger.window_minutes =
   45` comes out in green.
3. In `clock › frames_per_minute` type `0` and Enter: `... frames_per_minute must be more than zero` comes out in red, the
   field goes back to `20` and the revision does not change.
4. Uncheck `care › quirks › sleeping_hungry_keeps_weight`: revision 3.
5. *What would reloading change?*: lists `care.hunger.window_minutes: 45 -> 90` and the quirk. *Reload from files*: the
   values go back to those in the file and the revision goes up.
6. Change `window_minutes` again and *save to file*: `git diff content/packs/game/rules/original/care.yaml` shows
   only that line changed, with the comments intact. Undo it with `git checkout`.
7. Clock tab: press `x3600` and watch the hours run; uncheck `running` and see that it stops; jump to 20:00.
8. Systems tab: the active sections, all from the `original` ruleset, with the current revision.
