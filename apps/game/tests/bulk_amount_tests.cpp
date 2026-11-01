#include <doctest/doctest.h>

#include "bulk_amount.hpp"

using dwe::app::BulkAmount;

TEST_CASE("bulk amount: left and right move by one, up and down by ten, between 1 and the most") {
  BulkAmount amount;
  amount.reset(21);
  CHECK(amount.value == 1);
  CHECK(amount.act("left"));
  CHECK(amount.value == 1);
  CHECK(amount.act("right"));
  CHECK(amount.value == 2);
  CHECK(amount.act("up"));
  CHECK(amount.value == 12);
  CHECK(amount.act("up"));
  CHECK(amount.value == 21);
  CHECK(amount.act("down"));
  CHECK(amount.value == 11);
  CHECK(amount.act("down"));
  CHECK(amount.value == 1);
  CHECK(amount.act("extra"));
  CHECK(amount.value == 21);
  // Confirm and cancel are not the count's.
  CHECK_FALSE(amount.act("confirm"));
  CHECK_FALSE(amount.act("cancel"));
  // A limit of none still keeps one.
  amount.reset(0);
  CHECK(amount.most == 1);
  CHECK(amount.act("up"));
  CHECK(amount.value == 1);
}
