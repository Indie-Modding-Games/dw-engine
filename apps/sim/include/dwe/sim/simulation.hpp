#pragma once

#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>
#include <dwe/sim/report.hpp>
#include <dwe/sim/scenario.hpp>
#include <dwe/sim/sim_options.hpp>

#include <string>
#include <vector>

namespace dwe::sim {

// A finished simulation: its report as Markdown and CSV.
struct SimulationReport {
    std::string markdown;
    std::string runsCsv;
    std::string summaryCsv;
};

// Runs the scenario the options name on the packs: loads the ruleset, applies the --set values, sets the scenario up
// and runs it.
core::Result<SimulationReport> simulate(const assets::VirtualFileSystem& files, const ScenarioRegistry& registry,
                                        const SimOptions& options);

// Writes the report's files into a folder: <scenario>.md, <scenario>-runs.csv and <scenario>-summary.csv.
core::Result<void> writeReport(const SimulationReport& report, const std::filesystem::path& folder,
                               std::string_view scenario);

// The scenarios of a registry, one per line with its description.
std::string describeScenarios(const ScenarioRegistry& registry);

} // namespace dwe::sim
