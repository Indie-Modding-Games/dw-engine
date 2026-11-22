#include <dwe/assets/file_system.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/shop_job/shop_job.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace dwe::game::shop_job;
using dwe::ruleset::Random;

// The pack's species and items, in the rules' order.
constexpr std::size_t Goburimon = 0;
constexpr std::size_t Gotsumon = 1;
constexpr std::size_t Weedmon = 2;
constexpr std::size_t Muchomon = 3;
constexpr std::size_t Medicine = 0;
constexpr std::size_t Potty = 1;
constexpr std::size_t Meat = 2;
constexpr std::size_t Cheerful = 0;
constexpr std::size_t Normal = 1;
constexpr std::size_t Serious = 2;
constexpr Level Regular = 5;
constexpr Level Cheapest = 8;

ShopJobRules packRules() {
  const auto bytes = dwe::assets::DiskFileSystem{}.readFile(std::filesystem::path{DWE_GAME_PACK} /
                                                            "rules" / "original" / "shop_job.yaml");
  REQUIRE(bytes);
  auto node = dwe::core::parseYaml(
      std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()});
  REQUIRE(node);
  auto rules = parseShopJobRules(
      {.name = "shop_job", .owner = "test", .file = "shop_job.yaml", .node = *node});
  REQUIRE_MESSAGE(rules, (rules ? std::string{} : rules.error().message));
  return *rules;
}

Customer customer(std::size_t species, std::size_t item, std::size_t mood, Level budget) {
  return {.species = species, .item = item, .mood = mood, .budget = budget};
}

} // namespace

TEST_CASE("the shop job's rules hold twenty customers and the original's prices") {
  const ShopJobRules rules = packRules();
  CHECK(rules.customers() == 20);
  CHECK(rules.regularLevel == Regular);
  CHECK(rules.profit(Medicine, 0) == 800);
  CHECK(rules.profit(Potty, Regular) == 90);
  CHECK(rules.profit(Meat, Cheapest) == 0);
  CHECK(rules.profit(Meat, NoSale) == 0);
  CHECK(rules.bonusFrom == 3584);
  CHECK(rules.bonusItem == "game:steel_heart");
}

TEST_CASE("a species' budget weights come from its acceptance table") {
  const ShopJobRules rules = packRules();
  CHECK(budgetWeights(rules.species[Gotsumon]) == BudgetWeights{1, 9, 20, 20, 20, 20, 2, 2, 2, 4});
  CHECK(budgetWeights(rules.species[Goburimon]).back() == 0);
  CHECK(budgetWeights(rules.species[Muchomon]).back() == 15);
}

TEST_CASE("a budget moves up and down between the dearest level and buying nothing") {
  CHECK(shiftBudget(3, 1) == 2);
  CHECK(shiftBudget(0, 1) == 0);
  CHECK(shiftBudget(NoSale, 1) == Cheapest);
  CHECK(shiftBudget(NoSale, -1) == NoSale);
  CHECK(shiftBudget(Cheapest, -1) == NoSale);
  CHECK(shiftBudget(7, -1) == Cheapest);
}

TEST_CASE("the reaction says how far the offer was from the budget") {
  const ShopJobRules rules = packRules();
  CHECK(reactionTo(rules, 5, 3) == Reaction::Delighted);
  CHECK(reactionTo(rules, 4, 3) == Reaction::Pleased);
  CHECK(reactionTo(rules, 3, 3) == Reaction::Pleased);
  CHECK(reactionTo(rules, 2, 3) == Reaction::Doubtful);
  CHECK(reactionTo(rules, 1, 3) == Reaction::Annoyed);
  CHECK(reactionTo(rules, 0, 3) == Reaction::Furious);
  // Someone who buys nothing is furious at any price.
  CHECK(reactionTo(rules, Cheapest, NoSale) == Reaction::Furious);
}

TEST_CASE("every job's line lets a perfect player make 4000 to 4300") {
  const ShopJobRules rules = packRules();
  constexpr std::uint64_t Jobs = 500;
  for (std::uint64_t job = 0; job < Jobs; ++job) {
    Random random{lineupSeed(12345, job)};
    const Lineup line = makeLineup(rules, random);
    INFO("job " << job);
    REQUIRE(line.customers.size() == 20);
    CHECK(line.perfectScore >= 4000);
    CHECK(line.perfectScore <= 4300);
    std::int64_t perfect = 0;
    std::map<std::size_t, int> species;
    std::map<std::size_t, int> items;
    for (const Customer& each : line.customers) {
      perfect += perfectProfit(rules, each);
      ++species[each.species];
      ++items[each.item];
    }
    CHECK(perfect == line.perfectScore);
    CHECK(species ==
          std::map<std::size_t, int>{{Goburimon, 7}, {Gotsumon, 6}, {Weedmon, 4}, {Muchomon, 3}});
    CHECK(items == std::map<std::size_t, int>{{Medicine, 4}, {Potty, 10}, {Meat, 6}});
  }
}

