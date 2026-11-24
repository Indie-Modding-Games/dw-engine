#pragma once

#include <dwe/core/error.hpp>
#include <dwe/ruleset/random.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The canyon shop's job, improved (an engine improvement, the option shop_job.mode; the original's
// job is the story's scripts 49 and 199 to 202, docs/reference/shop-job.md): a fixed line of
// customers made at the job's start, each with a hidden budget drawn from the original's acceptance
// table of its species; the player offers one of nine exact prices, and the customer's reaction
// tells how far the offer was from the budget. It draws nothing and knows no story: the session
// asks it and writes the results into the story's variables.
namespace dwe::game::shop_job {

inline constexpr std::string_view ShopJobSectionName = "shop_job";

// The price levels, from the dearest (0, +50 %) to the cheapest (8, -30 %), as the original's
// menus have them (pstat 117: 1 to 9).
inline constexpr std::size_t Levels = 9;
using Level = std::int32_t;
// The budget of a customer who buys nothing at any price.
inline constexpr Level NoSale = static_cast<Level>(Levels);
// A budget distribution: the weight of each level and, last, of NoSale.
using BudgetWeights = std::array<std::int64_t, Levels + 1>;

// How a customer answers an offer: delighted or pleased buy, the rest refuse and cost patience.
enum class Reaction : std::uint8_t { Delighted, Pleased, Doubtful, Annoyed, Furious };
inline constexpr std::size_t Reactions = 5;

struct SpeciesRules {
  // The customer's story script (199 to 202) and species number.
  std::int32_t script = 0;
  std::int32_t species = 0;
  // How many of the job's customers are of this species.
  std::int32_t count = 0;
  std::int32_t patience = 0;
  // The text key's last part of the species' character (shop_job.character.<character>).
  std::string character;
  // The original's accept percent at each level (199-202: the store_random(110, 99) <= X rolls).
  std::array<std::int32_t, Levels> acceptance{};
};

struct ItemRules {
  // The item's id in the items table.
  std::string id;
  std::int32_t count = 0;
  // The price at each level; what a sale adds to the tally is the price minus the cheapest one.
  std::array<std::int32_t, Levels> prices{};
};

struct MoodRules {
  // The text key's last part (shop_job.mood.<name>).
  std::string name;
  std::int32_t weight = 0;
  // How many levels it moves the budget up (dearer).
  std::int32_t shift = 0;
};

struct ReactionRules {
  Reaction reaction = Reaction::Pleased;
  // The text key's last part (shop_job.reaction.<name>) and the mood bubble's face.
  std::string name;
  std::int32_t face = 0;
  // The highest gap (levels the offer is over the budget; negative under it) that gets this
  // reaction; the last reaction takes every larger gap.
  std::int32_t gapUpTo = 0;
  std::int32_t patienceCost = 0;
};

struct ShopJobRules {
  // The story variables a counter turn writes: its result, the offer's level (1 to 9, as the
  // original's pstat 117) and the reaction's face (the pstat 248 routine 28 reads).
  std::int32_t resultVariable = 0;
  std::int32_t levelVariable = 0;
  std::int32_t faceVariable = 0;
  std::vector<SpeciesRules> species;
  std::vector<ItemRules> items;
  std::vector<MoodRules> moods;
  // In order of their gaps, from the most under the budget.
  std::vector<ReactionRules> reactions;
  // The price level of the regular price (the original's «Keep», pstat 117 = 6).
  Level regularLevel = 0;
  // Talking: how many levels it moves the budget up, what it costs and the patience it needs.
  std::int32_t talkShift = 0;
  std::int32_t talkCost = 0;
  std::int32_t talkNeeds = 0;
  // The perfect score a line of customers must fall in, and how many lines are tried for it.
  std::int64_t perfectLeast = 0;
  std::int64_t perfectMost = 0;
  std::int32_t maxLines = 0;
  // The tally from which the job also gives the bonus item.
  std::int64_t bonusFrom = 0;
  std::string bonusItem;

  [[nodiscard]] std::size_t customers() const;
  // What a sale at the level adds to the tally.
  [[nodiscard]] std::int64_t profit(std::size_t item, Level level) const;
  [[nodiscard]] const ReactionRules& rulesOf(Reaction reaction) const;
};

core::Result<ShopJobRules> parseShopJobRules(const ruleset::Section& section);

// A customer of the line: indices into the rules' species, items and moods, and the hidden budget
// (the dearest level it buys at, NoSale for none) with its arrival mood already applied.
struct Customer {
  std::size_t species = 0;
  std::size_t item = 0;
  std::size_t mood = 0;
  Level budget = NoSale;

