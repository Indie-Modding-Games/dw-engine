# Reference sheet: inventory

How the bag, money, item use and shops worked in the original game, according to the decomp. The constants in this
sheet seed the `inventory` section of the `original` ruleset (`content/packs/game/rules/original/inventory.yaml`); the
per-item data is extracted by `dwx` from the user's disc.

## What it does

The player carries a bag of items and a wallet. Items are picked up from the ground, won in combat, bought in shops or
given by the story. From the menu they are used (almost all are given to the partner), moved, sorted or dropped. In
combat they are thrown at the partner. Shops sell what the story has unlocked and buy what can be dropped.

## Rules

### The bag

- **Size:** up to 30 slots fit, but only the first N are used. The story sets N with a script command
  (`set_inventory_size`): a new game leaves it at 10 and other scripts raise it to 20 and to 30 **(verified)**.
- **Stacking** (`giveItem`): an incoming item goes to the **first** stack of its type. If that stack is not full, it
  adds and whatever goes past 99 is lost; if it already has 99, it does not go in **even if there are free slots**. If
  there is no stack, it takes the first free slot; with the bag full it does not go in **(verified)**.
- **Removing** (`removeItem`): it subtracts from each stack of that type; a stack that reaches 0 leaves the slot empty,
  without moving the others **(verified)**.
- **Counting** (`getItemCount`): the quantity of the first stack of the type **(verified)**.
- **Moving:** it swaps two slots **(verified)**.
- **Dropping:** only in the field and only items with `droppable` 1; the whole stack is dropped **(verified)**.
- **Key items:** the original has no important-item mark. Those with `droppable` 0 (8 in the American version: 115 to
  120, 123 and 124) are neither sold (`shopFillSellItemList`) nor dropped (`selectOptionMenu`); those with
  `item_color` 255 (123 and 124) are also painted in another color (`renderInventoryTop`) **(verified)**.
- **Sorting** (`sortItems`): there are three orders. Each one is an order of the six `sorting_value` categories;
  within each category, by item number. Items end up at the start and the gaps at the end **(verified)**.
- **New game:** 10 slots and 0 money. The startup state (`initializeInventory`) brings test items and 50,000; the new
  game script removes them and subtracts the 50,000 **(verified)**. Which items you start with is decided by that
  script (phase 4).

### Money

- It does not go past 999,999 when adding (combat prizes, scripts and sales) and does not go below 0 when subtracting
  with the script **(verified)**.

### Using an item

- **Where:** according to `item_color`: 0 only in the field, 1 only in combat, 2 in both, 255 never **(verified)**.
- **In the field** (states 4 to 6 of `partner.c`): the player gives the item to the partner, which can refuse it
  (`partnerWillRefuseItem`, see the [care sheet](care.md)). If it refuses, it is not spent. If it accepts, the effect
  is applied, one is spent and it counts as food (`partnerHandleFoodFeed`) **(verified)**.
- **In combat** (`startThrowingItem`): one is spent when thrown and the effect arrives when it lands; it cannot be
  refused **(verified)**.
- **Effects:** `ITEM_FUNCTIONS` gives each item's function. There is no table besides that, `HEALING_VALUES` (what the
  HP and MP cures heal) and `EVOLUTION_ITEM_TARGET` (what each evolution item evolves into); the rest of the values
  are constants in the code **(verified)**:
  - **HP and MP cures:** they raise current HP or MP without going past the maximum. They do nothing with the partner
    at 0 HP.
  - **Double cure:** 1500 of HP and MP, also only with HP.
  - **Statuses** (`handleStatusItems`, only with HP): one cures combat statuses; another cures them in combat and also
    does the double cure; another protects the partner in that combat; the Bandage always cures the injury and the
    sickness 20% of the time (rolls of 3 and 10); the Medicine always cures both.
  - **Reviving** (`handleRestore`): one returns half of the max HP and another all of it and cures the statuses in
    combat. In combat they cancel the defeat countdown.
  - **Disks** (`handleBuffDisks`, only with HP): they raise offense, defense or speed by 20 or 50 for the combat, capped
    at the value at the start of combat plus 30%, at most 999. The Omni Disk adds 20 to each.
  - **Chips** (`handleChips`): they permanently raise offense, defense, Brains, speed (+50), max HP or MP (+500) or
    pairs (+100, or +1000 of HP and MP) in exchange for 24 hours of life; those three also have a 10% chance of
    lowering a tamer level. Caps: 999 for the attributes and 9999 for HP and MP. The Auto Pilot calls a section of the
    script if it is paused; the Portable Potty empties the partner's urge.
  - **Food** (`handleFood`): energy, tiredness, happiness, discipline, weight, life, max and current HP and MP,
    attributes, a training bonus and a chance of getting sick. The favorite food gives energy × 1.4 and +2 happiness.
    Every food lowers the bathroom counter by 1.
  - **Evolution** (`handleEvoItems`): it evolves into the table's destination if the partner is one level below; three
    items lead to fixed destinations without looking at the level.

### Shops

- **Stock:** there is no table. Item `i` is sold while trigger `0x180 + i` is active (`shopFillBuyItemList`)
  **(verified)**.
- **It can be bought** if the whole price fits in the money and, either there is a free slot and no stack of the item
  is full, or there is no slot and there is an unfilled stack of the item **(verified)**.
