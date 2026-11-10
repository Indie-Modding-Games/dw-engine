#include <dwe/game/evolution/messages.hpp>
#include <dwe/game/evolution/strategies.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
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

// The care mistakes point: at most the requirement with CareIsMaximumFlag, at least it otherwise.
void reportCareMistakes(RequirementReport& report, const Requirement& requirement,
                        const EvolutionInputs& inputs) {
  report.careMistakes = requirement.careMistakes;
  report.careIsMaximum = requirement.careIsMaximum;
  report.careValue = inputs.careMistakes;
  report.careMet = requirement.careIsMaximum ? inputs.careMistakes <= requirement.careMistakes
                                             : inputs.careMistakes >= requirement.careMistakes;
}

// The weight point: within the margin of the weight asked for.
void reportWeight(RequirementReport& report, const Requirement& requirement,
                  const EvolutionQuery& query) {
  const std::int32_t margin = query.rules->requirements.weightMargin;
  report.weight = requirement.weight;
  report.weightMin = requirement.weight - margin;
  report.weightMax = requirement.weight + margin;
  report.weightValue = query.inputs->weight;
  report.weightMet =
      report.weightMin <= query.inputs->weight && query.inputs->weight <= report.weightMax;
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

// The stats point. A rookie target scores when the partner's highest stat is one it favours; the
// others when every stat reaches its minimum (a minimum of NoRequirement always does).
void reportStats(RequirementReport& report, const Requirement& requirement,
                 const EvolutionQuery& query, Species target) {
  const StatValues stats = comparedStats(query);
  const StatValues required = requiredStats(requirement);
  const bool tiePicksLast = query.rules->quirks.on(RookieStatTiePicksLast);
  report.favouredStats = query.tables->levelOf(target) == Level::Rookie;
  report.highestStat = highestStat(stats, tiePicksLast);
  const BaseStats& base = query.inputs->stats;
  const StatValues raw{base.hp, base.mp, base.offense, base.defense, base.speed, base.brains};
  for (std::size_t i = 0; i < stats.size(); ++i) {
    StatLine& line = report.stats.at(i);
    line.required = required.at(i);
    line.value = stats.at(i);
    line.stat = raw.at(i);
    line.divisor = i < 2 ? query.rules->requirements.hpMpDivisor : 1;
    line.met = report.favouredStats ? i == report.highestStat && line.required == FavouredStat
                                    : line.value >= line.required;
  }
  report.statsMet = report.favouredStats ? report.stats.at(report.highestStat).met
                                         : std::ranges::all_of(report.stats, &StatLine::met);
}

void addBonus(RequirementReport& report, BonusKind kind, std::int32_t required, std::int32_t value,
              bool isMaximum = false) {
  if (required == NoRequirement) {
    return;
  }
  const bool met = isMaximum ? value <= required : value >= required;
  report.bonus.push_back(
      {.kind = kind, .required = required, .value = value, .isMaximum = isMaximum, .met = met});
}

// The bonus point: any one of the extra requirements.
void reportBonus(RequirementReport& report, const Requirement& requirement,
                 const EvolutionInputs& inputs) {
  if (requirement.from) {
    report.bonus.push_back({.kind = BonusKind::From,
                            .required = *requirement.from,
                            .value = inputs.species,
                            .met = inputs.species == *requirement.from});
  }
  addBonus(report, BonusKind::Discipline, requirement.discipline, inputs.discipline);
  addBonus(report, BonusKind::Happiness, requirement.happiness, inputs.happiness);
  addBonus(report, BonusKind::Battles, requirement.battles, inputs.battles,
           requirement.battlesAreMaximum);
  addBonus(report, BonusKind::LearnedMoves, requirement.learnedMoves, inputs.learnedMoves);
  report.bonusMet = std::ranges::any_of(report.bonus, &BonusLine::met);
}

// A target's gate (ruleset/species_extra.hpp): every trigger set, the hour within its range and
// the cup won; a cup gate is open while the inputs cannot tell (the tournaments' hook).
bool gateOpen(const ruleset::SpeciesGate& gate, const EvolutionInputs& inputs) {
  for (const std::int32_t trigger : gate.triggers) {
    if (!inputs.trigger || !inputs.trigger(trigger)) {
      return false;
    }
  }
  if (gate.hours && (!inputs.hourOfDay || !gate.hours->holds(*inputs.hourOfDay))) {
    return false;
  }
  return !gate.cup || !inputs.cupWon || inputs.cupWon(*gate.cup);
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

// The targets of a path in one tier: the original's (and every pack's but all_species'), or the
// fallback ones, which are looked at only when none of the first tier passes
// (docs/modulos/game/evoluciones-extra.md). Without fallback targets the second tier is empty.
enum class Tier : std::uint8_t { Original, Fallback };

bool inTier(const EvolutionQuery& query, Species target, Tier tier) {
  return query.tables->yields(target) == (tier == Tier::Fallback);
}

// getInTrainingEvolutionTarget: the passing target with the most points; on a tie the first.
std::optional<Species> bestScored(const EvolutionQuery& query, const RequirementScoring& scoring,
                                  Tier tier) {
  std::optional<Species> best;
  std::int32_t bestScore = 0;
  for (const Species target : query.tables->pathTargets(query.inputs->species)) {
    if (!inTier(query, target, tier)) {
      continue;
    }
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
std::optional<Species> bestAverage(const EvolutionQuery& query, const RequirementScoring& scoring,
                                   Tier tier) {
  const bool carriesOver = query.rules->quirks.on(SelectionAverageCarriesOver);
  std::optional<Species> best;
  std::int32_t bestValue = 0;
  StatAverage average;
  for (const Species target : query.tables->pathTargets(query.inputs->species)) {
    if (!inTier(query, target, tier)) {
      continue;
    }
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

// Each tier in turn: a fallback target only when no target of the original's passes.
template <typename Pick>
std::optional<Species> originalFirst(const EvolutionQuery& query, const RequirementScoring& scoring,
                                     Pick pick) {
  if (const auto target = pick(query, scoring, Tier::Original); target) {
    return target;
  }
  return pick(query, scoring, Tier::Fallback);
}

std::optional<Species> rookieTarget(const EvolutionQuery& query,
                                    const RequirementScoring& scoring) {
  if (const auto target = originalFirst(query, scoring, bestAverage); target) {
    return target;
  }
  if (query.inputs->evolutionTimer >= query.rules->fallback.rookieHours) {
    return query.tables->find(query.rules->fallback.rookieTarget);
  }
  return std::nullopt;
}

} // namespace

std::optional<RequirementReport> OriginalScoring::report(const EvolutionQuery& query,
                                                         Species target) const {
  const Requirement* requirement = query.tables->requirement(target);
  if (requirement == nullptr) {
    return std::nullopt;
  }
  RequirementReport report;
  report.target = target;
  report.passPoints = query.rules->requirements.passPoints;
  reportCareMistakes(report, *requirement, *query.inputs);
  reportWeight(report, *requirement, query);
  reportStats(report, *requirement, query, target);
  reportBonus(report, *requirement, *query.inputs);
  report.points =
      static_cast<std::int32_t>(report.careMet) + static_cast<std::int32_t>(report.weightMet) +
      static_cast<std::int32_t>(report.statsMet) + static_cast<std::int32_t>(report.bonusMet);
  if (const ruleset::SpeciesGate* gate = query.tables->gate(target)) {
    report.gateMet = gateOpen(*gate, *query.inputs);
    if (!*report.gateMet) {
      report.points = 0;
    }
  }
  return report;
}

std::int32_t OriginalScoring::score(const EvolutionQuery& query, Species target,
                                    std::optional<Species> currentBest) const {
  const auto scored = report(query, target);
  return scored ? preferNew(query, target, currentBest, scored->points) : 0;
}

std::optional<Species> OriginalSelection::select(const EvolutionQuery& query,
                                                 const RequirementScoring& scoring) const {
  switch (query.tables->levelOf(query.inputs->species)) {
  case Level::Baby:
    return babyTarget(query);
  case Level::InTraining:
    return originalFirst(query, scoring, bestScored);
  case Level::Rookie:
    return rookieTarget(query, scoring);
  case Level::Champion:
    return originalFirst(query, scoring, bestAverage);
  case Level::Ultimate:
    // The Mega step (an option): an Ultimate that is not a Mega picks among its Mega targets as a
    // champion picks among its own.
    if (query.tables->megaStep() != nullptr && !query.tables->isMega(query.inputs->species)) {
      return originalFirst(query, scoring, bestAverage);
    }
    return std::nullopt;
  case Level::None:
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