TEST_CASE("a job's line is the same for the same playthrough and job, and new for the next job") {
  const ShopJobRules rules = packRules();
  Random first{lineupSeed(77, 3)};
  Random again{lineupSeed(77, 3)};
  Random next{lineupSeed(77, 4)};
  const Lineup line = makeLineup(rules, first);
  CHECK(makeLineup(rules, again).customers == line.customers);
  CHECK(makeLineup(rules, next).customers != line.customers);
}

TEST_CASE("the chance of a sale is the species' acceptance until the customer reacts") {
  const ShopJobRules rules = packRules();
  const Haggle goburimon{rules, customer(Goburimon, Meat, Normal, 2)};
  CHECK(goburimon.chance(0) == 45);
  CHECK(goburimon.chance(Regular) == 95);
  CHECK(goburimon.chance(Cheapest) == 100);
  // A cheerful customer's budget is a level dearer: the chance of a level is the next one's.
  const Haggle cheerful{rules, customer(Gotsumon, Meat, Cheerful, 2)};
  CHECK(cheerful.chance(1) == 30);
  const Haggle serious{rules, customer(Weedmon, Meat, Serious, 6)};
  CHECK(serious.chance(Regular) == 50);
  CHECK(serious.chance(Cheapest) == 90);
}

TEST_CASE("a refusal rules out the budgets that would have answered otherwise") {
  const ShopJobRules rules = packRules();
  Haggle gotsumon{rules, customer(Gotsumon, Potty, Normal, 3)};
  CHECK(gotsumon.predicted(0) == Reaction::Furious);
  const OfferResult refused = gotsumon.offer(0);
  CHECK(refused.reaction == Reaction::Furious);
  CHECK_FALSE(refused.sold);
  CHECK_FALSE(refused.leaves);
  CHECK(gotsumon.patience() == 2);
  // Furious at +50 %: the budget is +20 % or cheaper, or none (70 of the 100 points).
  CHECK(gotsumon.chance(2) == 0);
  CHECK(gotsumon.chance(3) == 29);
  CHECK(gotsumon.chance(Regular) == 86);
  CHECK(gotsumon.predicted(3) == Reaction::Pleased);
  const OfferResult sold = gotsumon.offer(3);
  CHECK(sold.sold);
  CHECK(sold.reaction == Reaction::Pleased);
  CHECK(sold.profit == 150);
  CHECK(gotsumon.over());
  CHECK(gotsumon.offers().size() == 2);
}

TEST_CASE("an offer well under the budget delights and still sells at the offer") {
  const ShopJobRules rules = packRules();
  Haggle haggle{rules, customer(Goburimon, Medicine, Normal, 1)};
  const OfferResult result = haggle.offer(4);
  CHECK(result.reaction == Reaction::Delighted);
  CHECK(result.sold);
  CHECK(result.profit == 400);
}

TEST_CASE("talking raises the budget once for patience") {
  const ShopJobRules rules = packRules();
  Haggle haggle{rules, customer(Goburimon, Potty, Normal, 1)};
  CHECK(haggle.canTalk());
  CHECK(haggle.talk());
  CHECK(haggle.patience() == 3);
  CHECK(haggle.mood() == 1);
  CHECK_FALSE(haggle.canTalk());
  CHECK_FALSE(haggle.talk());
  CHECK(haggle.chance(Regular) == 97);
  CHECK(haggle.offer(0).sold);
}

TEST_CASE("a customer out of patience leaves without buying") {
  const ShopJobRules rules = packRules();
  Haggle haggle{rules, customer(Muchomon, Meat, Normal, NoSale)};
  CHECK(haggle.offer(Cheapest).reaction == Reaction::Furious);
  const OfferResult last = haggle.offer(Cheapest);
  CHECK(last.leaves);
  CHECK(haggle.over());
  CHECK(haggle.patience() == 0);
  CHECK_FALSE(haggle.offer(Cheapest).sold);
}

