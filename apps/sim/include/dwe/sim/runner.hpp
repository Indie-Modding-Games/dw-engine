#pragma once

#include <dwe/core/error.hpp>
#include <dwe/sim/scenario.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace dwe::sim {

// How many runs, from which seed, on how many threads.
struct RunSettings {
    std::size_t runs = 100;
    std::uint64_t seed = 1;
    std::size_t threads = 1;
};

// One run and what it measured.
struct RunRecord {
    std::size_t index = 0;
    std::uint64_t seed = 0;
    Metrics metrics;
};

// The seed of a run: the base seed and the run's index mixed (SplitMix64), so neighbouring runs do not play alike and
// a run plays the same whatever thread runs it.
std::uint64_t seedOfRun(std::uint64_t baseSeed, std::size_t index);

// Runs a scenario that is set up, on the threads asked for, and gives the runs in order: the same for any number of
// threads. The first failed run, by index, is the error.
core::Result<std::vector<RunRecord>> runScenario(const Scenario& scenario, const RunSettings& settings);

} // namespace dwe::sim
