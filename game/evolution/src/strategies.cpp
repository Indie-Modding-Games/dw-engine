#include <dwe/game/evolution/messages.hpp>
#include <dwe/game/evolution/strategies.hpp>

#include <array>
#include <cstddef>
#include <memory>
#include <string>

namespace dwe::game::evolution {

namespace {

constexpr std::size_t StatCount = 6;

using StatValues = std::array<std::int32_t, StatCount>;

// The partner's stats as the requirements compare them, in the original's order: HP and MP divided,
// then offense, defense, speed and brains.
StatValues comparedStats(const EvolutionQuery& query) {
  const BaseStats& stats = query.inputs->stats;
  const std::int32_t divisor = query.rules->requirements.hpMpDivisor;
  return {stats.hp / divisor, stats.mp / divisor, stats.offense,
          stats.defense,      stats.speed,        stats.brains};
}

StatValues requiredStats(const Requirement& requirement) {
  const BaseStats& minimum = requirement.minimum;
  return {minimum.hp, minimum.mp, minimum.offense, minimum.defense, minimum.speed, minimum.brains};
}

bool meetsCareMistakes(const Requirement& requirement, const EvolutionInputs& inputs) {
  if (requirement.careIsMaximum) {
    return inputs.careMistakes <= requirement.careMistakes;
  }
  return inputs.careMistakes >= requirement.careMistakes;
}

bool meetsWeight(const Requirement& requirement, const EvolutionQuery& query) {
  const std::int32_t margin = query.rules->requirements.weightMargin;
  return requirement.weight - margin <= query.inputs->weight &&
         query.inputs->weight <= requirement.weight + margin;
}

// The index of the partner's highest compared stat. On a tie the original keeps the last one.
std::size_t highestStat(const StatValues& stats, bool tiePicksLast) {
  std::size_t highest = 0;
  for (std::size_t i = 1; i < stats.size(); ++i) {
    if (stats.at(i) > stats.at(highest) || (tiePicksLast && stats.at(i) == stats.at(highest))) {
      highest = i;
    }
  }
  return highest;
}

// A rookie target scores when the partner's highest stat is one it favours; the others when every
// stat reaches its minimum (a minimum of NoRequirement always does).
bool meetsStats(const Requirement& requirement, const EvolutionQuery& query, Species target) {
  const StatValues stats = comparedStats(query);
  const StatValues required = requiredStats(requirement);
  if (query.tables->levelOf(target) == Level::Rookie) {
    const bool tiePicksLast = query.rules->quirks.on(RookieStatTiePicksLast);
    return required.at(highestStat(stats, tiePicksLast)) == FavouredStat;
  }
  for (std::size_t i = 0; i < stats.size(); ++i) {
    if (stats.at(i) < required.at(i)) {
      return false;
    }
  }
  return true;
}

bool meetsBattles(const Requirement& requirement, const EvolutionInputs& inputs) {
  if (requirement.battles == NoRequirement) {
    return false;
  }
  if (requirement.battlesAreMaximum) {
    return inputs.battles <= requirement.battles;
  }
  return inputs.battles >= requirement.battles;
}

bool reaches(std::int32_t value, std::int32_t minimum) {
  return minimum != NoRequirement && value >= minimum;
}

// The bonus point: any one of the extra requirements.
bool meetsBonus(const Requirement& requirement, const EvolutionInputs& inputs) {
  return (requirement.from && inputs.species == *requirement.from) ||
         reaches(inputs.discipline, requirement.discipline) ||
         reaches(inputs.happiness, requirement.happiness) || meetsBattles(requirement, inputs) ||
         reaches(inputs.learnedMoves, requirement.learnedMoves);
}

bool wasRaised(const EvolutionQuery& query, Species target) {
  const StatReference* reference = query.tables->statReference(target);
  return query.state->raised.contains(reference == nullptr ? target : reference->loads);
}

// Against a best candidate so far, a passing target the partner was already raised as loses
// everything if the best is new, and a new one gains a point if the best was raised.
std::int32_t preferNew(const EvolutionQuery& query, Species target,
                       std::optional<Species> currentBest, std::int32_t points) {
  if (points < query.rules->requirements.passPoints || !currentBest) {
    return points;
  }
  const bool targetRaised = wasRaised(query, target);
  const bool bestRaised = wasRaised(query, *currentBest);
  if (targetRaised && !bestRaised) {
    return 0;
  }
  if (!targetRaised && bestRaised) {
    return points + 1;
  }
  return points;
}

std::optional<Species> babyTarget(const EvolutionQuery& query) {
  const auto found = query.rules->babyTargets.find(query.tables->idOf(query.inputs->species));
  if (found == query.rules->babyTargets.end()) {
    return std::nullopt;
  }
  return query.tables->find(found->second);
}

// getInTrainingEvolutionTarget: the passing target with the most points; on a tie the first.
std::optional<Species> bestScored(const EvolutionQuery& query, const RequirementScoring& scoring) {
  std::optional<Species> best;
  std::int32_t bestScore = 0;
  for (const Species target : query.tables->pathTargets(query.inputs->species)) {
    const std::int32_t score = scoring.score(query, target, best);
    if (bestScore < score && score >= query.rules->requirements.passPoints) {
      best = target;
      bestScore = score;
    }
  }
  return best;
}

// The running sum and count of the stats the passing targets ask for.
struct StatAverage {
  std::int32_t total = 0;
  std::int32_t count = 0;

