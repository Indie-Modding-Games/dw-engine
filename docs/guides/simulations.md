# Simulations with `dwe-sim`

`dwe-sim` plays scenarios of the game's systems many times, without a window or graphics, and gives a report
with the distribution of what each one measures: to compare the balance of two rulesets, or the effect of changing a
value, without playing by hand. The code is in [`apps/sim`](../modules/apps/sim.md).

## Usage

```sh
dwe-sim --list
dwe-sim --scenario clock --runs 10
dwe-sim --content <content> --ruleset original --scenario clock --runs 1000 --seed 7 --threads 8 \
        --days 30 --speed 3600 --set clock.frames_per_minute=10 --out reports/
dwe-sim --scenario ruleset-diff --param against=original --set care.hunger.window_minutes=45
```

| Option | What it does | Default |
|---|---|---|
| `--scenario <name>` | The scenario that is played (`--list` lists them) | Required |
| `--content <folder>`, `--mods <folder>` | The packs, mounted the same way as in the game (`packages/content`) | `content` |
| `--dev-assets` | Also mounts the development assets from the `dev-assets.yaml` in the current folder or in the user's data folder, the same as the game's, with the disc image through `dwx` ([development assets](../architecture/development-assets.md)) | Off |
| `--ruleset <id>` | The ruleset it is played with | `original` |
| `--runs <n>` | How many games | 100 |
| `--seed <n>` | The base seed: each game uses its own, derived from this one and its number | 1 |
| `--threads <n>` | Threads; the result is the same with any number | One per processor thread |
| `--days <n>` | Game days each game plays | 1 |
| `--speed <x>` | The clock's fast forward (`GameClock::setSpeed`) | 3600: 60 game hours per real second (1 is one game minute per second, 60 one hour) |
| `--set <section>.<path>=<value>` | Changes a ruleset value without touching its files; can be repeated. It passes the same checks as the tuning panel | |
| `--param <key>=<value>` | A scenario parameter; can be repeated | |
| `--out <folder>` | Writes `<scenario>.md`, `<scenario>-runs.csv` and `<scenario>-summary.csv` | Only Markdown on standard output |

The report states the ruleset, the games, the seed, the days, the speed and the `--set` values; for each metric it
gives the mean, the deviation, the minimum, the 5th, 25th, 50th, 75th and 95th percentiles and the maximum, and a
histogram. It does not include the number of threads: the report for a seed is the same on any machine, so two reports
are compared with a `diff`.

## Scenarios

| Scenario | What it measures |
|---|---|
| `battle` | Plays one battle per game between `--param partner=<species>` and `--param enemies=<species>[,<species>...]` (one to three) from the Digimon table, with no tamer orders, and measures `victory`, `defeat`, `fled` and `unfinished` (the mean is the proportion), `seconds` and `partner_hp_percent`. The stats are made up: `stats` (offense, defense, speed and brains; by default 100 per level), `hp` (by default triple that), `mp` (by default 9999: in the original a fighter with no MP waits forever), `discipline` (50) and `frame_limit` (ten minutes of battle) |
| `clock` | Plays the clock `--days` days at `--speed` in frames of uneven length, like a game loop, and checks that it crosses the same minutes, hours and days as advancing those frames all at once (`mismatch` is 0). It is what the rules that run every hour or every day need: fast forward gives the same as playing |
| `raisings` | Raises a partner from `--param from=<form>` (by default the first Fresh of the rebirth) for `--days` days with random care at each level (`--param max_stat=<n>`, 300 by default, is the most a level adds to each stat; HP and MP, times 10), through the [evolution](../modules/game/evolution.md) rules: `final_level`, `evolutions` and one metric per form it can reach, 1 for the one it ends up as. It is not a model of how a player raises nor does it test the sleeping or scolding specials: it shows which forms the rules and tables reach with even care, and how an adjusted value moves it. It needs the extracted tables (`--content`) |
| `ruleset-diff` | Lists each value in which the ruleset (with its `--set` values) differs from `--param against=<id>` (by default `original`) |
| `care` | Raises a partner for `--days` days with a scripted caretaker and gives the first partner's life (`lifespan_days`), the deaths, the care mistakes, the sicknesses, the weight and the final stats (`final_*`) |

### `care`

It raises a partner with the rules in `packages/game/partner` and the ruleset's clock. Every game minute, at any
`--speed`, the caretaker looks at the partner and does a single thing: feeds it if it is hungry (the original's Meat:
energy 12 and weight +1, otherwise it refuses it), takes it to the potty if it needs it, puts it to bed if it is
sleepy or, at its time of day, trains it. If it dies, it is reborn with the tamer level the events carry. This
scenario does not evolve the partner: it stays in its species its whole life, so a Rookie's life is its entire one (15
days); evolution is measured by `raisings`. The starting stats are placeholders (HP and MP 100, the rest 10).