TEST_CASE("the job pays by the tally's tier") {
  const PayTiers tiers{.betterPay = 1792, .joins = 3072, .bonus = 3584};
  CHECK(payTierOf(tiers, 1791) == PayTier::Lowest);
  CHECK(payTierOf(tiers, 1792) == PayTier::Better);
  CHECK(payTierOf(tiers, 3072) == PayTier::Joins);
  CHECK(payTierOf(tiers, 3583) == PayTier::Joins);
  CHECK(payTierOf(tiers, 3584) == PayTier::Bonus);
}

// A design simulation, not a regression: it plays generated lines with real Haggle logic and
// reports how often each strategy clears the thresholds. Run it with --tc="*sim*" to read the
// table; it asserts nothing so it never blocks the suite.
TEST_CASE("sim: a risk-taker's win rate over generated lines") {
  const ShopJobRules rules = packRules();
  constexpr std::uint64_t GameSeed = 987654321ULL;
  constexpr std::uint64_t Lines = 4000;

  struct Rate {
    std::int64_t win3100 = 0;
    std::int64_t win3600 = 0;
    std::int64_t win4000 = 0;
    std::int64_t sum = 0;
    std::int64_t minScore = std::numeric_limits<std::int64_t>::max();
    std::int64_t n = 0;
  };

  auto play = [&](auto pick) -> Rate {
    Rate r;
    for (std::uint64_t job = 1; job <= Lines; ++job) {
      Random random{lineupSeed(GameSeed, job)};
      const Lineup line = makeLineup(rules, random);
      std::int64_t tally = 0;
      for (const Customer& each : line.customers) {
        Haggle haggle{rules, each};
        haggle.talk();
        for (int guard = 0; guard < 64 && !haggle.over(); ++guard) {
          const OfferResult result = haggle.offer(pick(rules, haggle, each.item));
          tally += result.profit;
        }
      }
      ++r.n;
      r.sum += tally;
      r.minScore = std::min(r.minScore, tally);
      if (tally > 3100) {
        ++r.win3100;
      }
      if (tally >= 3600) {
        ++r.win3600;
      }
      if (tally >= 4000) {
        ++r.win4000;
      }
    }
    return r;
  };

  const auto report = [](const std::string& name, const Rate& r) {
    std::cout << name << std::string(8 - std::min<std::size_t>(name.size(), 8), ' ')
              << " >3100:" << static_cast<int>(100.0 * static_cast<double>(r.win3100) / static_cast<double>(r.n))
              << "%  >=3600:"
              << static_cast<int>(100.0 * static_cast<double>(r.win3600) / static_cast<double>(r.n))
              << "%  >=4000:"
              << static_cast<int>(100.0 * static_cast<double>(r.win4000) / static_cast<double>(r.n))
              << "%  mean:" << r.sum / r.n << "  min:" << r.minScore << "\n";
  };

  std::cout << "shop_job sim, " << Lines << " lines (talk every customer first):\n";

  // The risk-taker: the dearest price the on-screen odds still make look likely enough to try.
  // A lower bar gambles on a dearer price; a higher bar plays safe and sells cheap.
  for (const int threshold : {20, 30, 40, 50, 60, 70, 80, 95}) {
    report("T=" + std::to_string(threshold),
           play([threshold](const ShopJobRules&, Haggle& haggle, std::size_t) -> Level {
             for (Level level = 0; level < NoSale; ++level) {
               if (haggle.chance(level) >= threshold) {
                 return level;
               }
             }
             return NoSale - 1;
           }));
  }

  // The calculated gambler: highest expected profit, chance times price, on every turn.
  report("EV",
         play([](const ShopJobRules& rules, Haggle& haggle, std::size_t item) -> Level {
           Level best = NoSale - 1;
           std::int64_t bestValue = -1;
           for (Level level = 0; level < NoSale; ++level) {
             const std::int64_t value = static_cast<std::int64_t>(haggle.chance(level)) *
                                        rules.profit(item, level);
             if (value > bestValue) {
               bestValue = value;
               best = level;
             }
           }
           return best;
         }));

  // The ceiling a perfect player reaches: the line's own perfect score.
  Rate perfect;
  for (std::uint64_t job = 1; job <= Lines; ++job) {
    Random random{lineupSeed(GameSeed, job)};
    const Lineup line = makeLineup(rules, random);
    ++perfect.n;
    perfect.sum += line.perfectScore;
    perfect.minScore = std::min(perfect.minScore, line.perfectScore);
    if (line.perfectScore > 3100) {
      ++perfect.win3100;
    }
    if (line.perfectScore >= 3600) {
      ++perfect.win3600;
    }
    if (line.perfectScore >= 4000) {
      ++perfect.win4000;
    }
  }
  report("perfect", perfect);
}
