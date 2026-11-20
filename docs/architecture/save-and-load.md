# Saving and loading

Status: implemented.

This document says how the player saves and loads: where they can save, how the slot is chosen, how to return to the
title screen and how to load from it. The file format, the migrations and the autosave are already in
[save.md](save.md); here only what the interface needs is changed. The citations are to the decomp (`dw_decomp`,
`src/main/`, `us` version) as `file.c: function: line`, to read, never to copy code or tables.

## 1. How the original does it

### 1.1 The essentials

In the original **the player never chooses a slot when saving**. The slot (card and block) is chosen once, when
starting a new game or continuing one, and each later save writes over that file without asking where
(`main_menu.c: tickMainMenu: 3929-3953`, which looks for the file of the slot already chosen). If "no
card" was chosen at the start, no save of the game happens (`main_menu.c: tickMainMenu: 3646-3647`; `partner.c: partnerTickSleep:
330-334`; `main_menu.c: openSaveMachine: 5284-5286`).

### 1.2 Where it saves

| Place | How you get there | Conditions | Locator |
|---|---|---|---|
| **Sleep** | Triangle menu, sleep option (`partnerSetState(3)`) | Only if the partner is sleepy (`condition & 1`); anywhere, no bed; the triangle menu only opens while walking, with no script, fade or text box | `game_menu.c: handleGameMenuSelection: 2178-2185`, `game_menu.c: addGameMenu: 1319-1323`, `tamer.c: tamerTickWalkingState: 517-528` |
| | After the fade to black the night is counted (`sleepRegen`, `handleSleeping`: the clock jumps to the wake-up time) and **then** the "Sleep / Save and sleep" menu comes up; what is saved is already the morning | Without a card the menu does not come up | `partner.c: partnerTickSleep: 278-366` (326-340) |
| | "Sleep" closes; "Save and sleep" saves; cancel does nothing | | `main_menu.c: tickMainMenu: 3881-3898` |
| | On closing: fade in, time of day, special evolutions on waking, the clock keeps going | | `partner.c: partnerTickSleep: 346-361` |
| **Save machine** | Routine 48 (`call_routine` 0x30) of a map script: the action is pressed in front of the machine, a yes/no comes up and then the routine | Scripts 165 and 178, maps 205 (`ROOM10`) and 218 (`ROOM08`), both the elder's house in the city. No cost | `script_interp.c: scriptInstruction64to7E: 1826-1830`, `script_interp.c: tickScript: 496-500`, `main_menu.c: openSaveMachine: 5273-5286` |
| | First it offers to **register the partner for versus mode** (40 slots); then "Do you want to save?" | If the partner is below level 3 it is not registered (notice) and goes on to the save question | `main_menu.c: tickMainMenu: 4204-4451`, `main_menu.c: registerBattleData: 5230-5245` |
| **End of the game** | Routine 54 (`call_routine` 0x36) of script 192 (maps 236 and 237, the ending) | Saves with the city as the return map and the "game cleared" state, and gives a fixed versus record in slot 39 | `script_interp.c: scriptInstruction64to7E: 1832-1836`, `main_menu.c: gameClearSave: 5318-5329`, `main_menu.c: awardMachinedramonData: 5299-5316` |

There are no other places: only `partner.c` and `script_interp.c` open these menus. The game clock is stopped while
they are open (`partner.c: partnerTickSleep: 282`; `script_interp.c: scriptPauseGame: 2736-2738`), and the game
is written with the clock running so that it runs when loaded (`main_menu.c: writeSavegame: 5080-5083, 5131-5133`).

### 1.3 The save itself

`main_menu.c: tickMainMenu: 3900-4057` (and its copy for the machine, 4259-4416):

1. It checks the card (`0x29`). If it is missing, swapped, unformatted or without the file, it shows the error with
   "The file will be overwritten / save?" (`0x30`) and lets you retry; **in game it never formats or creates a
   file** (4405-4413, 4046-4054).
2. It writes (`0x2B`): `writeSavegame` fills copy 0, the sum is calculated, it is duplicated in copy 1 and the
   block is written (`main_menu.c: tickMainMenu: 3955-4011`; `main_menu.c: writeSavegame: 5071-5134`;
   `main_menu.c: createSavegameChecksum: 5004-5014`).
3. If it goes well, the menu closes **without a message** (`main_menu.c: tickMainMenu: 3448-3452`). If it fails, error and return to the
   sleep menu or to the machine's question (`main_menu.c: setMemoryCardReadError: 4732-4737`).

### 1.4 The title screen

| Step | What it does | Locator |
|---|---|---|
| Menu 0 | NEW GAME, CONTINUE GAME, DELETE GAME, BATTLE MODE; continue and delete grayed out without a card | `main_menu.c: tickMainMenu: 3349-3385`, `main_menu.c: renderInitialMenu: 5359-5363` |
| New game | Card 1, card 2 or **no card** (notice: it will not be possible to save). An unformatted card can be formatted here | `main_menu.c: tickMainMenu: 3454-3507` |
| | With 15 blocks used: "no free block" error | `main_menu.c: tickMainMenu: 3509-3520`, `main_menu.c: getCardUsedBlockCount: 4757-4790` |
| | Only an **empty** slot is chosen (a game is not overwritten) and "Record new adventure?" is confirmed (with the cursor on "Yes") | `main_menu.c: tickMainMenu: 3522-3556` |
| | The file is created right away, with a default game in the "new game" state | `main_menu.c: tickMainMenu: 3558-3636`, `main_menu.c: initializeDefaultSavegame: 4911` |
| Continue | Card, list of slots (only valid ones can be chosen), "Continue the adventure?", read | `main_menu.c: tickMainMenu: 3659-3776` |
| Check | Copy 0 if its sum matches; if not, copy 1; if neither, "data error" and back to the title screen | `main_menu.c: tickMainMenu: 3758-3774` |
| Card removed | During the lists, back to the title screen | `main_menu.c: getMenuOnCardChange: 4708-4730` |

**The slot list** reads only the card header of each file, not the game
(`main_menu.c: loadSaveSlotData: 4792-4863`): slot number, tamer name, partner **species** (not its
nickname) and a line for the place (`main_menu.c: drawSaveSlotText: 3193-3198`). The `us` version leaves the place empty; `jp` and
`eu` write the map (`main_menu.c: tickMainMenu: 3982-3994`). An empty slot says "Not used"
(`main_menu.c: renderSaveSlotBox: 1750-1775`). It shows no play time, date or money. There are 15 slots per card
(`main_menu.c: SAVEGAME_SLOT_INFO: 208`) and two cards.

### 1.5 Returning to the field when loading

`main.c: main: 3411-3529`. After the title screen, depending on the saved state: 0 is the new-game scene (a file that
was created and never saved in game, 3439-3458), 1 is a normal game and 2 a cleared game, which plays a
video first (3459-3491). In the last two, the tamer is created, the partner with its species, the saved map with its
header script (and its music), the clock, and the tamer and partner are placed where they were
(`main.c: initializeLoadedMap: 3791-3899`). If the partner collides with certain NPCs on appearing, both go to the saved
exit (3821-3857). Then a fade from black (3494).

### 1.6 Returning to the title screen

**It does not exist.** Neither a menu option nor a button combination (`main.c: pollInputGame: 4035-4054`,
`main.c: gameLoop: 3933-3962`); the pause only freezes (`utils2.c: handlePause: 402-432`) and the partner's death leads
to rebirth, not to "game over". The only return is after the credits: routine 53 sets state 3, the loop exits and
returns to the title screen (`script_interp.c: 516-520`, `main.c: main: 3522-3524`).

## 2. What the engine builds on

- The format, the slots (`slot_01`…`slot_15`, `autosave_01`…), the `.bak` copy, the "damaged" state in the list and the
  migrations: [save.md](save.md), `packages/game/save` (`SaveSlots::list`, `SlotInfo`).
- `StoryState` and its `story` section (version 2: the triggers and the 35 variables that the original saved, plus the
  ruleset's extra ones; `readMissing` starts from a new game and marks as raised the species of the `evolution`
  section). When the session is recreated, its adapter is also recreated.
- Saving from a route: the debug command `save("slot_NN")` (`apps/game/src/save_command.hpp`).

## 3. The design

### 3.1 The pieces and their owners

| Piece | Where | What it does |
|---|---|---|
| `SaveSummary` (header) | `packages/game/save` | What the list shows: tamer, partner species and nickname, map. See [3.3](#33-the-slots) |
| `SaveSlots` | `packages/game/save` (exists) | List, load, load from the copy, save. No behavior changes |
| `AppFlow` | `apps/game` (`app_flow.hpp`, a template over the game so it can be tested without a window) | The app's state machine: `Title` → `Playing` → `Title`. It creates and destroys the game in play; the previous one goes away before the new one is created |
| `PlaySession` | `apps/game` (`play_session.{hpp,cpp}`) | Everything that lives as long as a game lasts: `GameSession`, `FieldScene`, the battle, the partner menu, the HUD and the tuning panel. It is built whole from a slot or from a new game and destroyed whole. The script VM belongs to the app: its game commands are queued in `SessionCommands` and, with no game, discarded with a warning |
| `SaveFlow` | `apps/game` (`save_flow.{hpp,cpp}`) | The **game's slot** (the one chosen when starting or continuing, or none) and the player's saves: sleep, machine, end of the game. It publishes the result for the interface |
| Documents | `content/packs/game/ui/` | `main_menu` (the title screen), `save_slots` (slot list), `dialog` (yes/no and notices, shared), `sleep_menu`, `pause_menu` (with "Sleep" and "Return to title") |
| Texts | `content/packs/game/localization/{en,es}/` | `save.yaml` (extended), `main_menu.yaml`, `pause_menu.yaml` |
| Routes | `tests/autotest/`, `apps/game/src/autotest.cpp` | The save routes ([section 6](#6-routes-and-tests)) |

`packages/game/save` still does not know the systems ([rule 4](dependency-rules.md)): the summary is filled in by the app when saving.
The interface talks to the app the way it already does, with `ui.send` and published settings (`ui.md`, "Commands and settings").

### 3.2 The app states

```text
startup ──► Title ──(New game / Continue)──► Playing ──(Back to title)──► Title
   │                                            ▲
   └── --load, --start, route without main_menu ┘
