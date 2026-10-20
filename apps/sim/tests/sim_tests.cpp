#include <dwe/content/game_content.hpp>
#include <dwe/ruleset/random.hpp>
#include <dwe/sim/report.hpp>
#include <dwe/sim/runner.hpp>
#include <dwe/sim/scenario.hpp>
#include <dwe/sim/sim_options.hpp>
#include <dwe/sim/simulation.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::sim {

namespace {

template <typename T>
std::string errorOf(const core::Result<T>& result) {
    return result ? std::string{} : result.error().message;
}

// A scenario of the tests: rolls dice with the run's seed, so a run that took another run's seed would show.
class DiceScenario final : public Scenario {
public:
    core::Result<void> setup(const ScenarioContext& /*context*/) override { return {}; }

    [[nodiscard]] core::Result<Metrics> run(std::uint64_t seed) const override {
        ruleset::Random random{seed};
        double total = 0.0;
        for (int roll = 0; roll < 100; ++roll) {
            total += static_cast<double>(random.below(6) + 1);
        }
        return Metrics{{.name = "total", .value = total},
                       {.name = "first", .value = static_cast<double>(random.below(100))}};
    }
};

// The packs dwe-sim mounts in the tests: a stand-in base pack and the team's packs from the source tree.
const assets::VirtualFileSystem& standInFiles() {
    static const auto mounted = content::GameContent::mount(DWE_STAND_IN_CONTENT, {});
    REQUIRE_MESSAGE(mounted.has_value(), errorOf(mounted));
    return (*mounted)->files();
}

SimOptions optionsFor(std::string_view scenario) {
    SimOptions options;
    options.scenario = scenario;
    options.runs = 8;
    options.threads = 2;
    return options;
}

// The value of a metric's column in the report's metrics table: "| <metric> | <runs> | <mean> | ...".
bool reportHasRow(const std::string& report, std::string_view row) {
    return report.contains(row);
}

} // namespace

TEST_CASE("the registry finds scenarios by name and refuses a name twice") {
    ScenarioRegistry registry;
    const auto dice = [] { return std::make_unique<DiceScenario>(); };
    REQUIRE(registry.add({.name = "dice", .description = "Rolls dice.", .create = dice}).has_value());
    CHECK(registry.find("dice") != nullptr);
    CHECK(registry.find("cards") == nullptr);
    const auto twice = registry.add({.name = "dice", .description = "Again.", .create = dice});
    REQUIRE_FALSE(twice.has_value());
    CHECK(errorOf(twice).contains("registered twice"));
    CHECK(describeScenarios(registry) == "dice: Rolls dice.\n");
}

TEST_CASE("the scenarios of dwe-sim register themselves") {
    const ScenarioRegistry& registry = builtInScenarios();
    CHECK(registry.find("clock") != nullptr);
    CHECK(registry.find("ruleset-diff") != nullptr);
}

TEST_CASE("the runs are the same on any number of threads, each with its own seed") {
    DiceScenario dice;
    const auto single = runScenario(dice, {.runs = 50, .seed = 7, .threads = 1});
    REQUIRE_MESSAGE(single.has_value(), errorOf(single));
    for (const std::size_t threads : {2U, 3U, 8U, 64U}) {
        const auto parallel = runScenario(dice, {.runs = 50, .seed = 7, .threads = threads});
        REQUIRE_MESSAGE(parallel.has_value(), errorOf(parallel));
        REQUIRE(parallel->size() == single->size());
        for (std::size_t index = 0; index < single->size(); ++index) {
            CHECK((*parallel)[index].seed == (*single)[index].seed);
            CHECK((*parallel)[index].metrics[0].value == (*single)[index].metrics[0].value);
            CHECK((*parallel)[index].metrics[1].value == (*single)[index].metrics[1].value);
        }
    }
    CHECK(seedOfRun(7, 0) != seedOfRun(7, 1));
    CHECK(seedOfRun(7, 0) != seedOfRun(8, 0));
    const auto other = runScenario(dice, {.runs = 50, .seed = 8, .threads = 1});
    REQUIRE(other.has_value());
    CHECK(runsCsv(*other) != runsCsv(*single));
}

