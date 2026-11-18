# save

Saved games (`dwe::game::save`, `game` layer): the versioned YAML file, each system's sections
with their migrations, the slots, autosave and atomic writing with a backup. The format, the
versions and how a system adds its section are in [Saved games](../../architecture/save.md).

It does not know the systems it saves: each one implements `SectionSerializer` in its module and the app registers it. It does not
read the clock or use `platform`: the app gives it the times and a `SaveStorage` with the saves folder.

## Usage

```cpp
namespace save = dwe::game::save;

// On startup.
save::SaveSections sections;
sections.add("partner", partnerSave, PartnerSave::migrations());
auto settings = save::loadSaveSettings(vfs);                    // save/settings
save::DiskSaveStorage storage{savesFolder};
save::SaveSlots slots{storage, *settings};
auto autosaveRules = save::readAutosaveRules(store->current());   // the ruleset's `save` section
save::AutosaveRequests autosaves{*autosaveRules};
autosaves.setEnabled(userSettings.autosave);  // the player's "Autosave" option

// Load: the header already comes migrated; read migrates the sections and gives them to each system.
auto file = slots.load({.kind = save::SlotKind::Manual, .index = 1});
if (file) {
    if (auto loaded = sections.read(*file); !loaded) { /* loaded.error().message */ }
    current = std::move(*file);              // it is saved over: this way unknown sections are kept
}

// Save.
current.header.updated = now;                // and label, summary, playSeconds...
sections.write(current);
slots.save({.kind = save::SlotKind::Manual, .index = 1}, current);

// Autosave: systems ask, the ruleset decides and nearby requests give a single save,
// in the game's slot (without a slot: slots.firstEmptySlot(); with none empty, nothing is saved).
autosaves.requestAutosave(save::AutosaveReason::MapChange);
autosaves.advance(tickSeconds);              // real seconds of each tick
if (auto reasons = autosaves.takeRequest()) {
    current.header.autosaveReasons = save::autosaveReasonNames(*reasons);
    sections.write(current);
    slots.save(homeSlot, current);
}
```

- **`SaveFile`**: the header (`SaveHeader`) and the sections by name (`SavedSection`: version and state in YAML).
  `parseSaveFile` and `writeSaveFile` read and write the text; a game read and written comes out identical.
- **`MigrationChain`**: the `vN → vN+1` steps of a section or of the format. `withoutKey` and `renamedKey` help
  write them.
- **`SaveSections`**: registers the sections, writes them into a `SaveFile` and migrates and distributes them on loading.
- **`SaveSlots`**: lists the slots (empty, saved or damaged, and whether it has a backup), saves, loads, loads the backup and
  gives the first empty slot (`firstEmptySlot`). Autosave has no slots of its own: it writes the game's one
  (`session::GameSession`). The `autosave_NN.yaml` files from earlier versions appear in the list as long as they exist
  (`SlotKind::Autosave`, "old autosave"): they are loaded and deleted, `save` rejects them.
- **`AutosaveRules`** and **`AutosaveRequests`**: which reasons (`AutosaveReason`) autosave according to the ruleset's `save`
  section (`readAutosaveRules`, `saveSectionCheck` for the `RulesetStore`), and the pending requests, grouped in
  a window of `coalesce_seconds`. `setEnabled(false)` (the player's option turned off) accepts none and discards
  the pending one. Which event of which system requests each reason is in
  [save.md](../../architecture/save.md#when-it-autosaves).
- **`text_key`** (`text_keys.hpp`): the localization keys of the save notices and of the reasons.
- **`SaveSettings`**: `loadSaveSettings` and `parseSaveSettings` read `save/settings`.
- **`SaveStorage`**: `DiskSaveStorage` (a folder) and `MemorySaveStorage` (tests, with `failNextWrite`).

## Data

- `save/settings` (`content/packs/game/save/settings.yaml`,
  [schema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/save_settings.schema.json)): the manual slots (`autosave_slots`, from before,
  is read and ignored).
- The ruleset's `save` section (`content/packs/game/rules/original/save.yaml`,
  [schema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/ruleset_save.schema.json)): when the game autosaves.

## Tests

`save_file_tests.cpp`: byte-for-byte identical round trip, migration of `tests/fixtures/partner_v1.yaml` to
`partner_v2.yaml`, unknown sections kept, newer or too old versions, missing sections,
format migration and files corrupted or cut at any byte. `slots_tests.cpp`: atomic writing and backup
on disk, settings (also those of the `game` pack and the ignored old key), slots, failed save, old
autosaves (listed, loaded and deleted, never written) and the first empty slot. `autosave_tests.cpp`: the `save`
section (also `original`'s), autosave requests, their grouping, the option turned off, the reasons in the
header and successive autosaves over the same slot with the previous copy in `.bak`.
