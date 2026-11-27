# shop_job

The canyon shop job, improved (`dwe::game::shop_job`, `game` layer): the line of 20 customers, their hidden budgets, what the player knows about each one, the haggling at the counter and the pay at the end. It is an engine improvement with an option (`shop_job_mode`: `improved` by default, `original` plays the disc's script as it is). It draws nothing: it depends on `core`, `assets` and `ruleset`. The numbers come from the `shop_job` section of the ruleset (`rules/<id>/shop_job.yaml`; the others inherit the `original` one). The rules of the game are in the [job reference](../../reference/shop-job.md).

## Usage

```cpp
auto rules = dwe::game::shop_job::parseShopJobRules(*ruleset.section("shop_job"));
dwe::ruleset::Random random{dwe::game::shop_job::lineupSeed(session.seed(), job)};
auto line = dwe::game::shop_job::makeLineup(*rules, random);   // 20 customers, perfect in [4000, 4300]
dwe::game::shop_job::Haggle haggle{*rules, line.customers[0]};
haggle.chance(5);                    // % chance to buy at the regular price, from what the player knows
auto result = haggle.offer(3);       // reaction, sale, what it adds, whether it leaves
```

- **Line** (`makeLineup`): each item's copies spread over the species, arrival mood by weight and a budget drawn with the original's acceptance table; budgets then move to the nearest level the customer could have had (`shiftWeights`) until the perfect score (`perfectProfit`: one chat and the sale at the budget) falls in the window, and a line that still misses is drawn again. The seed (`lineupSeed`) mixes the playthrough's seed with the job number: it spends no game random numbers and a load gives the same line.
- **Haggling** (`Haggle`): the probability of each price comes from the weights of the budgets the player still knows (species, mood and refusals); each offer gets a reaction by its distance from the budget and costs patience if the customer does not buy; chatting raises the budget one level, once; dismissing ends without anger.
- **Pay** (`payTierOf`): the original's three tiers by the tally, and the bonus on top.
- **Co-op**: every player runs its own job. The customers served, the tally, each turn's result and the halfway line are each player's own story state (`player_own_variables` and `player_own_triggers` in the story rules), so a player opening the shop never starts or moves the other's meter (#423, [multiplayer](../../architecture/multiplayer.md)). Monochromon joining the city stays shared.

## Parity with the original

| What | Original | Engine | Tests | Status |
|---|---|---|---|---|
| Customer line | Each customer is a roll (`store_random(110, 8)` in script 49): random species and item, the tally's ceiling goes from 800 to 16000 | `makeLineup`: 7/6/4/3 species and 4/10/6 items, mood and budget per customer, perfect in [4000, 4300] | `shop_job_tests.cpp` ("every job's line lets a perfect player make 4000 to 4300", "a job's line is the same for the same playthrough and job…", "every customer's budget is one its species and mood can give") | Improvement (option) |
| Budget and reaction | Raising the price rolls the markup at random and the face only depends on the level | Hidden budget with each species' acceptance table and the mood applied; reaction by distance (Delighted, Pleased, Doubtful, Annoyed, Furious) | "the reaction says how far the offer was from the budget", "a refusal rules out the budgets…", "talking raises the budget once…", "a customer out of patience leaves…" | Improvement (option) |
| Pay | Routine 56 and script 49 (0x52): 500, 1500 or 2000 bits and trigger 247 | `payTierOf` with the bonus (`bonus.from`, 3584) | "the job pays by the tally's tier" | Improvement (option) |
