#include <dwe/game/shop_job/shop_job.hpp>

#include <algorithm>
#include <cstddef>
#include <numeric>

namespace dwe::game::shop_job {

namespace {

constexpr std::int64_t Percent = 100;

// The weights after every budget moved `shift` levels.
BudgetWeights shifted(const BudgetWeights& weights, std::int32_t shift) {
  BudgetWeights moved{};
  for (std::size_t budget = 0; budget < weights.size(); ++budget) {
    const Level to = shiftBudget(static_cast<Level>(budget), shift);
    moved.at(static_cast<std::size_t>(to)) += weights.at(budget);
  }
  return moved;
}

} // namespace

Haggle::Haggle(const ShopJobRules& rules, const Customer& customer)
    : rules_{&rules}, customer_{customer}, patience_{rules.species.at(customer.species).patience},
      moodShift_{rules.moods.at(customer.mood).shift} {
  known_ = shifted(budgetWeights(rules.species.at(customer.species)), moodShift_);
}

bool Haggle::canTalk() const {
  return !over_ && !talked_ && patience_ >= rules_->talkNeeds;
}

std::int64_t Haggle::total() const {
  return std::accumulate(known_.begin(), known_.end(), std::int64_t{0});
}

std::int32_t Haggle::chance(Level level) const {
  const std::int64_t total = this->total();
  if (total <= 0 || level < 0 || level >= NoSale) {
    return 0;
  }
  std::int64_t buys = 0;
  for (Level budget = 0; budget <= level; ++budget) {
    buys += known_.at(static_cast<std::size_t>(budget));
  }
  // Rounded to the nearest percent.
  return static_cast<std::int32_t>(((buys * Percent) + (total / 2)) / total);
}

Reaction Haggle::predicted(Level level) const {
  std::array<std::int64_t, Reactions> likely{};
  for (std::size_t budget = 0; budget < known_.size(); ++budget) {
    const Reaction reaction = reactionTo(*rules_, level, static_cast<Level>(budget));
    likely.at(static_cast<std::size_t>(reaction)) += known_.at(budget);
  }
  const auto most = std::ranges::max_element(likely);
  return static_cast<Reaction>(std::distance(likely.begin(), most));
}

OfferResult Haggle::offer(Level level) {
  if (over_ || level < 0 || level >= NoSale) {
    return {.reaction = Reaction::Pleased, .sold = false, .profit = 0, .leaves = false};
  }
  const Reaction reaction = reactionTo(*rules_, level, customer_.budget);
  offers_.push_back({.level = level, .reaction = reaction});
  if (level >= customer_.budget) {
    over_ = true;
    return {.reaction = reaction,
            .sold = true,
            .profit = rules_->profit(customer_.item, level),
            .leaves = false};
  }
  // The reaction rules out every budget that would have answered otherwise.
  for (std::size_t budget = 0; budget < known_.size(); ++budget) {
    if (reactionTo(*rules_, level, static_cast<Level>(budget)) != reaction) {
      known_.at(budget) = 0;
    }
  }
  patience_ = std::max(0, patience_ - rules_->rulesOf(reaction).patienceCost);
  over_ = patience_ == 0;
  return {.reaction = reaction, .sold = false, .profit = 0, .leaves = over_};
}

bool Haggle::talk() {
  if (!canTalk()) {
    return false;
  }
  talked_ = true;
  patience_ -= rules_->talkCost;
  moodShift_ += rules_->talkShift;
  customer_.budget = shiftBudget(customer_.budget, rules_->talkShift);
  known_ = shifted(known_, rules_->talkShift);
  return true;
}

void Haggle::dismiss() {
  over_ = true;
}

} // namespace dwe::game::shop_job
