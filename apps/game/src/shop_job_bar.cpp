#include "shop_job_bar.hpp"

#include <algorithm>
#include <utility>

namespace dwe::app {

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
  return {.shown = true,
          .tally = frame.tally,
          .goal = rules_->tallyFrom(rules_->joinsFrom),
          .betterPay = rules_->tallyFrom(rules_->betterPayFrom),
          // Some of the customers' scenes count one more on their way out (the original's 20 can
          // end on 21), so the count shown stops at the job's customers.
          .customer = std::min(frame.customer, rules_->customers),
          .customers = rules_->customers};
}

screens::ScreenSettings settingsOf(const ShopJobBarView& view) {
  return {
      {.name = "shop_job.shown", .value = view.shown},
      {.name = "shop_job.tally", .value = static_cast<double>(view.tally)},
      {.name = "shop_job.goal", .value = static_cast<double>(view.goal)},
      {.name = "shop_job.better_pay", .value = static_cast<double>(view.betterPay)},
      {.name = "shop_job.customer", .value = static_cast<double>(view.customer)},
      {.name = "shop_job.customers", .value = static_cast<double>(view.customers)},
  };
}

} // namespace dwe::app
