# Saved games

How the engine saves a game, how it is versioned and how a system adds its own part. The code is in
[`packages/game/save`](../modules/game/save.md) and guarantees that a saved game loads the same after updating the game, or is migrated.

How the player saves and loads from the interface (where it is saved, the title screen, the slots, going back to the
title screen): [save-and-load.md](save-and-load.md).

## The file

A saved game is a readable YAML document, written with `core::emitYaml`: the same game always gives the same text,
and a file that is read and written again comes out byte-for-byte identical.

```yaml
format: dwe-save
format_version: 4
game_version: "0.1.0"
ruleset: "original"
label: "File City"
summary:
  slot: "slot_03"
  tamer: ""
  species: "base:<species>"
  nickname: ""
  map: "base:<map>"
  mode: "classic"
created: "2026-10-01T08:00:00Z"
updated: "2026-10-02T21:30:15Z"
play_seconds: 5400
autosave_reasons:
- partner_sleep
- new_day
sections:
  inventory:
    version: 1
    state:
      ...
  partner:
    version: 2
    state:
      ...
complete: true
```

| Key | What it is |
|---|---|
| `format` | Always `dwe-save`: it tells a saved game apart from any other YAML |
| `format_version` | The version of this structure (the keys above and how the sections are stored) |
| `game_version` | The version of the game that wrote it, for people and bug reports |
| `ruleset` | The ruleset the game is played with |
| `label` | The text in the slot list (the map, the partner's name...) |
| `summary` | What the slot list shows ([save and load](save-and-load.md#33-the-slots)): `slot`, the game's manual slot (the one that each of its autosaves also saves to; `""` if it has none); `tamer`, the tamer's name; `species`, the partner's species ID; `nickname`, its nickname; `map`, the map ID; `mode`, the game's [variant](game-variants.md) (its mode id, empty in a game from before format 4, which the list reads as Folder Mode). The game fills it in when saving; a text the game does not have yet is left empty (today `tamer` and `nickname`) |
| `created`, `updated` | When the game started and when it was last saved, in UTC (`YYYY-MM-DDTHH:MM:SSZ`) |
| `play_seconds` | Play time in seconds |
| `autosave_reasons` | Why it was autosaved, with the name of each reason in the order it was requested (see [below](#when-it-autosaves)); `[]` if the player requested it. They are names and not numbers, so a reason from a newer game is listed the same |
| `sections` | The saved state of each system, sorted by name, each with its `version` and its `state` |
| `complete` | Always `true` and always the last key: a file cut anywhere gives an error instead of losing its last sections |

The module never reads the clock: whoever saves sets `created` and `updated`, so tests and replays are
deterministic. An unknown or missing key is an error, as in the rest of the engine's YAML.

## Versions and migrations

There are two levels of version, and each has its own **migration chain** (`MigrationChain`): a list of `vN → vN+1`
steps that transform the YAML. The current version is the oldest one that is read plus the number of steps, so a
version cannot be raised without writing its step.

- **The format** (`format_version`): its steps are in `formatMigrations()` in `packages/game/save` and are applied
  when reading the file, over the whole document. Today it is version 4; step 1 → 2 adds `autosave_reasons: []` (a
  version 1 game was never an autosave with reasons) and 2 → 3 an empty `summary`: `packages/game/save` does not know
  the sections it would come from. The list fills it in when reading it with what the game gives it
  (`SaveSlots::setSummaryReader`; the game gives `session::summaryFromSections`, which reads the species and the map
  from the `partner` and `field` sections), and the next save writes it in full. Step 3 → 4 adds an empty `summary.mode`, read the same way.
- **Each section** (`sections.<name>.version`): the chain is registered by the system that owns the section and is
  applied when loading, over the section's `state`. Each system versions its own without touching the others.

When loading:

1. A version **newer** than the one the game knows is a clear error: "section 'partner' is version 3, newer than this
   game reads (2): it was saved by a newer version of the game". The same for the format.
2. A version older than the first one that is read (if a system retires very old migrations) is also an error.
3. **All** the sections are migrated before any system reads its own, so a file that cannot be migrated changes
   nothing.
4. An **unknown section** (from a mod, or from a newer game) is kept as is in the `SaveFile` and written back
   untouched when saving. That is why the game saves over the `SaveFile` it loaded, not over a new one.
5. If the game does not have a system's section (it was saved before it existed), its `readMissing` is called, which
   by default is an error and which a system can use to start from its new-game state.

The steps use `YamlNode` directly; `withoutKey` and `renamedKey` cover what its API lacks (removing and renaming keys
in place).

## How a system adds its section

The system implements `save::SectionSerializer` in its own module: `save` declares the interface and never includes
the system's headers ([rule 4](dependency-rules.md)). The app joins the two.

```cpp
// packages/game/partner (an example: the names are the partner's).
class PartnerSave final : public dwe::game::save::SectionSerializer {
public:
    explicit PartnerSave(Partner& partner) : partner_{&partner} {}

    dwe::core::Result<dwe::core::YamlNode> write() const override;       // state -> YAML, current version
    dwe::core::Result<void> read(const dwe::core::YamlNode& state) override; // already-migrated YAML -> state

    // Version 1: `name`; version 2: `nickname`.
    static dwe::game::save::MigrationChain migrations() {
        dwe::game::save::MigrationChain chain;    // starts at version 1
        chain.add([](dwe::core::YamlNode state) -> dwe::core::Result<dwe::core::YamlNode> {
            return dwe::game::save::renamedKey(state, "name", "nickname");
        });                                         // now the current version is 2
        return chain;
    }

private:
    Partner* partner_;
};

// In the app, once:
dwe::game::save::SaveSections sections;
sections.add("partner", partnerSave, PartnerSave::migrations());
sections.add("inventory", inventorySave);   // no migrations: version 1
```

To change a section's format:

1. `write` and `read` are changed for the new version.
2. The step from the previous version is added to the chain.
3. A test is added with a file of the previous version that loads with the expected result (like
   `packages/game/save/tests/fixtures/partner_v1.yaml` and `partner_v2.yaml`).

In the game that join is the game session, `session::GameSession` ([packages/session](../modules/apps/session.md)): it
registers `clock`, `partner`, `tamer`, `field` and `random`, and each system that plugs in as an adapter adds its own.
It also requests the autosaves: the partner falls asleep, new day, map change, end of battle; `AutosaveRequests` merges
them into one and the ruleset says which ones count.

Section names are 1 to 64 characters of `a-z`, `0-9` and `_`. A mod uses a prefix of its own (`mod_fishing`) so as not
to clash with the game's.

## Slots, autosave and backups

How many slots there are is set by `save/settings` of the `game` package
([schema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/save_settings.schema.json)), and
when the game saves by itself, by the `save` section of the ruleset
([schema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/ruleset_save.schema.json),
[rulesets](rulesets.md)). The slots are a game preference and do not change the rules; autosave is a rule, so each
ruleset decides it.

```yaml
# content/packs/game/save/settings.yaml
manual_slots: 15

# content/packs/game/rules/original/save.yaml (abridged)
autosave:
  on_map_change: true
  on_battle_end: true
  # ... one `on_<reason>` for each reason in the table below, all mandatory
  coalesce_seconds: 2
```

- **Manual slots**: `slot_01.yaml` to `slot_NN.yaml`. The original saved one file per slot, up to 15 per memory card
  (`main_menu.c: SAVEGAME_SLOT_INFO`).
- **Autosave**: it has no slots of its own (since 2026-11-04). It writes **the game's slot**
  (`GameSession::homeSlot`): the one that was loaded or that the player last saved to, so saving and autosaving are
  the same game and "Continue" always loads the latest. It writes like any save (atomic, with the previous game in
  `.bak`, so a bad autosave can be recovered). Rules, in order:
  - The game's slot, if it is still its own: empty (the file is gone, it is written again) or with a save of the same
    game (same `created`).
  - If it has no slot (a new game from `--new-game`, `--start` or the control's `load`; from the title one is always
    chosen), or its slot now has another game, a damaged one or one from a newer game: **the first empty slot**, which
    becomes the game's.
  - If there is no empty one: **it does not autosave** and the save notice says so ("Not autosaved: every slot holds
    another adventure. Save from the menu.", `save.autosave_no_slot`), until the player saves. It never writes over
    another game's save.
  - An "unsaved" game (without a saves folder) does not autosave.
- **Old autosaves**: the `autosave_01.yaml`... that earlier versions left stay in the list while they are on disk
  (`SaveSlots::slots` looks for them by name, from 01 to 99), marked "Old autosave N": they are loaded and deleted,
  **never written** (`SaveSlots::save` rejects it). When loading one, the game continues in the manual slot that its
  summary names (if it is still its own, as above). Deleting a manual slot also deletes its old autosaves.
  `autosave_slots` in `save/settings` is read and ignored, so an old package loads.
- **Player option**: "Autosave" (Yes/No, `autosave` in the user's settings, on by default), in Options of the game
  menu (Tab) and of the title, in both skins. When off, the session never autosaves (`GameSession::setAutosaveEnabled`,
  which discards a pending request) and no notice appears; the player's saves do not change. When to autosave is still
  the ruleset's business.
- Saving over a damaged slot that already has a backup does not replace the backup with the damaged one.

### When it autosaves

The game's systems only **emit their events**; the session's work (the app) joins them to
`AutosaveRequests::requestAutosave(reason)`. Each `AutosaveReason` has its flag in the ruleset and its name in the
game's header:

| `AutosaveReason` | Flag | Name | System event that requests it |
|---|---|---|---|
| `MapChange` | `on_map_change` | `map_change` | Field (`packages/game/field`): the map change, when finishing entering the new map |
| `BattleEnd` | `on_battle_end` | `battle_end` | Battle: the result of each battle, won or lost |
| `PartnerSleep` | `on_partner_sleep` | `partner_sleep` | Partner: the `Slept` event (it falls asleep) |
| `NewDay` | `on_new_day` | `new_day` | Partner / clock: the `NewDay` event (the game clock's day changes) |
| `Evolution` | `on_evolution` | `evolution` | Evolution: the result of an evolution, when the partner is already the new species |
| `TournamentResult` | `on_tournament_result` | `tournament_result` | Battle: the result of a tournament or an arena fight |
| `RecruitJoin` | `on_recruit_join` | `recruit_join` | Battle / story: a recruit joins the city (after their battle or their event) |
| `KeyItem` | `on_key_item` | `key_item` | Inventory: a key item is bought or used |
| `DangerousArea` | `on_dangerous_area` | `dangerous_area` | Field: at the start of the exit fade toward a map marked as dangerous (decision 2026-10-20). Nothing requests it yet: there is no data on which maps are dangerous |
| `StoryProgress` | `on_story_progress` | `story_progress` | Script / story: a story trigger is activated (nothing requests it yet) |
| `PartnerReborn` | `on_partner_reborn` | `partner_reborn` | Partner: the partner is reborn from its egg after dying |

Only the reasons that the ruleset has set to `true` count (`AutosaveRules`, which is read with `readAutosaveRules` and
checked with `saveSectionCheck`), and only with the "Autosave" option on. A ruleset without a `save` section never
autosaves, like the original. The flags have no default value: each ruleset declares them all.

**Coalescing.** Several requests in a row give **one** autosave: the first opens a window of `coalesce_seconds` and
those that arrive inside it are added to it. The reasons are merged without repeats, in the order they were requested,
and go into the header's `autosave_reasons` (useful for the interface and for debugging). So, falling asleep and the
new day, or several events in the same frame, save once. The window:

- Counts in **real seconds** of the game loop (`AutosaveRequests::advance(seconds)` on each tick), not in the game
  clock: when sleeping the clock jumps hours at once (falling asleep and the new day would be hours of game time
  apart) and in menus and battles the clock stops. The module reads no clock: the app passes it each tick's time.
- Counts from the **first** request, not from the last: a streak of events does not delay the save forever. Requests
  further apart than the window give two autosaves.
- Is 1 second or more: `updated` has whole seconds, and two autosaves in the same second could not be told apart.

The game loop, on each tick and at a safe point (outside a battle or a half-done map change):

```cpp
autosaves.advance(tickSeconds);
if (auto reasons = autosaves.takeRequest()) {
    current.header.autosaveReasons = save::autosaveReasonNames(*reasons);
    sections.write(current);
    slots.save(homeSlot, current);            // the game's slot; and the save notice
}
```

The texts the player sees (the "saved" notice, the name of each reason) are localization keys from
`dwe/game/save/text_keys.hpp` (`save.autosaved`, `save.autosave_failed`, `save.autosave_reason.<name>`); the texts are
added by the interface and the localization.

- **Atomic write**: `<file>.tmp` is written, the previous file is copied to `<file>.bak` and the temporary is renamed
  over the file, which the file system does in one step. If the game is cut off, the slot has the previous game or the
  new one, never half. The copy, rather than a rename, makes sure the game is never missing from the disk.
- **Backup**: `<file>.bak` is the slot's previous game. The slot list says whether it exists, and `loadBackup` loads it
  when the game is damaged.

**Autosave is a deliberate difference from the original**, which only saved when the player asked: when going to
sleep (`partner.c: partnerTickSleep`, "Save and go to sleep") or at the save machine on some maps
(`main_menu.c: openSaveMachine`), and after finishing the game (`main_menu.c: gameClearSave`). The design lead
approved it on 2026-10-20 with all the reasons in the table and the coalescing ("the more autosave options, the
better"), and `original`'s `save.yaml` marks them as an "Approved deviation". A ruleset faithful to the original sets
them all to `false`.

## Where the files are

The app saves them in the user data folder given by the system (`platform::userDataFolder`, with `SDL_GetPrefPath`),
in `saves/`: `$XDG_DATA_HOME/indie-modding-games/dwe/saves/` on Linux (normally `~/.local/share/...`) and
`%APPDATA%\indie-modding-games\dwe\saves\` on Windows. `--saves <folder>` uses another one, and
`dwe-game --list-saves` writes each slot's state to the log and exits.

`packages/game/save` does not use `platform`: it reads and writes through `SaveStorage`, which the app creates
(`DiskSaveStorage` with the folder, `MemorySaveStorage` in tests).

## What the original saved

For the systems that add their section: this is the list of what has to be savable, not a format to copy. The
original copied its state to a block when saving (`main_menu.c: writeSavegame`), restored it when loading
(`main_menu.c: loadSavegame`) and had some new-game values (`main_menu.c: initializeDefaultSavegame`).

| Group | What it stores |
|---|---|
| Progress | The story state; the cards; the story and recruit triggers; which map each NPC is on; the poop on each map (map, position and size) |
| Position | The current and previous map and exit; where the tamer and the partner are; the tamer's click path |
| Clock | The game's year, day, hour and minute, and the time played |
| Partner | Species, learned techniques, stats (offense, defense, speed, brains, HP, MP, techniques and their priority), the raising parameters (care, hunger, energy, weight, life, age, care mistakes, battles, evolution timers...), name and lives |
| Tamer | Name, level, Digimon raised, money, merit, battles started and escaped, tournaments won and lost |
| Inventory | Up to 30 item slots with their quantity, and how many slots there are |
| Battle record | Up to 40 Digimon registered for versus mode (`main_menu.c: registerBattleData`) |

The original's file had the memory card header (title and icon), two copies of the game (the second as a backup) and a
byte sum as a check (`main_menu.c: createSavegameChecksum`), with no version number. Here the backup is `<file>.bak`
and the version goes in the file.