| `--param` | What it changes | Default |
|---|---|---|
| `species` | The starting species (id from the `digimon` and `raise` tables) | `agum` |
| `feed`, `toilet`, `sleep` | Whether the caretaker feeds, takes to the potty and puts to bed (`true` or `false`) | `true` |
| `train_sessions` | Consecutive sessions each day; 0 does not train | 2 |
| `train_hour` | The hour of the day it trains at | 10 |
| `train_place`, `train_course` | The place and the course (`training.yaml: places`) | `trn`, `hp` |
| `tamer_level` | The tamer level at the start | 1 |

A parameter it does not know or a value it cannot use stops the simulation with the reason.

Example with the user's disc extracted, `dwe-sim --content <content> --scenario care --runs 100 --days 30
--seed 7 --param sleep=false --param toilet=false` (summarized): without putting it to bed or taking it to the potty,
each partner dies at 2 days from untreated sicknesses and mistakes, and 10 deaths follow one another in 30 days.

| metric | runs | mean | deviation | min | p5 | p25 | p50 | p75 | p95 | max |
|---|---|---|---|---|---|---|---|---|---|---|
| lifespan_days | 100 | 2 | 0 | 2 | 2 | 2 | 2 | 2 | 2 | 2 |
| deaths | 100 | 10 | 0 | 10 | 10 | 10 | 10 | 10 | 10 | 10 |
| care_mistakes | 100 | 255 | 0 | 255 | 255 | 255 | 255 | 255 | 255 | 255 |
| sicknesses | 100 | 11 | 0 | 11 | 11 | 11 | 11 | 11 | 11 | 11 |
| weight | 100 | 29.27 | 1.58022 | 26 | 27 | 28 | 29 | 30 | 32 | 33 |
| final_hp | 100 | 425.28 | 29.6183 | 388 | 388 | 410 | 420 | 472 | 472 | 472 |
| final_offense | 100 | 19.12 | 3.92245 | 12 | 12 | 18 | 20 | 24 | 24 | 24 |

With the full caretaker (`--days 14`, no `--param`), the 100 raisings end the same: 0 mistakes, 0 sicknesses, 0
deaths, weight 15 and HP 2340 (100 + 28 sessions of 80). The life does not vary because without evolution there is
nothing to change it.

## Adding a scenario

A scenario is **one file** in `apps/sim/scenarios/`: CMake picks them all up, and `dwe-sim` can use all the modules in
`packages/game/`, so nothing else needs to be touched.

```cpp
// apps/sim/scenarios/evolution_scenario.cpp
#include <dwe/game/evolution/...>
#include <dwe/ruleset/random.hpp>
#include <dwe/sim/scenario.hpp>

namespace dwe::sim {
namespace {

class RaisingScenario final : public Scenario {
public:
    // Once: reads the ruleset section, the tables and the parameters.
    core::Result<void> setup(const ScenarioContext& context) override { /* context.rules, context.files, ... */ }

    // Each game, on any thread: only reads what setup prepared. All the randomness comes from the seed.
    [[nodiscard]] core::Result<Metrics> run(std::uint64_t seed) const override {
        ruleset::Random random{seed};
        // ... a thousand days of raising with the clock at context.speed ...
        return Metrics{{.name = "species_a", .value = 1.0}, {.name = "species_b", .value = 0.0}};
    }

    // Optional: tables for the report.
    [[nodiscard]] std::vector<ReportTable> tables() const override { return {}; }
};

const ScenarioRegistration registration{{
    .name = "raisings",
    .description = "Raises a partner from its egg and counts what it becomes.",
    .create = [] { return std::make_unique<RaisingScenario>(); },
}};

} // namespace
} // namespace dwe::sim
```

Rules:

- **The same for a seed.** `run(seed)` depends only on the seed and on what `setup` prepared: no shared state that
  changes, and no real clocks. This way the report is the same with any number of threads.
- **The same metrics in the same order** in every game. For a histogram of results (which Digimon a raising ends up
  as, who wins a battle), one metric per result with 0 or 1: the mean is the proportion.
- **Time with `GameClock`** at `context.speed`, and `context.days` days, so that fast forward is what is tested and
  not a shortcut.
- **The ruleset's rules.** The scenario reads the sections of `context.rules` with its system's functions, so `--set`
  and `--ruleset` reach it on their own.
- A test in `apps/sim/tests/` that plays the scenario with few games using the tests' placeholder content.
