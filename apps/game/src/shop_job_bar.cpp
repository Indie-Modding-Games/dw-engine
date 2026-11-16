#include "shop_job_bar.hpp"

#include <dwe/game/shop_job/shop_job.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace dwe::app {

namespace {

namespace shop_job = game::shop_job;

// The pay tiers' names, the bar's fill classes, in the order of shop_job::PayTier.
constexpr std::array<std::string_view, 4> TierNames{"lowest", "better", "joins", "bonus"};
// A bonus tier no tally reaches.
constexpr std::int64_t NoBonus = std::numeric_limits<std::int64_t>::max();

} // namespace

ShopJobBar::ShopJobBar(std::optional<session::ShopJobRules> rules) : rules_(std::move(rules)) {}

ShopJobBarView ShopJobBar::update(const ShopJobFrame& frame) {
  if (!rules_ || frame.map != rules_->map) {
    lastCustomer_.reset();
    running_ = false;
    return {};
  }
  if (frame.customer == 0) {
    // A new job clears the count before its first customer.
    running_ = false;
  } else if (lastCustomer_ && frame.customer != *lastCustomer_ &&
             frame.customer <= rules_->customers) {
    running_ = true;
  }
  lastCustomer_ = frame.customer;
  if (!running_ || !frame.enabled) {
    return {};
  }
  ShopJobBarView view{.shown = true,
                      .tally = frame.tally,
                      .goal = rules_->tallyFrom(rules_->joinsFrom),
                      .betterPay = rules_->tallyFrom(rules_->betterPayFrom),
                      // Some of the original's customers count one more on their way out (its 20
                      // can end on 21), so the count shown stops at the job's customers.
                      .customer = std::min(frame.customer, rules_->customers),
                      .customers = rules_->customers};
  const bool improved = frame.improved && frame.perfectScore > 0;
  view.improved = improved;
  view.top = improved ? frame.perfectScore : view.goal;
  // The bonus mark only where the bar reaches it.
  if (improved && frame.bonus > 0 && frame.bonus <= view.top) {
    view.bonus = frame.bonus;
    view.bonusIcon = frame.bonusIcon;
    view.bonusIconRect = frame.bonusIconRect;
  }
  // The original job has no bonus: its tiers stop at the joining tally.
  const std::int64_t bonus = improved && frame.bonus > 0 ? frame.bonus : NoBonus;
  view.tier = TierNames.at(static_cast<std::size_t>(shop_job::payTierOf(
      {.betterPay = view.betterPay, .joins = view.goal, .bonus = bonus}, frame.tally)));
  return view;
}

screens::ScreenSettings settingsOf(const ShopJobBarView& view) {
  return {
      {.name = "shop_job.shown", .value = view.shown},
      {.name = "shop_job.tally", .value = static_cast<double>(view.tally)},
      {.name = "shop_job.goal", .value = static_cast<double>(view.goal)},
      {.name = "shop_job.better_pay", .value = static_cast<double>(view.betterPay)},
      {.name = "shop_job.customer", .value = static_cast<double>(view.customer)},
      {.name = "shop_job.customers", .value = static_cast<double>(view.customers)},
      {.name = "shop_job.top", .value = static_cast<double>(view.top)},
      {.name = "shop_job.bonus", .value = static_cast<double>(view.bonus)},
      {.name = "shop_job.bonus_icon", .value = view.bonusIcon},
      {.name = "shop_job.bonus_icon_rect", .value = view.bonusIconRect},
      {.name = "shop_job.tier", .value = std::string{view.tier}},
      {.name = "shop_job.improved", .value = view.improved},
  };
}

} // namespace dwe::app
