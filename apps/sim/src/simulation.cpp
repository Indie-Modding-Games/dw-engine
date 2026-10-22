#include <dwe/assets/file_system.hpp>
#include <dwe/core/log.hpp>
#include <dwe/ruleset/ruleset_store.hpp>
#include <dwe/sim/messages.hpp>
#include <dwe/sim/runner.hpp>
#include <dwe/sim/simulation.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace dwe::sim {

namespace {

constexpr std::string_view MarkdownExtension = ".md";
constexpr std::string_view RunsCsvSuffix = "-runs.csv";
constexpr std::string_view SummaryCsvSuffix = "-summary.csv";
constexpr char ParameterSeparator = '=';

std::string knownScenarios(const ScenarioRegistry& registry) {
  std::string names;
  for (const auto& [name, info] : registry.scenarios()) {
    if (!names.empty()) {
      names += messages::NameSeparator;
    }
    names += name;
  }
  return names;
}

// One thread per hardware thread when the options leave it open.
std::size_t threadsFor(const SimOptions& options) {
  if (options.threads > 0) {
    return options.threads;
  }
  return std::max<std::size_t>(std::thread::hardware_concurrency(), 1);
}

ReportHeader headerOf(const SimOptions& options, const ScenarioInfo& info) {
  ReportHeader header{.scenario = info.name,
                      .description = info.description,
                      .ruleset = options.ruleset,
                      .runs = options.runs,
                      .seed = options.seed,
                      .days = options.days,
                      .speed = options.speed,
                      .overrides = {},
                      .parameters = {}};
  for (const ValueOverride& value : options.overrides) {
    header.overrides.push_back(value.text);
  }
  for (const auto& [key, value] : options.parameters) {
    std::string parameter = key;
    parameter += ParameterSeparator;
    parameter += value;
    header.parameters.push_back(std::move(parameter));
  }
  return header;
}

core::Result<void> writeText(const std::filesystem::path& file, std::string_view text) {
  std::ofstream stream{file, std::ios::binary | std::ios::trunc};
  stream.write(text.data(), static_cast<std::streamsize>(text.size()));
  if (!stream) {
    return core::fail(messages::formatMessage(messages::WriteFailed, assets::pathText(file)));
  }
  return {};
}

} // namespace

core::Result<SimulationReport> simulate(const assets::VirtualFileSystem& files,
                                        const ScenarioRegistry& registry,
                                        const SimOptions& options) {
  const ScenarioInfo* info = registry.find(options.scenario);
  if (info == nullptr) {
    return core::fail(messages::formatMessage(messages::UnknownScenario, options.scenario,
                                              knownScenarios(registry)));
  }
  auto store = ruleset::RulesetStore::load(files, options.ruleset);
  if (!store) {
    return core::fail(
        messages::formatMessage(messages::RulesetFailed, options.ruleset, store.error().message));
  }
  if (auto applied = applyOverrides(*store, options.overrides); !applied) {
    return core::fail(
        messages::formatMessage(messages::RulesetFailed, options.ruleset, applied.error().message));
  }
  const std::unique_ptr<Scenario> scenario = info->create();
  const ScenarioContext context{.files = &files,
                                .rules = &store->current(),
                                .days = options.days,
                                .speed = options.speed,
                                .parameters = &options.parameters};
  if (auto ready = scenario->setup(context); !ready) {
    return core::fail(
        messages::formatMessage(messages::ScenarioFailed, info->name, ready.error().message));
  }
  const std::size_t threads = threadsFor(options);
  auto records =
      runScenario(*scenario, {.runs = options.runs, .seed = options.seed, .threads = threads});
  if (!records) {
    return core::fail(
        messages::formatMessage(messages::ScenarioFailed, info->name, records.error().message));
  }
  core::logInfo("{}",
                messages::formatMessage(messages::Finished, options.runs, info->name, threads));
  const std::vector<MetricSummary> summaries = summarize(*records);
  const std::vector<ReportTable> tables = scenario->tables();
  return SimulationReport{.markdown = markdownReport(headerOf(options, *info), summaries, tables),
                          .runsCsv = runsCsv(*records),
                          .summaryCsv = summaryCsv(summaries)};
}

core::Result<void> writeReport(const SimulationReport& report, const std::filesystem::path& folder,
                               std::string_view scenario) {
  std::error_code error;
  std::filesystem::create_directories(folder, error);
  if (error) {
    return core::fail(
        messages::formatMessage(messages::FolderFailed, assets::pathText(folder), error.message()));
  }
  const std::string stem{scenario};
  for (const auto& [suffix, text] :
       {std::pair{MarkdownExtension, &report.markdown}, std::pair{RunsCsvSuffix, &report.runsCsv},
        std::pair{SummaryCsvSuffix, &report.summaryCsv}}) {
    if (auto written = writeText(folder / (stem + std::string{suffix}), *text); !written) {
      return written;
    }
  }
  core::logInfo("{}", messages::formatMessage(messages::ReportWritten, assets::pathText(folder)));
  return {};
}

std::string describeScenarios(const ScenarioRegistry& registry) {
  std::string text;
  for (const auto& [name, info] : registry.scenarios()) {
    text += messages::formatMessage(messages::ScenarioLine, name, info.description);
  }
  return text;
}

} // namespace dwe::sim