```

- **Title**: no game. The title screen is drawn over black, like the original (`ui.md`, table of the original look). There
  is no `GameSession` or `FieldScene`.
- **Playing**: one `PlaySession`. Entering `Playing` is always building a new `PlaySession`; leaving is
  destroying it. That way **reloading in the middle of a game** is leaving to `Title` and entering again: there is no need for a "warp to
  any place" or to undo state by hand, and whatever the previous game left (queued scripts, battle,
  events) disappears with it.
- **Transactional load**: the new `PlaySession` is created and loads the game **before** letting go of anything. If the load
  fails, the new one is destroyed, the error is shown and the app stays in `Title`. This also fixes `loadFile` leaving
  half-done state: nobody uses that session.
- `--new-game`, `--load`, `--start` and routes without `main_menu: true` go straight into `Playing`, with no title screen.
  When entering from the title screen, the loading time does not count as play time (there is no burst of ticks).

**Returning to the field** when loading: `FieldScene::create` with the saved map and position, as startup already does
with `--load` (`apps/game/src/game.cpp: runGame`). The partner appears where it was. The original's repositioning when the
partner collides with an NPC (`main.c: initializeLoadedMap: 3821-3857`) is not done.

### 3.3 The slots

- **How many**: the 15 manual ones from `save/settings` (like a card in the original). The autosave has no slots
  of its own (since 2026-11-04): it writes the game's slot, as in [save.md](save.md). A single folder, not two cards (deviation, [section 5](#5-differences-from-the-original)).
- **The game's slot** (`SaveFlow`): it is chosen when starting (an empty slot or "without saving") or when continuing (the
  slot that is loaded). Every player save goes to it, without asking, as in the original. `--load slot_NN` sets it
  to that slot; `--load autosave_NN`, to the one that old autosave says (below); a route without a games folder
  (`SessionDesc::storage` null) plays "without saving". "Play without saving" in the slot list also starts with no
  folder (`PlayStart::withoutSaving`): that game writes nothing, neither saves nor autosaves, as its
  question warns.
- **Autosave and slot**: the autosave **writes the game's slot**, the same one as the player's saves
  (`GameSession::writeRequestedAutosave`), with its atomic write and the previous game in `.bak`. Saving and
  autosaving are a single game: there are no two files to choose from. Without a slot (a new game outside the title screen) it goes
  to the first empty one; it never writes over another game; with no empty ones it is skipped and the notice says so (rules in
  [save.md](save.md#slots-autosave-and-backups)). The "Autosave" option (Yes/No, in the title screen's and the game's
  options) turns it off.
- **Old autosaves**: the `autosave_NN.yaml` files from earlier versions point to the manual slot of their game
  (`summary.slot`). They stay in the list while they are on disk ("Old autosave N"), they can be loaded (the game
  stays in that manual slot if it is still its own) and erased, but they are never written. Erasing the manual slot also erases
  its old autosaves and their `.bak` files (`SaveSlots::erase`).
- **The list** (`save_slots`, published as `save.slot.<n>.*` settings):

| Column | Where from | Original |
|---|---|---|
| Slot number | The file name | Same |
| Tamer name | `summary.tamer` | Same |
| Partner species | `summary.species` (an ID; the interface puts the name in the language) | Same (`us` shows the species, not the nickname) |
| Place | `summary.map` (a map ID, with its translated name) | Like `jp`/`eu`; `us` left it empty |
| "Not used" | Empty slot | Same |
| "Damaged" / "Has backup" | `SlotInfo::Damaged`, `hasBackup` | New (the original did not list it as valid) |
| Play time and save date | `play_seconds`, `updated` | Documented **improvement**, from the header that already exists |
| Game variant | `summary.mode` (Classic Enhanced or Folder Mode, [game variants](game-variants.md)); empty reads as Folder Mode | New: the original had one way to play |

The old autosaves that remain go below the manual ones ("Old autosave N"), only when continuing and
when erasing (they are not chosen when starting).

The list remembers its page and the chosen row while a question over it is open, and starts anew when
the title screen's menu opens it: the game then shows it with the `fresh` argument (`on_show(document, args)`), also
when returning to the title screen after playing.

**Header**: `format_version: 3` adds to it

```yaml
summary:
  slot: "slot_03"          # the game's manual slot; "" if played without saving
  tamer: "Juan"
  species: "base:<species>"  # the species ID, not the name
  nickname: "Chispa"
  map: "base:<map>"          # the map ID
