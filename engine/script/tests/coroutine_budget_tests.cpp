#include <dwe/script/budget_stop.hpp>
#include <dwe/script/script_vm.hpp>

#include <doctest/doctest.h>
#include <lua.hpp>
#include <sol/sol.hpp>

#include <cstdint>
#include <string>

#include "script_test_support.hpp"

// The instruction budget of coroutines, as a runner that resumes them from C++ (one resume per frame) uses them: each
// thread's count hook charges what it ran, across its yields, its resumes and the calls that resume it.
using namespace dwe;
using script::BudgetStop;
using script::testing::checkCeilingStop;
using script::testing::makeVm;
using script::testing::SmallLimits;
using script::testing::stopCause;

namespace {

// Fewer Lua instructions than any loop of these scripts runs between two yields.
constexpr std::int64_t FewestInstructionsPerResume = 3;

// The most resumes a call's instructions pay for.
constexpr std::int64_t MostResumesPerCall =
    static_cast<std::int64_t>(SmallLimits.instructionsPerCall) / FewestInstructionsPerResume;

// Where a test gives up resuming a coroutine the budget failed to stop.
constexpr std::int64_t ResumeCap = 10 * MostResumesPerCall;

// How a thread's resumes ended: the last status, and how many there were.
struct ResumeResult {
    int status = LUA_OK;
    std::int64_t resumes = 0;
};

// Resumes a thread until it stops yielding, or ResumeCap times.
ResumeResult resumeWhileYielding(lua_State* thread, lua_State* resumer) {
    ResumeResult result{.status = LUA_YIELD, .resumes = 0};
    while (result.status == LUA_YIELD && result.resumes < ResumeCap) {
        int values = 0;
        result.status = lua_resume(thread, resumer, 0, &values);
        lua_pop(thread, values);
        ++result.resumes;
    }
    return result;
}

// Makes a thread from C++ that will run the global function `name`, kept in the registry. Gives its reference.
int newThreadRunning(script::ScriptVm& vm, const char* name) {
    const script::ScriptCall call{vm};
    lua_State* main = call.lua().lua_state();
    lua_State* thread = lua_newthread(main);
    lua_getglobal(thread, name);
    return luaL_ref(main, LUA_REGISTRYINDEX);
}

lua_State* threadOf(const script::ScriptCall& call, int reference) {
    lua_State* main = call.lua().lua_state();
    lua_rawgeti(main, LUA_REGISTRYINDEX, reference);
    lua_State* thread = lua_tothread(main, -1);
    lua_pop(main, 1);
    return thread;
}

} // namespace

TEST_CASE("a coroutine resumed from C++ keeps its instruction count across its yields and resumes") {
    // Each resume runs a few instructions and yields, long before a check of the budget: were the count restarted on a
    // resume, or charged short, the loop would run on to the time ceiling.
    auto vm = makeVm(SmallLimits);
    REQUIRE(vm->runString("function tick() local n = 0 while true do n = n + 1 coroutine.yield(n) end end", "test"));
    const int reference = newThreadRunning(*vm, "tick");
    {
        const script::ScriptCall call{*vm};
        const ResumeResult result = resumeWhileYielding(threadOf(call, reference), call.lua().lua_state());
        CHECK(result.status == LUA_ERRRUN);
        CHECK(result.resumes <= MostResumesPerCall);
        CHECK(vm->lastBudgetStop() == BudgetStop::Instructions);
        luaL_unref(call.lua().lua_state(), LUA_REGISTRYINDEX, reference);
    }
}

TEST_CASE("a coroutine resumed from C++ runs on the budget of the call that resumes it") {
    // Each step spends most of a call's instructions: three steps in three calls finish; the endless last one stops.
    auto vm = makeVm(SmallLimits);
    REQUIRE(vm->runString("function steps()"
                          "   for _ = 1, 3 do for _ = 1, 600000 do end coroutine.yield() end"
                          "   while true do end"
                          " end",
                          "test"));
    const int reference = newThreadRunning(*vm, "steps");
    constexpr int FinishingSteps = 3;
    for (int step = 0; step < FinishingSteps; ++step) {
        CAPTURE(step);
        const script::ScriptCall call{*vm};
        int values = 0;
        lua_State* thread = threadOf(call, reference);
        CHECK(lua_resume(thread, call.lua().lua_state(), 0, &values) == LUA_YIELD);
        lua_pop(thread, values);
        CHECK(vm->lastBudgetStop() == BudgetStop::None);
    }
    {
        const script::ScriptCall call{*vm};
        int values = 0;
        CHECK(lua_resume(threadOf(call, reference), call.lua().lua_state(), 0, &values) == LUA_ERRRUN);
        CHECK(vm->lastBudgetStop() == BudgetStop::Instructions);
        luaL_unref(call.lua().lua_state(), LUA_REGISTRYINDEX, reference);
    }
}

TEST_CASE("a coroutine that yields at once on every resume from Lua is stopped by its instructions") {
    auto vm = makeVm(SmallLimits);
    CHECK(stopCause(*vm, "local step = coroutine.wrap(function() while true do coroutine.yield() end end)"
                         " while true do step() end") == BudgetStop::Instructions);
    CHECK(stopCause(*vm, "local co = coroutine.create(function() while true do coroutine.yield() end end)"
                         " while true do coroutine.resume(co) end") == BudgetStop::Instructions);
}

TEST_CASE("the budget's error says why it stopped the script") {
    auto vm = makeVm(SmallLimits);
    const std::string spent = script::testing::errorOf(vm->runString("while true do end", "test"));
    CHECK(spent.contains("ran too long and was stopped: it ran all the instructions of its budget"));
    auto late = makeVm(script::testing::ShortDeadline);
    const std::string ceiling = checkCeilingStop(*late, "local big = ('x'):rep(3 << 20)"
                                                        " local other = ('x'):rep((3 << 20) - 1) .. 'y'"
                                                        " while true do local _ = big < other end");
    CHECK(ceiling.contains("ran too long and was stopped: it reached the time ceiling of its budget"));
    REQUIRE(late->runString("local _ = 1", "test"));
    CHECK(late->lastBudgetStop() == BudgetStop::None);
}
