# ruleset

The ruleset format (`dwe::ruleset`, services layer): it loads `rules/<id>/` from the packs, follows `extends`,
checks the sections, creates strategies by name, reloads and adjusts on the fly, and carries the game clock. It does not know
what each section means: each game system reads its own into its own struct. The format and how a
system uses it are in [rulesets](../../architecture/rulesets.md).

## Usage

```cpp
auto store = dwe::ruleset::RulesetStore::load(vfs, "original", checks);   // checks: the systems' SectionChecks
const dwe::ruleset::Ruleset& rules = store->current();
const dwe::ruleset::Section* care = rules.section("care");                // YAML, file and ruleset of origin
const dwe::assets::AssetId* raise = rules.table("raise");                 // data/raise
dwe::ruleset::GameClock clock{*dwe::ruleset::readClockRules(rules)};
clock.setSpeed(60.0);                                                      // one game hour per real second
dwe::ruleset::ClockAdvance step = clock.advance(1.0 / 60.0);               // step.hours: the rules of each hour
store->setValue("care", "hunger.window_minutes", "45");                   // tuning panel; bumps revision()
store->reload();                                                           // after editing the YAML
```

| Header | What it has |
|---|---|
| `ruleset.hpp` | `Ruleset`, `Section`, `SectionCheck(s)`, `loadRuleset`, `checkRuleset`, `findValue`, `Quirks` and `readQuirks` |
| `ruleset_store.hpp` | `RulesetStore`: the active ruleset, `revision()`, `reload()`, `select(id)` and `setValue` |
| `strategy.hpp` | `StrategyRegistry<Interface>`: implementations by name; an unknown name is an error on its line |
| `game_clock.hpp` | `ClockRules` (`clock` section), `readClockRules`, `GameTime`, `ClockAdvance` and `GameClock` |
| `ruleset_values.hpp` | `listValues`, `diffValues` (the values that differ between two rulesets), `listRulesets` and `inlineYaml` |
| `section_file.hpp` | `patchSectionText` and `saveSection`: write an adjusted section to its file, value by value, without losing the comments |
| `species_extra.hpp` | `SpeciesExtra` ([per-species patches](../../architecture/rulesets.md#per-species-patches)): `readSpeciesExtra`, `applySpeciesExtra` and the `loadPatched…Table` functions, which give the disc's species, raising and evolution tables with the ruleset's patches |
| `messages.hpp` | The templates of the module's error messages |
| `random.hpp` | `Random`: seeded PCG32, `below(n)` like the original's `randomLimit` and `chance(p)`; `Random::original(state)` draws with the original's generator (`rand()`), only for the oracle |

## Tests

`ruleset_tests.cpp` uses two in-memory packs: loading, `extends`, errors with file and line (unknown keys,
out-of-range values, broken manifest, unknown strategy), `setValue` with its revision, a reload that keeps the previous
ruleset if it fails, ruleset change and quirks. `game_clock_tests.cpp` tests the clock against `clock.c`
(minutes, days, years, fast advance, pause, jump on sleeping) and that `Random` repeats with its seed.
`ruleset_values_tests.cpp` tests the listing and comparison of values and the writing to the file (citations and format
intact, change of shape rejected, pack on disk and in memory). `original_ruleset_tests.cpp` loads the `original` ruleset from the `game` pack and, if `DWE_CONTENT` points to the extracted
content, checks that each table it names is in the base pack and meets its schema.