  friend bool operator==(const Customer&, const Customer&) = default;
};

struct Lineup {
  std::vector<Customer> customers;
  // The tally of selling each customer at its budget after one talk.
  std::int64_t perfectScore = 0;
};

// The budget weights of a species' acceptance table: P(budget is the level) in percent points.
[[nodiscard]] BudgetWeights budgetWeights(const SpeciesRules& species);
// The budget after moving it `shift` levels up (a negative shift moves it down): a customer who
// buys nothing buys at the cheapest level when moved up, and one at the cheapest buys nothing when
// moved down.
[[nodiscard]] Level shiftBudget(Level budget, std::int32_t shift);
// The weights after every budget moved `shift` levels (shiftBudget).
[[nodiscard]] BudgetWeights shiftWeights(const BudgetWeights& weights, std::int32_t shift);
// The reaction to an offer at `offer` from a customer whose budget is `budget`.
[[nodiscard]] Reaction reactionTo(const ShopJobRules& rules, Level offer, Level budget);
// The best tally a customer can give: one talk, then a sale at its budget.
[[nodiscard]] std::int64_t perfectProfit(const ShopJobRules& rules, const Customer& customer);
// A line of customers: each item's copies spread over the species, then a mood and a budget each.
// Budgets move to neighbours the customer could have had until the perfect score falls in the
// rules' window; a line that still misses it is drawn again (the last one if none fits within
// maxLines).
[[nodiscard]] Lineup makeLineup(const ShopJobRules& rules, ruleset::Random& random);
// The seed of a job's line: a mix of the playthrough's seed and the job's number, never a roll of
// the game's random numbers.
[[nodiscard]] std::uint64_t lineupSeed(std::uint64_t gameSeed, std::uint64_t job);

// One offer the player made and how the customer took it.
struct Offer {
  Level level = 0;
  Reaction reaction = Reaction::Pleased;

  friend bool operator==(const Offer&, const Offer&) = default;
};

// What an offer did.
struct OfferResult {
  Reaction reaction = Reaction::Pleased;
  bool sold = false;
  std::int64_t profit = 0;
  // The customer ran out of patience and leaves without buying.
  bool leaves = false;
};

// The haggle with one customer: what the player knows of its budget (species, mood, talk and the
// reactions so far) and its patience.
class Haggle {
public:
  // The rules must outlive the haggle.
  Haggle(const ShopJobRules& rules, const Customer& customer);

  [[nodiscard]] const Customer& customer() const { return customer_; }
  [[nodiscard]] std::int32_t patience() const { return patience_; }
  [[nodiscard]] std::int32_t mood() const { return moodShift_; }
  [[nodiscard]] bool talked() const { return talked_; }
  [[nodiscard]] bool over() const { return over_; }
  [[nodiscard]] const std::vector<Offer>& offers() const { return offers_; }
  [[nodiscard]] bool canTalk() const;

  // The chance in percent (0 to 100) that the customer buys at the level, from what the player
  // knows.
  [[nodiscard]] std::int32_t chance(Level level) const;
  // The reaction an offer at the level most likely gets.
  [[nodiscard]] Reaction predicted(Level level) const;

  OfferResult offer(Level level);
  // Raises the mood one step (the budget moves up) for patience; nothing when it cannot talk.
  bool talk();
  // Sends the customer away: no sale, no anger.
  void dismiss();

private:
  [[nodiscard]] std::int64_t total() const;

  const ShopJobRules* rules_;
  Customer customer_;
  BudgetWeights known_{};
  std::int32_t patience_ = 0;
  std::int32_t moodShift_ = 0;
  bool talked_ = false;
  bool over_ = false;
  std::vector<Offer> offers_;
};

// The pay tiers of the job's end, by tally: the original's three and the bonus item over them.
struct PayTiers {
  std::int64_t betterPay = 0;
  std::int64_t joins = 0;
  std::int64_t bonus = 0;
};
enum class PayTier : std::uint8_t { Lowest, Better, Joins, Bonus };
[[nodiscard]] PayTier payTierOf(const PayTiers& tiers, std::int64_t tally);

} // namespace dwe::game::shop_job
