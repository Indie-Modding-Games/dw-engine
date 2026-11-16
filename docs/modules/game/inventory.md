# inventory

The inventory (`dwe::game::inventory`, `game` layer): the player's bag, their money, item use and shop trading.
What the original did is in the [inventory sheet](../../reference/inventory.md).

It does not draw menus or know the partner: it depends on `core`, `assets` and `ruleset`. The numbers come from the
ruleset's `inventory` section (`rules/<id>/inventory.yaml`) and the per-item data from the base pack's tables
`data/items`, `data/item_effects` and `data/shops`, which `dwx` extracts.

## Usage

```cpp
auto inventory = dwe::game::inventory::Inventory::fromRuleset(ruleset);   // new game: 10 slots, 0 money
auto items = dwe::assets::loadItemTable(vfs);
auto table = dwe::assets::loadItemEffectTable(vfs);
dwe::game::inventory::ItemEffects effects{*table, inventory->rules().effects, inventory->rules().quirks};

auto item = *items->handleOf("bandage");   // the item's handle in this session (docs/modules/engine/assets.md: ids in the session)
inventory->add(item, 1);                    // pick up, prize, chest: AddResult says what went in, what was lost and what did not
if (!inventory->hasRoomFor(item)) { ... }   // the item stays on the ground or in the chest
auto used = useItem(*inventory, item, UseContext::Field, *items, effects, partnerTarget);
```

- **State** (`InventoryState`): the 30 slots (`std::optional<Stack>` with the item's handle and the quantity),
  how many are in use (`slotCount`) and the money. `toYaml` and `fromYaml` convert it to YAML for the saved game's
  section, with each item by its id and the version in `SectionVersion` (2); the session wraps them in a
  `SectionSerializer` from `packages/game/save`. Version 1 saved the item's number: `itemNumbersToIds` migrates it to the id
  of the original item with that number. A slot whose item is in no mounted table (that of a mod that is no longer
  there, or a version 1 number that is not any original item) is left empty: `fromYaml` returns those
  stacks (`DroppedStack`, id and quantity) and the session names them in the log; the rest of the game loads. The
  ids are written in quotes.
- **New game**: `bag.new_game_slots`, `bag.new_game_money` and `bag.new_game_items` (`StartingItem`: item id
  and quantity, from 1 to `max_stack`). The inventory does not set them: the session creates the bag and adds the list. In the
  original the list is empty: script `dg 193` removes everything that `initializeDefaultSavegame` leaves.
- **Bag** (`Inventory`): `add`, `hasRoomFor`, `countOf`, `remove`, `move`, `discard`, `sort`, `setSlotCount`,
  `loseOnDefeat` (returns the chosen stacks, the ones the recycle shop counts) and `addMoney`, `spendMoney`.
- **Effects** (`ItemEffect`): a flat description of what an item does (heal, revive, cure statuses, protect,
  medicine, raise stats in battle or permanently, care changes, Tamer level, script section, potty,
  evolution). `ItemEffects::resolve` builds it from the table and the ruleset.
- **Target** (`ItemEffectTarget`): implemented by whoever has the partner. `accepts` decides whether it accepts (only on the
  field: `partnerWillRefuseItem`) and `apply` applies the effect with its caps.
- **Use** (`useItem`): checks whether the item is there, whether its `item_color` allows using it there and its effect; on the field it asks the
  partner; it spends one and applies. It returns `UseResult`: the `UseOutcome` (`Used`, `Refused`, `NotUsableHere`, `NotInBag`,
  `Useless`) and `keyItem`.
- **Key items** (`isKeyItem`): the original has no mark of its own; they are those with `droppable` equal to
  `use.key_item_droppable` (0), which are neither sold nor discarded. `useItem` and `buy` (`Purchase::keyItem`) report it so that
  the session decides the autosave; the inventory never saves.
- **Shops** (`shop.hpp`): `shopStock` with the story triggers (`StockTriggers`), `buyPrice`, `sellPrice`,
  `maxPurchase`, `canAfford`, `buy`, `saleOffers` and `sell`. `Buyer` says whether the partner is a baby. The triggers go
  by the item's number in the original, so an item that a mod adds is not yet sold in any shop.
- **Ids**: the ruleset section names each item by its id; `checkItemIds` checks that they are in `data/items`.
- **Stacking** (`StackingRule`): the strategy named by `bag.stacking`; `stackingRules()` registers `original`.

## Quirks

Each one is a `quirks` flag in the section; `original` has them all at `true` (decided by the design
lead, 2026-10-20). The corrected variants will come later in a design ruleset, not in `original`.

| Quirk | What it does |
|---|---|
| `stack_overflow_is_lost` | What goes over 99 when stacking is lost; with `false`, it does not go in and stays where it was. |
| `baby_discount_limited_by_full_price` | The maximum that can be bought with a baby is computed with the full price. |
| `omni_disk_ignores_limit` | The three-stat disk does not look at the battle cap. |
| `useless_items_consumed` | An item with no effect is spent when used; with `false`, it stays in the bag. |

## Design decisions (2026-10-20)

From the design lead:

1. **Where the effects come from**: hybrid. Each item's numbers come from `data/item_effects` (extracted by
   `dwx`) and the constants of each function's logic, from the ruleset (`effects`), with their citation.
