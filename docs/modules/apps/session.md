# session

The game session (`dwe::session`): the app library that brings together the game's systems. The modules of `packages/game/` do not use
each other ([rule 4](../../architecture/dependency-rules.md)), so this is the only place that joins them. The game
(`apps/game`) and `dwe-sim` (`sim::HeadlessSessions`) play it the same way, with and without a window.

## What it holds

`GameSession` owns:

- the ruleset (`ruleset::RulesetStore`), which the tuning panel changes on the fly: on the next tick the session
  rereads its sections (clock, partner, `new_game`, `tamer`, autosave and those of each adapter);
- the game clock (`ruleset::GameClock`), the only one in the session;
- the partner (`game::partner::Partner`), the tamer (`TamerState`) and where the player is (`FieldPlace`);
- the save slots (`game::save::SaveSlots`) and the autosave requests (`AutosaveRequests`), if it is
  given a `SaveStorage`.

The field is not its own: the app creates it (`FieldScene`) and lends it with `attachField`. The session reads from it the map, the
player's position, whether a map change is in progress and whether the last tick ran game time.

```cpp
auto session = session::GameSession::create({.files = &files, .rulesetId = "original", .storage = &storage,
                                             .gameVersion = "0.1.0", .wallClock = clock});
(*session)->startNewGame();               // or load(slot) / loadFile(file)
(*session)->attachField(scene.field());
// On every logic tick, after the field:
(*session)->tick(TickSeconds);
```

### The tick

1. If the ruleset changed (`revision`), it rereads the rules; a value that does not work is logged and nothing changes.
2. If the field entered a new map: `Partner::enterMap` and `MapChange` autosave.
3. It advances the clock, taking fast-forward (`setSpeed`) into account, but only if the field ran game time
   (`Field::gameTimeRan`, false during the fades of a map change: `tamer.c: stopGameTime`). Without a field it always runs
   (simulations, tests).
4. The partner's care with what the clock advanced (`Partner::tick`), and then each adapter.
5. It dispatches the partner's events: `Slept` → `PartnerSleep` autosave, `NewDay` → `NewDay` autosave,
   `TamerLevelChanged` → tamer level within the limits of `tamer`, `Died` → rebirth (unless the story
   plays the death: see below). What an event causes (rebirth gives `Reborn`) is dispatched too.