  void add(const EvolutionQuery& query, Species target) {
    const Requirement* requirement = query.tables->requirement(target);
    if (requirement == nullptr) {
      return;
    }
    const StatValues stats = comparedStats(query);
    const StatValues required = requiredStats(*requirement);
    for (std::size_t i = 0; i < stats.size(); ++i) {
      if (required.at(i) != NoRequirement) {
        total += stats.at(i);
        ++count;
      }
    }
  }
};

// getRookieEvolutionTarget and getChampionEvolutionTarget: among the passing targets, the one with
// the highest average of the stats it asks for. The original only resets the sum and the count when
// a target becomes the best.
std::optional<Species> bestAverage(const EvolutionQuery& query, const RequirementScoring& scoring) {
  const bool carriesOver = query.rules->quirks.on(SelectionAverageCarriesOver);
  std::optional<Species> best;
  std::int32_t bestValue = 0;
  StatAverage average;
  for (const Species target : query.tables->pathTargets(query.inputs->species)) {
    if (scoring.score(query, target, best) < query.rules->requirements.passPoints) {
      continue;
    }
    if (!carriesOver) {
      average = {};
    }
    average.add(query, target);
    // The original would divide by zero; no target of the original's tables asks for no stat.
    if (average.count == 0) {
      continue;
    }
    average.total /= average.count;
    if (bestValue < average.total) {
      best = target;
      bestValue = average.total;
      average = {};
    }
  }
  return best;
}

std::optional<Species> rookieTarget(const EvolutionQuery& query,
                                    const RequirementScoring& scoring) {
  if (const auto target = bestAverage(query, scoring); target) {
    return target;
  }
  if (query.inputs->evolutionTimer >= query.rules->fallback.rookieHours) {
    return query.tables->find(query.rules->fallback.rookieTarget);
  }
  return std::nullopt;
}

} // namespace

std::int32_t OriginalScoring::score(const EvolutionQuery& query, Species target,
                                    std::optional<Species> currentBest) const {
  const Requirement* requirement = query.tables->requirement(target);
  if (requirement == nullptr) {
    return 0;
  }
  const std::int32_t points =
      static_cast<std::int32_t>(meetsCareMistakes(*requirement, *query.inputs)) +
      static_cast<std::int32_t>(meetsWeight(*requirement, query)) +
      static_cast<std::int32_t>(meetsStats(*requirement, query, target)) +
      static_cast<std::int32_t>(meetsBonus(*requirement, *query.inputs));
  return preferNew(query, target, currentBest, points);
}

std::optional<Species> OriginalSelection::select(const EvolutionQuery& query,
                                                 const RequirementScoring& scoring) const {
  switch (query.tables->levelOf(query.inputs->species)) {
  case Level::Baby:
    return babyTarget(query);
  case Level::InTraining:
    return bestScored(query, scoring);
  case Level::Rookie:
    return rookieTarget(query, scoring);
  case Level::Champion:
    return bestAverage(query, scoring);
  case Level::None:
  case Level::Ultimate:
    return std::nullopt;
  }
  return std::nullopt;
}

const ruleset::StrategyRegistry<RequirementScoring>& scoringStrategies() {
  static const auto registry = [] {
    ruleset::StrategyRegistry<RequirementScoring> strategies{std::string{messages::ScoringWhat}};
    strategies.add(std::string{OriginalStrategy},
                   [] { return std::make_unique<OriginalScoring>(); });
    return strategies;
  }();
  return registry;
}

const ruleset::StrategyRegistry<EvolutionSelection>& selectionStrategies() {
  static const auto registry = [] {
    ruleset::StrategyRegistry<EvolutionSelection> strategies{std::string{messages::SelectionWhat}};
    strategies.add(std::string{OriginalStrategy},
                   [] { return std::make_unique<OriginalSelection>(); });
    return strategies;
  }();
  return registry;
}

} // namespace dwe::game::evolution
