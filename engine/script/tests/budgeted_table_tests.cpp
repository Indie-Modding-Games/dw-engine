#include <doctest/doctest.h>
#include <sol/sol.hpp>

#include <string>
#include <string_view>

#include "script_test_support.hpp"

using namespace dwe;
using script::testing::errorOf;
using script::testing::makeVm;
using script::testing::SmallLimits;

TEST_CASE("table.insert and table.remove work as Lua's own") {
    auto vm = makeVm();
    REQUIRE(
        vm->runString("local t = {1, 2, 3}"
                      " table.insert(t, 4) table.insert(t, 1, 0) table.insert(t, 3, 1.5)"
                      " local removed = table.remove(t, 1) local last = table.remove(t)"
                      " local fromEmpty = table.remove({})"
                      " shown = table.concat(t, ' ') .. ' | ' .. removed .. ' ' .. last .. ' ' .. tostring(fromEmpty)",
                      "test"));
    CHECK(script::ScriptCall{*vm}.lua()["shown"].get<std::string>() == "1 1.5 2 3 | 0 4 nil");
}

TEST_CASE("table.insert and table.remove keep Lua's errors") {
    auto vm = makeVm();
    CHECK(errorOf(vm->runString("table.insert({1}, 5, 'x')", "test")).contains("position out of bounds"));
    CHECK(errorOf(vm->runString("table.insert({1}, 1, 2, 3)", "test")).contains("wrong number of arguments"));
    CHECK(errorOf(vm->runString("table.remove({1}, 5)", "test")).contains("position out of bounds"));
    CHECK(errorOf(vm->runString("table.insert(1, 2)", "test")).contains("table expected"));
}

TEST_CASE("a table whose length lies cannot keep table.insert or table.remove running") {
    auto vm = makeVm(SmallLimits);
    REQUIRE(vm->runString("huge = setmetatable({}, {__len = function() return math.maxinteger // 2 end})", "test"));
    CHECK(errorOf(vm->runString("table.insert(huge, 1, 'x')", "test")).contains("ran too long"));
    CHECK(errorOf(vm->runString("table.remove(huge, 1)", "test")).contains("ran too long"));
}

TEST_CASE("table.move charges the budget for what it copies") {
    auto vm = makeVm(SmallLimits);
    CHECK(errorOf(vm->runString("local t = {} while true do table.move(t, 1, 1000000, 2) end", "test"))
              .contains("ran too long"));
}
