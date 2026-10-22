#include <dwe/sim/messages.hpp>
#include <dwe/sim/runner.hpp>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <utility>
#include <vector>

namespace dwe::sim {

namespace {

// SplitMix64 (Steele, Lea and Flood, 2014): a different stream of bits for each index of the same
// base seed.
constexpr std::uint64_t GoldenGamma = 0x9E3779B97F4A7C15ULL;
constexpr std::uint64_t FirstMultiplier = 0xBF58476D1CE4E5B9ULL;
constexpr std::uint64_t SecondMultiplier = 0x94D049BB133111EBULL;
constexpr unsigned FirstShift = 30;
constexpr unsigned SecondShift = 27;
constexpr unsigned ThirdShift = 31;

// Takes indices in turn and runs each, so the threads share the work whatever each run costs.
void runIndices(const Scenario& scenario, const RunSettings& settings,
                std::atomic<std::size_t>& next, std::vector<core::Result<Metrics>>& results) {
  for (std::size_t index = next++; index < settings.runs; index = next++) {
    results[index] = scenario.run(seedOfRun(settings.seed, index));
  }
}

} // namespace

std::uint64_t seedOfRun(std::uint64_t baseSeed, std::size_t index) {
  std::uint64_t mixed = baseSeed + (GoldenGamma * (static_cast<std::uint64_t>(index) + 1));
  mixed = (mixed ^ (mixed >> FirstShift)) * FirstMultiplier;
  mixed = (mixed ^ (mixed >> SecondShift)) * SecondMultiplier;
  return mixed ^ (mixed >> ThirdShift);
}

core::Result<std::vector<RunRecord>> runScenario(const Scenario& scenario,
                                                 const RunSettings& settings) {
  // Every run writes its own slot: the threads never share one.
  std::vector<core::Result<Metrics>> results(settings.runs);
  std::atomic<std::size_t> next{0};
  const std::size_t threads =
      std::clamp<std::size_t>(settings.threads, 1, std::max<std::size_t>(settings.runs, 1));
  {
    std::vector<std::jthread> workers;
    for (std::size_t worker = 1; worker < threads; ++worker) {
      workers.emplace_back([&] { runIndices(scenario, settings, next, results); });
    }
    runIndices(scenario, settings, next, results);
  }
  std::vector<RunRecord> records;
  records.reserve(settings.runs);
  for (std::size_t index = 0; index < settings.runs; ++index) {
    const std::uint64_t seed = seedOfRun(settings.seed, index);
    core::Result<Metrics>& result = results[index];
    if (!result) {
      return core::fail(
          messages::formatMessage(messages::RunFailed, index, seed, result.error().message));
    }
    records.push_back(RunRecord{.index = index, .seed = seed, .metrics = std::move(*result)});
  }
  return records;
}

} // namespace dwe::sim
