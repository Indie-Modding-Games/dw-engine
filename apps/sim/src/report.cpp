#include <dwe/sim/report.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <iterator>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::sim {

namespace {

constexpr double HundredPercent = 100.0;
// The longest bar of a histogram, in characters.
constexpr std::size_t HistogramWidth = 40;
constexpr char HistogramBar = '#';
constexpr char BarSeparator = ' ';

// The texts of the report.
constexpr std::string_view TitleFormat = "# Simulation: {}\n\n{}\n\n";
constexpr std::string_view SettingsHeader = "| setting | value |\n|---|---|\n";
constexpr std::string_view SettingFormat = "| {} | {} |\n";
constexpr std::string_view MetricsTitle = "\n## Metrics\n\n";
constexpr std::string_view MetricsHeader = "| metric | runs | mean | deviation | min |";
constexpr std::string_view PercentileHeaderFormat = " p{:g} |";
constexpr std::string_view MaxHeader = " max |\n";
constexpr std::string_view HistogramsTitle = "\n## Histograms\n";
constexpr std::string_view HistogramTitleFormat = "\n### {}\n\n```\n";
constexpr std::string_view HistogramLineFormat = "{:>12} {:>6}{}\n";
constexpr std::string_view CodeFenceEnd = "```\n";
constexpr std::string_view TableTitleFormat = "\n## {}\n\n";
constexpr std::string_view NumberFormat = "{:.6g}";
constexpr std::string_view SpeedFormat = "x{:g}";
constexpr std::string_view NoneText = "none";
constexpr std::string_view ListSeparator = ", ";
constexpr std::string_view RunsCsvHeader = "run,seed";
constexpr std::string_view SummaryCsvHeader = "metric,runs,mean,deviation,min";
constexpr std::string_view PercentileCsvFormat = ",p{:g}";
constexpr std::string_view MaxCsvHeader = ",max\n";
constexpr char CsvSeparator = ',';
constexpr char LineEnd = '\n';
constexpr std::string_view CellSeparator = " | ";
constexpr std::string_view RowStart = "| ";
constexpr std::string_view RowEnd = " |\n";
constexpr std::string_view HeaderCell = "---|";
// The columns of the metrics table besides the percentiles: metric, runs, mean, deviation, min and
// max.
constexpr std::size_t FixedMetricColumns = 6;

// Setting names.
constexpr std::string_view RulesetSetting = "ruleset";
constexpr std::string_view RunsSetting = "runs";
constexpr std::string_view SeedSetting = "seed";
constexpr std::string_view DaysSetting = "days";
constexpr std::string_view SpeedSetting = "speed";
constexpr std::string_view OverridesSetting = "--set";
constexpr std::string_view ParametersSetting = "--param";

std::string numberText(double value) {
  return std::vformat(NumberFormat, std::make_format_args(value));
}

std::string joined(const std::vector<std::string>& texts) {
  if (texts.empty()) {
    return std::string{NoneText};
  }
  std::string line;
  for (const std::string& text : texts) {
    if (!line.empty()) {
      line += ListSeparator;
    }
    line += text;
  }
  return line;
}

void appendSetting(std::string& text, std::string_view name, const std::string& value) {
  text += std::vformat(SettingFormat, std::make_format_args(name, value));
}

void appendSettings(std::string& text, const ReportHeader& header) {
  text += SettingsHeader;
  appendSetting(text, RulesetSetting, header.ruleset);
  appendSetting(text, RunsSetting, std::to_string(header.runs));
  appendSetting(text, SeedSetting, std::to_string(header.seed));
  appendSetting(text, DaysSetting, std::to_string(header.days));
  appendSetting(text, SpeedSetting, std::vformat(SpeedFormat, std::make_format_args(header.speed)));
  appendSetting(text, OverridesSetting, joined(header.overrides));
  appendSetting(text, ParametersSetting, joined(header.parameters));
}

void appendRow(std::string& text, const std::vector<std::string>& cells) {
  text += RowStart;
  for (std::size_t index = 0; index < cells.size(); ++index) {
    if (index > 0) {
      text += CellSeparator;
    }
    text += cells[index];
  }
  text += RowEnd;
}

void appendMetricsTable(std::string& text, std::span<const MetricSummary> summaries) {
  text += MetricsTitle;
  text += MetricsHeader;
  for (const double percent : ReportedPercentiles) {
    text += std::vformat(PercentileHeaderFormat, std::make_format_args(percent));
  }
  text += MaxHeader;
  text += '|';
  const std::size_t columns = FixedMetricColumns + ReportedPercentiles.size();
  for (std::size_t column = 0; column < columns; ++column) {
    text += HeaderCell;
  }
  text += LineEnd;
  for (const MetricSummary& summary : summaries) {
    std::vector<std::string> cells{summary.name, std::to_string(summary.count),
                                   numberText(summary.mean), numberText(summary.deviation),
                                   numberText(summary.minimum)};
    for (const double percentile : summary.percentiles) {
      cells.push_back(numberText(percentile));
    }
    cells.push_back(numberText(summary.maximum));
    appendRow(text, cells);
  }
}

void appendHistogramLine(std::string& text, double from, std::size_t runs, std::size_t tallest) {
  const std::string start = numberText(from);
  const std::size_t width = tallest == 0 ? 0 : runs * HistogramWidth / tallest;
  std::string bar;
  if (width > 0) {
    bar = BarSeparator + std::string(width, HistogramBar);
  }
  text += std::vformat(HistogramLineFormat, std::make_format_args(start, runs, bar));
}

// A metric with a single value gets one bar; any other gets one per bin.
void appendHistogram(std::string& text, const MetricSummary& summary) {
  text += std::vformat(HistogramTitleFormat, std::make_format_args(summary.name));
  const std::size_t tallest = *std::ranges::max_element(summary.histogram);
  const double step = (summary.maximum - summary.minimum) / static_cast<double>(HistogramBins);
  const std::size_t bins = step > 0.0 ? HistogramBins : 1;
  for (std::size_t bin = 0; bin < bins; ++bin) {
    appendHistogramLine(text, summary.minimum + (step * static_cast<double>(bin)),
                        summary.histogram[bin], tallest);
  }
  text += CodeFenceEnd;
}

void appendTable(std::string& text, const ReportTable& table) {
  text += std::vformat(TableTitleFormat, std::make_format_args(table.title));
  appendRow(text, table.columns);
  text += '|';
  for (std::size_t column = 0; column < table.columns.size(); ++column) {
    text += HeaderCell;
  }
  text += LineEnd;
  for (const std::vector<std::string>& row : table.rows) {
    appendRow(text, row);
  }
}

std::array<std::size_t, HistogramBins> histogramOf(std::span<const double> values, double minimum,
                                                   double maximum) {
  std::array<std::size_t, HistogramBins> bins{};
  const double range = maximum - minimum;
  for (const double value : values) {
    std::size_t bin = 0;
    if (range > 0.0) {
      bin =
          static_cast<std::size_t>((value - minimum) / range * static_cast<double>(HistogramBins));
    }
    ++bins[std::min(bin, HistogramBins - 1)];
  }
  return bins;
}

MetricSummary summaryOf(std::string name, std::vector<double> values) {
  std::ranges::sort(values);
  MetricSummary summary;
  summary.name = std::move(name);
  summary.count = values.size();
  if (values.empty()) {
    return summary;
  }
  double sum = 0.0;
  for (const double value : values) {
    sum += value;
  }
  summary.mean = sum / static_cast<double>(values.size());
  double squares = 0.0;
  for (const double value : values) {
    squares += (value - summary.mean) * (value - summary.mean);
  }
  summary.deviation = std::sqrt(squares / static_cast<double>(values.size()));
  summary.minimum = values.front();
  summary.maximum = values.back();
  for (std::size_t index = 0; index < ReportedPercentiles.size(); ++index) {
    summary.percentiles[index] = percentileOf(values, ReportedPercentiles[index]);
  }
  summary.histogram = histogramOf(values, summary.minimum, summary.maximum);
  return summary;
}

} // namespace