TEST_CASE("the report gives the distribution of each metric") {
    const std::array<double, 5> sorted{1.0, 2.0, 3.0, 4.0, 5.0};
    CHECK(percentileOf(sorted, 0.0) == 1.0);
    CHECK(percentileOf(sorted, 50.0) == 3.0);
    CHECK(percentileOf(sorted, 100.0) == 5.0);
    CHECK(percentileOf(sorted, 25.0) == 2.0);
    CHECK(percentileOf(sorted, 10.0) == doctest::Approx(1.4));

    std::vector<RunRecord> records;
    records.reserve(sorted.size());
    for (std::size_t index = 0; index < 5; ++index) {
        records.push_back({.index = index,
                           .seed = index,
                           .metrics = {{.name = "wins", .value = sorted[index]}, {.name = "flat", .value = 2.0}}});
    }
    const std::vector<MetricSummary> summaries = summarize(records);
    REQUIRE(summaries.size() == 2);
    CHECK(summaries[0].name == "wins");
    CHECK(summaries[0].count == 5);
    CHECK(summaries[0].mean == 3.0);
    CHECK(summaries[0].deviation == doctest::Approx(1.41421356));
    CHECK(summaries[0].minimum == 1.0);
    CHECK(summaries[0].maximum == 5.0);
    CHECK(summaries[0].percentiles[2] == 3.0);
    CHECK(summaries[0].histogram[0] == 1);
    CHECK(summaries[0].histogram[HistogramBins - 1] == 1);
    CHECK(summaries[1].histogram[0] == 5);

    const ReportHeader header{.scenario = "dice",
                              .description = "Rolls dice.",
                              .ruleset = "original",
                              .runs = 5,
                              .seed = 3,
                              .days = 2,
                              .speed = 60.0,
                              .overrides = {"care.hunger.window_minutes=45"},
                              .parameters = {}};
    const std::vector<ReportTable> tables{
        {.title = "Extra", .columns = {"a", "b"}, .rows = {{"1", "2"}}},
    };
    const std::string markdown = markdownReport(header, summaries, tables);
    CHECK(markdown.starts_with("# Simulation: dice\n\nRolls dice.\n"));
    CHECK(markdown.contains("| speed | x60 |"));
    CHECK(markdown.contains("| --set | care.hunger.window_minutes=45 |"));
    CHECK(markdown.contains("| --param | none |"));
    CHECK(markdown.contains("| metric | runs | mean | deviation | min | p5 | p25 | p50 | p75 | p95 | max |"));
    CHECK(markdown.contains("| wins | 5 | 3 | 1.41421 | 1 | 1.2 | 2 | 3 | 4 | 4.8 | 5 |"));
    CHECK(markdown.contains("### wins"));
    CHECK(markdown.contains("## Extra\n\n| a | b |\n|---|---|\n| 1 | 2 |\n"));
    CHECK(runsCsv(records).starts_with("run,seed,wins,flat\n0,0,1,2\n1,1,2,2\n"));
    CHECK(summaryCsv(summaries) == "metric,runs,mean,deviation,min,p5,p25,p50,p75,p95,max\n"
                                   "wins,5,3,1.41421,1,1.2,2,3,4,4.8,5\n"
                                   "flat,5,2,0,2,2,2,2,2,2,2\n");
}

TEST_CASE("the command line names the scenario, the runs, the seeds and the values to change") {
    const std::array<std::string_view, 18> words{
        "--scenario", "clock",     "--runs",    "10",      "--seed", "42",    "--threads",
        "3",          "--days",    "5",         "--speed", "60",     "--set", "care.hunger.window_minutes=45",
        "--param",    "against=x", "--ruleset", "original"};
    const auto options = parseSimOptions(words);
    REQUIRE_MESSAGE(options.has_value(), errorOf(options));
    CHECK(options->scenario == "clock");
    CHECK(options->runs == 10);
    CHECK(options->seed == 42);
    CHECK(options->threads == 3);
    CHECK(options->days == 5);
    CHECK(options->speed == 60.0);
    REQUIRE(options->overrides.size() == 1);
    CHECK(options->overrides[0].section == "care");
    CHECK(options->overrides[0].path == "hunger.window_minutes");
    CHECK(options->overrides[0].yaml == "45");
    CHECK(options->parameters.at("against") == "x");

    CHECK_FALSE(parseSimOptions(std::array<std::string_view, 2>{"--runs", "0"}).has_value());
    CHECK_FALSE(parseSimOptions(std::array<std::string_view, 2>{"--speed", "-1"}).has_value());
    CHECK_FALSE(parseSimOptions(std::array<std::string_view, 2>{"--bogus", "1"}).has_value());
    CHECK_FALSE(parseSimOptions(std::array<std::string_view, 1>{"--scenario"}).has_value());
    CHECK_FALSE(parseSimOptions(std::array<std::string_view, 0>{}).has_value());
    CHECK(parseSimOptions(std::array<std::string_view, 1>{"--list"}).has_value());
    CHECK_FALSE(parseValueOverride("care=45").has_value());
    CHECK_FALSE(parseValueOverride("care.hunger").has_value());
    CHECK_FALSE(parseValueOverride(".x=1").has_value());
}