2. **New game items**: a ruleset list (`bag.new_game_items`) cited to script `dg 193`, which the
   session sets; in `original` it is empty. When the phase 4 script machine runs `dg 193`, it stays as a fallback.
3. **Quirks**: all 4 at `true` in `original`; the corrected variants, in a design ruleset later.
4. **Key item**: `droppable` equal to 0 (8 items in the original), with no mark of its own.
5. **Loss on losing a battle**: with `ruleset::Random` (the game's numbers), not with the original's roll;
   the effect is the same (Same).

## Parity with the original

Status: **Same** (same behavior), **Approved deviation** (a change decided by design) or **Gap** (missing).

| Rule | Decomp | Our code | Test | Status |
|---|---|---|---|---|
| Stack in the first stack, cap 99, the rest is lost | `item.c: giveItem` | `stacking.cpp: OriginalStackingRule`; `bag.max_stack` | `bag_tests`: "a stack holds 99…" | Same |
| A full stack blocks even if there are free slots | `item.c: giveItem` | `OriginalStackingRule::addToStack` | `bag_tests`: "a stack holds 99…" | Same |
| Full bag: the item does not go in | `item.c: giveItem`, `pickupItem` | `Inventory::add`, `hasRoomFor` | `bag_tests`: "a full bag refuses…" | Same |
| Removing leaves a gap | `item.c: removeItem` | `Inventory::remove` | `bag_tests`: "a stack that runs out…" | Same |
| Counting the first stack | `item.c: getItemCount` | `Inventory::countOf` | `bag_tests`: "items join…" | Same |
| Sizes 10, 20 and 30 by script | `script_interp.c: set_inventory_size`; `item.c: setInventorySize` | `Inventory::setSlotCount`; `bag.slot_counts` | `bag_tests`: "the story grows…" | Same |
| New game: 10 slots and 0 money | script `dg 193`; `main_menu.c: initializeDefaultSavegame` | `Inventory` constructor; `bag.new_game_*` | `bag_tests`: "a new game's bag…" | Same |
| New game: no items | script `dg 193` (`script_interp.c: remove_item` of the 29 slots of `main_menu.c: initializeDefaultSavegame`, without `give_item`) | `bag.new_game_items: []`; the session sets them (`InventoryAdapter::startNewGame`) | `rules_tests`: "the original starts with an empty bag…"; `packages/session` `inventory_adapter_tests` | Same (when the phase 4 script machine runs `dg 193`, the list stays as a fallback; decided on 2026-10-20) |
| Moving swaps slots | `inventory.c: selectOptionMenu`, `moveItem` (swaps item, quantity and name of the two slots) | `Inventory::move` | `bag_tests`: "moving swaps…" | Same |
| Discarding: field, `droppable` 1, whole stack | `inventory.c: selectOptionMenu` | `Inventory::discard`; `use.discard_droppable` | `bag_tests`: "moving swaps…" | Same |
| Discarding removes the slot's quantity from all the item's stacks | `inventory.c: selectDropItem` (Yes: `tickInventoryUI`, case 7, `removeItem(type, the slot's quantity)`). Measured on 2026-11-04 (`15_item_option_box`): Small Recovery 2 in slot 2 and 5 in slot 5; discarding slot 2 empties it and leaves slot 5 with 3 | `Inventory::discard` calls `remove` with the slot's stack; before, it emptied only that slot (with the original's stacking there are never two stacks of an item, so it only differs with an edited game) | `bag_tests`: "discarding a slot takes…" | Same |
| Spend one on use | `game_menu.c: removeOneSelectedItem` (partner.c, state 6, when eating it); `main.c: startThrowingItem` in battle | `use.cpp: giveItem` (`remove(item, 1)`) | `use_tests` | Same |
| Key item: `droppable` 0, neither sold nor discarded (no mark of its own) | `script_common.c: shopFillSellItemList`; `inventory.c: selectOptionMenu` | `isKeyItem`; `use.key_item_droppable`; `UseResult::keyItem`, `Purchase::keyItem` | `use_tests`: "using an item says…"; `shop_tests`: "buying a key item…" | Same (8 items; decided on 2026-10-20; the notice is for the autosave, which does not exist in the original) |
| Sort by categories and number | `inventory.c: sortItems`, `ITEM_SORT_ORDER`; `utils.c: sortItemsById` | `Inventory::sort`; `sort.orders` | `bag_tests`: "sorting groups…" | Same |
| Money: cap 999,999, floor 0 | `battle_ui.c`; `script_interp.c` | `addMoney`, `spendMoney`; `money.max` | `bag_tests`: "money stops…" | Same |
| Defeat: 30% of the occupied slots, rounded down and counted once; each roll picks an occupied slot and takes all the stacks of its item; if the bag empties, nothing more is lost | `script_common.c: handleItemLoss`; `item.c: removeItem` | `Inventory::loseOnDefeat`; `defeat.lost_percent` | `bag_tests`: "a defeat loses…", "a defeat's pick takes every stack…", "a defeat that empties the bag…", "a defeat loses what the original's rule loses…" | Same in effect (decided on 2026-10-20): roll with `ruleset::Random`, the game's numbers (`InventoryAdapter::loseOnDefeat`), not the original's. With a mod's `bag.max_stack` above 99 all the item's stacks are removed whole, whereas the original's `removeItem(…, 99)` would leave whatever goes over 99 |
| Recycle shop with what was lost | `script_common.c: handleItemLoss`, menu 2 | `loseOnDefeat` returns only the chosen stacks, as the original counts them; the session passes them to the shop (`InventoryAdapter::setLossListener` → `ShopCounter::recycleLost`) | — | Outside this module (`packages/session`) |
| Use according to `item_color` | `inventory.c: selectOptionMenu` | `use.cpp: usableIn`; `use.*_colors` | `use_tests`: "colour 0…" | Same |
| Field: refusal without spending; spend and apply | `partner.c` (states 4 to 6); `partner_impl.c: partnerWillRefuseItem` | `useItem`, `ItemEffectTarget::accepts` | `use_tests`: "in the field…" | Same (the partner decides the refusal) |
| Battle: spent on throwing, always arrives | `main.c: startThrowingItem`, `tickThrownItem` | `useItem` with `UseContext::Battle` | `use_tests`: "in the field…" | Same (the throw's delay belongs to the battle) |
| Items with no effect are spent | `partner.c`; null `ITEM_FUNCTIONS` | `useless_items_consumed` | `use_tests`: "an item that does nothing…" | Same |
| HP and MP heals | `item.c: handleHPHealingItem`, `handleMPHealingItem`, `HEALING_VALUES` | `ItemEffects::describe`; `data/item_effects` | `effects_tests`: "healing items…" | Same |
| Double heal 1500 | `item.c: handleDoubleFloppy` | `effects.double_floppy` | `effects_tests`: "healing items…" | Same |
| Statuses, protection, medicine | `item.c: handleStatusItems`, `handleMedicineHealing` | `describeStatus`; `effects.status` | `effects_tests`: "status items…" | Same |
| Revive | `item.c: handleRestore` | `describeRevive`; `effects.revive` | `effects_tests`: "status items…" | Same |
| Disks with a +30% cap, 999 | `main.c: handleBuffDisks` | `describeDisk`; `effects.battle_disk` | `effects_tests`: "status items…", "the omni disk…" | Same |
| Chips: stats, life, Tamer level | `item.c: handleChips` | `describeChip`; `data/item_effects`, `effects.chip` | `effects_tests`: "chips give…" | Same |
| Autopilot and potty | `item.c: handleChips`, `handlePortaPotty` | `effects.chip.script`, `effects.chip.toilet` | `effects_tests`: "chips give…" | Same |
| Food, favorite, potty −1 | `item.c: handleFood` | `describeFood`; `data/item_effects`, `effects.food` | `effects_tests`: "food gives…" | Same |
| Evolution by table and fixed | `item.c: handleEvoItems`, `EVOLUTION_ITEM_TARGET` | `describeEvolution`; `effects.evolution` | `effects_tests`: "evolution items…" | Same |
| Healing particles: with a ring for the large items; medicine only if it cures the sickness | `item.c` (`addHealingParticleEffect`), `handleMedicineHealing` | `ItemEffect::healingParticles`; `effects.healing_particles`; `InventoryAdapter::takeHealingParticles` | `effects_tests`: "the larger items show…" | Same |
| Stock by trigger `0x180 + i` | `script_common.c: shopFillBuyItemList` | `shopStock`, `StockTriggers`; `shop.stock_trigger_base` | `shop_tests`: "the stock is…" | Same |
| When it can be bought | `script_common.c: shopFillBuyItemList` | `shop.cpp: isBuyable` | `shop_tests`: "the stock is…", "with the bag full…" | Same |
| Price, 90% baby discount | `script_common.c: tickItemMenuAmountBox`, `isPartnerBaby` | `buyPrice`, `buy`; `shop.baby_discount_percent` | `shop_tests`: "buying pays…" | Same |
| Maximum purchase | `script_common.c: createItemMenuAmountBox` | `maxPurchase`; `shop.max_amount` | `shop_tests`: "buying pays…", "the stack's room…" | Same |
| Sale at half price, only what can be discarded | `script_common.c: shopFillSellItemList`, `createItemMenuAmountBox` | `saleOffers`, `sell`; `shop.sell_divisor` | `shop_tests`: "selling pays…" | Same |
| The merit exchange list: items 0 to 127 with merit, by number; it can be taken if the merits suffice and it fits in the bag (a free slot or a stack below the cap) | `shop/script_menu.c: shopFillMeritItemList` | `meritOffers` | `merit_shop_tests`: "the merit list is every item…", "with a full bag…" | Same |
| A card is worth its `meritValue`; merits do not exceed 9999 | `shop/script_menu.c: createMeritCardTradeDialogue`, `openMeritShop` (state 8) | `cardMerit`, `addMerit` | `merit_shop_tests`: "a card is worth…" | Same |
| Items on the ground (11 at a time) | `item.c: DROPPED_ITEMS`, `pickupItem` | `hasRoomFor`, `add` | `bag_tests`: "a full bag refuses…" | Outside this module: the ground belongs to `packages/game/field` (`field_items.hpp`) and the session picks it up into the bag (`FieldItemsAdapter`) |

## Tests

- Unit tests in `tests/`, with the `original` ruleset from the `game` pack and made-up items (`tests/fixture.hpp`):
  no table of the original is copied.
- `rules_tests` changes a ruleset value (`bag.max_stack`) and swaps the stacking strategy for another one registered in
  the test.
- `content_tests` ("the user's disc, extracted: …") loads with `DWE_CONTENT` all the items, effects and shops of the
  user's disc and resolves each item's effect: in the US one all 128 resolve.
