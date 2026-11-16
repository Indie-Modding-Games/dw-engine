# The canyon shop job

Monochromon's job at the canyon shop (map gcan11): the player serves 20 customers and, if the tally of what was earned reaches the goal, Monochromon joins the city. The engine ships an improved version, documented and with an option: `shop_job_mode` in the Game tab (`improved` by default; `original` plays the disc's script as it is; in the console and in routes, `shop_job_mode("original")`). The improved version's numbers are in `rules/original/shop_job.yaml` of the game pack; the code is in [`game/shop_job`](../modules/game/shop_job.md).

## The original

- **Verified** (scripts 49 and 199–202): script 49 (section 0x51) starts the job (routine 30 sets the tally to 0, variable 123 counts the customers) and its section 0x52 sends each customer, by a roll, to one of the four customer scripts (199–202, section 0x33). At 10 customers the shopkeeper says how it is going; at 20, he pays.
- **Verified**: each customer asks for a random item (Meat, Portable Potty or Medicine) and the player picks in menus to raise, keep or lower the price. "Raise" rolls the markup at random and "I'll think about it" rolls again at no cost. The mood bubble's face (routine 28) only depends on the price level.
- **Verified**: a customer who leaves angry adds two to variable 123 (it uses up two of the 20 slots).
- **Verified**: the pay comes from the tally's high byte (routine 56): under 1792, 500 bits; from 1792, 1500; from 3072, 2000 bits, prosperity goes up 2 and Monochromon joins (trigger 247).
- With the item's luck, the tally's ceiling goes from 800 to 16000: playing perfectly, he joins in about 4 of every 10 jobs (simulation).

## The improved version

### Customers

- When the job starts (first customer) the line of 20 is made with the playthrough's seed and the job number: saving and loading does not change it and it spends no game random numbers.
- Species and items come from shuffled bags (the counts, in the ruleset). Each species has patience and a character (spendthrift, haggler, miser, fickle), which show at the counter.
- Each customer arrives in a mood that is visible (cheerful, normal, serious) and moves its budget one level up or down.
- The budget (the highest price it accepts) is hidden and comes from the original's acceptance table for its species; some buy nothing.
- The line is drawn again until the perfect score (selling to each one at its budget after a chat) falls between 4000 and 4300: the goal can always be reached.
- A customer who leaves uses up a single slot.

### At the counter

The counter screen (`shop_haggle`) replaces the price menus; the walk, the lines and the mood bubble are still the original's.

- **Price**: one of the original's 9 levels (from +50% to −30%). Each row says the price, what it adds to the tally (the price minus the cheapest, like the original), the chance to buy from what the player knows (species, mood, chat and refusals) and the most likely face. The chosen level is exact: there is no roll.
- **Face**: Delighted (2 levels or more below the budget) or Pleased buys; Doubtful (1 above) and Annoyed (2) cost 1 patience; Furious (3 or more), 2. Each refusal rules out budgets and the list is recalculated. The face shows in the mood bubble (faces 0 to 4) and on the screen.
- **Chat**: once per customer, with patience 2 or more; it raises the budget one level for 1 patience.
- **Dismiss**: the customer leaves without buying or getting angry.
- With no patience left, it leaves without buying.

### Pay

| Tally | Pay |
|---|---|
| under 1792 | 500 bits |
| from 1792 | 1500 bits |
| from 3072 | 2000 bits and Monochromon joins (trigger 247), like the original |
| from 3584 | the above and the bonus, a Steel Heart (`game:steel_heart`); with a full bag it goes to the item keeper (the job does not repeat, so it is not lost) |

## Interface

- **HUD bar** (option `shop_job_bar`): from 0 to the line's perfect score (in original mode, from 0 to the goal) with three marks and their label: better pay, joins (the main one) and bonus, with the item's icon. The fill carries a class for each pay tier reached. The **?** button opens the rules panel; with keyboard or gamepad, the `help` action (H or the left trigger) while the bar is visible, with its key drawn next to the **?**.
- **Rules panel** (`shop_job_rules`): it opens by itself at the first customer of each job, with the bar's **?**, with `help` (H or the left trigger) and with the menu button (Tab or the gamepad's menu button) at the counter. Confirm, cancel, `help`, Esc or the gamepad's Back close it.
- **Counter** (`shop_haggle`): species (with its sprite from the tournament board, `shop_haggle.species_icon`), character, mood, face, patience, the item with its regular price, the 9 prices, Chat, Dismiss, the offers made to this customer and the tally. Mouse or keyboard/gamepad (arrows, confirm).

## How it hooks into the story

The patch `story/patches/shop_job_049.lua` changes only section 0x51 (the counter): it asks the engine for the mode (routine 256) and in the original lets the disc's section run, touching nothing else of the job. In the improved mode it plays the original's chat and continues in a section of its own, 0x300 of script 49 (the loop: next customer, halfway, the pay) and of scripts 199–202 (`shop_job_199.lua`…`shop_job_202.lua`: the customer). This way the two modes do not mix and `dwx script-warps` follows each on its own. The engine's own routines (numbers from 256, `session/src/story_routines/routine_shop_job.cpp`) answer in variable 110: 256 the mode (1 improved, 0 original), 257 the script of the next customer in the line, 258 the item of the arriving customer, 259 a turn at the counter (opens the screen and waits: 0 sold, 1 refusal, 2 chat, 3 leaves, 4 dismissed; the level in 117 and the face in 248) and 260 the bonus at the end (0 nothing, 1 in the bag, 2 with the item keeper, 3 no room anywhere). The sale adds to the tally from the engine; the patch shows the original's profit line for that level. Changing the option in the middle of a job applies to the next one.
