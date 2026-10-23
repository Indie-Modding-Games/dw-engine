// The raisings scenario: raises a partner from a form (--param from=<id>, the first baby by
// default) for --days days with random care at each level, through the evolution rules of the
// ruleset, and counts which form each raising ends in (game/evolution). The care is random, not a
// model of how a player raises: it shows which forms the rules and tables can reach and how often
// under even odds, and how a tuned value moves that.

#include <dwe/game/evolution/evolution.hpp>
#include <dwe/game/evolution/state.hpp>
#include <dwe/ruleset/game_clock.hpp>
#include <dwe/ruleset/random.hpp>
#include <dwe/sim/messages.hpp>
#include <dwe/sim/scenario.hpp>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dwe::sim {

namespace {

namespace evolution = game::evolution;

// The parameters: the starting form and the highest stat a level of training can add.
constexpr std::string_view FromParameter = "from";
constexpr std::string_view MaxStatParameter = "max_stat";
constexpr std::int32_t DefaultMaxStat = 300;

// The random care of a level, on the scales of EvolutionInputs.
constexpr std::int32_t HpMpScale = 10;
constexpr std::int32_t MaxWeight = 61;
constexpr std::int32_t MaxCareMistakes = 11;
constexpr std::int32_t MaxPercent = 101;
constexpr std::int32_t HappinessRange = 201;
constexpr std::int32_t HappinessOffset = 100;
constexpr std::int32_t MaxBattles = 61;
constexpr std::int32_t MaxMovesPerLevel = 11;

// The metrics: how far each raising got, and then one per form it can end in, 1 for the one it
// ended in.
constexpr std::string_view LevelMetric = "final_level";
constexpr std::string_view EvolutionsMetric = "evolutions";
constexpr double Ended = 1.0;
constexpr double NotEnded = 0.0;

// The stats after an evolution: the restored ones, or the change added with no cap (the partner's
// caps are not part of this scenario).
evolution::BaseStats statsAfter(const evolution::BaseStats& stats,
                                const evolution::EvolutionResult& result) {
  if (result.restoredStats) {
    return *result.restoredStats;
  }
  const evolution::BaseStats& change = result.statChange;
  return {.hp = stats.hp + change.hp,
          .mp = stats.mp + change.mp,
          .offense = stats.offense + change.offense,
          .defense = stats.defense + change.defense,
          .speed = stats.speed + change.speed,
          .brains = stats.brains + change.brains};
}

std::optional<std::int32_t> wholeNumber(std::string_view text) {
  std::int32_t number = 0;
  const auto [end, problem] = std::from_chars(text.data(), text.data() + text.size(), number);
  if (problem != std::errc{} || end != text.data() + text.size() || number <= 0) {
    return std::nullopt;
  }
  return number;
}

class RaisingsScenario final : public Scenario {
public:
  core::Result<void> setup(const ScenarioContext& context) override {
    auto loaded = evolution::Evolution::load(*context.files, *context.rules);
    if (!loaded) {
      return std::unexpected{loaded.error()};
    }
    evolution_ = std::make_unique<evolution::Evolution>(std::move(*loaded));
    const evolution::EvolutionTables& tables = evolution_->tables();
    const auto from = context.parameters->find(FromParameter);
    const std::vector<std::string>& babies = evolution_->rules().rebirth.babies;
    std::string fromId = babies.empty() ? std::string{} : babies.front();
    if (from != context.parameters->end()) {
      fromId = from->second;
    }
    const auto start = tables.find(fromId);
    if (!start) {
      return core::fail(
          messages::formatMessage(messages::UnknownFormParameter, FromParameter, fromId));
    }
    start_ = *start;
    if (const auto maxStat = context.parameters->find(MaxStatParameter);
        maxStat != context.parameters->end()) {
      const auto value = wholeNumber(maxStat->second);
      if (!value) {
        return core::fail(
            messages::formatMessage(messages::BadScenarioCount, MaxStatParameter, maxStat->second));
      }
      maxStat_ = *value;
    }
    hours_ = context.days * ruleset::HoursPerDay;
    endings_ = reachableForms(tables);
    return {};
  }

