#pragma once

#include <dwe/sim/runner.hpp>
#include <dwe/sim/scenario.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace dwe::sim {

// The percentiles every metric reports.
inline constexpr std::array<double, 5> ReportedPercentiles{5.0, 25.0, 50.0, 75.0, 95.0};

// How many bars the histogram of a metric has.
inline constexpr std::size_t HistogramBins = 10;

// The distribution of a metric over the runs.
struct MetricSummary {
  std::string name;
  std::size_t count = 0;
  double mean = 0.0;
  double deviation = 0.0;
  double minimum = 0.0;
  double maximum = 0.0;
  std::array<double, ReportedPercentiles.size()> percentiles{};
  // Runs per bin, from the minimum to the maximum in equal steps.
  std::array<std::size_t, HistogramBins> histogram{};
};

// A percentile (0 to 100) of sorted values, interpolated between the two nearest ones.
double percentileOf(std::span<const double> sorted, double percent);

// The summary of each metric, in the order the first run gives them.
std::vector<MetricSummary> summarize(std::span<const RunRecord> records);

// What the report says about the simulation it describes. Nothing in it depends on the number of
// threads, so the report of a seed is the same on any machine.
struct ReportHeader {
  std::string scenario;
  std::string description;
  std::string ruleset;
  std::size_t runs = 0;
  std::uint64_t seed = 0;
  std::int64_t days = 0;
  double speed = 0.0;
  // The --set values, as given.
  std::vector<std::string> overrides;
  // The --param values, as given.
  std::vector<std::string> parameters;
};

// The report as Markdown: the header, a table of the metrics' distributions, their histograms and
// the scenario's tables.
std::string markdownReport(const ReportHeader& header, std::span<const MetricSummary> summaries,
                           std::span<const ReportTable> tables);

// Every run as CSV: run, seed and one column per metric.
std::string runsCsv(std::span<const RunRecord> records);

// The summaries as CSV: one line per metric.
std::string summaryCsv(std::span<const MetricSummary> summaries);

} // namespace dwe::sim
