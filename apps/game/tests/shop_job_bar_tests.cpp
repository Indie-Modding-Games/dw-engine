#include <doctest/doctest.h>

#include "shop_job_bar.hpp"

using dwe::app::ShopJobBar;
using dwe::app::ShopJobBarView;
using dwe::app::ShopJobFrame;

namespace {

dwe::session::ShopJobRules canyonShop() {
  return {.map = "gcan11",
          .customersVariable = 123,
          .customers = 20,
          .betterPayFrom = 7,
          .joinsFrom = 12};
}

ShopJobFrame frame(std::string_view map, std::int64_t customer, std::int64_t tally,
                   bool enabled = true) {
  return {.enabled = enabled, .map = map, .customer = customer, .tally = tally};
}

} // namespace

TEST_CASE("the shop job's bar shows from the first customer until the player leaves the map") {
  ShopJobBar bar{canyonShop()};
  CHECK_FALSE(bar.update(frame("gcan09", 0, 0)).shown);
  // In the shop, before the job: the talk and the setup show nothing.
  CHECK_FALSE(bar.update(frame("gcan11", 0, 0)).shown);
  const ShopJobBarView first = bar.update(frame("gcan11", 1, 0));
  CHECK(first.shown);
  CHECK(first.customer == 1);
  CHECK(first.customers == 20);
  CHECK(first.goal == 3072);
  CHECK(first.betterPay == 1792);
  // The values follow the tally and the count.
  const ShopJobBarView later = bar.update(frame("gcan11", 7, 840));
  CHECK(later.shown);
  CHECK(later.tally == 840);
  CHECK(later.customer == 7);
  // The ending (count 20) still shows it; the warp out hides it.
  CHECK(bar.update(frame("gcan11", 20, 3100)).shown);
  // A customer who counts twice on the way out takes the count to 21: it shows 20 of 20.
  const ShopJobBarView past = bar.update(frame("gcan11", 21, 3100));
  CHECK(past.shown);
  CHECK(past.customer == 20);
  CHECK_FALSE(bar.update(frame("gcan09", 20, 3100)).shown);
}

TEST_CASE("a count left over from an earlier job shows nothing until the next job's customer") {
  ShopJobBar bar{canyonShop()};
  // Back in the shop after a lost job: the count still says 20.
  CHECK_FALSE(bar.update(frame("gcan11", 20, 960)).shown);
  CHECK_FALSE(bar.update(frame("gcan11", 20, 960)).shown);
  // The new job clears it, then counts its first customer.
  CHECK_FALSE(bar.update(frame("gcan11", 0, 0)).shown);
  CHECK(bar.update(frame("gcan11", 1, 0)).shown);
}

TEST_CASE("the shop job's bar stays hidden with the option off or without the rule") {
  ShopJobBar bar{canyonShop()};
  CHECK_FALSE(bar.update(frame("gcan11", 0, 0, false)).shown);
  CHECK_FALSE(bar.update(frame("gcan11", 1, 120, false)).shown);
  // Turned on mid-job, it shows at once.
  CHECK(bar.update(frame("gcan11", 1, 120)).shown);
  ShopJobBar none{std::nullopt};
  CHECK_FALSE(none.update(frame("gcan11", 0, 0)).shown);
  CHECK_FALSE(none.update(frame("gcan11", 1, 120)).shown);
}

TEST_CASE("the shop job's settings carry the bar's values") {
  const ShopJobBarView view{.shown = true,
                            .tally = 1500,
                            .goal = 3072,
                            .betterPay = 1792,
                            .customer = 9,
                            .customers = 20};
  const dwe::screens::ScreenSettings settings = dwe::app::settingsOf(view);
  const auto value = [&](std::string_view name) {
    for (const auto& setting : settings) {
      if (setting.name == name) {
        return setting.value;
      }
    }
    FAIL("no setting " << name);
    return dwe::ui::UiValue{};
  };
  CHECK(std::get<bool>(value("shop_job.shown")));
  CHECK(std::get<double>(value("shop_job.tally")) == 1500.0);
  CHECK(std::get<double>(value("shop_job.goal")) == 3072.0);
  CHECK(std::get<double>(value("shop_job.better_pay")) == 1792.0);
  CHECK(std::get<double>(value("shop_job.customer")) == 9.0);
  CHECK(std::get<double>(value("shop_job.customers")) == 20.0);
}