- **Buying:** the price is `value`; with a Fresh Digimon as partner, × 90 / 100. At most 99 are bought at once, no more
  than what is missing to reach 99 in the bag and no more than the money covers **at the full price**
  (`createItemMenuAmountBox`) **(verified)**.
- **Selling:** only what can be dropped (`droppable` other than 0), at `value / 2`; money does not go past 999,999
  **(verified)**.
- **Other shops:** the merit exchange (`merit_value`) and the recycling shop, which sells what was lost on falling in
  combat, are menus opened by the script. In the engine: `session::MeritShop` and `session::ShopCounter`.

### Defeat

- On losing (`handleItemLoss`), `occupied slots × 30 / 100` slots are lost at random, rounding down, each with its
  whole stack. What is lost that can be recycled goes to the recycling shop, up to 99 of each **(verified)**.

### Ground items and chests

- The items an enemy drops fall to the ground (up to 11 at a time; the rest are lost). On picking them up or opening a
  chest, one is tried to be put in the bag; if it does not fit, it stays where it was **(verified)**.

### Cards

- The player keeps up to 9 copies of each card (`script_interp.c: getCardAmount`, four bits per card) **(verified)**.
  A pack gives 3 cards (`rollCardPack`); each one rolls rarity with `randomLimit(100)` (0: rarity 0; less than 5: 1;
  from 20: 2; from 50: 3; otherwise, 4) and then a random card of that rarity (`rollCard`) **(verified)**. The text
  line (MAPHEAD 0x4d3) says whether it is new (by its rarity) or a duplicate, and variable 249 carries its Digimon
  (`showCardTextbox`) **(verified)**.
- Engine: `packages/game/inventory/cards`, `session::CardsAdapter` (`cards` rules, `cards` save section). The card shop
  and its selling are in `session::CardShop`, and the merit exchange in `session::MeritShop`.
- The scripts: `story.give_card(n)` adds one copy without going past `max_copies` and `story.take_card(n)` removes one
  without going below 0 (`script_interp.c:1143`, `:1151`); `story.cards(n)` says how many there are
  (`scriptCompareCard`). A card that the table does not have is a script error; without cards (no table or no rules)
  they change nothing (`story_orders_cards.cpp`).

## Data

| Table | Record size | Entries (US) | What it stores |
|---|---|---|---|
| `ITEM_PARA` | 32 bytes | 128 | Per item: name, price (`value`), merit value, sort category (`sorting_value`), where it is used (`item_color`), whether it can be dropped (`droppable`) and an unidentified field. `dwx` extracts it to `data/items`. |
| `ITEM_FUNCTIONS` | 4 bytes | 128 | Each item's function. |
| `HEALING_VALUES` | 2 bytes | 4 | HP or MP that each cure heals. |
| `EVOLUTION_ITEM_TARGET` | 1 byte | 44 | Destination of each evolution item. |
| `SHOP_MAPS`, `SHOPKEEP_DIGIMON_TYPES` | | 8 and 11 | Maps with a shop and shopkeepers (`data/shops`). |

`dwx` writes to `data/item_effects` each item's function, what it heals, what it evolves into and the values of each
food and chip case, which it reads from the code without copying it ([tables](../formats/tables.md#item-effects)).

## Oddities

- **What does not fit in the stack is lost** (`giveItem`), and the shop already charged for it. Quirk
  `stack_overflow_is_lost`.
- **A full stack blocks the item even if there are free slots** (`giveItem` only looks at the first stack).
- **A Fresh Digimon's purchase cap uses the full price** (`createItemMenuAmountBox`, `shopFillBuyItemList`): with the
  discount a few more would fit. Quirk `baby_discount_limited_by_full_price`.
- **The Omni Disk does not look at the cap** (`handleBuffDisks`). Quirk `omni_disk_ignores_limit`.
- **Items without effect that are spent:** keys, rods and the like (null function) and five chips whose case does
  nothing can be given to the partner and are spent without effect. Quirk `useless_items_consumed`.
- **Food does not check life at 0** (it adds `remainingLifetime` directly) and chips do (`modifyLifetime`).

## Where it is in the decomp

| Topic | Files and functions |
|---|---|
| Bag | `src/main/item.c`: `giveItem`, `removeItem`, `getItemCount`, `setInventorySize`, `initializeInventory`, `pickupItem` |
| Menu | `src/main/inventory.c`: `selectOptionMenu`, `sortItems`; `src/main/utils.c`: `sortItemsById` |
| Effects | `src/main/item.c`: `ITEM_FUNCTIONS`, `handleHPHealingItem`, `handleMPHealingItem`, `handleDoubleFloppy`, `handleStatusItems`, `handleMedicineHealing`, `handleRestore`, `handleChips`, `handlePortaPotty`, `handleFood`, `handleEvoItems`; `src/main/main.c`: `handleBuffDisks`, `startThrowingItem` |
| Use in the field | `src/main/partner.c` (states 4 to 8 of giving an item); `src/main/partner_impl.c`: `partnerWillRefuseItem` |
| Shops | `src/main/script_common.c`: `shopFillBuyItemList`, `shopFillSellItemList`, `createItemMenuAmountBox`, `tickItemMenuAmountBox`, `isPartnerBaby` |
| Defeat | `src/main/script_common.c`: `handleItemLoss` |
| Money | `src/main/battle_ui.c`; `src/main/script_interp.c` (`ADD_MONEY`, `REDUCE_MONEY`, `set_inventory_size`) |
