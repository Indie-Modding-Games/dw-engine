#include <dwe/game/shop_job/shop_job.hpp>

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <utility>

namespace dwe::game::shop_job {

namespace {

constexpr std::int32_t PercentRoll = 100;
// splitmix64 (Steele, Lea and Flood): spreads a seed's bits so close seeds draw unrelated lines.
constexpr std::uint64_t MixIncrement = 0x9E3779B97F4A7C15ULL;
constexpr std::uint64_t MixFirst = 0xBF58476D1CE4E5B9ULL;
constexpr std::uint64_t MixSecond = 0x94D049BB133111EBULL;
constexpr std::uint32_t MixShiftFirst = 30;
constexpr std::uint32_t MixShiftSecond = 27;
constexpr std::uint32_t MixShiftLast = 31;

std::uint64_t mix(std::uint64_t value) {
  value += MixIncrement;
  value = (value ^ (value >> MixShiftFirst)) * MixFirst;
  value = (value ^ (value >> MixShiftSecond)) * MixSecond;
  return value ^ (value >> MixShiftLast);
}

template <typename T>
void shuffle(std::vector<T>& values, ruleset::Random& random) {
  for (std::size_t index = values.size(); index > 1; --index) {
    const auto other = static_cast<std::size_t>(random.below(static_cast<std::int32_t>(index)));
    std::swap(values[index - 1], values[other]);
  }
}

template <typename Entries>
std::vector<std::size_t> bagOf(const Entries& entries) {
  std::vector<std::size_t> bag;
  for (std::size_t index = 0; index < entries.size(); ++index) {
    bag.insert(bag.end(), static_cast<std::size_t>(std::max(entries[index].count, 0)), index);
  }
  return bag;
}

std::size_t drawMood(const ShopJobRules& rules, ruleset::Random& random) {
  const std::int32_t total =
      std::accumulate(rules.moods.begin(), rules.moods.end(), 0,
                      [](std::int32_t sum, const MoodRules& mood) { return sum + mood.weight; });
  std::int32_t roll = random.below(total);
  for (std::size_t index = 0; index < rules.moods.size(); ++index) {
    if (roll < rules.moods[index].weight) {
      return index;
    }
    roll -= rules.moods[index].weight;
  }
  return rules.moods.size() - 1;
}

// The dearest level whose accept percent covers the roll (199-202: store_random(110, 99) <= X).
Level drawBudget(const SpeciesRules& species, ruleset::Random& random) {
  const std::int32_t roll = random.below(PercentRoll);
  for (std::size_t level = 0; level < Levels; ++level) {
    if (roll < species.acceptance.at(level)) {
      return static_cast<Level>(level);
    }
  }
  return NoSale;
}

Lineup drawLine(const ShopJobRules& rules, ruleset::Random& random) {
  std::vector<std::size_t> species = bagOf(rules.species);
  std::vector<std::size_t> items = bagOf(rules.items);
  shuffle(species, random);
  shuffle(items, random);
  Lineup line;
  for (std::size_t index = 0; index < species.size() && index < items.size(); ++index) {
    Customer customer{.species = species[index], .item = items[index], .mood = 0, .budget = 0};
    customer.mood = drawMood(rules, random);
    customer.budget = shiftBudget(drawBudget(rules.species[customer.species], random),
                                  rules.moods[customer.mood].shift);
    line.perfectScore += perfectProfit(rules, customer);
    line.customers.push_back(customer);
  }
  return line;
}

} // namespace

BudgetWeights budgetWeights(const SpeciesRules& species) {
  BudgetWeights weights{};
  std::int32_t below = 0;
  for (std::size_t level = 0; level < Levels; ++level) {
    weights.at(level) = species.acceptance.at(level) - below;
    below = species.acceptance.at(level);
  }
  weights.back() = PercentRoll - below;
  return weights;
}

Level shiftBudget(Level budget, std::int32_t shift) {
  constexpr Level Cheapest = NoSale - 1;
  if (budget == NoSale) {
    return shift > 0 ? Cheapest : NoSale;
  }
  if (budget == Cheapest && shift < 0) {
    return NoSale;
  }
  return std::clamp(budget - shift, Level{0}, Cheapest);
}

Reaction reactionTo(const ShopJobRules& rules, Level offer, Level budget) {
  if (budget == NoSale) {
    return rules.reactions.back().reaction;
  }
  const std::int32_t gap = budget - offer;
  for (const ReactionRules& reaction : rules.reactions) {
    if (gap <= reaction.gapUpTo) {
      return reaction.reaction;
    }
  }
  return rules.reactions.back().reaction;
}

std::int64_t perfectProfit(const ShopJobRules& rules, const Customer& customer) {
  const SpeciesRules& species = rules.species.at(customer.species);
  const Level budget = species.patience >= rules.talkNeeds
                           ? shiftBudget(customer.budget, rules.talkShift)
                           : customer.budget;
  return rules.profit(customer.item, budget);
}

Lineup makeLineup(const ShopJobRules& rules, ruleset::Random& random) {
  Lineup line;
  for (std::int32_t tries = 0; tries < rules.maxLines; ++tries) {
    line = drawLine(rules, random);
    if (line.perfectScore >= rules.perfectLeast && line.perfectScore <= rules.perfectMost) {
      break;
    }
  }
  return line;
}

std::uint64_t lineupSeed(std::uint64_t gameSeed, std::uint64_t job) {
  return mix(mix(gameSeed) ^ job);
}

PayTier payTierOf(const PayTiers& tiers, std::int64_t tally) {
  if (tally >= tiers.bonus) {
    return PayTier::Bonus;
  }
  if (tally >= tiers.joins) {
    return PayTier::Joins;
  }
  return tally >= tiers.betterPay ? PayTier::Better : PayTier::Lowest;
}

} // namespace dwe::game::shop_job