TEST_CASE("the clock scenario crosses the same days at any speed") {
    for (const double speed : {1.0, 10.0, 60.0, 3600.0, 0.0}) {
        SimOptions options = optionsFor("clock");
        options.days = 3;
        options.speed = speed;
        const auto report = simulate(standInFiles(), builtInScenarios(), options);
        REQUIRE_MESSAGE(report.has_value(), errorOf(report));
        CHECK_MESSAGE(reportHasRow(report->markdown, "| days_advanced | 8 | 3 | 0 | 3 |"), speed);
        CHECK_MESSAGE(reportHasRow(report->markdown, "| hours_advanced | 8 | 72 | 0 | 72 |"), speed);
        CHECK_MESSAGE(reportHasRow(report->markdown, "| mismatch | 8 | 0 | 0 | 0 |"), speed);
    }
}

TEST_CASE("a simulation's report is the same for a seed on any number of threads") {
    SimOptions options = optionsFor("clock");
    options.speed = 10.0;
    options.threads = 1;
    const auto single = simulate(standInFiles(), builtInScenarios(), options);
    options.threads = 5;
    const auto parallel = simulate(standInFiles(), builtInScenarios(), options);
    REQUIRE_MESSAGE(single.has_value(), errorOf(single));
    REQUIRE_MESSAGE(parallel.has_value(), errorOf(parallel));
    CHECK(single->markdown == parallel->markdown);
    CHECK(single->runsCsv == parallel->runsCsv);
}

TEST_CASE("--set changes the ruleset the scenarios see, without editing its files") {
    SimOptions options = optionsFor("ruleset-diff");
    options.overrides = {*parseValueOverride("clock.frames_per_minute=10"),
                         *parseValueOverride("care.hunger.window_minutes=45")};
    const auto report = simulate(standInFiles(), builtInScenarios(), options);
    REQUIRE_MESSAGE(report.has_value(), errorOf(report));
    CHECK(report->markdown.contains("| differences | 8 | 2 |"));
    CHECK(report->markdown.contains("| section | path | original | original (simulated) |"));
    CHECK(report->markdown.contains("| care | hunger.window_minutes | 90 | 45 |"));
    CHECK(report->markdown.contains("| clock | frames_per_minute | 20 | 10 |"));

    // With a minute of 10 frames, a day of the clock scenario is half as many frames: still one day.
    SimOptions clock = optionsFor("clock");
    clock.overrides = {*parseValueOverride("clock.frames_per_minute=10")};
    const auto faster = simulate(standInFiles(), builtInScenarios(), clock);
    REQUIRE_MESSAGE(faster.has_value(), errorOf(faster));
    CHECK(faster->markdown.contains("| days_advanced | 8 | 1 | 0 | 1 |"));

    // A value the clock check refuses stops the simulation with its reason.
    clock.overrides = {*parseValueOverride("clock.frames_per_minute=0")};
    const auto refused = simulate(standInFiles(), builtInScenarios(), clock);
    REQUIRE_FALSE(refused.has_value());
    CHECK(errorOf(refused).contains("frames_per_minute"));
}

TEST_CASE("an unknown scenario names the known ones") {
    const auto report = simulate(standInFiles(), builtInScenarios(), optionsFor("battles"));
    REQUIRE_FALSE(report.has_value());
    CHECK(errorOf(report).contains("unknown scenario 'battles'; known: clock, ruleset-diff"));
}

} // namespace dwe::sim
