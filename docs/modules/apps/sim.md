# sim

`dwe-sim`: plays scenarios of the game systems many times, without graphics, with seeds, and writes a report in
Markdown and CSV with the distribution of each metric. Usage, options and how to add a scenario:
[simulations](../../guides/simulations.md).

| File | What it has |
|---|---|
| `scenario.hpp` | `Scenario` (`setup`, `run(seed)`, `tables`), `ScenarioRegistry`, `builtInScenarios()` and `ScenarioRegistration` |
| `runner.hpp` | `runScenario`: the runs split across threads, in order and with their seed (`seedOfRun`, SplitMix64) |
| `report.hpp` | `summarize`, `percentileOf` and the report in Markdown and CSV |
| `sim_options.hpp` | The command line, the `--set` overrides (`parseValueOverride`, `applyOverrides`) |
| `simulation.hpp` | `simulate`: loads the ruleset, applies the `--set` overrides, prepares the scenario and plays it; `writeReport` |
| `headless_session.hpp` | `HeadlessSessions`: windowless games (`session::GameSession`) for the runs of a scenario, with the ruleset and the `--set` overrides of the context and the game systems (evolution, inventory and battle, with their tables read once in `prepare`); `prepare` in `setup`, `start(seed)` in `run`; `startGame(seed)` also gives the per-type adapters (`HeadlessGame`) for the item and combat commands, and `tables()` the tables read |
| `scenarios/*.cpp` | One scenario per file, which registers itself: `battle`, `care`, `clock`, `raisings` and `ruleset-diff` |

The module can use all the modules in `packages/game/` (CMake collects them with the `DWE_GAME_MODULES` property), so a
new scenario is a new file. The packs are mounted with `packages/content`, the same as in the game.

## Tests

`tests/sim_tests.cpp`: the registry, that the runs are the same with any number of threads, the report
(percentiles, histogram, Markdown and CSV), the command line, the clock at several speeds, that `--set` changes what the
scenario sees, that a windowless game is the same with the same seed, that a value the ruleset does not accept
stops the simulation, and the `care` scenario (its metrics, that the caretaker policy changes the raisings and the
parameters it rejects). `tests/loop_tests.cpp` is the test for the closing criterion of phase 3 (see below). They use a stand-in base pack that CMake writes in the build folder: empty tables,
except `digimon` and `raise`, with some made-up species (`tests/stand-in/`) for `care` and the forms named by the
`evolution` section, `moves`, with made-up techniques with the ids named by the `battle` section, `items`, with made-up items for the
game systems, and `evolution`, with no rows; and the `game` pack from the source tree. `sim.smoke` runs `dwe-sim --scenario clock --runs 10` and requires that no run deviates;
`sim.care.smoke`, four one-week raisings with `care`.

### The phase 3 loop

`tests/loop_tests.cpp` (`the user's disc, extracted: raising, training and battling the first partner evolves it as
the original`) plays the whole loop on a `HeadlessGame` with the `original` ruleset, the extracted disc
(`DWE_CONTENT`; it is skipped without it) and the clock at 60 (one game hour per second), in turns of one game minute:

- **New game**: the partner from `new_game` (agum, Rookie).
- **Target form**: the first one on its card (`data/evolution`, `paths`) whose points the caretaker can earn without
  weight (`evolution.c: calculateRequirementScore`): a single stat other than HP or MP, care mistakes at most, and
  discipline or bonus battles. With the user's disc it is the Champion that asks for brains 100, at most 3 mistakes and discipline 60.
- **Caretaker** (one thing per turn, like a player): cures the sickness or the wound with the medicine that surely cures
  (`item.c: handleMedicineHealing`), gives the favorite food (`raise`) through the inventory, takes it to the potty, puts it to bed,
  trains the course for that stat (`training`) without going past the tiredness of `overwork_tiredness`, scolds until the
  required discipline and fights two battles against the first NPC on the starting map that can fight, like the game's
  debug trigger.
- **Checks** each effect (energy, potty, sleep and clock, trained stat and tiredness, discipline and happiness, stats,
  HP, battles and lives after combat), that it does not die and that at the Rookie hours (`check_hours.rookie`), before the
  fallback, it evolves through the periodic check into the target form with at least `pass_points` points.

It plays six seeds (in the sixth the partner gets sick) and takes about 2 s in the `ci` build:

```sh
DWE_CONTENT=<content> scripts/dev.sh shell -c "build/ci/apps/sim/dwe_sim_tests -tc='*evolves it as the original*'"
```
