#include <dwe/content/dev_assets.hpp>
#include <dwe/ruleset/ruleset_values.hpp>
#include <dwe/sim/messages.hpp>
#include <dwe/sim/sim_options.hpp>

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace dwe::sim {

namespace {

constexpr std::string_view ListOption = "--list";
constexpr std::string_view ScenarioOption = "--scenario";
constexpr std::string_view ContentOption = "--content";
constexpr std::string_view ModsOption = "--mods";
constexpr std::string_view RulesetOption = "--ruleset";
constexpr std::string_view RunsOption = "--runs";
constexpr std::string_view SeedOption = "--seed";
constexpr std::string_view ThreadsOption = "--threads";
constexpr std::string_view DaysOption = "--days";
constexpr std::string_view SpeedOption = "--speed";
constexpr std::string_view SetOption = "--set";
constexpr std::string_view ParamOption = "--param";
constexpr std::string_view OutOption = "--out";

constexpr char PathSeparator = '.';
constexpr char ValueSeparator = '=';

template <typename Number>
std::optional<Number> numberOf(std::string_view text) {
  Number value{};
  const auto [stop, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (text.empty() || error != std::errc{} || stop != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

// A whole number above zero.
template <typename Number>
core::Result<Number> countOf(std::string_view option, std::string_view text) {
  const std::optional<Number> value = numberOf<Number>(text);
  if (!value || *value <= 0) {
    return core::fail(messages::formatMessage(messages::BadCount, option, text));
  }
  return *value;
}

core::Result<std::pair<std::string, std::string>> keyAndValue(std::string_view text,
                                                              std::string_view problem) {
  const std::size_t separator = text.find(ValueSeparator);
  if (separator == std::string_view::npos || separator == 0) {
    return core::fail(messages::formatMessage(problem, text));
  }
  return std::pair{std::string{text.substr(0, separator)}, std::string{text.substr(separator + 1)}};
}

// Reads the value of an option that takes one into the options.
core::Result<void> readValue(SimOptions& options, std::string_view option, std::string_view value) {
  if (option == ScenarioOption) {
    options.scenario = value;
  } else if (option == ContentOption) {
    options.content = std::filesystem::path{value};
  } else if (option == ModsOption) {
    options.mods.emplace_back(value);
  } else if (option == RulesetOption) {
    options.ruleset = value;
  } else if (option == OutOption) {
    options.out = std::filesystem::path{value};
  } else if (option == RunsOption) {
    auto runs = countOf<std::size_t>(option, value);
    if (!runs) {
      return std::unexpected{runs.error()};
    }
    options.runs = *runs;
  } else if (option == ThreadsOption) {
    auto threads = countOf<std::size_t>(option, value);
    if (!threads) {
      return std::unexpected{threads.error()};
    }
    options.threads = *threads;
  } else if (option == DaysOption) {
    auto days = countOf<std::int64_t>(option, value);
    if (!days) {
      return std::unexpected{days.error()};
    }
    options.days = *days;
  } else if (option == SeedOption) {
    const std::optional<std::uint64_t> seed = numberOf<std::uint64_t>(value);
    if (!seed) {
      return core::fail(messages::formatMessage(messages::BadNumber, option, value));
    }
    options.seed = *seed;
  } else if (option == SpeedOption) {
    const std::optional<double> speed = numberOf<double>(value);
    if (!speed || *speed < 0.0 || !std::isfinite(*speed)) {
      return core::fail(messages::formatMessage(messages::BadNumber, option, value));
    }
    options.speed = *speed;
  } else if (option == SetOption) {
    auto parsed = parseValueOverride(value);
    if (!parsed) {
      return std::unexpected{parsed.error()};
    }
    options.overrides.push_back(std::move(*parsed));
  } else if (option == ParamOption) {
    auto parameter = keyAndValue(value, messages::BadParameter);
    if (!parameter) {
      return std::unexpected{parameter.error()};
    }
    options.parameters.insert_or_assign(std::move(parameter->first), std::move(parameter->second));
  } else {
    return core::fail(messages::formatMessage(messages::UnknownOption, option));
  }
  return {};
}

} // namespace

core::Result<ValueOverride> parseValueOverride(std::string_view text) {
  auto parts = keyAndValue(text, messages::BadOverride);
  if (!parts) {
    return std::unexpected{parts.error()};
  }
  const std::string& target = parts->first;
  const std::size_t separator = target.find(PathSeparator);
  if (separator == std::string::npos || separator == 0 || separator + 1 == target.size()) {
    return core::fail(messages::formatMessage(messages::BadOverride, text));
  }
  return ValueOverride{.section = target.substr(0, separator),
                       .path = target.substr(separator + 1),
                       .yaml = std::move(parts->second),
                       .text = std::string{text}};
}

core::Result<SimOptions> parseSimOptions(std::span<const std::string_view> arguments) {
  SimOptions options;
  for (std::size_t index = 0; index < arguments.size(); ++index) {
    const std::string_view option = arguments[index];
    if (option == ListOption) {
      options.list = true;
      continue;
    }
    if (option == content::DevAssetsOption) {
      options.devAssets = true;
      continue;
    }
    if (index + 1 >= arguments.size()) {
      return core::fail(messages::formatMessage(messages::MissingValue, option));
    }
    if (auto read = readValue(options, option, arguments[++index]); !read) {
      return std::unexpected{read.error()};
    }
  }
  if (!options.list && options.scenario.empty()) {
    return core::fail(std::string{messages::NoScenario});
  }
  return options;
}

core::Result<void> applyOverrides(ruleset::RulesetStore& store,
                                  std::span<const ValueOverride> overrides) {
  for (const ValueOverride& value : overrides) {
    if (auto set = store.setValue(value.section, value.path, value.yaml); !set) {
      return set;
    }
  }
  return {};
}

} // namespace dwe::sim
