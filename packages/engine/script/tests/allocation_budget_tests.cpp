#include <doctest/doctest.h>
#include <sol/sol.hpp>

#include "script_test_support.hpp"

using namespace dwe;
using script::BudgetStop;
using script::testing::errorOf;
using script::testing::makeVm;
using script::testing::SmallLimits;
using script::testing::stopCause;

// Each of these loops runs few instructions but would copy about 100 GB if allocations were free.

TEST_CASE("building big strings in a loop costs budget") {
  auto vm = makeVm(SmallLimits);
  CHECK(stopCause(*vm, "for _ = 1, 100000 do local _ = string.rep('x', 1 << 20) end") ==
        BudgetStop::Instructions);
  CHECK(stopCause(*vm,
                  "local part = string.rep('x', 1 << 20)"
                  " for _ = 1, 100000 do local _ = part .. 'y' end") == BudgetStop::Instructions);
  CHECK(stopCause(*vm, "local parts = {string.rep('x', 1 << 19), string.rep('y', 1 << 19)}"
                       " for _ = 1, 100000 do local _ = table.concat(parts) end") ==
        BudgetStop::Instructions);
}

TEST_CASE("pcall does not hide the cost of allocations") {
  auto vm = makeVm(SmallLimits);
  CHECK(stopCause(*vm, "for _ = 1, 100000 do pcall(string.rep, 'x', 1 << 20) end") ==
        BudgetStop::Instructions);
}

TEST_CASE("a coroutine pays for its allocations from the same budget") {
  auto vm = makeVm(SmallLimits);
  CHECK(stopCause(*vm, "local build = coroutine.wrap(function()"
                       "   for _ = 1, 100000 do local _ = string.rep('x', 1 << 20) end"
                       " end)"
                       " build()") == BudgetStop::Instructions);
  CHECK(stopCause(*vm, "local build = coroutine.create(function()"
                       "   for _ = 1, 100000 do pcall(string.rep, 'x', 1 << 20) end"
                       " end)"
                       " while coroutine.status(build) ~= 'dead' do coroutine.resume(build) end") ==
        BudgetStop::Instructions);
}

TEST_CASE("allocations within the budget work, and the next call gets a full budget") {
  auto vm = makeVm(SmallLimits);
  const auto built =
      vm->runString("for _ = 1, 10 do local _ = string.rep('x', 1 << 20) end", "test");
  REQUIRE_MESSAGE(built.has_value(), built.error().message);
  CHECK(
      errorOf(vm->runString("for _ = 1, 100000 do local _ = string.rep('x', 1 << 20) end", "test"))
          .contains("ran too long"));
  REQUIRE(vm->runString("built = #string.rep('x', 1 << 20)", "test"));
  CHECK(script::ScriptCall{*vm}.lua()["built"].get<int>() == 1 << 20);
}
