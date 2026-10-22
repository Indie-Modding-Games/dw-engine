#pragma once

#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::sim {

// A number a run measured: days advanced, a win rate, how many raisings ended in each Digimon.
struct Metric {
  std::string name;
  double value = 0.0;
};

using Metrics = std::vector<Metric>;

// A table a scenario adds to its report once, after setup (ruleset-diff lists the values that
// differ).
struct ReportTable {
  std::string title;
  std::vector<std::string> columns;
  std::vector<std::vector<std::string>> rows;
};

// The scenario parameters of the command line (--param key=value), by key.
using ScenarioParameters = std::map<std::string, std::string, std::less<>>;

// What a scenario sets up from: the packs, the ruleset with the --set values applied, and the
// command line's time and parameters.
struct ScenarioContext {
  const assets::VirtualFileSystem* files = nullptr;
  const ruleset::Ruleset* rules = nullptr;
  // Game days each run plays (--days).
  std::int64_t days = 1;
  // The game clock's fast-forward multiplier (--speed), as ruleset::GameClock::setSpeed takes it.
  double speed = 1.0;
  const ScenarioParameters* parameters = nullptr;
};

// A simulation: set up once, then run many times with different seeds, each run giving its metrics.
// Runs happen on several threads at once, so run() only reads what setup() prepared, and a run's
// result depends only on its seed.
class Scenario {
public:
  Scenario() = default;
  Scenario(const Scenario&) = delete;
  Scenario& operator=(const Scenario&) = delete;
  Scenario(Scenario&&) = delete;
  Scenario& operator=(Scenario&&) = delete;
  virtual ~Scenario() = default;

  // Reads what the runs need: the scenario's ruleset sections, its tables, its parameters.
  virtual core::Result<void> setup(const ScenarioContext& context) = 0;

  // Plays one run with its seed and gives what it measured, the same metrics in the same order
  // every run.
  [[nodiscard]] virtual core::Result<Metrics> run(std::uint64_t seed) const = 0;

  // The tables the scenario adds to the report, after setup. None by default.
  [[nodiscard]] virtual std::vector<ReportTable> tables() const { return {}; }
};

// A scenario as the registry lists it.
struct ScenarioInfo {
  std::string name;
  std::string description;
  std::function<std::unique_ptr<Scenario>()> create;
};

// The scenarios by name.
class ScenarioRegistry {
public:
  // Adds a scenario; a name that is already there is an error.
  core::Result<void> add(ScenarioInfo info);

  // The scenario with that name, or nullptr.
  [[nodiscard]] const ScenarioInfo* find(std::string_view name) const;

  // Every scenario, sorted by name.
  [[nodiscard]] const std::map<std::string, ScenarioInfo, std::less<>>& scenarios() const {
    return scenarios_;
  }

private:
  std::map<std::string, ScenarioInfo, std::less<>> scenarios_;
};

// The scenarios that come with dwe-sim: each file of apps/sim/scenarios adds its own with a
// ScenarioRegistration.
ScenarioRegistry& builtInScenarios();

// Adds a scenario to builtInScenarios() when the program starts. A scenario file declares one at
// namespace scope: `const ScenarioRegistration registration{{.name = "clock", .description = "...",
// .create = ...}};`.
class ScenarioRegistration {
public:
  explicit ScenarioRegistration(ScenarioInfo info);
};

} // namespace dwe::sim
