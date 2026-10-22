// The ruleset-diff scenario: lists every value in which the simulated ruleset (with its --set
// values) differs from another one, --param against=<id> (original by default). Its report's table
// is the comparison; each run only counts the differences.

#include <dwe/ruleset/ruleset.hpp>
#include <dwe/ruleset/ruleset_values.hpp>
#include <dwe/sim/scenario.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::sim {

namespace {

constexpr std::string_view AgainstParameter = "against";
constexpr std::string_view DifferencesMetric = "differences";
constexpr std::string_view MissingValue = "(none)";
constexpr std::string_view TableTitleStart = "Values that differ from ";
constexpr std::string_view SectionColumn = "section";
constexpr std::string_view PathColumn = "path";
constexpr std::string_view SimulatedColumnEnd = " (simulated)";

std::string shown(const std::optional<std::string>& value) {
  return value.value_or(std::string{MissingValue});
}

class RulesetDiffScenario final : public Scenario {
public:
  core::Result<void> setup(const ScenarioContext& context) override {
    const auto against = context.parameters->find(AgainstParameter);
    against_ = against == context.parameters->end() ? std::string{ruleset::OriginalRuleset}
                                                    : against->second;
    auto other = ruleset::loadRuleset(*context.files, against_);
    if (!other) {
      return std::unexpected{other.error()};
    }
    simulated_ = context.rules->id();
    differences_ = ruleset::diffValues(*other, *context.rules);
    return {};
  }

  [[nodiscard]] core::Result<Metrics> run(std::uint64_t /*seed*/) const override {
    return Metrics{{.name = std::string{DifferencesMetric},
                    .value = static_cast<double>(differences_.size())}};
  }

  [[nodiscard]] std::vector<ReportTable> tables() const override {
    ReportTable table{.title = std::string{TableTitleStart} + against_,
                      .columns = {std::string{SectionColumn}, std::string{PathColumn}, against_,
                                  simulated_ + std::string{SimulatedColumnEnd}},
                      .rows = {}};
    for (const ruleset::ValueDifference& difference : differences_) {
      table.rows.push_back(
          {difference.section, difference.path, shown(difference.before), shown(difference.after)});
    }
    return {table};
  }

private:
  std::string against_;
  std::string simulated_;
  std::vector<ruleset::ValueDifference> differences_;
};

const ScenarioRegistration registration{{
    .name = "ruleset-diff",
    .description =
        "Lists every value in which the ruleset (with its --set values) differs from --param against=<id> "
        "(original by default).",
    .create = [] { return std::make_unique<RulesetDiffScenario>(); },
}};

} // namespace

} // namespace dwe::sim
