#include <dwe/game/shop_job/shop_job.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <numeric>
#include <optional>
#include <utility>
#include <vector>

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

// The species and item of one customer, before its mood and budget.
struct Pair {
  std::size_t species = 0;
  std::size_t item = 0;
};

// How many of each item the species ask for, spread as evenly as the species' capacities allow:
// each item's copies go to the species that has asked for it fewest so far (a tie broken at
// random), so no species hoards an expensive item and none is left without one the bag has. The
// rows are each species' count and the columns each item's, like the bags this replaces.
std::vector<Pair> assignPairs(const ShopJobRules& rules, ruleset::Random& random) {
  const std::size_t speciesCount = rules.species.size();
  const std::size_t itemCount = rules.items.size();
  std::vector<std::int32_t> room(speciesCount);
  for (std::size_t index = 0; index < speciesCount; ++index) {
    room[index] = std::max(rules.species[index].count, 0);
  }
  std::vector<std::int32_t> given(speciesCount * itemCount, 0);
  const auto at = [itemCount](std::size_t species, std::size_t item) {
    return species * itemCount + item;
  };
  std::vector<std::size_t> order(itemCount);
  std::iota(order.begin(), order.end(), 0);
  shuffle(order, random);
  for (const std::size_t item : order) {
    const std::int32_t toGive = std::max(rules.items[item].count, 0);
    for (std::int32_t copy = 0; copy < toGive; ++copy) {
      // The species with room that has asked for this item the least; a tie is broken at random.
      std::vector<std::size_t> best;
      std::int32_t least = std::numeric_limits<std::int32_t>::max();
      for (std::size_t species = 0; species < speciesCount; ++species) {
        if (room[species] <= 0) {
          continue;
        }
        const std::int32_t has = given[at(species, item)];
        if (has < least) {
          least = has;
          best.clear();
          best.push_back(species);
        } else if (has == least) {
          best.push_back(species);
        }
      }
      const std::size_t species =
          best[static_cast<std::size_t>(random.below(static_cast<std::int32_t>(best.size())))];
      --room[species];
      ++given[at(species, item)];
    }
  }
  std::vector<Pair> pairs;
  for (std::size_t species = 0; species < speciesCount; ++species) {
    for (std::size_t item = 0; item < itemCount; ++item) {
      for (std::int32_t copy = 0; copy < given[at(species, item)]; ++copy) {
        pairs.push_back({species, item});
      }
    }
  }
  shuffle(pairs, random);
  return pairs;
}

std::size_t customersOf(const std::vector<Pair>& pairs) {
  return pairs.size();
}

// The distribution the even fill guarantees: every item the bag carries reaches the counter at
// least once, so a line shows the whole shelf instead of quietly dropping an item type. With
// assignPairs this always holds; the check is the second metric makeLineup keeps, the one the
// old single total could not see (a line could pass on its sum yet hide an item type).
bool distributionIsComplete(const ShopJobRules& rules, const std::vector<Customer>& line) {
  for (std::size_t item = 0; item < rules.items.size(); ++item) {
    if (rules.items[item].count <= 0) {
      continue;
    }
    const bool present = std::ranges::any_of(line, [item](const Customer& customer) {
      return customer.item == item;
    });
    if (!present) {
      return false;
    }
  }
  return true;
}

// The move of one customer's budget to the neighbour (in perfect profit) that shifts the line's
// total toward the window the least: `wantUp` raises it, `wantDown` lowers it. The budgets the
// species' table would never give are reached only when the window needs it (the small bias the
// guarantee accepts). Returns the new budget and the profit it adds (positive up, negative down),
// or none when no move in that direction exists.
std::optional<std::pair<Level, std::int64_t>> stepToward(const ShopJobRules& rules,
                                                         const Customer& customer, bool wantUp,
                                                         ruleset::Random& random) {
  const std::int64_t now = perfectProfit(rules, customer);
  std::int64_t bestDelta = 0;
  std::vector<Level> candidates;
  for (std::size_t level = 0; level <= NoSale; ++level) {
    Customer moved = customer;
    moved.budget = static_cast<Level>(level);
    const std::int64_t delta = perfectProfit(rules, moved) - now;
    if (delta == 0 || (wantUp ? delta < 0 : delta > 0)) {
      continue;
    }
    const std::int64_t size = std::abs(delta);
    if (candidates.empty() || size < std::abs(bestDelta)) {
      bestDelta = delta;
      candidates.clear();
      candidates.push_back(moved.budget);
    } else if (size == std::abs(bestDelta)) {
      candidates.push_back(moved.budget);
    }
  }
  if (candidates.empty()) {
    return std::nullopt;
  }
  const Level chosen =
      candidates[static_cast<std::size_t>(random.below(static_cast<std::int32_t>(candidates.size())))];
  return std::make_pair(chosen, bestDelta);
}

// Draws the line so its perfect score always lands in the window: the pairs are spread evenly,
// each customer's mood and budget come from the rules as before, and the score is then repaired
// with the smallest moves until it is inside. The guarantee removes the old risk that a seed ran
// out of tries and shipped a line a perfect player could not finish in the window.
Lineup drawLine(const ShopJobRules& rules, ruleset::Random& random) {
  const std::vector<Pair> pairs = assignPairs(rules, random);
  Lineup line;
  line.customers.reserve(customersOf(pairs));
  for (const Pair& pair : pairs) {
    Customer customer{.species = pair.species, .item = pair.item, .mood = 0, .budget = 0};
    customer.mood = drawMood(rules, random);
    customer.budget = shiftBudget(drawBudget(rules.species[customer.species], random),
                                  rules.moods[customer.mood].shift);
    line.customers.push_back(customer);
  }
  for (const Customer& customer : line.customers) {
    line.perfectScore += perfectProfit(rules, customer);
  }
  // Repair with the smallest moves the window allows, raising or lowering whole customers at a
  // time; a few dozen steps bound the work because the spread is under the whole bag's margin.
  std::vector<std::size_t> order(line.customers.size());
  std::iota(order.begin(), order.end(), 0);
  const std::int32_t guard =
      static_cast<std::int32_t>(line.customers.size()) * (NoSale + 1) + 16;
  for (std::int32_t steps = 0; steps < guard; ++steps) {
    if (line.perfectScore >= rules.perfectLeast && line.perfectScore <= rules.perfectMost) {
      break;
    }
    const bool wantUp = line.perfectScore < rules.perfectLeast;
    shuffle(order, random);
    bool moved = false;
    for (const std::size_t index : order) {
      Customer& customer = line.customers[index];
      const auto step = stepToward(rules, customer, wantUp, random);
      if (!step) {
        continue;
      }
      // Keep the move from overshooting the window when a nearer one exists.
      if (wantUp && line.perfectScore + step->second > rules.perfectMost) {
        continue;
      }
      if (!wantUp && line.perfectScore + step->second < rules.perfectLeast) {
        continue;
      }
      customer.budget = step->first;
      line.perfectScore += step->second;
      moved = true;
      break;
    }
    if (!moved) {
      break;
    }
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
  // The draw already lands in the window and fills the shelf, so this is the safety net the old
  // rejection-only loop was: a line that for any reason fails either metric is drawn again, up to
  // maxLines. In practice it passes on the first draw.
  Lineup line;
  for (std::int32_t tries = 0; tries < rules.maxLines; ++tries) {
    line = drawLine(rules, random);
    if (line.perfectScore >= rules.perfectLeast && line.perfectScore <= rules.perfectMost &&
        distributionIsComplete(rules, line.customers)) {
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