```

with its migration step `2 → 3`, which sets an empty `summary` (`packages/game/save` does not know the sections). The list
fills in that of such a game with what it reads from its `partner` and `field` sections (`session::summaryFromSections`,
which the app gives to `SaveSlots::setSummaryReader`), and the next save writes it whole. `label` stays as it is. The IDs follow
the phase 4 rule (IDs, not indices).

### 3.4 New game

`New game` → slot list with **only the empty ones** selectable and a final entry "Play without saving" →
"Record new adventure?" (yes/no; it starts on "Yes", like the original, and the other questions on "No") → the new-game
`PlaySession` is created and **it is saved right away in the slot**, as the original creates the file at the start. With all 15 taken: notice "There are no free slots" and only "Play without
saving" or going back. "Play without saving" warns that it will not be possible to save (like "no card").

Without a games folder (the smoke routes) `New game` goes straight in, with no list: it is the "no
card" path. That way `route-loop-keyboard` and the others stay the same, with the `play` button.

### 3.5 Continue

`Continue` (grayed out if there is no game, like without a card) → list with the saved slots that can be chosen →
"Continue this adventure?", **with the cursor on "Yes"** (documented improvement: it is the expected answer; the original
asks the same) → transactional load ([3.2](#32-the-app-states)) → `Playing` with a fade from black.

**Continuing a slot loads the latest of that game**: the slot holds both the player's saves and the
autosaves, so "Yes" loads with no further questions (`SaveFlow::answer`). The list starts with the cursor on the
most recently saved game (`save.slots.newest`, in both skins; documented improvement). The old autosaves
stay in the list as rows of their own ("CONTINUE OLD n"; the box below says "Old autosave n").

With the original look the list is the original's (`main_menu.c: renderSelectCardSlot`): one row per slot,
"CONTINUE SLOT n" and then, if any remain, "CONTINUE OLD n", the empty ones grayed out and not selectable, eight in
view with arrows if there are more. The game publishes how many manual ones there are (`save.slots.manual`) and how many old
autosaves (`save.slots.autosave`). Under the
list, a box with the summary of the focused row (partner; place, play time and date), which the original does not
have (documented improvement). The question comes up over the list (`dialog.at_title`).

### 3.5b Erase

`Delete game` (grayed out with no games, like `Continue`) → the same list with "ERASE SLOT n" and the slots that this
game can write (not those of a newer game) → "Erase this adventure?" **with the cursor on "No"** → the
slot is left empty (`SaveSlots::erase`): its file, its `.bak`, the copy a co-op guest's return kept of it
(`.before_coop`, [multiplayer.md](multiplayer.md) §5.6) and the old autosaves of that game (and their
`.bak` files) are erased, so no copy is left of a game that the player erased. If it cannot be erased, notice "save.erase_failed" and the error goes to the log. After erasing it returns to the
list. The original erases the card file without a copy (`main_menu.c: tickMainMenu`).

### 3.6 Sleep

`pause_menu` gains **"Sleep"**, active only if `Partner::canSleep()` (the original grays it out the same way) and the menu
could be opened over the field: no combat or fade (`tamer.c: 517-528`). On choosing it, like `partner.c: partnerTickSleep`:

1. The menu closes, the partner comes up to the player and both play their sleep animations (`sleep.scene`,
   [game module](../modules/apps/game.md#sleeping)); then the screen fades to black: `sleep.fade.out_frames` (40)
   original frames, with the stepped curve
   of `fade.c` (the step is `160 / (frames + 1)`, the same calculation that gives the 7 of the map change). On the
   last frame, `GameSession::fallAsleep` (the clock jumps to the morning; it already existed). `SleepScreen`
   (`apps/game/src/sleep_screen.cpp`) carries it; the clock waits, the field goes on without the player and the pause menu does not open
   on top.
2. If the game has a slot, `sleep_menu`: "Go to sleep" / "Save and go to sleep". Cancel does nothing, like the original.
   Without a slot nothing comes up.
3. "Save and go to sleep" saves in the slot (`GameSaves`, `apps/game/src/game_saves.cpp`). First it looks at the slot
   (`SaveSlots::list`): if it no longer holds this game, it asks or warns instead of writing over it ([4](#4-errors)). If it
   fails, a notice with the error and back to step 2. The questions start on "No".
4. Fade in of `sleep.fade.in_frames` (40) frames. On frame `sleep.fade.control_frame` (35) the player
   regains control, the clock goes on and `GameSession::wakeUp` runs the special wake-up evolutions
   (`SessionSystem::afterWakeUp`): the save of step 3 saves the partner from before evolving, like the original.
   The last frames of the fade are drawn with the player already moving and the pause menu does not open until it ends
   (`FADE_PROTECTION`).

The clock is stopped while the menu is open (the loop already pauses with modal documents). When it ends well there is no
message, like the original; the autosave notice, if it comes up, is separate.

### 3.7 Save machine and end of the game

`SaveMachine` (`apps/game/src/save_machine.cpp`) opens the machine when the elder's house script calls
routine 48 (`StorySceneGlue`; the script continues when it closes) or with the debug aid `save_machine("")`. Its menus
are those of `main_menu.c: openSaveMachine` and `tickMainMenu` 0x3C to 0x46, in `SaveMachineFlow`
(`save_machine_flow.{hpp,cpp}`, without interface):

- **0x3C, registration**: the partner's card (name, species, HP, MP, offense, defense, techniques 1 to 3) and "Do you want to
  register it?". Yes leads to the list; no or cancel, to "Do you want to save?".
- **0x3D, list**: ten rows that scroll through the 39 slots of the registry (40 is the end-of-game gift,
  `awardMachinedramonData`); an empty one shows a dot. Cancel returns to 0x3C.
- **0x3E, confirm**: "Do you want to register `<name>`?" and the note that it is saved on registering. Yes registers
  (`registerBattleData`: `BattleRegistry::registerAt`) and saves; no returns to the list; cancel, to 0x3C.
- **0x45**: "Do you want to save?"; yes saves, no closes; it does not allow cancel.
- **0x44**: without a slot (the game started without saving, the original's "no card"), the notice that it cannot be
  registered; it closes with any button or only after 0x79 original frames (6.05 s).
- **0x46**: a partner below Rookie is not registered; the notice and then "Do you want to save?".

The save is the sleep one ([4](#4-errors)): to the game's slot, with the same check (another game,
damaged, from a newer game) and its dialogs in the box of the original's overwrite question (0x42). The
card checks (0x3F to 0x43, "Checking the card", "Saving") have no equivalent: the
PC save waits for nothing and the machine closes when done, like the original after writing (menu 9), without a notice.
A registration whose save is not written is undone: the original stores it inside the file itself
(`SAVE_FILE.saves[0]`). The registry is the `battle_registry` save section (`packages/session`,
`battle_registry.{hpp,cpp}`); a game without it starts with none registered. The field and the clock wait while the
machine is open. The boxes are the original's, where the original draws them (`ui/save_machine`, in both
looks). The registered ones fight in versus mode (`BattleRegistry`). The end-of-game
save (routine 54, `gameClearSave`) is done: it marks the game as finished (`cleared: true` in the `field` section,
the `MAIN_STATE` 2), saves it with the city from `getFileCityTopMap` as the return map and the player's position, and when
it is loaded the ED2 movie plays before the field (`packages/session/include/dwe/session/game_clear.hpp`, `SaveMachine::openClearSave`); the
machine and the sleep menu go back to `MAIN_STATE` 1.

### 3.8 Save, return to the title screen and quit the game

It is an **optional PC improvement**, on purpose: the original has no way to return to the title screen except by finishing
the game ([1.6](#16-returning-to-the-title-screen)). It does not change rules and whoever does not use it plays as in the original.

`pause_menu` gains **"Save"**, the quick save: it opens the **Options** page (`Esc` / `settings`) at the top, with
**"Save"**, **"Return to title"** and **"Quit game"** in a bar above its tabs in both looks
(↑ from the first row reaches "Save", ← / → move among the three; see
[in-game options](../modules/apps/game.md#in-game-options)) (in
the modern one they are also on the main page; in the original, where the icon grid stays faithful, it is the
visible place). With `quick_save` → `PlaySession::quickSave` writes the game
to the adventure's manual slot (`GameSession::homeSlot`: the one that loaded it or the one it last saved to) or, in a
new game that has not saved yet, to the first free manual slot; with no free slots, a `dialog` warns about it
(`save.slots.full`) and a write failure does too (`save.failed`). It works at any moment when the menu can be
opened (never during a sleep, a combat or a care scene, like `to_title`), shows the save notice
(`ui/save_indicator`) and leaves the menu closed.

`pause_menu` gains **"Return to title"** (above "Quit game"), which sends `to_title` → `dialog`: "Anything you haven't saved will be lost.
Back to the title?" (it does not come up during a sleep or a combat) → `AppFlow` destroys
the `PlaySession` and `TitleScreen::show()` returns to the main menu, like the original after the ending (`main.c: main`).
It saves nothing: whoever wants to keep the adventure presses "Save" first, or uses "Quit game", which does ask.

`pause_menu` also gains **"Quit game"**, which in a game sends `quit` → `dialog`: "Save the game before
quitting?", with the cursor on "Yes" — the reminder to save before closing. "Yes" writes the quick save and,
if it succeeds, `AppFlow` closes the window; if the save fails, the notice appears and the game keeps running. "No"
(no, and also Cancel) closes the dialog without quitting. From the title screen, the main menu's exit still closes without
asking. The choice of destination —the title screen or closing the game— is the two menu entries.

### 3.9 Texts (en/es)

New keys; the texts are ours, not those from the disc.

| Key | en | es |
|---|---|---|
| `main_menu.new_game` | New game | `Nueva partida` |
| `main_menu.continue` | Continue | `Continuar` |
| `save.slots.title_new` | Choose a slot for your new adventure | `Elige una ranura para tu nueva aventura` |
| `save.slots.title_continue` | Choose an adventure to continue | `Elige la aventura que quieres continuar` |
| `save.slots.slot` | Slot {number} | `Ranura {number}` |
| `save.slots.empty` | Not used | `Sin usar` |
| `save.slots.damaged` | Damaged save | `Partida dañada` |
| `save.slots.newer` | From a newer version | `De una versión más nueva` |
| `save.slots.autosave` | Old autosave {number} | `Autoguardado antiguo {number}` |
| `save.slots.no_save` | Play without saving | `Jugar sin guardar` |
| `save.slots.no_save_warning` | You won't be able to save this game. Start anyway? | `No podrás guardar esta partida. ¿Empezar igualmente?` |
| `save.slots.full` | There are no free slots. | `No hay ranuras libres.` |
| `save.confirm_new` | Record new adventure? | `¿Grabar una nueva aventura?` |
| `save.confirm_continue` | Continue this adventure? | `¿Continuar esta aventura?` |
| `save.sleep` | Go to sleep | `Dormir` |
| `save.sleep_and_save` | Save and go to sleep | `Guardar y dormir` |
| `save.ask_save` | Do you want to save? | `¿Quieres guardar?` |
| `save.saving` | Saving… | `Guardando…` |
| `save.failed` | The game could not be saved. | `No se ha podido guardar la partida.` |
| `save.load_failed` | The game could not be loaded. | `No se ha podido cargar la partida.` |
| `save.start_failed` | The game could not start. | `No se ha podido empezar la partida.` |
| `save.damaged_use_backup` | This save is damaged. Load the previous copy? | `Esta partida está dañada. ¿Cargar la copia anterior?` |
| `save.damaged_no_backup` | This save is damaged and has no copy. | `Esta partida está dañada y no tiene copia.` |
| `save.too_new` | This save is from a newer version of the game. | `Esta partida es de una versión más nueva del juego.` |
| `save.home_damaged` | This adventure's save is damaged. Save over it? | `La partida de esta aventura está dañada. ¿Guardar encima?` |
| `save.home_other` | This slot holds another adventure. Save over it? | `Esta ranura tiene otra aventura. ¿Guardar encima?` |
| `save.home_newer` | This slot holds a save from a newer version of the game. It cannot be saved over. | `Esta ranura tiene una partida de una versión más nueva del juego. No se puede guardar encima.` |
| `pause_menu.sleep` | Sleep | `Dormir` |
| `pause_menu.quick_save` | Save | `Guardar` |
| `pause_menu.to_title` | Back to title | `Volver a la portada` |
| `pause_menu.to_title_confirm` | Anything you haven't saved will be lost. Back to the title? | `Se perderá lo que no hayas guardado. ¿Volver a la portada?` |
| `save.quit_confirm` | Save the game before quitting? | `¿Guardar la partida antes de salir del juego?` |
| `common.yes`, `common.no` | Yes, No | `Sí, No` |

The technical detail of the error goes to the log in English; the interface shows the translated text and, below it, the file
name (`dialog.file`).

## 4. Errors

| Case | Original | Engine |
|---|---|---|
| Damaged game (cannot be read, cut off, wrong sum or YAML) | Copy 1 if 0 fails; if both do, "data error" and back to the title screen (`main_menu.c: tickMainMenu: 3758-3774`) | The list marks it "damaged". Choosing it: if there is a `.bak`, "load the previous copy?" → `loadBackup`; if not, a notice and it stays in the list. It is never erased or overwritten on its own |
| A section cannot be read or migrated | — | Load error, transactional load: the app stays on the title screen, the slot does not change |
| Version newer than the game | — | The list shows it as "from a newer version" (the `problem` of `SlotInfo`), it cannot be loaded or overwritten when starting |
| Older version | — | It is migrated on loading without asking ([save.md](save.md)); the file on disk does not change until the next save, which leaves the old version in `.bak` |
| Game saved before a system's section existed | — | The `readMissing` of its system ([save.md](save.md)); for `story`, a new game plus the raised species |
| All slots taken when starting | "No free block", or play without a card (`main_menu.c: tickMainMenu: 3509-3520`) | "There are no free slots": play without saving or go back. Overwriting is not offered (the original does not either; for erasing, [3.5b](#35b-erase)) |
| The write fails in game (disk full, permissions) | Error and return to the sleep menu or to the question (`main_menu.c: tickMainMenu: 4001-4009`) | Same: `save.failed` and back to `sleep_menu` or to the question. The atomic write leaves the previous game intact |
| The slot's file disappeared during the game | "The file does not exist", it is not created (`main_menu.c: tickMainMenu: 3929-3953`) | It is written again in the slot (minor deviation: there is no card to swap) |
| The game's slot changed outside the game during play | — (the swapped card is the case above) | When saving in game: damaged, "save over it?"; with another game, "save over it?"; from a newer game, notice and nothing is written. "No" returns to the sleep menu without writing |

"The same game" is the same `created` in the header (the time it started, to the second). Two games started in the same
second count as the same, so saving one over the other does not ask.

## 5. Differences from the original

| What | Type | Why |
|---|---|---|
| Choosing the slot only when starting or continuing, saving without asking where | Vanilla | |
| Sleep → "Sleep / Save and sleep" after counting the night, without cancel | Vanilla | |
| Save machine and end of the game (dialog and save) | Vanilla | |
| New game only in an empty slot; "without saving" as "no card" | Vanilla | |
| List: tamer, species, "Not used" | Vanilla | |
| List: place | Vanilla (`jp`/`eu`) | `us` left it empty |
| One folder of 15 slots, not two cards | Documented improvement | There are no cards on a PC; the number comes from `save/settings` |
| List: play time, date, "damaged", "has backup", old autosaves separately, cursor on the most recent one when continuing | Documented improvement | Data that the format already has; the autosave is the approved deviation from [save.md](save.md) |
| Previous copy in `.bak` instead of two copies in the file | Documented improvement | Already in [save.md](save.md); it covers the same case |
| Return to the title screen from the pause | Documented improvement | The original has none; it is interface, with no effect on the rules |
| No formatting cards, no "remove the card" | No equivalent | There are no cards |
| Slot file erased outside the game: it is rewritten | Minor improvement | |
| When saving while sleeping, the game's slot is checked first (another game, damaged, from a newer game) | Documented improvement | The original writes over it without looking; a PC lets the folder be touched outside the game |
| The machine registers for versus mode | Vanilla | The registered ones fight in versus mode |
| The machine does not show "Checking the card" or "Saving" | No equivalent | There is no card; the PC save is immediate |
| Erase game on the title screen; its `.bak` and its old autosaves are also erased | Vanilla + documented improvement | The original erases the file without a copy |
| Continuing a slot loads the latest of that game: the autosave writes the same slot ("Autosave" option, Yes/No) | Same + documented improvement | The original does not autosave: loading the slot is loading the last save; here too, with the autosaves inside |
| The continue and erase questions start on "Yes" and "No" | Documented improvement | The expected answer and the safe one |
| Reposition the partner if it collides with an NPC on loading | Not done | It needs the field's NPCs at that point |

## 6. Routes and tests

**Routes** (`tests/autotest/`):

- `route-save-load` starts a new game in `slot_01` from the title screen, walks to the elder's house, moves the clock on
  and saves with the debug command; the gate then runs `route-save-load-check`, which loads that slot from the same saves
  folder and checks the map, the position, the partner, the clock and the bag (`follow_up_routes` in
  `scripts/gate/route_jobs.sh`). Both run in `en` and `es`.
- `route-sleep-save` ([3.6](#36-sleep)): a new game, the clock run on until the partner is sleepy, "Sleep" and "Save and go to sleep".
- `route-save-actions` ([3.8](#38-save-return-to-the-title-screen-and-quit-the-game)): quick save, back to title and quit.

The routes that play with saves of their own run with an empty saves folder of the job's (`saves_routes` in
`route_jobs.sh`).

**Unit tests:**

- `packages/game/save`: the v3 header is written and read back byte for byte; a v2 game migrates to v3 with an empty summary,
  and the list fills it in with the game's reader. `packages/session`: the summary taken from the sections; with
  missing sections, empty.
- `SaveFlow`: continuing starts on the most recent game and loads the slot without further questions; an old
  autosave comes up separately and is loaded (`save_flow_tests.cpp`). `AppFlow`: after playing on, the autosave writes the
  game's slot and loading it gives the later state (`app_flow_tests.cpp`).
- `SaveFlow`: the game's slot when starting, when continuing, from an autosave and without saving; save on sleep
  with and without a slot; write failure (`MemorySaveStorage::failNextWrite`) returns to the menu; machine and end of the game
  with their return map.
- `AppFlow`: Title → Playing → Title → Playing with another slot leaves nothing from the first game (clock, inventory,
  `story`); a load that fails leaves the app in Title and the slot untouched; damaged game with and without a copy.
- The slot list: empty, saved, damaged, newer, old autosaves, full.