double percentileOf(std::span<const double> sorted, double percent) {
  if (sorted.empty()) {
    return 0.0;
  }
  const double position = std::clamp(percent, 0.0, HundredPercent) / HundredPercent *
                          static_cast<double>(sorted.size() - 1);
  const auto below = static_cast<std::size_t>(std::floor(position));
  const std::size_t above = std::min(below + 1, sorted.size() - 1);
  const double fraction = position - static_cast<double>(below);
  return sorted[below] + ((sorted[above] - sorted[below]) * fraction);
}

std::vector<MetricSummary> summarize(std::span<const RunRecord> records) {
  std::vector<std::string> names;
  std::map<std::string, std::vector<double>, std::less<>> values;
  for (const RunRecord& record : records) {
    for (const Metric& metric : record.metrics) {
      auto [found, added] = values.try_emplace(metric.name);
      if (added) {
        names.push_back(metric.name);
      }
      found->second.push_back(metric.value);
    }
  }
  std::vector<MetricSummary> summaries;
  summaries.reserve(names.size());
  for (const std::string& name : names) {
    summaries.push_back(summaryOf(name, std::move(values[name])));
  }
  return summaries;
}

std::string markdownReport(const ReportHeader& header, std::span<const MetricSummary> summaries,
                           std::span<const ReportTable> tables) {
  std::string text =
      std::vformat(TitleFormat, std::make_format_args(header.scenario, header.description));
  appendSettings(text, header);
  appendMetricsTable(text, summaries);
  text += HistogramsTitle;
  for (const MetricSummary& summary : summaries) {
    appendHistogram(text, summary);
  }
  for (const ReportTable& table : tables) {
    appendTable(text, table);
  }
  return text;
}

std::string runsCsv(std::span<const RunRecord> records) {
  std::string text{RunsCsvHeader};
  if (!records.empty()) {
    for (const Metric& metric : records.front().metrics) {
      text += CsvSeparator;
      text += metric.name;
    }
  }
  text += LineEnd;
  for (const RunRecord& record : records) {
    text += std::to_string(record.index);
    text += CsvSeparator;
    text += std::to_string(record.seed);
    for (const Metric& metric : record.metrics) {
      text += CsvSeparator;
      text += numberText(metric.value);
    }
    text += LineEnd;
  }
  return text;
}

std::string summaryCsv(std::span<const MetricSummary> summaries) {
  std::string text{SummaryCsvHeader};
  for (const double percent : ReportedPercentiles) {
    text += std::vformat(PercentileCsvFormat, std::make_format_args(percent));
  }
  text += MaxCsvHeader;
  for (const MetricSummary& summary : summaries) {
    text += summary.name;
    text += CsvSeparator;
    text += std::to_string(summary.count);
    for (const double value : {summary.mean, summary.deviation, summary.minimum}) {
      text += CsvSeparator;
      text += numberText(value);
    }
    for (const double percentile : summary.percentiles) {
      text += CsvSeparator;
      text += numberText(percentile);
    }
    text += CsvSeparator;
    text += numberText(summary.maximum);
    text += LineEnd;
  }
  return text;
}

} // namespace dwe::sim
