#pragma once

#include <dwe/core/message.hpp>

#include <string_view>

namespace dwe::sim::messages {

using core::formatMessage;

// The messages of dwe-sim, as std::format templates for core::formatMessage.

// The command line.
inline constexpr std::string_view UnknownOption = "unknown option '{}'";
inline constexpr std::string_view MissingValue = "{} needs a value";
inline constexpr std::string_view BadCount = "{} must be a whole number above zero, not '{}'";
inline constexpr std::string_view BadNumber = "{} must be a number of zero or more, not '{}'";
inline constexpr std::string_view BadOverride = "--set takes <section>.<path>=<value>, not '{}'";
inline constexpr std::string_view BadParameter = "--param takes <key>=<value>, not '{}'";
inline constexpr std::string_view NoScenario = "name a scenario with --scenario, or list them with --list";
inline constexpr std::string_view UsageLine = "usage: {}";

// The scenarios.
inline constexpr std::string_view RepeatedScenario = "scenario '{}' is registered twice";
inline constexpr std::string_view UnknownScenario = "unknown scenario '{}'; known: {}";
inline constexpr std::string_view ScenarioFailed = "scenario {}: {}";
inline constexpr std::string_view RunFailed = "run {} (seed {}): {}";
inline constexpr std::string_view ScenarioLine = "{}: {}\n";
inline constexpr std::string_view NameSeparator = ", ";

// The run.
inline constexpr std::string_view RulesetFailed = "ruleset {}: {}";
inline constexpr std::string_view WriteFailed = "could not write {}";
inline constexpr std::string_view FolderFailed = "could not create {}: {}";
inline constexpr std::string_view ReportWritten = "dwe-sim: report in {}";
inline constexpr std::string_view Finished = "dwe-sim: {} runs of {} on {} threads";

} // namespace dwe::sim::messages