  [[nodiscard]] core::Result<Metrics> run(std::uint64_t seed) const override {
    ruleset::Random random{seed};
    evolution::EvolutionState state;
    evolution::EvolutionInputs inputs{.species = start_};
    std::int64_t hoursLeft = hours_;
    std::int32_t evolutions = 0;
    while (hoursLeft > 0) {
      trainLevel(inputs, random);
      // The counter runs on through the hours left; the check replays them and evolves at the first
      // that passes.
      inputs.evolutionTimer = static_cast<std::int32_t>(
          std::min<std::int64_t>(hoursLeft, std::numeric_limits<std::int32_t>::max()));
      const evolution::NaturalCheck check =
          evolution_->checkNatural(inputs, state, evolution::CheckSpan::hourly(hoursLeft), random);
      if (!check.evolution) {
        break;
      }
      const evolution::EvolutionResult& result = *check.evolution;
      evolution::recordEvolution(state, result);
      hoursLeft = check.evolutionTimer;
      inputs.species = result.target;
      inputs.stats = statsAfter(inputs.stats, result);
      inputs.weight = result.weight;
      inputs.careMistakes = result.careMistakes;
      inputs.battles = result.battles;
      ++evolutions;
    }
    Metrics metrics{
        {.name = std::string{LevelMetric},
         .value = static_cast<double>(evolution_->tables().levelOf(inputs.species))},
        {.name = std::string{EvolutionsMetric}, .value = static_cast<double>(evolutions)},
    };
    for (const evolution::Species form : endings_) {
      metrics.push_back({.name = std::string{evolution_->tables().idOf(form)},
                         .value = form == inputs.species ? Ended : NotEnded});
    }
    return metrics;
  }

private:
  // A level of random care: training gains on top of the stats, and the rest drawn anew.
  void trainLevel(evolution::EvolutionInputs& inputs, ruleset::Random& random) const {
    evolution::BaseStats& stats = inputs.stats;
    stats.hp += random.below(maxStat_ * HpMpScale);
    stats.mp += random.below(maxStat_ * HpMpScale);
    stats.offense += random.below(maxStat_);
    stats.defense += random.below(maxStat_);
    stats.speed += random.below(maxStat_);
    stats.brains += random.below(maxStat_);
    inputs.weight = random.below(MaxWeight);
    inputs.careMistakes = random.below(MaxCareMistakes);
    inputs.discipline = random.below(MaxPercent);
    inputs.happiness = random.below(HappinessRange) - HappinessOffset;
    inputs.battles = random.below(MaxBattles);
    inputs.learnedMoves += random.below(MaxMovesPerLevel);
  }

  // The forms a raising from the start can end in: the start, the charts' targets, the baby targets
  // and the fallback, followed from the start. The random care never fills the virus bar. Sorted by
  // index, so every run gives its metrics in one order.
  [[nodiscard]] std::vector<evolution::Species>
  reachableForms(const evolution::EvolutionTables& tables) const {
    const evolution::EvolutionRules& rules = evolution_->rules();
    std::set<evolution::Species> reached{start_};
    std::vector<evolution::Species> pending{start_};
    const auto reach = [&](std::optional<evolution::Species> form) {
      if (form && reached.insert(*form).second) {
        pending.push_back(*form);
      }
    };
    while (!pending.empty()) {
      const evolution::Species form = pending.back();
      pending.pop_back();
      // The selection never reads the chart of an ultimate (one of them names the main character).
      if (tables.levelOf(form) != evolution::Level::Ultimate) {
        for (const evolution::Species target : tables.pathTargets(form)) {
          reach(target);
        }
      }
      if (const auto baby = rules.babyTargets.find(tables.idOf(form));
          baby != rules.babyTargets.end()) {
        reach(tables.find(baby->second));
      }
      if (tables.levelOf(form) == evolution::Level::Rookie) {
        reach(tables.find(rules.fallback.rookieTarget));
      }
    }
    return {reached.begin(), reached.end()};
  }

  std::unique_ptr<evolution::Evolution> evolution_;
  evolution::Species start_ = 0;
  std::int32_t maxStat_ = DefaultMaxStat;
  std::int64_t hours_ = 0;
  std::vector<evolution::Species> endings_;
};

const ScenarioRegistration registration{{
    .name = "raisings",
    .description =
        "Raises a partner from --param from=<form> for --days days with random care and counts the form "
        "each raising ends in.",
    .create = [] { return std::make_unique<RaisingsScenario>(); },
}};

} // namespace

} // namespace dwe::sim
