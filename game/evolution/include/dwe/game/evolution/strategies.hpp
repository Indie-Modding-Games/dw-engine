#pragma once

#include <dwe/game/evolution/inputs.hpp>
#include <dwe/game/evolution/report.hpp>
#include <dwe/game/evolution/rules.hpp>
#include <dwe/game/evolution/state.hpp>
#include <dwe/game/evolution/tables.hpp>
#include <dwe/ruleset/strategy.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

namespace dwe::game::evolution {

// The name of the original game's strategies.
inline constexpr std::string_view OriginalStrategy = "original";

// What a strategy reads: the tables, the section and the partner's inputs and evolution state.
struct EvolutionQuery {
  // None of them is null.
  const EvolutionTables* tables = nullptr;
  const EvolutionRules* rules = nullptr;
  const EvolutionInputs* inputs = nullptr;
  const EvolutionState* state = nullptr;
};

// Scores a candidate target against its requirements. `currentBest` is the best candidate so far,
// if any.
class RequirementScoring {
public:
  RequirementScoring() = default;
  RequirementScoring(const RequirementScoring&) = delete;
  RequirementScoring& operator=(const RequirementScoring&) = delete;
  RequirementScoring(RequirementScoring&&) = delete;
  RequirementScoring& operator=(RequirementScoring&&) = delete;
  virtual ~RequirementScoring() = default;

  [[nodiscard]] virtual std::int32_t score(const EvolutionQuery& query, Species target,
                                           std::optional<Species> currentBest) const = 0;

  // What the target asks and how the partner stands, group by group, with the points the scoring
  // gives before comparing with a best candidate; nothing when the strategy cannot tell or the
  // target has no requirement.
  [[nodiscard]] virtual std::optional<RequirementReport> report(const EvolutionQuery& query,
                                                                Species target) const {
    (void)query;
    (void)target;
    return std::nullopt;
  }
};

// Picks the target of the periodic check, if any, once the counter has reached the level's hours.
class EvolutionSelection {
public:
  EvolutionSelection() = default;
  EvolutionSelection(const EvolutionSelection&) = delete;
  EvolutionSelection& operator=(const EvolutionSelection&) = delete;
  EvolutionSelection(EvolutionSelection&&) = delete;
  EvolutionSelection& operator=(EvolutionSelection&&) = delete;
  virtual ~EvolutionSelection() = default;

  [[nodiscard]] virtual std::optional<Species> select(const EvolutionQuery& query,
                                                      const RequirementScoring& scoring) const = 0;
};

// The registries of the section's `scoring` and `selection`, with `original` registered.
const ruleset::StrategyRegistry<RequirementScoring>& scoringStrategies();
const ruleset::StrategyRegistry<EvolutionSelection>& selectionStrategies();

// The original game's scoring (evolution.c: calculateRequirementScore).
class OriginalScoring final : public RequirementScoring {
public:
  [[nodiscard]] std::int32_t score(const EvolutionQuery& query, Species target,
                                   std::optional<Species> currentBest) const override;
  // The score is built from this report, so the two always agree.
  [[nodiscard]] std::optional<RequirementReport> report(const EvolutionQuery& query,
                                                        Species target) const override;
};

// The original game's selection (evolution.c: getFreshEvolutionTarget,
// getInTrainingEvolutionTarget, getRookieEvolutionTarget, getChampionEvolutionTarget).
class OriginalSelection final : public EvolutionSelection {
public:
  [[nodiscard]] std::optional<Species> select(const EvolutionQuery& query,
                                              const RequirementScoring& scoring) const override;
};

} // namespace dwe::game::evolution