6. If there is an autosave request the ruleset lets through, it writes it to **the session's slot**
   (`homeSlot`: the one that was loaded or last saved to), the same one as the player's saves; while
   the field holds the player in a care scene (`savesWait`, see "Parity") the request waits. Without a slot
   (a new game from `--new-game`), or if its slot now holds another game, it goes to the first empty slot, which becomes
   its own; with none empty nothing is written and `autosavesSkipped()` counts it for the notice. The full rules
   are in [save.md](../../architecture/save.md#slots-autosave-and-backups).

The player's orders (`feed`, `cure`, `scold`, `praise`, `useToilet`, `sleep`, `train`) and the end of a battle
(`BattleAdapter::finish`) also dispatch their events and their autosave. `takePartnerEvents` gives the events of the
last tick or order for the UI, and `takeAutosaved` the autosave written (slot and reason; no slot if it was skipped).
`setAutosaveEnabled(false)` is the player's "Autosave" option turned off: it never autosaves and discards the
pending request; the player's saves do not change.

### What the UI sees

`partnerView()` gives a read-only copy (`PartnerView`): species, level, age, lives, life left,
stats, meters with their range (`CareMeter::fill()` from 0 to 1), conditions, the deadline of each need
(`needDeadlines`, from `game::partner::needDeadlinesOf` with the session's clock), care mistakes and battles. The
"Partner" tab of the tuning panel (`apps/game/src/partner_tab.cpp`) shows it.

## New game

The sections of the session's ruleset (in `content/packs/game/rules/original/`):

- `new_game` ([schema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/ruleset_new_game.schema.json)): the starting partner, its stats and
  the tamer level. The section's stats are the fallback: if the base pack carries `data/new_game_stats`
  (what script 193 sets after the introduction, extracted by `dwx`;
  [scripts](../../formats/scripts.md#new-game-stats)), the partner starts with those of its
  species in that document, or with those of `others`, with the current HP and MP the script gives.
- `new_game_scene` ([schema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/ruleset_new_game_scene.schema.json)):
  the old man's questions before the first map ([apps/game](game.md#opening-of-a-new-game)). What the
  player answers reaches `startNewGame(NewGameChoice)`: the partner the answers choose replaces the one in
  `new_game` and the names typed end up in `GameSession::names()`, which the save keeps in its header
  summary (`tamer`, `nickname`) and which the story's text box uses for `{tamer}` and `{partner}`.
- `tamer` ([schema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/ruleset_tamer.schema.json)): the tamer level limits and the
  levels an evolution gives when its probability comes up (`evolution_level_gain`, `evl.c: EVL_applyEvolution`).
- `story` ([schema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/ruleset_story.schema.json)): `ticks_per_frame`, the engine ticks per
  frame of the original's story (3: 60 Hz against 20 frames per second), and `variable_max`, the cap of a
  story variable (255), and the numbers of a script's flow that the runner uses: the variable and value of
  "player still" (0 and 3), the turn-around trigger in a chat (44) and the map name variable with its three
  values (6; 0, 1, 2). `StoryAdapter` reads it, and on the first read creates the session's script VM,
  publishes the `story` API in it and checks that the story modules come from this `dwx`; if any of that fails, the
  session is not created ([story interpreter](../../architecture/story-interpreter.md)).

A new game with the same seed (`SessionDesc::seed`) is always the same, even if another was played before in
the same `GameSession`: the seed resets the partner's random numbers and those of the systems (`random()`, which start at the seed
plus `SystemsSeedOffset`), and the clock goes back to the start time
of `clock` (with the speed it had).

## Saving

| Section | What it saves |
|---|---|
| `clock` | `elapsed_frames` and `pending_frames`: the frames and the fraction the next tick completes |
| `partner` | The partner's state (`partner::toYaml`, version `PartnerStateVersion`); a version 1 save is upgraded with `partner::addVirusBackupSpecies` (the copy of the virus form without a form) |
| `tamer` | `level` |
| `field` | `map`, `position` and `yaw` of the player, and `partner_position` and `partner_yaw` of the partner (a save without them puts it next to the player), and `gone_npcs`, the map's NPC slots a battle took off (only when there are some; a save without it loads the map whole); empty without a map. `cleared: true` in a finished game (the original's `MAIN_STATE` 2, `main_menu.c: gameClearSave`): when it is loaded the ED2 movie plays before the field; the save machine and the sleep menu remove it (`MAIN_STATE` 1) |
| `random` | `partner` and `session`: the state of the partner's generator and the systems', so a loaded game goes on like the saved one; a version 1 save (only `partner`) follows the systems' from the partner's |
| `inventory` | The slots, how many are used and the money (`inventory::toYaml`, version `SectionVersion`) |
| `evolution` | The forms already raised (`evolution::toYaml`, version `EvolutionStateVersion`); the mark of an evolution item used is not saved, as in the original |
| `moves` | The partner's techniques: `learned` (ids from `data/moves`) and `equipped` (four positions of the species' list or `null`; the fourth is the finisher) |
| `story` | The story's triggers and variables (`StoryAdapter`, version 2, [packages/game/story](../game/story.md)); it saves variables 0 to 34, as the original does, all the triggers and the extra variables of the ruleset's capacity ([capacity](../../modding/story.md#capacity); version 1 is upgraded with `storyStateFromVersion1`), and the "player still" variable as 0 if it has its value (`main_menu.c: writeSavegame`); when loading, the others start at 0; a save without it loads the story as a new game leaves it, with the raised triggers (0x200 + number) of the forms the `evolution` section saved, and logs it |
| `mode` | `id`, the save's game mode (`SessionDesc::modeId`, [modes](../game/modes.md)); a save without the section is `original` |
| `battle_registry` | The partners registered in the save machine for versus mode (`BattleRegistry`, `main_menu.c: registerBattleData`): `partners`, each with `slot` (0 to 39), `name`, `species`, `hp`, `mp`, `offense`, `defense`, `speed`, `brains`, `discipline` and `techniques` (three ids from `data/moves` or `null`); a save without the section starts with none registered |
| `weather` | `seed`: the field weather's seed (`WeatherSeed`, engine improvement, [packages/game/weather](../game/weather.md)); a save without the section derives it from the session's seed (`weatherSeedOf`) and saves it on the next save. It does not touch the game's random numbers |
| `shop_job` | `jobs` and `running`: the shop jobs started and whether one is running (`ShopJobAdapter`, engine improvement, [packages/game/shop_job](../game/shop_job.md)); on load, the running job's line is rebuilt from its number and comes out the same. A save without the section has played none |

`savesWritten()` counts the saves the session wrote, the player's (`save`) or autosaves, and
`autosavesSkipped()` the autosaves skipped for lack of a free slot; the game looks at them to show the save
notice ([game](game.md#save-notice)).

The header carries the ruleset, the game version, the time played and a `<species> <map>` label. Loading a
save from another ruleset selects it before reading the sections. When loading, `savedPlace()` says where to start: the
game enters that map at that position (`FieldSceneDesc::startAt`).

## Events and mod scripts

`GameHooks` (`game_hooks.hpp`) fires the game's events in the story VM, each with a versioned table
(`GameEvents` lists them with their fields; [Lua API](../../modding/lua-api.md#game-events)): the session fires
`field.map_entered`, `field.map_left`, `clock.new_day`, `game.new_game` and `game.loaded`; `BattleAdapter`,
`battle.started` and `battle.ended`; `EvolutionAdapter`, `evolution.evolved`; `InventoryAdapter`,
`inventory.item_used` (in the field). `ModScripts` (`mod_scripts.hpp`), the system that goes right after the
story, ties `GameHooks` to the VM and runs the `scripts/main.lua` of each pack that has one, from the bottom up, in
its mod environment, once per session (when starting or loading, before `game.new_game` or `game.loaded`). An event with no
VM, or that no pack listens to, does nothing; a handler's failure goes to the log and the session goes on.

## Adapters

Each system that comes after (evolution, battle, inventory) plugs in with one file: a class that implements
`SessionSystem` (`session_system.hpp`) and is passed in `SessionDesc::systems`. It gives its section checks
(`sectionChecks`), reads its rules (`readRules`, also after a tuning), registers its save sections
(`addSaveSections`), starts the new game (`startNewGame`), advances with the clock (`tick`) and receives the
partner's events (`routePartnerEvent`) and those of praises and scoldings (`afterPraiseOrScold`, with the discipline and happiness
from before). For their orders the session gives them `careContext()`, `currentPlace()`, `random()` (the systems'
random numbers, saved with the session), `addTamerLevel(probability, levels)` (`tamer.c: addTamerLevel`) and
`afterSystemCommand()`, which dispatches the events and writes the autosave like the session's orders.

The game and `dwe-sim` plug in all three with `createGameSystems(files, tables)` (`game_systems.hpp`): it gives the list for
`SessionDesc::systems`, in the order they advance (evolution, inventory, battle), and the adapters by type for
the app's orders (starting a battle, using an item), valid while the session lives. `loadSystemTables` reads
the three's tables once (`dwe-sim` shares them among its sessions); without them, each adapter reads its own.

`field_facts.hpp` joins the session with the field's [script warps](../../architecture/script-warps.md):
`fieldGuardFacts(session, inventory, story)` gives the readers of triggers, bag, money, stats and
partner conditions with which the field decides its conditions, and `fieldTriggerWriter(story)` writes the taken warp's
`sets_flags` into the story.

- **Story lock** (`story_lock.hpp`, ruleset section `story_lock`, schema `ruleset_story_lock`): the game
  is delivered in stages and the lock says which part is open. Each stage names the maps it opens (ids or
  prefixes with `*`) and, only while it is the active one, its frontier: `exits` (which open maps a
  map's exits lead to) and `sections` (which cell sections a map launches). The stages are **cumulative**: stage N opens the
  maps of stages 1 to N, and `active_stage` is the last one delivered. A map a pack adds (id
  `<pack>:<name>`) is always open, and so is an exit that leads to it or leaves it toward an open map:
  the lock only closes maps from the original. The `--story-stage <id>` option of `dwe-game`
  plays another without touching the data and `--story-stage all` opens everything (development and tests); the log says when reading the
  rules which stage is being played (`[port:story-lock]`). `StoryAdapter::lock()` answers it and the app
  (`StoryFieldLink`) applies it: a stepped-on exit, a script's map change or a cell section toward
  what is closed does not happen and the dialog shows `closed_message` (a script's map change closed this way is not an error in the log:
  it is one only if there is no map or nobody plays it). The script change that does not happen ends the script on the
  player's map without the other map's arrival section (`StoryRunner::dropMapChangeArrival`), and the cell that launched it does not
  launch its sections again until the player leaves it. Only **new** map changes are closed: a
  save beyond the stage does not break, the player stays where it is and leaves by any exit toward
  an open map. A ruleset without the section closes nothing. Opening one more stage is just editing data. Stage 1
  comes from verification E1-01 in the decomp. The active one is 9 (the post-game): all nine stages are released,
  each with its routes in PASS under its own lock ([vertical slice](../../guides/vertical-slice.md)), and with
  it all maps are open. `--story-stage` still serves to test a stage's frontier: with
  `stage_2` (the jungle and the Drill Tunnel) its exits to later stages (trop02 to dgha01, trop03 to gcan01, tunn03
  and tunn03_2 to leom02, mayo02_2 to miha00) and the script map changes toward them (the `species_ab` trip from
  trop06 to koda00, mayo11 and tunn08 to miha04b) do not happen (`tests/autotest/route-stage2-lock.yaml`).
  The grown old man's house (room10, cell 52 of twna02-13 with triggers 220 or 214 or prosperity 50) is
  Stage 5, not 8.

The story is the sole owner of the raised forms, as in the original (`setDigimonRaised` sets trigger
0x200 + number): `EvolutionAdapter` (with `EvolutionAdapterDesc::story`) writes them there when evolving, on rebirth and
at start, and before each evolution check it reads from there the ones it sees as raised, so a script that sets
one of those triggers counts too. The `evolution` section still saves a copy, from which the story
of a save without a `story` section comes.

### Evolution (`EvolutionAdapter`)

`EvolutionAdapter({.files = &files})` reads the `evolution` section and its tables (the manifest's, once; the tests
give it theirs in `.tables`) and registers its section check and the `evolution` save section.

- **Inputs**: it fills `EvolutionInputs` from the partner: form, stats, weight, care mistakes, discipline,
  happiness, tiredness, battles, days, evolution counter, the virus form
  (`care.toilet.virus_form`), the virus form's copy, the map
  (its id from `data/maps`, empty outside the field; a special rule names the map by id, `map: { equals: mayo02 }`,
  and an id that is not in the map table is warned about when reading the rules and the rule is never met; `dwx validate` gives it
  as a problem), whether the tamer is free
  (`careContext().tamerIdle`), whether a script is running (only in script routines) and the learned techniques: those of the
  `BattleAdapter` (`moves().learned`, by ID), counted with `countMasteredMoves` against the session's technique table
  (the original's with a number below `evolution.requirements.mastered_moves_below`, 64:
  `evolution.c: getNumMasteredMoves`, and those named by `mastered_move_ids`, like the ones a pack adds). The battle adapter is
  created later (it needs the inventory, which needs the evolution), so it is given with
  `setMoveSource(battle)`; without it, evolution sees no learned techniques.
- **For the chart**: `outlook(session)` gives the partner's `EvolutionOutlook` (with the same inputs and the raised
  forms up to date) without touching the session, for the pause menu's evolution requirements.
- **When it looks**: on every tick in which the game clock runs, `checkNatural` with the frames run (one
  check per frame, like `tickConditions`; the counter is raised by the partner; if it does not evolve, the adapter
  leaves the one it returns), except in the protected hour after waking (`HAS_IMMORTAL_HOUR`) or if the virus bar
  filled on that tick:
  the virus wins (`partner_impl.c: tickConditions`) and natural evolution waits. `VirusBarFull` → `fillVirusBar`;
  `Slept` → `wakeUp`; after praising or scolding → `praiseOrScold`; `Reborn` marks the baby as raised
  (`evolution.c: reincarnatePartner`). Orders: `useItem(session, item)` (the inventory calls it),
  `returnFromVirus(session)` (script routine 2) and `evolveByScript(session, form)` (routine 10).
- **Applying** (`evl.c: EVL_applyEvolution`): `Partner::changeSpecies` to the target form; `statChange` by
  `addStatGains` or, when returning from the virus form, `restoredStats` with `setStats` and each stat at its cap from
  `care.limits` (without the per-gain cap, decided on 2026-10-20); current HP and MP within the new maximums;
  the learned technique (with those learned along with it, `utils2.c: learnMove`) and the four equipped slots by
  `BattleAdapter::evolveMoves`; weight, care mistakes, battles and counter; extra lifetime hours (`addLifetime`); the tamer
  level probability with `tamer.evolution_level_gain`; the virus bar to 0; the virus form's copy (form and stats) in
  `PartnerState::virusBackup`; `recordEvolution` and `Evolution` autosave. `takeEvolutions()` gives the applied ones
  for the UI.

### Inventory (`InventoryAdapter`)

`InventoryAdapter({.files = &files, .evolution = &evolution})` owns the bag (`game::inventory::Inventory`) and
the effects (`ItemEffects`, with `data/items` and `data/item_effects`, read once; the tests give it theirs).
It registers the checks of the `inventory` and `item_use` sections and the `inventory` save section
(`inventory::toYaml`, version `SectionVersion`; the migration of version 1, which saved numbers, to ids is
`inventory::itemNumbersToIds`). When reading the rules it checks that every item they name is in `data/items` and that the
`item_use` ranges only name items from the original (`checkItemIds`, `checkItemUseItems`).

- **New game**: the bag from the `inventory` section with the items of `bag.new_game_items` (in the original
  none: script `dg 193` empties the startup one; see `packages/game/inventory`). An item that is not in `data/items` is
  an error.
- **Use** (`use(session, item)`, in the field): the partner may refuse it (`Partner::refuses` with the check
  that `item_use.refusal` says: never, a discipline roll or only the favorite food; evolution items are
  refused outside their level). If it takes it: food goes through `Partner::eat` (real food, which answers hunger,
  if it is in `item_use.meals`; the training boost with the bits of `item_use.training_boost_bits`); the rest
  heals HP and MP up to the maximum, revives, cures with the medicine's probability, raises stats by `addStatGains`, gives
  lifetime hours (`addLifetime`), changes the tamer level (`addTamerLevel`), relieves (`usePortableToilet`) or passes the
  evolution item to `EvolutionAdapter::useItem`. A key item used requests the `KeyItem` autosave.
- **Buy** (`buy(session, item, amount, triggers)`): with the baby discount if the partner's level is
  in `item_use.shop_discount_levels`; a key item requests the `KeyItem` autosave.
- **Defeat** (`loseOnDefeat(session)`): the stacks lost, with the session's random numbers.
- What only works in battle (curing statuses, protecting, disks) belongs to the battle adapter.

The `item_use` section ([schema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/ruleset_item_use.schema.json)) belongs to the session: which items
the partner refuses (`partner_impl.c: partnerWillRefuseItem`), which ones answer hunger
(`partner_impl.c: partnerHandleFoodFeed`), which levels get the baby discount (`script_common.c: isPartnerBaby`)
and each stat's bit in the training boost (`trn_reward.c`).

### Battle (`BattleAdapter`)

`BattleAdapter({.files = &files, .inventory = &inventory})` reads the `battle` sections (`readBattleRules`, the
technique catalog with `data/moves` and the strategies) and `partner_moves`, with `data/digimon` and `data/moves` read
once (the tests give it theirs). It registers the checks of the two sections and the `moves` save section:
it owns the partner's techniques (`PartnerMoves`).

- **Techniques**: like the original, the ones learned by id (`utils2.c: learnMove`) and the ones equipped by their position in the
  species' list (`stats.base.moves`, `0x2E` + position), so after an evolution each position names the
  new species' technique and a position with no technique stays empty in battle (`btl.c`). In a new game the
  partner learns and equips the technique of `partner_moves.new_game_slot` (or its species' one in
  `partner_moves.new_game_species_slots`: the second starting species begins with position 2) and equips as finisher the
  last one in its list that is a finisher (`partner_impl.c: initializePartner`); on rebirth (`Reborn`) it equips only `partner_moves.reborn_slot` and
  keeps the learned ones (`evolution.c: reincarnatePartner`). `equip(session, place, position)` only equips learned
  techniques (`utils2.c: hasMove`); the finisher goes with the species.
- **Start** (`start(session, BattleRequest)`): the partner's fighter (`partnerCombatant`: the `data/digimon`
  row, its stats, HP and MP and the equipped techniques), its state (discipline, tiredness, sick, wounded and
  the techniques it already knows), the clock's minute and a seed from `random()`. The session decides the recruit that joins
  if it wins (`recruitOnVictory`).
- **Finish** (`finish(session, BattleResult)`): the final HP and MP (`setStats`; after losing, HP at 0: the defeat map's script
  returns them, dg 212 section 0x48), the gains by `addStatGains`, and the care, wound, lives and
  minutes as the battle gives them by `Partner::applyBattle` (with the partner's hourly count if the battle ends an
  hour: `tamer.c: advanceBattleTime`); the new techniques and the bits to the bag; the items the enemies drop do not go to the bag: they come out in
  `BattleAftermath::drops` and the field puts them on the ground to be picked up (`item.c: spawnDroppedItems`). On a
  defeat (not in a tournament), the stacks lost by `InventoryAdapter::loseOnDefeat`. It requests the `BattleEnd`, `RecruitJoin`
  (if the recruit joined: `recruitJoined`) and `TournamentResult` (tournament or arena) autosaves.
- **Throwing an item** (`throwItem(battle, item)`): the tamer throws an item from the bag to the partner. `InventoryAdapter::useInBattle` uses it
  with `UseContext::Battle` (only those with `item_color` 1 or 2: `inventory.c:
  selectOptionMenu`), which spends it when thrown (`main.c: startThrowingItem`), and its effect becomes the
  battle's `ThrownItem`, which always arrives (`main.c: tickThrownItem`). It fails if there is no bag or if the battle does not
  let it be opened (`Battle::bagAvailable`).
- **Reading again** (`readRules` after a tuning, a player option that changes the rules or another ruleset): it writes
  into the rules and the catalog it already gave instead of creating others, so `effectRules()`, `rules()` and `catalog()` keep
  pointing to the same objects with the new values, and a battle in progress and the field do not read freed memory.

The `partner_moves` section ([schema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/ruleset_partner_moves.schema.json)) belongs to the session.

### Shop (`ShopCounter`)

The shop the story opens (`script_interp.c: CALL_ROUTINE 8` → `script_common.c: openShop`). The story calls
`open(ShopVisit)` with the shopkeeper (variable 254 and its species) and waits while `visit()` stays open; the game's
`shop_menu` screen (`apps/game: ShopScreen`) closes it when the player leaves. It buys with
`InventoryAdapter::buy` (the assortment is the story's `0x180 + i` triggers) and sells with
`game::inventory::sell`. The shopkeeper's phrases are those of its MAPHEAD section (`getShopkeeperLine`,
`resolveMapHeadEntry`), read from the `script/maphead` listing. That listing is read once per content
(`readMapHeadLineTables` keeps the tables of the last one it read) and the game reads it when loading the session: before, each
shop and service read it on its first visit and that frame stalled (#221). The `shop` section
([schema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/ruleset_shop.schema.json)) is optional: without it there are no shops. The recycle
shop (`script_menu.c: openRecycleShop`, with the `recycle_trigger` trigger or `ShopVisit::recycle`) sells what the
defeats took (`handleItemLoss`: the items in `recyclable`, up to `recycle_max` of each, which arrive through
`InventoryAdapter::setLossListener`) and does not buy; its assortment is saved in the session's `shop` section.

### Item keeper (`ItemKeeper`)

The item keeper the story opens (`script_interp.c: CALL_ROUTINE 11` → `shop/script_menu.c:
tickItemKeeper`). The story calls `open(ItemKeeperVisit)` with the keeper (variable 254 and its species) and waits
while `visit()` stays open; the game's `item_keeper` screen (`apps/game: ItemKeeperScreen`) closes it when
the player says goodbye. `keptRows` and `bagRows` are the lists of `itemKeeperFillItemList` and `move` passes one, ten or the
stack (`keeperMoveOne`, `keeperMoveTen`, `keeperMoveStack`) between what is kept and the bag. `keep` leaves items with the keeper from outside the bag (the shop job's bonus when the bag is full). What is kept (`SCRIPT_STATE`
0x54 + item in the original) goes in the session's `item_keeper` section, by item id. The ruleset's
`item_keeper` section ([schema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/ruleset_item_keeper.schema.json)) gives the MAPHEAD section
with the phrases, each item's maximum and the alternate button's step; without it the visit closes at once.

### Dropping an item (`DiscardMenu`)

The menu the story opens to choose an item from the bag (`script_interp.c: CALL_ROUTINE 0` →
`script_common.c: openDiscardItem`): it appears when the player receives something with a full bag, or when a character asks for
an item. `open()` opens it if the bag has anything that can be dropped (`shopFillSellItemList`: one slot per row, and only
what has `droppable` can be chosen); if there is nothing, it sets trigger 3 and 255 in variable 254 and does not open, like
state 0 of the original. `choose()` writes the item's number in 254 and clears trigger 3 (`tickSingleCardShop`
with "Yes"); `cancel()` only closes if trigger 3 is set (`tickItemMenu`) and writes 255. The menu takes nothing out
of the bag: the script does it afterward (routines 12 and 13). The ruleset's `discard_menu` section
([schema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/ruleset_discard_menu.schema.json)) gives the MAPHEAD section with "Is it OK?" (1240,
line 0), the trigger, the variable and the "none" value; without it the routine closes at once. The game's
`discard_menu` screen (`apps/game: DiscardScreen`) shows it.

### Jukebox (`Jukebox`)

The restaurant's jukebox that the story opens (`script_interp.c: CALL_ROUTINE 9` → `shop/script_menu.c:
openJukebox`; in room05b `species_at` opens it, script 175, section 0x6). `open()` fills the list like
`jukeboxFillTitleList`: entries 0 to 62 of `BGM_TRACK_NAMES`, the 62nd only with trigger 50; it looks for the song
that is playing (font and track) among those in the list and writes its row in variable 249, or 255 and stops the music if it is not
there. `startView` places the list like `createJukeboxMenu` (the marked row at the top if it fits in the first window, otherwise
at the very bottom), `choose` writes the chosen one in 249 and `playChosen` plays it if it is not the one already playing
(`tickJukeboxMenu`: `stopBGM` and `playMusic(font, track)`); `close` plays the map's music again from the
start (`playBGM(ACTIVE_BGM_FONT)`, state 2). Each entry's font and track (`MAIN_D_801303B8`) come from
`font` and `track` in `bgm_tracks` of `data/music.yaml`; a pack extracted earlier looks for them in `used_by.jukebox` of
each `music/NNN/font_meta.yaml`. `SessionMusic` provides the music through the `JukeboxMusic` interface. The ruleset's `jukebox` section
([schema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/ruleset_jukebox.schema.json)) gives the MAPHEAD section with the phrases
(0x4d6: greeting 3, question 0, answer 2, farewell 1), the trigger, the locked entries, the variable and the "none"
value; without it the routine closes at once. The game's `jukebox` screen (`apps/game: JukeboxScreen`) shows
it.

### Item trade (`ItemTrader`)

The trade the story opens (`script_interp.c: CALL_ROUTINE 2` → `shop/script_menu.c: openMojyamonShop`). The script
sets in variable 249 which of the three tables it uses and in 254 who is trading; `open(ItemTraderVisit)` reads the table and
writes 255 in 249 (state 0). `rows()` are the rows of `mojyaTradeFillItemList`: what the player gives, what the
other gives, whether the bag has it (if not, it goes in red) and whether that trade was already done (the mark). `offer(row)` writes the
offered item in 249 (the phrase "{item:pstat=249}" asks for it) or fails if the bag does not have it; `trade()` is state
5: first it gives the new item and, if it fits, removes the offered one and sets the trade's trigger (`setMojyaItemTradedTrigger`:
5 + 3 × table + row, and 14 when all nine are done); if it does not fit nothing changes ("Too many Items."). The ruleset's
`item_trader` section ([schema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/ruleset_item_trader.schema.json)) gives the MAPHEAD section
(1238, lines 8 to 14), the variable, the triggers, the refusal sound and the tables (`MOJYAMON_ITEMS_GIVE` and
`MOJYAMON_ITEMS_GET`); without it the routine closes at once. The game's `trade_menu` screen
(`apps/game: TradeScreen`) shows it.

### Bird transport (`BirdTransport`)

The bird in room12 that takes the player to another zone (`script_interp.c: CALL_ROUTINE 10` → `shop/script_menu.c:
tickBirdraTransport`). The script sets in variable 254 who speaks. `open()` takes the rows once, like
`birdraFillTargetList`: the destinations whose trigger is set, in the table's order, and whether the money is enough (if not,
they go in red). `pick(row)` fails on a row the money does not pay for (the refusal plays); `travel()` is state 5:
it writes the destination's map and entrance into variables 247 and 248, charges the price and returns the MAPHEAD section
(0x4e3, "Hold on tight!" and the `teleport` of those variables) where the script continues: routine 10 ends with `TaskJump`
(`StoryScenes::screenClosed(jump)`) instead of continuing after its call. The ruleset's `bird_transport` section
([schema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/ruleset_bird_transport.schema.json)) gives the phrases (MAPHEAD 1238: 4 greeting, 5
farewell, 6 "Are you sure?", 7 yes or no; the money box's, line 6 of 1220), the variables, the travel section,
the trigger that prevents canceling the yes or no (49), the refusal sound (0, 11) and the six destinations of
`BIRDRA_TRANSPORT_TARGETS` (map, entrance, trigger and price); without it the routine closes at once. The game's
`bird_transport` screen (`apps/game: BirdTransportScreen`) shows it. With the story lock, `open(placeOpen)`
also marks the destinations whose map the stage closes (`BirdRow::open`): they come out in red like those that cannot be paid for and `pick`
rejects them the same way, so the transporter never charges for a trip the lock does not allow (decision of the coordinator,
2026-10-31; not an original rule).

### Card shop (`CardShop`)

The card shop in gias06b (`script_interp.c: CALL_ROUTINE 14` → `shop/script_menu.c: openBuyCardMenu`,
`openSellCardMenu`, `rollCardPack`). With the sell trigger the routine opens the `CardSell` screen; if not, with the
buy cards one, `CardBuy`; if not, a pack: three cards, one per state, each with its phrase and the player as the
speaker (`rollCard`, `showCardTextbox`; variable 249 carries the card's species, which the phrase names). The day's six
cards (`hasAnyCardToBuy`, the sold slot at 0xFF) are drawn in the new game and on each new day, from
1 to 64 without 4 (they can repeat). `buy` charges the price by rarity (`CARD_PRICES`) and adds a copy; `sell` pays
half per copy. The ruleset's `card_shop` section ([schema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/ruleset_card_shop.schema.json)) gives
the draw, the sale divisor, the MAPHEAD phrases (1235 and "Is it OK?" of 1240), the fail sound (0, 11) and the
deal sound (0, 23) only in gias06b (`playShopSoundOnlyInSavannah`); the state (the six cards) goes in the session's
`card_shop` section. The game's `card_shop` screen (`apps/game: CardShopScreen`) shows the lists.

### Merit exchange (`MeritShop`)

The Shogun's exchange in gkyo02 (`script_interp.c: CALL_ROUTINE 1` → `shop/script_menu.c: openMeritShop`). The
player gives cards for merit points (the player record's `merit`) and takes the items that have a price in
merits (`shopFillMeritItemList`). `giveCard` removes a copy and adds its `meritValue` up to 9999; `takeItem` charges the
price and puts one in the bag, only if it can be taken. The ruleset's `merit_shop` section
([schema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/ruleset_merit_shop.schema.json)) gives the phrases (MAPHEAD 1236: greeting 0, options 1,
yes or no of the card 2 and of the item 3, over the card list 5 and the item list 6, what the card is worth from 7 plus
the rarity), the points box's line (7 of section 1220), the cap (9999), the trigger that prevents canceling
the options (49), the fail sound (0, 11) and the deal sound (0, 23) only in gias06b; without it the routine closes
at once. The game's `merit_shop` screen (`apps/game: MeritShopScreen`) shows it.

### Gym in the field (`GymFieldPorts`)

The ports of a [gym](../game/gym.md) course (`game::gym::GymPorts`) over the field and the session
(`gym_field_ports.hpp`). While a course runs the story holds the field, so the actors move as in
a script: they walk to their post at the speed of their walking clip (`anim.c: applyRootMomentum`), turn, look at each other,
animate and hold their clip in place (`holdInPlace`), the camera goes to the post, the sounds play (those that last until
the course stops them) and the session trains the partner and gives the reward (`award`). `GymFieldPortsDesc` receives the
session, the field, the `gym.yaml` rules and, if present, the story (the gym improvements and the text box) and the
inventory (the training manual). What only the game draws (the map's objects, the effects, the rows
of the screen) is supplied by a subclass: `GymScreen` in `apps/game`, and a windowless test one in
`gym_course_tests.cpp`, which is the basis of the [training check](../../guides/check-training.md).
The clouds and the moves that the intelligence course teaches are the only random part, with a seed taken from the
partner: a repeated session gives the same. `gymWorldOf` turns a `gym.yaml` point, in the original's axes (z
reversed), into world space.

## Parity with the original

| Rule | Decomp | Ours | Test | Status |
|---|---|---|---|---|
| New game with the partner of id 3 | `main.c: main` (`initializePartner(3)`) | `new_game.partner: agum` | `a new game starts the partner, ...` | Same |
| The partner's starting stats | script 193 of `DG.SCN`, offsets 0x0918–0x0964 (`store_digimon_type`, `if` and `set_stat`; `script_interp.c: SCRIPT_OP_SET_STAT`) | `data/new_game_stats` (`dwx`, `findNewGameStats`), `GameSession::startingStats`; `new_game.stats` only without the document | `a new game takes the partner's stats from what the new-game script sets ...`, `the user's disc, extracted: the partner starts with ...` | Same (with extracted data); the script sets them after the introduction and here at the start |
| Tamer level 0 and time 8:00 at the start | `main_menu.c: initializeDefaultSavegame` | `new_game.tamer_level`, `clock.start` | same | Same |
| Saving at the machine and loading gives back the whole session, partner in its place included: the original saves the player's and the partner's position and on loading places both (if the partner lands on an NPC, the original removes that NPC or takes both to the exit). Checked live (room08, the machine in the old man's house; save, load and save again): the sections only change in what time runs after loading, and player and partner end at the same point (player -300, -790; partner -196, -1029 after 240 ticks in both games). Before, the partner appeared next to the player | `main_menu.c: writeSavegame` (`partnerPosition`), `main.c: initializeLoadedMap` | `field` section (`partner_position`, `partner_yaw`), `SessionState::currentPlace`, `packages/game/field: Field::enterMapAt` (`placePartnerAt`) | `a game saved on the field loads with the player's and the partner's places` (`session_tests.cpp`), `a loaded game puts the partner back where it stood, ...` (`field_tests.cpp`) | Same except for the NPC clash: dwe puts the partner next to the player if its spot has an NPC on it or is a blocked cell (Assumption; not measured on the original). `PREVIOUS_SCREEN`, `CURRENT_EXIT`, `PREVIOUS_EXIT`, the NPCs on the map (`npcMaps`) and the player's half-finished path (`writeSavegame`'s waypoints) are not saved: Gap |
| Enemies beaten before a save stay beaten after loading it on the same map (a tester's report: saving and loading brought them back, sometimes on top of the partner). In the original a battle's end takes the beaten enemy, the fled ones and those off screen at its start off the map until the map loads again (`isOnMap` 0); it saves only at save points and a load runs the map's MAPHEAD, so this never shows there. dwe saves anywhere, so the save keeps that list for the map it is on and the load keeps those slots off when the map's script spawns them; leaving the map loads it whole, as the original. Checked live (dwe, gcan08_1: beat the Goburimon of slot 0 with slots 1 and 2 off screen; quick save writes `gone_npcs: [0, 1, 2]`; loading leaves the three off). A save placed on an enemy's spot: until the player first moves, the release rule takes him out of the NPC (to the nearest free spot) at once, before he can touch it; checked live (gcan08_1, player saved at (1740, -1700) on slot 0's (1726, -1687): moved to (1954, -1768) on the first free tick, no touch). The partner already gave way to everybody | `btl.c: handleBattleStart` (off screen, `isOnMap` 0), `battle_setup.c` (the fled ones), `battle_main.c: BTL_deinitializeCombat`, `main.c: initializeLoadedMap` | `FieldNpcs::takeOffUntilReload`, `keepOffUntilReload`, `goneUntilReload` (`battle_screen_exit.cpp`), `FieldPlace::goneNpcs`, `field` section (`gone_npcs`), `Field::yieldPlayerToNpcsOnLoad` (`play_session.cpp`) | `a game saved on the field loads with the player's and the partner's places` (`session_tests.cpp`), `an NPC a battle took off stays off through a save until the map loads again`, `after a load the player steps off an enemy the map spawns on him, until he plays`, `the player's load rule ends with a map change` (`field_tests.cpp`) | dwe safety rule (the original cannot save there). An enemy that sees the player next to it can still charge him, as in the original |
| Tamer level between 0 and 10 | `tamer.c: addTamerLevel` | `tamer.level_min/level_max`, `changeTamerLevel` | `the original's tamer level ...` | Same |
| The clock runs in the field and stops on map changes | `clock.c: tickGameClock`, `tamer.c: stopGameTime/startGameTime` | `advanceGameTime` with `Field::gameTimeRan` | `field_clock_tests.cpp` | Same |
| The clock and care stop during the whole battle; when it ends, 20 minutes at once | `tamer.c: startBattle` (`stopGameTime`), `clock.c: tickGameClock` (returns if `GAME_STATE != 0`), `tamer.c: advanceBattleTime` | the field does not run while the battle is shown (`apps/game`: `BattleScreen::active`), so `Field::gameTimeRan` does not advance the clock; the minutes are given by `Partner::applyBattle` | `a won battle gives the partner its HP, raises, care, minutes...` | Same |
| One game minute per second | `clock.c: tickGameClock` | `clock` section | `on the field the game clock keeps ...` | Same |
| Play time: 1200 frames per minute (a real minute at 20 frames per second) across the whole game loop, also under menus and in battles; it does not run during pause (P) or a movie; it stops at 999:59 | `clock.c: addClock` (the object of `tickGameClock` and that of `tickPlaytime`), `tickPlaytime` (`PLAYTIME_HOURS` up to 999, the minutes stay at 59). Measured on 2026-11-04: 140 frames in 420 vblanks with the bag options open, 100 in 307 in a battle, none in 476 vblanks of pause | `GameSession::countPlayTime`, which the app calls on every tick that is not paused (`PlaySession::countPlayTime`); before it only counted the field's tick, so menus, battles and scenes did not add up, and it had no cap | `play time counts the ticks the game gives it, ...` | Same |
| Area effects start over on each map | `partner.c: setupPartnerOnWarp` | `followMap` → `Partner::enterMap` | `a map change autosaves, ...` | Same |
| Autosave on sleeping, on a map change and at the end of a battle | The original does not autosave | `save` section | `the partner going to sleep autosaves`, `a map change autosaves, ...` | Approved deviation (2026-10-20) |
| Autosave at the start of a day | The original does not autosave | `save.autosave.on_new_day: true` | `a new day autosaves only when ...` | Approved deviation (2026-10-20) |
| No save during a care scene (poop in the field, toilet, praise or scolding, eating, sleeping), nor while the living partner has 0 HP (after losing a battle, until the defeat map's script returns its HP): autosave, `save("slot_NN")` and the save machine wait for it to end | The original never saves there: the save machine is a chat, which only starts with the player free (`checkMapInteraction`, which only runs from the walking player's tick, its state 0; care scenes put it in state 6), and it does not autosave. Only the sleep save writes in its own scene (`partner.c: partnerTickSleep`) | `GameSession::savesWait` (field with `heldForCare`, or living partner with 0 HP: `savesHeld`): `writeRequestedAutosave` leaves the request pending, `SaveCommand::apply` leaves it queued and `SaveMachine::open` opens the machine on the `tick` in which the scene ends; after `SaveWaitLimitSeconds` (60 real seconds of a single wait) it saves anyway and warns in the log, so no wait is eternal. The sleep menu's save is still immediate | `an autosave waits while a care scene holds the field ...`, `a care scene that never ends keeps a save waiting ...`, `an autosave waits while the living partner has 0 HP ...`; in `apps/game`: `a save command waits while a care scene holds the field ...`; round trip `s1-care` (poop in the field when saving): no differences after loading (before `conditions: [sleepy]` → `[]` and `care_mistakes` +1) | dwe safety rule (SOAK-01): saving mid-scene lost the scene on loading and the partner's care ran instantly |
| On dying, it is reborn right away as a baby | `partner.c` (death and rebirth after the death scene) | `routePartnerEvent` → `Partner::rebirth` | `a partner that dies is reborn as a baby` | Only without the story; with it, the next row |
| A lost life or a death run section `0x4DE` of script 0 (variable 255 to 0); the death waits for the rebirth scene (partner routine 4), which uses the egg from the box (routine 29) if lives remain, or a random one | `partner_impl.c: tickDeathCondition`, `doo2.c: DOO2_openEggBox`, `DOO2_tickEggInput`, `dooa.c: DOOA_getSequenceState` | `story_scenes.death`: `StoryScenes::routePartnerEvent` and `tick` request the section when the player is free (`playerFree`) and no script is running; a dead partner without its section (a save made mid-death) requests it again; `GameSession::setRebirthByStory`, `StoryScenes::chooseEgg`, `rebirthPartner(egg)` | `a partner whose death the story plays waits ...`, `the egg box's egg counts only while ...` | Same, without loading the overlay (`loadDynamicLibrary`), the eggs' sprites in VRAM or the box's animation. A cell that runs its section every frame (tunn09, script 32 section 0x34) holds the death in both: measured on the original, `tickDeathCondition` sees `IS_SCRIPT_PAUSED` at 0 every frame over it and section `0x4DE` comes out as soon as the player leaves it; dwe waits the same for `playerFree` (no change) |
| The partner's response to the area (likes it, dislikes it), computed on entering each map with the map's region lists and the species' favorite region | `map.c: loadMapSetup` (`MAP_TILE_DATA.partnerAreaResponse`). In the decompilation `map.c: loadMap` calls it, and the scripts' map change calls that (`script_interp.c`); according to this, evolution and rebirth do not recompute it until another map is loaded | `attachField` and `followMap` → `game::partner::areaResponseOf`; `careContext`, `partnerView` and sleep (`care_commands.cpp`) | `how the partner feels about the map follows the map entered, ...`; in `packages/game/partner`: `area response: ...` | Same |
| Piles of poop on the ground: up to 16 per map and 100 in total, in front of the partner, saved in the session and drawn in the field; poop in the field has its scene: the player stops and looks, the partner runs to it, crouches, leaves it and does its after-animation | `partner_impl.c: createPoopPile`, `renderPoop`, `initializePoop`, `handleSleeping`; `partner.c: tickWildPoop` (state 7) | `PoopAdapter` (`poop_adapter.hpp`): `PoopedOnGround` leaves a pending pile and the field places it (`apps/game`: `FieldPoop`, with `Field::runPartnerUp`); with a field (`CareContext::fieldScenes`) the poop waits for its scene (`Partner::wildPoopDue`), which leaves it with its effects (`GameSession::leaveWildPile`) and rolls the tamer level when it ends (`GameSession::finishWildPoop`); save section `poop_piles`; rules `poop_piles` (how they look, `wild`) and `care.toilet` (how many fit) | `a pile the partner left waits for the field...`, `the piles go in the save and come back`; in `packages/game/partner`: `with a field the pile and its care effects wait...`; in `packages/game/field`: `a pile's scene runs the partner up...` | Same: the effects (care mistake, mood, weight) arrive when the pile is left and the tamer level when the scene ends |
| The partner eats a pile of poop near the player | `partner.c: partnerTickEatShit` (state 9), `partner_impl.c: tickPoopDetection` (radius 200 or 300 in x and in z), `handleEatingPoop` | `FieldPoop::update` with the field free: if `Partner::eatsPoop`, the first pile on the map with the player in its box; the partner walks over, does the eating animation and `GameSession::eatPoop` | `the original's piles play their scene and are found nearer when small`; in `packages/game/partner`: those of `eatPoop` | Same; if the partner gets stuck on the way, it eats where it stops |
| The first partner and each reborn baby are left as raised | `main.c: main`, `evolution.c: reincarnatePartner` (`setDigimonRaised`) | `EvolutionAdapter::startNewGame`, `Reborn` | `the evolution adapter marks the first partner as raised ...` | Same |
| Evolution is checked on every game clock frame, even if sped up, except in the protected hour | `partner_impl.c: tickConditions` | `EvolutionAdapter::tick` → `checkNatural` with `advance.frames` | `a baby evolves when the clock's hour ...`, `the natural check runs on every frame ...`, `no natural evolution in the hour after waking` | Same |
| No natural evolution while the map fades the palette between times of day: at 6, 16 or 20 on a map with no fixed time, during the 25 frames of the fade `tickConditions` exits before looking at evolution (the decomp's name is backwards: it returns 1 when there is no fade). The field signals with `FieldScene::daytimeFadeRunning`, which also covers the frame in which the time changes before the field follows it | `map.c: isInDaytimeTransition`, `initializeDaytimeTransition`; `partner_impl.c: tickConditions` | `EvolutionAdapter::setDaytimeFade`, `PlaySession` | `no natural evolution while the field's palette fades between times of day` | Same. Gap: the virus bar (by events in the engine) does not wait for the fade, and the fade's pause is not modeled in the states the original freezes |
| What changes on evolving | `evl.c: EVL_applyEvolution`, `EVL_clampBaseStats` | `EvolutionAdapter::apply` | `an evolution item turns ...`, `back from the virus form ...` | Same (return from virus: decision of 2026-10-20) |
| Tamer level on evolving to Champion or Ultimate | `evl.c: EVL_applyEvolution` (`addTamerLevel(20/100, 1)` if no script is running) | `tamer.evolution_level_gain`, `GameSession::addTamerLevel` | `a tamer level chance of 100 ...` | Same |
| Virus bar full and natural evolution in the same hour | `partner_impl.c: tickConditions` (the virus is looked at afterward and its fate wins) | `EvolutionAdapter::tick` does not look at natural evolution if the partner has `VirusBarFull` pending (`Partner::pendingEvents`); the event turns it into the virus form | `the virus bar filling in the hour the counter reaches ...` | Same (decided on 2026-10-20): it is decided per session tick, not per frame |
| Which items the partner refuses | `partner_impl.c: partnerWillRefuseItem` | `item_use.refusal`, `InventoryAdapter` (`RefusalCheck`) | `the partner never takes some items ...`, `a fixed evolution item is refused ...` | Same |
| Only food answers hunger | `partner_impl.c: partnerHandleFoodFeed` | `item_use.meals` → `FoodEffect::meal` | `food feeds the partner ...` | Same |
| Item trade: table from variable 249, the new item before removing the offered one, trigger 5 + 3 × table + row and 14 with all nine | `shop/script_menu.c: openMojyamonShop`, `mojyaTradeFillItemList`, `setMojyaItemTradedTrigger` | `ItemTrader`, `tradeRows`, `makeTrade`; `item_trader` section | `item_trader_tests.cpp`; route `trade` (frzl14) | Same |
| Baby discount in shops | `script_common.c: isPartnerBaby` (`BABY_IDS`) | `item_use.shop_discount_levels` | `buying a key item asks ...` | Same |
| Probability of medicines | `item.c: handleMedicineHealing` (`randomLimit(roll) < cure`) | `CureEffect` with `cure × 100 / roll` | — | Same with the original's values (3/3, 2/10, 10/10) |
| A chip's script section | `item.c: handleChips` (0x16): with the script paused, `callScriptSection(0, 0x4dd, 0)` | `InventoryAdapter::takeScriptSection`; the game requests the MAPHEAD section and the story discards it if another script runs; the chip is spent anyway | `inventory_adapter_tests`: "item.c handleChips…"; `w11-missing/shots/pilot-after.png` (yaka14 → twna01) | Same |
| Learned techniques in the requirements | `evolution.c: getNumMasteredMoves` | `EvolutionAdapter::setMoveSource`, `BattleAdapter::moves`, `evolution.requirements.mastered_moves_below` | `the evolution requirements count the learned moves below ...` | Same |
| The first partner's techniques: it learns and equips one, and its finisher | `partner_impl.c: initializePartner` | `partner_moves.new_game_slot`, `BattleAdapter::startNewGame` | `a new game's partner learns ...`, `the user's disc, extracted: the first partner ...` | Same |
| The second starting species' technique: position 2 (a technique of power 52), not 0 (power 155) | `partner_impl.c: initializePartner` (type 0x11: `moves[0] = 0x30`, `learnMove(0x2B)`); measured on the original after choosing the second answer: `moves = [0x30, 0xFF, 0xFF, 0x35]`, `learnedMoves = [0, 0x800]`, discipline and happiness 50, 360 h, energy 25, weight 15 | `partner_moves.new_game_species_slots`, `PartnerMoveRules::newGameSlotOf`; stats from script 193 (70/80/60/70, HP and MP 700) | `a new game's partner of a species with its own position ...`, `the user's disc, extracted: the second starting partner ...` | Same (before it equipped and learned position 0, the strongest technique in its list) |
| Equipped techniques are positions of the species' list | `stats.base.moves`, `btl.c`, `utils2.c: entityGetTechFromAnim` | `PartnerMoves::equipped`, `partnerCombatant` | `the partner fights with its stats, ...` | Same |
| The reborn equips one technique and keeps the learned ones | `evolution.c: reincarnatePartner` | `partner_moves.reborn_slot`, `Reborn` | `a reborn partner equips one move again ...` | Same |
| What a battle leaves on the partner (stats, care, wound, lives, 20 minutes and the time) | `battle_ui.c: battleStatsGainsAndDrops`, `tamer.c: startBattle/advanceBattleTime` | `BattleAdapter::finish` → `Partner::applyBattle` | `a won battle gives ...`, `a lost battle costs ...` | Same |
| Praising and scolding in the field, next to the partner: it comes close, the player makes its gesture, the partner responds and it counts when the gesture ends | `partner.c: partnerTickPraiseScold`, `tamer.c: tamerTickPraiseScold`, `partner_impl.c: handlePraiseScold` | `GameSession::praise`/`scold`; `field_items.praise_scold` (animations 38/39 and 11 when praising, 7 and 12/25 when scolding); the scene in `apps/game` (`PraiseScoldCall`) | `system_hook_tests.cpp`, `the original's praise reaches up to a tall partner ...` | Same ([game](game.md)); the partner menu's tab stays as a temporary deviation (decided on 2026-10-20/21, TODO) |
| Giving an item in the field: the partner comes close, eats it or refuses it | `game_menu.c: startFeedingItem`, `partner.c: partnerTickFeedItem`, `checkEatDistance` | `InventoryAdapter::offer`/`give`, `field_items.feed`; the scene in `apps/game` (`FeedScene`) | `the feeding scene offers an item first ...`, `the original's feeding takes the item at 360 ...` | Same, except the partner decides when the item is chosen, not when it is offered, and the item in the hand is not drawn ([game](game.md)) |
| Going to the toilet: the partner runs to the map's toilet and comes back | `partner.c: tickPartnerToilet`, section 0x4e2 | `field_items.toilet_walk`, the `toilets` of `maps.yaml`; the scene in `apps/game` (`FieldToilet`) | `a care scene runs the partner to a point ...` | Same; if it gets stuck, it stops ([game](game.md)) |
| Autosave when a recruit joins and after a tournament | The original does not autosave | `save.autosave.on_recruit_join/on_tournament_result` | `a recruit that joins ...`, `a tournament's battle ...` | Approved deviation (2026-10-20) |
| The Drill Tunnel's mine cart and dirt pile: routine 26 loads the cart without a model, 19 shows the model variable 254 names in front of the partner, 27 loads the pile at its size and 20 shrinks it one step (15 at the start, it never goes back; drawn from 6); 19 and 20 end the frame | `script_interp.c` (`call_routine` 0x13, 0x14, 0x1a, 0x1b), `map_object.c: loadDirtCartModel`, `setDirtCartModel`, `renderDirtCartModel`, `loadDirtPileModel`, `decreaseDirtPileSize`, `renderDirtPile` | `story_routines/routine_dirt_cart.cpp` (`DirtCartShown`, `DirtPileShown`, `story_props.hpp`); `apps/game`: `FieldStoryProps` | `the dirt pile shrinks a step ...` (`story_props_tests.cpp`); route `route-stage2-dirt.yaml` | Same; the pile's size is not saved in the session, as in the original. Place and scale compared with the original in tunn05 (E2-04: map forced through RAM, player at (1612, 0, 450), partner at (1430, 0, 241) turned 2514, cart model set by hand): `DIRT_PILE_LOCATION` = (2037, 0, 849), `DIRT_PILE_ROTATION` and `DIRT_PILE_SIZE_ROTATION` at zero, scale size × 0x1000 / 15; the pile comes out to the player's right with the same relative height (about 1.6 times the player's), the cart in front and to the partner's left, at ground level; model 0 is the box with dirt and 1 the empty box; with size 5 the pile is not drawn (`_work/routes-e2/stage2-release/cart-pile-orig-vs-dwe.png`) |
| The tunn06 boulder: `spawn_boulder` puts it at (-300, -500, 2000), `move_boulder` moves it in y and z, turns it 10 in x and ends the frame, `despawn_boulder` removes it; changing map removes boulder, cart and pile | `script_interp.c` (`SCRIPT_OP_SPAWN_BOULDER`, `MOVE_BOULDER`, `DESPAWN_BOULDER`), `map_object.c: spawnBoulder`, `moveBoulder`, `unloadMapParts` | `StoryLuaHost::publishPropApi`, `story_props.cpp` (`BoulderShown`) | `the boulder's orders: ...`, `the boulder starts above ...`, route `route-stage2.yaml` | Same (boulder's place compared with the original in tunn06) |
| The giant poop of Trash Mountain: when loading map 0xa5 (gomi02), and only that one, whatever the story, the model `ETCNA\TRY` at (1922, 0, 503), without rotation and at scale 1; drawn while the map is drawn | `map.c: loadMap`, `map_object.c: initializeTrainingPoop`, `renderTrainingPoop`, `TRAINING_POOP_TRANSLATION` | `trainingPoopOn` (`story_props.hpp`); `apps/game`: `FieldStoryProps` | `Trash Mountain has the giant poop ...` (`story_props_tests.cpp`) | Same; the position is the original's constant |
| The teleport crystals of the ice sanctuary: when loading maps 101 to 103 (icsa05, icsa06, icsa07), whatever the story, the model `ETCNA\WARP` without rotation at each spot: four in icsa05 ((±2237, -1667) and (±2358, 419)), one in icsa06 ((0, 0)) and two in icsa07 ((2376, -127) and (1566, -3358)), at height -188 and its second object another 188 higher; the original's icsa08 case never runs. They are drawn while the map is drawn | `map.c: loadMap`, `map_object.c: loadWarpCrystals`, `addWarpCrystal`, `renderWarpCrystals` | `warpCrystalsOn` (`story_props.hpp`); `apps/game`: `FieldStoryProps` | `map_object.c loadWarpCrystals ...` (`story_props_tests.cpp`) | Same; the positions are the original's constants |
| The `species_ap` sanctuary pedestal (icsa01, script 93): routine 49 puts `ETCNA\ABOX` at (0, 0, 1500) and goes on; 50 slides it 10 per frame up to z 2000 (50 frames from the one after the order), with x at 5 when z is a multiple of 20 and at -5 if not, and holds the section until the end; changing map removes it | `script_interp.c` (`call_routine` 0x31, 0x32; `tickScript`), `map_object.c: spawnAngemonPedestal`, `moveAngemonPedestal`, `renderAngemonPedestal`, `unloadMapParts` | `story_routines/routine_angemon_pedestal.cpp` (`AngemonPedestalShown`); `apps/game`: `FieldStoryProps` | `the Angemon pedestal starts at z 1500 ...` (`story_props_tests.cpp`) | Same (decomp logic; position and model read from the executable); not measured on the original: in `_work/tmp/measure-queue.md` |
| Toy Town: routine 41 puts omoc03's gearbox `ETCNA\GAND` at (-100, 0, 1700); 43 puts in omoc08 the big box `ETCNA\BIGBOX` at (500, -600, 600) and the small one `ETCNA\SMABOX` at (-400, -400, 600), lids closed; 42 and 44 choose the box with variable 254 as a signed byte (41 and 43 set it to 0) and the lid of the chosen one (1 big, 2 small) turns -100 per frame at 20 per second up to -0x400 (11 frames, a quarter turn) and stays; all four go on in the same frame; 42's background reread (box 0 with trigger 350) is not needed in the engine | `script_interp.c` (`call_routine` 0x29-0x2c), `map_object.c: spawnGearbox`, `renderGearbox`, `somethingToyTown`, `spawnToyTownBoxes`, `renderToyTownBoxes`, `openToyTownBox`; `map.c: readMapTFS` | `story_routines/routine_toy_town.cpp` (`ToyTownGearboxShown`, `ToyTownBoxesShown`, `ToyTownBoxSelected`, `toyTownLidAfterFrame`); `apps/game`: `FieldStoryProps`, `FieldScene::setPosedProps` | `Toy Town's gearbox and boxes go on at once ...` (`story_props_tests.cpp`) | Same (decomp logic; positions and models from the executable); not measured on the original: in `_work/tmp/measure-queue.md` |
| Beetle Cup entry (routine 33, betl02, script 102): sets triggers 37, 38 and 39, variable 2 to 0x95 (0x16 if today is day 0x16; the next day leaves the seven low bits), 3 to cup 0x16 and 4 to the partner's species, and goes on in the same frame | `script_interp.c` (`call_routine` 0x21), `script_common.c: dailyPStatTrigger` | `story_routines/routine_beetle_cup_entry.cpp` (`beetleCupEntry`) | `the Beetle Cup entry writes the cup ...` (`story_props_tests.cpp`) | Same. Measured in the original's code (2026-11-07): case 0x21 of `scriptInstruction64to7E` (0x80104260) calls `setTrigger` with 0x25, 0x26 and 0x27 and writes 0x95 or 0x16 according to day 0x16. 37 is deliberately the same "entered" as routine 18's: script 102 looks at it to offer canceling an arena entry (or the cup's own) before entering; the tournament that reads these variables is routine 6 (E3-02) |
| Forgetting techniques after losing the last life (routine 47, script 212 section 0x49, in the old man's house after rebirth): counts the learned techniques and removes ⌈(10 − trainer level) × 2 × learned / 100⌉ (rounding from .50; 0 lets the script continue); for each one it picks at random among the learned, forgets it, empties its equipped slot, puts its number in variable 243 and shows "… was not passed on." (MAPHEAD 0x4d8, line 7, no speaker); 244 carries those remaining and the section waits for the last box | `script_common.c: lostAllLives`, `utils2.c: unlearnMove`, `hasMove` | `story_routines/routine_forget_moves.cpp`, `movesToForget`, `movesClearedBy`, `BattleAdapter::unlearnMoveNumber`; rules `story_scenes.forget_moves` | `lostAllLives ...`, `unlearnMove ...` (`battle_adapter_tests.cpp`); section forced in room08 with 11 techniques and level 0: two boxes (Red Inferno, Heat Laser) | Same, the original's bug too: the mask is 16 bits, so forgetting a technique with a bit from 16 to 31 in its word also clears all those of those bits (and 0x2C/0x30 use the mask 0x1000, 0x37/0x39 none). Measured in the original's code (2026-11-07, `unlearnMove` at 0x800E66E0): `andi 0xffff`, `xori 0xffff`, `andi 0xffff` and then `and` on the 32-bit word (`lw`/`sw`), so the high half is always cleared. Quirk `partner_moves.quirks.forget_mask_16_bits` (true in `original`); with false only the chosen technique is forgotten (`the original ruleset keeps unlearnMove's 16-bit mask ...`) |
| Removing the mist (routine 37, mist05, script 115): holds the section until `tickRemoveMist` finishes (+0x10 per frame to the two palettes, end at 0xc0/0x80: 7 frames from the starting ones); since the script sets trigger 0x94 first and `renderMist` clears the mist on every frame drawn with it (outside screen 0xdc), the routine ends on its first tick | `script_interp.c` (`call_routine` 0x25, `tickScript`), `overworld.c: tickRemoveMist`, `renderMist` (`MIST_CLUT_Y`) | `story_routines/routine_remove_mist.cpp` (`MistPalettes`, `removeMistOneFrame`) | `the mist goes in 7 frames ...` (`story_props_tests.cpp`) | Same: measured in mist01 (2026-11-03): with trigger 0x94 `MIST_CLUT_Y` goes to 0xc0 / 0x80 in the following frames and the mist leaves at once, leaving only the back layer; the field draws it (`StoryScenes::mistOn`, `apps/game` `field_mist.cpp`) |
| Loads for the final boss and the ending (routines 51 and 52, mgen99 and script 192): they read `BOSS_EFE.TMD`, the EAB overlay and its sound bank 0x15 (51) or the ending image, the ENDI overlay and bank 0x14 (52); 51 also loads species 0x73's `VBALL` (the boss) into slot 5. In the engine the banks remain: slot 8's goes back to the map's when the field loads another map or a battle is won (`BTL_handleVictorySequence` reloads `getMapSoundId`), and slot 5's stays until the next battle. Bank 0x15 only has sounds 0–3 (notes 60–63 of `ESALL` entry 21): mgen99's `play_sound(8, 6)` before the boss battle (0x0602) does not play in the original either; the one after (0x08d8) and 7 and 8 play with the map's bank (17) that the victory reloads; both go on in the same frame | `script_interp.c` (`call_routine` 0x33, 0x34), `sound_async.c: loadMapSounds2`, `readVBALLSection`, `map.c: loadMap`, `battle_main.c: BTL_handleVictorySequence` | `story_routines/routine_boss_and_ending_loads.cpp` (`loadRoutineSounds`, `loadRoutineSpeciesBank`, `dropRoutineSoundsOnNewMap`), `GameSession::useSceneSounds`, `useSpeciesSounds`, `endBattleSounds` | `routines 51 and 52 load ...` (`story_props_tests.cpp`); `a won battle gives slot 8 back ...`, `a slot from 5 to 7 takes the vball entry ...` (`session_sound_tests.cpp`); temporary route in mgen99: after winning, 6, 7, 8, 12, 13 and 14 of bank 17 play and the boss plays `playSound(5, 54)` from its `VBALL` | Same (banks and notes from the decomp and the disc); Assumption: not measured live (there is no save state in mgen99) |
| Final boss load (routine 40, mgen99, right after the script puts the boss, species 115, in slot 0): requests the load on the character with id 5 (the map's first NPC, the boss) and the section goes on in the same frame; the app removes it from what is drawn, puts the rings and the humming until partner routine 8 brings it | `script_interp.c` (`call_routine` 0x28), `eab.c: EAB_startBuildup`, `EAB_tickBuildup`, `EAB_addBuildupRing`, `EAB_tickBuildupRing`, `EAB_setModelColor` | `story_routines/routine_boss_buildup.cpp` (`BossBuildupStarted`); `packages/game/field`: `BossArrival`; `apps/game`: `BossArrivalScene` | `routine 40 starts the final boss's charge ...` (`story_props_tests.cpp`), `boss_arrival_tests.cpp` | Assumption: not measured on the original (there is no save state near the boss; in the measurement queue); the numbers come from the decomp |
| Final boss arrival (partner routine 8, mgen99 after routine 40): the session requests the `BossAppears` scene from the app and the section waits for the sync (`wait_for_entity(201)`); the app plays 340 frames (black, the boss alone with the camera turning, rings, particles, sounds 1, 2 and 3) and marks it done. From 50 to 290 the boss goes from wireframe to solid (`ENTITY1_WIREFRAME_TOTAL` = 16 − 16·sin, `renderWireframed`: of each object's faces, those of the solid part with texture and the models' light, the others as lines of a random gray from 0x37 to 0xFE); animation 0x33 starts over each time it reaches its last frame; then the battle against character 5 starts | partner routine 8 of `partner.c` and the player's state 18, `eab.c: EAB_tick`, `EAB_tickSpawn` | `PartnerScenePort` (`PartnerScene::BossAppears`); `apps/game`: `StorySceneGlue`, `BossArrivalScene` | `boss_arrival_tests.cpp`; route `_work/routes-e2/boss-arrival/route-mgen99.yaml` (arrival and battle) | Assumption: not measured (there is no save state near the boss). The 9 flashes of `EFECreateFlash` on the first frame (mode 0x20, subtracted, 50 frames, points and scales of `EAB_D_8006170C` read from the original's module) are drawn by `BossArrivalScene` with `addFlashPrims` |
| Starting the ending (routine 53, script 192): clears the scene sync, starts the player's final scene (player state 0x10, ENDI overlay) and holds the section; from the next frame, when the scene sets the sync, the game ends (`MAIN_STATE` 3): the app closes it, plays the ending movie and goes back to the title screen | `script_interp.c` (`call_routine` 0x35, `tickScript` case 53), `main.c: main` | `story_routines/routine_start_ending.cpp`, `StoryScenes::takeGameEnded`; `apps/game`: `PlaySession` (`MovieMoment::Ending`) | `routine 53 waits for the ending scene's sync ...` (`story_props_tests.cpp`); probe with script 192 at tend01's entrance (`_work/routes-e2/routines-pool2/tend01-ending/`) | Same: the final scene (partner routine 6) is played by `EndingScene` in `apps/game` ([game](game.md)) and routine 54, the end-of-game save, is in `story_routines/routine_clear_save.cpp` |
| The fire scene's shake (routine 34): 210 frames at 20 per second, steps of 1, 2, 3, 2 and 1 pixels, alternating; a square of the `SPOT256` image (the white disc, texels 64..127 × 0..63, CLUT 0x1e7) grows from the center 32 pixels per side every even frame up to 200, shakes with the drawing and is drawn subtractively with the color (0, 0xff, 0xff): it subtracts the cyan and the screen turns red; its last square is seen until the next frame. Measured on the original (tunn08, scene with trigger 230 not set): `MERAMON_SHAKE_POS_X` = -128 after 7 frames, whole screen red after 16 (a background pixel (74, 33, 16) becomes (74, 0, 0)), the text box neither shakes nor turns red; there is no HUD during the scene | `map.c: createMeramonShake`, `tickMeramonShake` (`MERAMON_SHAKE_OFFSET` 0x20, `MERAMON_SHAKE_GREEN`/`_BLUE` 0xff, `MERAMON_SHAKE_RED` 0) | `ScreenShakeStarted`; `apps/game`: `ScreenShake` (`screen_shake.hpp`), `FieldStoryProps::showShakeSquare`, `FieldScene::setStoryScreenPrims`, `MapCamera::setShake` | `screen_shake_tests.cpp`, route `route-stage2.yaml` (`_work/routes-e2/stage2-release/shake-orig-vs-dwe.png`) | Same; on screens wider (or taller) than 4:3 the square grows in the same proportion to cover the edges as at 320 × 240 |
| The mood bubble (routine 28): variable 247 gives the character and 248 the face (0 … 4); with 5 or more a bubble is removed. The face is a 32 × 32 frame of the map's first 4-bit image (u = face × 32), additive, at the condition point of the species' original type (`conditionFx` of the row `getOriginalType` gives: two of the cannon's four customers are variants), depth-sorted. Measured on the original (twna01, objects set by hand): the counter drops from 30 to 0 at one per frame at 20 per second; scale and rise are 16 − counter / 2 (scale 0x100 → 0x1000, rises 1 → 16 pixels); two routines in a row leave two objects that lower the counter twice per frame; without the character on the map it is neither drawn nor advances | `script_interp.c` (`call_routine` 0x1c), `script_common.c: createMonochromonMoodBubble`, `renderMonochromonMoodBubble`, `MONOCHROMON_BUBBLE_TIMER`, `translateConditionFXToEntity` | `story_routines/routine_mood_bubble.cpp` (`MoodBubbleShown`); `apps/game`: `MoodBubble`, `FieldStoryProps::takeMoodBubble`, `FieldScene::setStorySprites` | `routine 28 puts the mood ...` (`story_props_tests.cpp`), `mood_bubble_tests.cpp`; route in gcan11 (`_work/routes-e2/mood-bubble/`, shift at the shop: angry face over `species_bf`) | Same; when changing map the bubble goes away (in the original the object stays, but all the scripts remove it first) |
| The cannon shop shift's tally (routines 30, 31 and 56): 30 sets it to 0, 31 adds the sale's profit to it (variables 243 high and 244 low, with a 16-bit sign) and 56 leaves it in 243 and 244; the pages show it with `{shopvar}` (code 0x10, no leading zeros). It belongs to the session, not the map: a new game clears it and it is not saved. With 20 customers, script 49 pays 500 if 243 ≤ 6, 1500 if ≤ 11 and with more (tally of 3072 or more) wins the shift and the shopkeeper joins | `script_interp.c` (`call_routine` 0x1e, 0x1f and 0x38, `SHOP_VARIABLE`), `script_textbox.c: showTextbox` (code 0x10, `intToStringSJIS(.., 5, 1)`); scripts 49 and 199 to 202 | `StoryAdapter::shopTally`, `story_routines/routine_shop_tally.cpp`; `apps/game: story_textbox.cpp` (`{shopvar}`) | `routines 30, 31 and 56 keep the shop's tally ...` (`story_props_tests.cpp`); live in gcan11 with a keyboard: the "Total profit" page shows the number (before `{shopvar}` came out as is) | Same (#177). The victory (#178) follows the disc's script: sale and leaving thresholds per customer, `store_random` as `randomLimit` and the same tally; hard, not impossible: a simulation with the script's thresholds wins ~27 % of the shifts with the best policy and ~0.3 % without raising prices; two shifts measured in dwe ended at 2060 and 2440 (pays 1500). Assumption: the shift was not measured on the original (there is no save state in gcan11; in the measurement queue) |
| The cannon shop job for the progress bar (engine improvement, option `shop_job_bar`): the rule `story_scenes.shop_job` says the map (gcan11), the variable that counts the customers (123), how many there are (20) and from which high byte of the tally the ending pays better (7) and the shopkeeper joins (12); `ShopJobRules::tallyFrom` turns it into a tally with the same shift as routine 56 (`ShopTallyHighShift`, 8): 1792 and 3072 | `script_interp.c` (`call_routine` 56), script 49 section 0x52 | `story_scenes_rules.cpp` (`readShopJob`), `ShopJobRules`; `apps/game`: `ShopJobBar` | `the game pack's shop job joins from a tally of 3072 and pays better from 1792` (`story_props_tests.cpp`) | Improvement (option) |
| The improved shop job (engine improvement, option `shop_job_mode`): engine routines from 256 that the `story/patches/shop_job_*.lua` patches call and that answer in variable 110: 256 the mode, 257 the script of the next customer in the line (it makes it before the first one of each job, once), 258 the item of the arriving customer, 259 a turn at the counter (asks for the `ShopHaggle` screen and waits; `ShopJobAdapter::finishTurn` writes the result, the level in 117 and the face in 248 and adds the sale to the tally) and 260 the bonus at the end (`game:steel_heart` from 3584; with a full bag the `ItemKeeper` keeps it and it answers 2). The line comes from the playthrough's seed and the job number (save section `shop_job`) | script 49 (0x51, 0x52), scripts 199–202 (0x33), routines 28, 30, 31 and 56; the patches only touch 0x51 and add section 0x300 | `ShopJobAdapter` (`shop_job_adapter.hpp`), `routine_shop_job.cpp`; [game/shop_job](../game/shop_job.md); [reference](../../reference/shop-job.md) | `shop_job_tests.cpp` ("the improved shop job's line comes back the same after a save and a load", "routines 256 to 259…", "a dismissed customer leaves at once…", "routine 260 gives the bonus item from a tally of 3584", "with a full bag the bonus item goes to the item keeper") | Improvement (option) |
| Bird transport (routine 10): greeting, money box and list of destinations with price, "Are you sure?", the player's yes or no; yes charges, writes map and entrance in 247 and 248 and the script goes on at MAPHEAD 0x4e3; no or cancel gives the farewell. Measured on the original (routine forced through RAM at room02's counter with the six triggers and 1800 bits): the first three rows white, the last three red; confirming a red one does nothing; yes to Ancient Dino Region leaves 300 bits, 247 = 0x4f, 248 = 9 and leads to koda00 | `shop/script_menu.c: tickBirdraTransport`, `createBirdraTransportMenu`, `tickBirdraTransportMenu`, `birdraFillTargetList`, `script_common.c: calculateBirdramonMenuStrings` | `BirdTransport`, `story_routines/routine_bird_transport.cpp`, `ScreenTask` with `TaskJump`; `apps/game`: `BirdTransportScreen` | `bird_transport_tests.cpp`, `bird_cursor_tests.cpp`; route in room12 (`_work/routes-e2/bird-transport/`: list, yes and arrival at koda00) | Same; the money box's phrase is the first shopkeeper's (1220), because the bird is not in the shopkeepers' table |
| The fade to white and back (routines 45 and 46): variable 254 gives the frames; the section goes on in the same frame. Measured on the original (twna01, object 4005 set by hand): `fadeToWhite(40)` rises 3 per frame from the tone the last fade left (`FADE_PROGRESS` 20 → 50 in ten frames) and stays at 160 (white screen, except what is drawn in front: the clock, the text boxes); `fadeFromWhite(40)` draws 157 … 40 over 40 frames and on the 41st is removed at once, leaving `FADE_PROGRESS` at 40 | `script_interp.c` (`call_routine` 0x2d, 0x2e), `fade.c: fadeToWhite`, `fadeFromWhite`, `renderFadeOut`, `renderFadeIn`, `renderFade` (`FADE_MODE` 1, add) | `story_routines/routine_white_fade.cpp` (`WhiteFadeStarted`); `packages/game/field`: `WhiteFade`; `apps/game`: `FieldStoryProps`, `render::ScreenFade::setWhiteness` | `routines 45 and 46 fade to white ...` (`story_props_tests.cpp`), `white_fade_tests.cpp`; route in ogre03 (`_work/routes-e2/white-fade/`) | Same; the white is drawn behind all the UI (in the original the bottom HUD also whitens) |
| `spawn_sprite_at_entity`: the map's sprite is centered on the character's node point | `overworld.c: spawnSpriteAtEntity`, `spawnSpriteAtLocation` | `SpriteAtCharacter`; `apps/game`: `FieldStoryProps::placeSprite` | `the boulder's orders: ...` (the operands) | Same. Measured at the west lane trap in trop06 (`route-stage2-amida-trap.yaml`): in the original sprite 44 comes out at (-1538, -167, -3171) with the player at (-1540, 0, -3196), a 22×24 px mark centered at 0.47 of its height; in dwe node 10 sits at height 170 and the mark measures 22×22 px at 0.48. Flag 0 shows, 1 hides, in both |
| City prosperity: at the start of the loop (new game or load) and before each player save, variable 1 is rebuilt from the recruitment triggers 200 + species (3 to 0x3a, level 3 or more): each adds its level - 2, `species_ar`, `species_p` and `species_be` add 1; with no cup entered (variable 3 ≥ 0x17) triggers 0x25 to 0x27 are cleared. Measured on the original (e1-cp37): only `species_a` recruited (trigger 203) and variable 1 = 1, which gives the formula | `main.c: recalculatePPandArena` (called in `main` and `writeSavegame`) | `CityAdapter` (`city.yaml`), `packages/game/story: recalculateProsperity`; hooks `SessionSystem::beforeSave/afterLoad` | `a save of the player's rebuilds the prosperity ...`, `a load rebuilds the prosperity ...` (`city_medals_tests.cpp`) | Same; the autosave (new) does not rebuild it, the load does |
| File City screen where a saved game continues after the ending (0xa8 to 0xb3 according to triggers 0xdc+0xd6, 0xdd, 0xe1, 0xf6) | `map.c: getFileCityTopMap` (`main_menu.c: gameClearSave`) | `CityAdapter::clearedMap`, `cleared_map` of `city.yaml` | `a finished game continues on the File City screen ...` | Gap: the save after the ending (routine 0x36) does not exist yet, so nothing calls it |
| Field medals: on every frame the player walks free the ones that are missing and met are given (all techniques 0-56 → 5, stats 9999/999 → 7, money 999999 → 13, year 10 → 14, species 1-61 raised → 6 and +1 tamer level without notice, 100 fish → 9, all 66 cards → 12) as trigger 0x16c + medal, and leave the notice pending. Measured on the original: with 999999 bits in twna01 trigger 377 (0x16c + 13) is set and `MEDAL_AWARD_PENDING` = 1 | `tamer.c: checkMedalConditions`, `script_common.c: unlockMedal`, `hasMedal` | `MedalsAdapter` (`medals.yaml`), `packages/game/story: checkMedalConditions` | `the field's medal check gives a medal once ...`, `each condition met gives its medal once ...` | Same |
| Tamer level notice: `addTamerLevel` leaves it pending (up or down) only if the new level, before clamping, is still between 0 and 10 | `tamer.c: addTamerLevel` (`TAMER_LEVEL_AWARD_PENDING`, `TAMER_LEVELS_AWARDED`) | `GameSession::tamerLevelAward` | `a tamer level change is announced only when it stays within the limits` | Same; it is not saved, as in the original |
| The save machine registers the partner for versus mode: 40 records, the machine reaches 39; it saves name, species, max HP and MP, offense, defense, speed, brains, discipline and the first three equipped techniques | `main_menu.c: registerBattleData`, `tickMainMenu` 0x3D (`MENU_HIGHLIGHTS[15]`), `awardMachinedramonData` | `BattleRegistry` (`battle_registry.hpp`), `registeredPartnerOf`; `battle_registry` section | `a partner registered at the save machine goes in the save and comes back`, `the registry has the original's 40 slots, the machine reaching the first 39` | Same; versus mode, phase 5 |
| How many records have a partner (the 40); versus mode does not start with none (card error 8) | `main_menu.c: countRegisteredDigimon` (`tickMainMenu` 0x36) | `BattleRegistry::registeredCount` | `the registry counts the slots that hold a partner, as the versus mode asks` | Same; the versus menu that asks for it comes with that mode |
| Card shop: six cards of the day drawn in the new game and every new day (1 to 64 without 4; they can repeat); the sold one stays at 0xFF and with none the shop says "sold out" | `script_common.c: dailyPStatTrigger`, `shop/script_menu.c: hasAnyCardToBuy` | `packages/game/inventory: rollCardStock`, `CardShop`; quirk `card_shop.quirks.stock_repeats` | `a new game rolls the card shop's stock, and so does each new day`; `without the stock_repeats quirk ...` (`card_shop_tests.cpp`) | Same. Measured in the original's code (2026-11-07): the repeat check loads `st->smth[i]` (the slot being drawn, still 0xFF) instead of `[j]`, so it never rerolls and one card can fill two slots (≈22 % of days). The quirk `stock_repeats` (true in `original`) keeps it; a ruleset with false rerolls the repeat while another card remains |
| Buying a card: price by rarity; measured with 3000 bits: 3000 → 2900, the slot to 0xFF, copies 2 → 3 and the menu closes | `script_common.c: tickSingleCardShop` | `CardShop::buy` | `the card shop sells one card of the stock for its price` | Same |
| Selling cards: half the price per copy; measured: 2 copies at 2500 → +5000, copies 3 → 1 | `script_common.c: tickItemMenuAmountBox` | `CardShop::sell` | `the card shop buys copies back for half their price` | Same |
| Card pack: rarity `randomLimit(100)` (0 → 0, <5 → 1, <20 → 2, <50 → 3, otherwise 4) and then `randomLimit` among those of that rarity; measured: with state 0xCF0A481B card 0x3D comes out and with 0x0589E791 card 0x40 | `shop/script_menu.c: rollCard`, `rollCardPack` | `CardCollection::drawPackCard` | `a pack draws the cards the original drew from the same generator state` | Same |
| The pack's phrase names the card's species (variable 249), with no space before "that" as in the original | `shop/script_menu.c: showCardTextbox` | `CardsAdapter::drawPackCard` | `a pack's card puts its species in variable 249 ...` | Same |
| Sounds: fail (0, 0xB) when choosing an empty row or one that cannot be bought; deal (0, 0x17) only in gias06b | `script_common.c: createSingleCardShopMenu`, `playShopSoundOnlyInSavannah` | `CardShopRules::failSound`, `tradeSound` | `the card shop's rules carry the fail sound ...` | Same |
| Merit exchange: giving a card adds its `meritValue` (cap 9999) and removes one copy; taking an item subtracts its price and puts one in the bag; measured in dwe with 150 merits: card 2 (100) → 250, copies 1 → 0; Sup.recovery (20) → 230 | `shop/script_menu.c: openMeritShop` (states 8 and 0xc), `createMeritCardTradeDialogue` | `MeritShop::giveCard`, `takeItem` | `the merit exchange takes a card for its merit…`, `…gives an item…`, route `merit-shop` | Same |
| Canceling in the options answers the last one except with trigger 49 (then (0, 0xB) sounds) | `shop/script_menu.c: openMeritShop` (state 3), `tickSelectionDialogue` | `MeritShop::choiceCancellable` | `the merit exchange's rules carry…` | Same |
| Routine 15: the money box while the section goes on; routine 22: the tournaments' medal | `script_common.c: createShopBitsBox`, `tournament.c: scriptCheckTournamentMedal` | `story_routines/routine_money_box.cpp`, `routine_tournament_medal.cpp` | `script_common.c tickShopBitsBox: ...` (`apps/game/tests/money_box_tests.cpp`) | Ported; 22 without its own test |
| A script's step cell asks for its section once per frame of the original, on the tick when that frame runs: a section that ends at once has already ended when the next one arrives, so the menu (Triangle, Tab on PC) opens over the cell on every press. Measured on the original (state `01_field_outside_jijimon_house`, player at exit 0 of twna01, cell 66, section 0x42 of its script, which ends at once): the menu opens on 2 of 2 presses with `SCRIPT_SECTION` at 0x42 | `tamer.c: tamerTickWalkingState` (Triangle with `IS_SCRIPT_PAUSED` 1 before `checkMapInteraction`), `script_interp.c: callScriptSection` | `StoryAdapter::runCellSections` (only with `StoryRunner::frameRunsNextTick`) | `a step cell asked on the tick before each frame leaves the runner idle between frames`; route `menu-stress` | Same. Before, the cell asked for the section on every tick and the request waited for the next frame: the script counted as running almost always and the menu did not open (6 of 6 attempts failed, one second each), nor did talking with confirm |
| A confirm cell asks for its section on the original's frame that follows the press: the field reads confirm on the tick it arrives, which usually falls between two frames, and the request waits for that frame. The original reads the press once per frame, so it loses none | `tamer.c: tamerTickWalkingState`, `checkMapInteraction` | `FrameCells` (in `StoryAdapter::runCellSections`), `Field::confirmSection` | `a confirm cell pressed between frames is asked for at the next frame` and the field's `generous talk` (confirmSection only on the press's tick); chain `route-full-04` (tunn05 minecart, cell 81) | Same. Before, the section was only asked for if the press fell on the frame's tick: about 2 of every 3 presses were lost without a trace (2026-11-15) |
| The battle rules are read again (an adjustment, the difficulty or a change in the player's options, another ruleset) without leaving dangling those who already hold them: the field's mood bubble (`FieldStoryProps`, the effect points of `effectRules()`) and a battle in progress (`Battle`: rules and technique catalog). Before, `readRules` created new rules and a new catalog and freed the old ones, so with easy difficulty or a rules option on, the mood bubble of a shop customer after choosing an answer read freed memory (crash with 0xC0000005 on Windows) | The original does not reread its rules; `effect_common.c: getOriginalType` from `renderMonochromonMoodBubble` | `BattleAdapter::readRules` writes into the rules and catalog it already handed out | `the rules read again stay where the field and a running battle point at them` | Same (engine) |

## Tests

`tests/session_tests.cpp` (new game, same seed, raising days at fast forward → save → load in another
game → continue the same, slots, autosave when sleeping and on a new day according to the ruleset, the autosave in the game's slot
(loaded from 2 → writes 2; saved in 3 → writes 3; new game → first empty; never
over another game; with no empty ones it is skipped), the option off, old autosaves that load, adjustment in progress, death and
rebirth, the partner view, slot names), `tests/field_clock_tests.cpp` (with a test field: the clock's earlier
pace, the clock stopped during fades at any speed, autosave when changing map and the saved
spot), `tests/save_migration_tests.cpp` (games with sections from earlier versions),
`tests/evolution_adapter_tests.cpp` (with the made-up tables of `packages/game/evolution/tests/fixture.hpp`: evolution by
hours and autosave, virus bar, return with stats capped, item, script, save and load, section
check), `tests/inventory_adapter_tests.cpp` (with the made-up items of `packages/game/inventory/tests/fixture.hpp`: new-game
bag and its list, heal, eat with boost, chip, rejections, level evolution item, purchase with key
item and discount, save and load, defeat with the same seed, section checks),
`tests/battle_adapter_tests.cpp` (with the made-up techniques of `packages/game/battle/tests/fixture.hpp`: new-game
techniques, fighter, equip, victory and defeat, autosaves, a battle from start to finish, rebirth, save and load,
section checks; with `DWE_CONTENT`, the disc's first partner), `tests/system_hook_tests.cpp` (praise and scolding to the systems, tamer level with probability) and `tests/rules_tests.cpp`
(schemas of `new_game`, `tamer`, `item_use` and `partner_moves`, the original's values and checks). They use the
source tree's `game` package on a stand-in base package with empty tables, made-up species
(not the original's) and `MemorySaveStorage`.
