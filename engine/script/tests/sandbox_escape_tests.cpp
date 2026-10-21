#include <dwe/script/script_vm.hpp>

#include <doctest/doctest.h>
#include <sol/sol.hpp>

#include <chrono>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include "script_test_support.hpp"

// The ways a script could try to escape its call's budget, the shared tables or its pack, found in review.
using namespace dwe;
using script::ApiCall;
using script::ApiValue;
using script::testing::DeadlineStop;
using script::testing::environmentOf;
using script::testing::errorOf;
using script::testing::LogCapture;
using script::testing::makeVm;
using script::testing::MemoryPacks;
using script::testing::publishReport;
using script::testing::QuickStop;
using script::testing::ShortDeadline;
using script::testing::SmallLimits;
using script::testing::stopTime;
using script::testing::TestDeadline;

namespace {

constexpr std::size_t MebiByte = std::size_t{1024} * 1024;

} // namespace

TEST_CASE("finalizers and error handlers that loop are stopped or refused") {
    auto vm = makeVm(SmallLimits);
    auto mod = environmentOf(*vm, "my_mod");
    CHECK(errorOf(mod.run("setmetatable({}, {__gc = function() while true do end end})", "=my_mod")) ==
          "my_mod:1: setmetatable: scripts cannot use __gc, since finalizers run outside their calls and their budget");
    CHECK(errorOf(vm->runString("setmetatable({}, {__gc = print})", "=test")).contains("cannot use __gc"));
    CHECK(mod.run("local plain = setmetatable({}, {__index = {}})", "=my_mod"));
    CHECK(errorOf(mod.run("do local spin <close> = setmetatable({}, {__close = function() while true do end end}) end",
                          "=my_mod"))
              .contains("ran too long"));
    CHECK(errorOf(mod.run("xpcall(error, function() while true do end end) while true do end", "=my_mod"))
              .contains("ran too long"));
}

TEST_CASE("a coroutine may yield inside xpcall") {
    auto vm = makeVm(SmallLimits);
    auto& reported = publishReport(*vm);
    auto mod = environmentOf(*vm, "my_mod");
    REQUIRE(mod.run("local steps = coroutine.wrap(function()"
                    "   test.report(tostring(xpcall(function() coroutine.yield('inside') return 'done' end, print)))"
                    "   local ok, message = xpcall(function() coroutine.yield('again') error('failed', 0) end,"
                    "                              function(text) return 'handled ' .. text end)"
                    "   test.report(tostring(ok) .. ' ' .. message)"
                    "   return 'end' end)"
                    " test.report(steps()) test.report(steps()) test.report(steps())",
                    "=my_mod"));
    CHECK(reported == std::vector<std::string>{"inside", "true", "again", "false handled failed", "end"});
}

TEST_CASE("an error value whose __tostring loops is not run by the engine") {
    auto vm = makeVm(SmallLimits);
    auto mod = environmentOf(*vm, "my_mod");
    constexpr std::string_view Source = "error(setmetatable({}, {__tostring = function() while true do end end}))";
    CHECK(errorOf(mod.run(Source, "=my_mod")) == "the error is not a string");
    CHECK_FALSE(vm->runString(Source, "test"));
}

TEST_CASE("string.rep charges for its result before building it") {
    auto vm = makeVm(SmallLimits);
    auto& reported = publishReport(*vm);
    auto mod = environmentOf(*vm, "my_mod");
    REQUIRE(mod.run("test.report(#string.rep('', math.maxinteger)) test.report(#('a'):rep(3, ''))"
                    " test.report(('ab'):rep(3, ','))",
                    "=my_mod"));
    CHECK(reported == std::vector<std::string>{"0", "3", "ab,ab,ab"});
    CHECK(stopTime(*vm, "local _ = string.rep('x', 200000000)") < QuickStop);
    CHECK(errorOf(vm->runString("local _ = ('x'):rep(math.maxinteger, 'y')", "test")).contains("too large"));
}

TEST_CASE("work that grows with the length of a text stops on time") {
    auto vm = makeVm(ShortDeadline);
    const std::string bigText = "local big = ('x'):rep(3 << 20) ";
    CHECK(stopTime(*vm, bigText + "while true do utf8.len(big) end") < DeadlineStop);
    CHECK(stopTime(*vm, bigText + "local other = ('x'):rep((3 << 20) - 1) .. 'y'"
                                  " while true do local _ = big < other end") < DeadlineStop);
    CHECK(stopTime(*vm, bigText + "local other = ('x'):rep((3 << 20) - 1) .. 'y'"
                                  " while true do local _ = big == other end") < DeadlineStop);
    CHECK(stopTime(*vm, bigText + "while true do local _ = big:upper() end") < DeadlineStop);
    CHECK(stopTime(*vm, bigText + "while true do local _ = utf8.offset(big, #big) end") < DeadlineStop);
    CHECK(stopTime(*vm, "local big = 'a' .. ('\\x80'):rep(3 << 20) local step = utf8.codes(big, true)"
                        " while true do step(big, 1) end") < DeadlineStop);
    CHECK(stopTime(*vm, "local texts = {} for i = 1, 200 do texts[i] = ('x'):rep(1 << 16) end"
                        " while true do table.sort(texts) end") < DeadlineStop);
}

TEST_CASE("utf8.len, utf8.offset and the steps of utf8.codes charge for the bytes they go over") {
    // A few Lua instructions per call: only the charge for the bytes stops these loops within the instruction budget.
    auto vm = makeVm(SmallLimits);
    CHECK(stopTime(*vm, "local big = ('x'):rep(3 << 20) while true do utf8.len(big) end") < QuickStop);
    CHECK(stopTime(*vm, "local big = ('x'):rep(3 << 20) while true do local _ = utf8.offset(big, #big) end") <
          QuickStop);
    CHECK(stopTime(*vm, "local big = 'a' .. ('\\x80'):rep(3 << 20) local step = utf8.codes(big, true)"
                        " while true do step(big, 1) end") < QuickStop);
}

TEST_CASE("string.pack and string.unpack charge for the bytes they write and read") {
    // A few Lua instructions per call: only the charge for the bytes stops these loops within the instruction budget.
    auto vm = makeVm(SmallLimits);
    CHECK(stopTime(*vm, "while true do local _ = string.pack('c3000000', '') end") < QuickStop);
    CHECK(stopTime(*vm, "local big = ('x'):rep(3 << 20) local format = 'c' .. #big"
                        " while true do local _ = string.unpack(format, big) end") < QuickStop);
    CHECK(errorOf(vm->runString("local _ = string.pack('c5000000', '')", "test")).contains("not enough memory"));
}

TEST_CASE("a script cannot reserve one block larger than the limit") {
    auto vm = makeVm();
    CHECK(errorOf(vm->runString("local _ = ('x'):rep(5 << 20)", "test")).contains("not enough memory"));
    CHECK(vm->runString("local _ = ('x'):rep(3 << 20)", "test"));
}

TEST_CASE("each pack has its own memory quota, and the engine keeps a reserve") {
    auto vm = makeVm({.memoryBytes = 32 * MebiByte,
                      .packMemoryBytes = 4 * MebiByte,
                      .largestBlockBytes = 1 * MebiByte,
                      .instructionsPerCall = 100'000'000,
                      .callDeadline = TestDeadline});
    auto greedy = environmentOf(*vm, "greedy");
    auto modest = environmentOf(*vm, "modest");
    CHECK(errorOf(greedy.run("local kept = {} for i = 1, 100000 do kept[i] = ('x'):rep(1000) .. i end", "=greedy"))
              .contains("not enough memory"));
    CHECK(modest.run("local kept = {} for i = 1, 2000 do kept[i] = ('y'):rep(1000) .. i end", "=modest"));
    CHECK(vm->runString("local kept = {} for i = 1, 4000 do kept[i] = ('z'):rep(1000) .. i end", "test"));
}

TEST_CASE("collectgarbage('count') tells a pack only its own memory") {
    auto vm = makeVm();
    auto& reported = publishReport(*vm);
    REQUIRE(vm->hooks().declare("story.day_passed"));
    auto greedy = environmentOf(*vm, "greedy");
    auto modest = environmentOf(*vm, "modest");
    // The handler keeps its table alive after the script ends.
    REQUIRE(greedy.run("local kept = {} for i = 1, 3000 do kept[i] = ('x'):rep(1000) .. i end"
                       " hooks.on('story.day_passed', function() return #kept end)"
                       " test.report(math.floor(collectgarbage('count')))",
                       "=greedy"));
    REQUIRE(modest.run("test.report(math.floor(collectgarbage('count')))", "=modest"));
    REQUIRE(vm->runString("test.report(math.floor(collectgarbage('count')))", "test"));
    REQUIRE(reported.size() == 3);
    constexpr int KeptKilobytes = 3000;
    CHECK(std::stoi(reported[0]) >= KeptKilobytes);
    CHECK(std::stoi(reported[1]) < KeptKilobytes / 10);
    CHECK(std::stoi(reported[2]) >= KeptKilobytes);
}

TEST_CASE("a hook's handlers share one budget, and a pack has a limited number of them") {
    auto vm = makeVm(SmallLimits);
    REQUIRE(vm->hooks().declare("story.day_passed"));
    auto mod = environmentOf(*vm, "my_mod");
    REQUIRE(mod.run("for _ = 1, 64 do hooks.on('story.day_passed', function() for i = 1, 300000 do end end) end",
                    "=my_mod"));
    CHECK(errorOf(mod.run("hooks.on('story.day_passed', function() end)", "=my_mod")) ==
          "my_mod:1: a pack cannot register more handlers of the event story.day_passed");
    const auto report = vm->hooks().fire("story.day_passed");
    REQUIRE(report.has_value());
    // A million instructions pay for three of the loops, not for all 64: the one that spends the rest fails, and the
    // others are skipped.
    CHECK(report->handlersRun + report->handlersSkipped == 64);
    CHECK(report->handlersFailed == 1);
    CHECK(report->handlersSkipped > 55);
}

TEST_CASE("a module calling into Lua through a ScriptCall shares the budget with the hooks it fires") {
    auto vm = makeVm(SmallLimits);
    REQUIRE(vm->hooks().declare("ui.clicked"));
    script::HookRegistry hooks = vm->hooks();
    REQUIRE(vm->api().publish("ui", "click", [hooks](const ApiCall&) mutable -> core::Result<ApiValue> {
        auto report = hooks.fire("ui.clicked");
        if (!report) {
            return std::unexpected{report.error()};
        }
        return ApiValue{};
    }));
    auto mod = environmentOf(*vm, "my_mod");
    REQUIRE(mod.run("hooks.on('ui.clicked', function() for i = 1, 300000 do end end)", "=my_mod"));
    REQUIRE(vm->runString("function spin() while true do ui.click() end end", "test"));
    const script::ScriptCall call{*vm};
    sol::protected_function spin = call.lua()["spin"];
    const sol::protected_function_result result = spin();
    REQUIRE_FALSE(result.valid());
    CHECK(sol::stack::get<std::string>(call.lua().lua_state(), result.stack_index()).contains("ran too long"));
}

namespace {

// What the hooks that ui.click fired did, added up.
struct ClickTotals {
    std::size_t failed = 0;
    std::size_t skipped = 0;
};

// Declares ui.clicked and publishes ui.click(), which fires it from inside the script's call; my_mod handles it with a
// loop that never ends.
ClickTotals& publishEndlessClick(script::ScriptVm& vm) {
    static ClickTotals totals;
    totals = {};
    REQUIRE(vm.hooks().declare("ui.clicked"));
    script::HookRegistry hooks = vm.hooks();
    REQUIRE(vm.api().publish("ui", "click", [hooks](const ApiCall&) mutable -> core::Result<ApiValue> {
        auto report = hooks.fire("ui.clicked");
        if (!report) {
            return std::unexpected{report.error()};
        }
        totals.failed += report->handlersFailed;
        totals.skipped += report->handlersSkipped;
        return ApiValue{};
    }));
    auto mod = environmentOf(vm, "my_mod");
    REQUIRE(mod.run("hooks.on('ui.clicked', function() while true do end end)", "=my_mod"));
    return totals;
}

} // namespace

TEST_CASE("a hook fired inside a call gets its own smaller budget, and the call pays for what it spent") {
    SUBCASE("instructions") {
        // Each click's handler loops until its 100000 instructions run out; the call's 250000 pay for two clicks.
        constexpr script::ScriptLimits Limits{.instructionsPerCall = 250'000,
                                              .callDeadline = TestDeadline,
                                              .instructionsPerHook = 100'000,
                                              .hookDeadline = TestDeadline,
                                              .hookInstructionsPerFrame = 100'000'000'000,
                                              .hookTimePerFrame = TestDeadline};
        auto vm = makeVm(Limits);
        const ClickTotals& totals = publishEndlessClick(*vm);
        REQUIRE(vm->runString(
            "clicks = 0 function go(count) for _ = 1, count do ui.click() clicks = clicks + 1 end end", "test"));
        REQUIRE(vm->runString("clicks = 0 go(2)", "test"));
        CHECK(script::ScriptCall{*vm}.lua()["clicks"].get<int>() == 2);
        CHECK(totals.failed == 2);
        CHECK(errorOf(vm->runString("clicks = 0 go(3)", "test")).contains("ran too long"));
        CHECK(script::ScriptCall{*vm}.lua()["clicks"].get<int>() < 3);
    }
    SUBCASE("the hook's deadline, when it is the earlier one") {
        // The handler stops after its 50 ms; the call, with a minute, goes on.
        constexpr script::ScriptLimits Limits{.instructionsPerCall = 100'000'000'000,
                                              .callDeadline = TestDeadline,
                                              .instructionsPerHook = 100'000'000'000,
                                              .hookDeadline = std::chrono::milliseconds{50},
                                              .hookInstructionsPerFrame = 100'000'000'000,
                                              .hookTimePerFrame = TestDeadline};
        auto vm = makeVm(Limits);
        const ClickTotals& totals = publishEndlessClick(*vm);
        CHECK(vm->runString("ui.click() after = true", "test"));
        CHECK(totals.failed == 1);
        CHECK(script::ScriptCall{*vm}.lua()["after"].get<bool>());
    }
    SUBCASE("the call's deadline, when it is the earlier one") {
        // The hook could run for a minute, but the call has 200 ms: both stop then.
        auto vm = makeVm({.instructionsPerCall = 100'000'000'000,
                          .callDeadline = ShortDeadline.callDeadline,
                          .instructionsPerHook = 100'000'000'000,
                          .hookDeadline = TestDeadline,
                          .hookInstructionsPerFrame = 100'000'000'000,
                          .hookTimePerFrame = TestDeadline});
        const ClickTotals& totals = publishEndlessClick(*vm);
        const auto started = std::chrono::steady_clock::now();
        CHECK(errorOf(vm->runString("ui.click() while true do end", "test")).contains("ran too long"));
        CHECK(std::chrono::steady_clock::now() - started < DeadlineStop);
        CHECK(totals.failed == 1);
    }
}

TEST_CASE("nested hooks pay their pack's frame budget, which nothing credits back") {
    // A fire may spend 100000 instructions and the frame 150000: the second click gets the 50000 left, the third none.
    constexpr script::ScriptLimits Limits{.instructionsPerCall = 10'000'000,
                                          .callDeadline = TestDeadline,
                                          .instructionsPerHook = 100'000,
                                          .hookDeadline = TestDeadline,
                                          .hookInstructionsPerFrame = 150'000,
                                          .hookTimePerFrame = TestDeadline};
    auto vm = makeVm(Limits);
    const ClickTotals& totals = publishEndlessClick(*vm);
    REQUIRE(vm->runString("for _ = 1, 3 do ui.click() end", "test"));
    CHECK(totals.failed == 2);
    CHECK(totals.skipped == 1);
    vm->hooks().beginFrame();
    REQUIRE(vm->runString("ui.click()", "test"));
    CHECK(totals.failed == 3);
}

TEST_CASE("a hook that runs out inside a coroutine leaves the main thread's budget checks as they were") {
    constexpr script::ScriptLimits Limits{.instructionsPerCall = 1'000'000,
                                          .callDeadline = TestDeadline,
                                          .instructionsPerHook = 100'000,
                                          .hookDeadline = TestDeadline,
                                          .hookInstructionsPerFrame = 100'000'000'000,
                                          .hookTimePerFrame = TestDeadline};
    auto vm = makeVm(Limits);
    const ClickTotals& totals = publishEndlessClick(*vm);
    // Were the main thread's hook left on every instruction, each would cost a thousand and this loop would stop.
    CHECK(vm->runString("local click = coroutine.wrap(function() ui.click() end) click()"
                        " local n = 0 for _ = 1, 100000 do n = n + 1 end",
                        "test"));
    CHECK(totals.failed == 1);
}

TEST_CASE("coroutine.close stops a __close handler that loops") {
    auto vm = makeVm(SmallLimits);
    CHECK(
        errorOf(vm->runString("local co = coroutine.create(function()"
                              "   local guard <close> = setmetatable({}, {__close = function() while true do end end})"
                              "   coroutine.yield() end)"
                              " coroutine.resume(co) local ok, message = coroutine.close(co)"
                              " while true do end",
                              "test"))
            .contains("ran too long"));
}

TEST_CASE("the string methods are the engine's, whatever a script does to its string table") {
    auto vm = makeVm();
    auto& reported = publishReport(*vm);
    REQUIRE(vm->runString("string.upper = function() return 'changed' end", "test"));
    auto mod = environmentOf(*vm, "my_mod");
    REQUIRE(mod.run("test.report(('a'):upper()) test.report(string.upper('b'))", "=my_mod"));
    CHECK(reported == std::vector<std::string>{"A", "B"});
    REQUIRE(vm->runString("test.report(('c'):upper())", "test"));
    CHECK(reported.back() == "C");
}

TEST_CASE("a mod cannot provide a script that runs with the engine's globals") {
    auto vm = makeVm();
    MemoryPacks packs;
    packs.addEnginePack("game");
    packs.addFile("game", "scripts/console.lua", "value = 1");
    packs.addPack("my_mod");
    packs.addFile("my_mod", "scripts/dev.lua", "value = 2");
    const auto& files = packs.mount();
    CHECK(vm->runAsset(files, script::testing::assetId("scripts/console")));
    CHECK(errorOf(vm->runAsset(files, script::testing::assetId("scripts/dev"))) ==
          "the engine script scripts/dev comes from the pack 'my_mod' (a mod, mods layer); only the engine's own packs "
          "of the base and remaster layers provide engine scripts, and a pack's scripts run in its own environment");
    CHECK(vm->number("value") == 1.0);
}

TEST_CASE("a mod that declares an engine layer cannot provide a script that runs with the engine's globals") {
    // Where a pack was found decides, not the layer its manifest declares.
    auto vm = makeVm();
    MemoryPacks packs;
    packs.addEnginePack("game");
    packs.addFile("game", "scripts/console.lua", "value = 1");
    packs.addPack("fake_base", {}, "base");
    packs.addFile("fake_base", "scripts/dev.lua", "value = 2");
    packs.addPack("fake_remaster", {}, "remaster");
    packs.addFile("fake_remaster", "scripts/tools.lua", "value = 3");
    const auto& files = packs.mount();
    CHECK(vm->runAsset(files, script::testing::assetId("scripts/console")));
    CHECK(errorOf(vm->runAsset(files, script::testing::assetId("scripts/dev")))
              .starts_with("the engine script scripts/dev comes from the pack 'fake_base' (a mod, base layer)"));
    CHECK(errorOf(vm->runAsset(files, script::testing::assetId("scripts/tools")))
              .starts_with("the engine script scripts/tools comes from the pack 'fake_remaster' (a mod, remaster "
                           "layer)"));
    CHECK(vm->number("value") == 1.0);
}

TEST_CASE("a script larger than the cap is not compiled") {
    auto vm = makeVm();
    MemoryPacks packs;
    packs.addEnginePack("game");
    constexpr std::size_t MaxScriptBytes = std::size_t{1} << 20;
    packs.addFile("game", "scripts/huge.lua", std::string(MaxScriptBytes + 1, ' '));
    packs.addFile("game", "scripts/limit.lua", std::string(MaxScriptBytes - 10, ' ') + "value = 1");
    const auto& files = packs.mount();
    CHECK(errorOf(vm->runAsset(files, script::testing::assetId("scripts/huge"))) ==
          std::format("the script scripts/huge has {} bytes; scripts may have at most {}", MaxScriptBytes + 1,
                      MaxScriptBytes));
    CHECK(vm->runAsset(files, script::testing::assetId("scripts/limit")));
    CHECK(vm->number("value") == 1.0);
}

TEST_CASE("a script cannot flood the log") {
    const LogCapture log;
    auto vm = makeVm();
    auto mod = environmentOf(*vm, "my_mod");
    REQUIRE(mod.run("for i = 1, 1000 do print(i) end", "=my_mod"));
    CHECK(log.contains("mod my_mod: 100"));
    CHECK_FALSE(log.contains("mod my_mod: 101"));
    CHECK(log.contains("mod my_mod: printed more than 100 lines in one call; the rest are left out"));
    REQUIRE(mod.run("print('next call')", "=my_mod"));
    CHECK(log.contains("mod my_mod: next call"));
    REQUIRE(vm->hooks().declare("story.day_passed"));
    REQUIRE(mod.run("hooks.on('story.day_passed', function() error(('x'):rep(10000)) end)", "=my_mod"));
    REQUIRE(vm->hooks().fire("story.day_passed"));
    CHECK(log.contains("more bytes]"));
}

TEST_CASE("reloading a pack whose script makes other packs' environments uses no stale state") {
    auto vm = makeVm();
    auto& reported = publishReport(*vm);
    script::ScriptVm* engine = vm.get();
    // New packs on every call, so the list of packs grows while the reloading pack's scripts run.
    REQUIRE(
        vm->api().publish("test", "make_packs", [engine, made = 0](const ApiCall&) mutable -> core::Result<ApiValue> {
            for (int count = 0; count < 32; ++count, ++made) {
                auto environment = engine->modEnvironment(script::testing::packNamed(std::format("other_{}", made)));
                if (!environment) {
                    return std::unexpected{environment.error()};
                }
            }
            return ApiValue{};
        }));
    MemoryPacks packs;
    packs.addPack("my_mod");
    packs.addFile("my_mod", "scripts/main.lua", "test.make_packs() test.report('v1')");
    packs.addFile("my_mod", "scripts/more.lua", "test.report('more')");
    const auto& files = packs.mount();
    REQUIRE(vm->runModScript(files, script::testing::assetId("scripts/main")));
    REQUIRE(vm->runModScript(files, script::testing::assetId("scripts/more")));
    packs.addFile("my_mod", "scripts/main.lua", "test.make_packs() test.report('v2')");
    vm->reloadChanged();
    CHECK(reported == std::vector<std::string>{"v1", "more", "v2", "more"});
}

TEST_CASE("each pack has its own random numbers, the same in every run") {
    const auto drawsOf = [](bool otherPackDraws) {
        auto vm = makeVm();
        auto& reported = publishReport(*vm);
        auto first = environmentOf(*vm, "first");
        auto other = environmentOf(*vm, "other");
        if (otherPackDraws) {
            REQUIRE(other.run("math.randomseed(5) for _ = 1, 100 do math.random() end", "=other"));
        }
        REQUIRE(first.run("for _ = 1, 5 do test.report(math.random(1, 1000)) end", "=first"));
        return reported;
    };
    const std::vector<std::string> alone = drawsOf(false);
    CHECK(alone.size() == 5);
    CHECK(drawsOf(true) == alone);
    auto vm = makeVm();
    auto& reported = publishReport(*vm);
    auto mod = environmentOf(*vm, "first");
    REQUIRE(mod.run("math.random(7) math.randomseed() for _ = 1, 5 do test.report(math.random(1, 1000)) end"
                    " local x = math.random() test.report(x >= 0 and x < 1)"
                    " test.report(math.random(3, 3)) test.report(math.type(math.random(0)))",
                    "=first"));
    CHECK(std::vector<std::string>(reported.begin(), reported.begin() + 5) == alone);
    CHECK(std::vector<std::string>(reported.begin() + 5, reported.end()) ==
          std::vector<std::string>{"true", "3", "integer"});
    CHECK(errorOf(mod.run("math.random(3, 1)", "=first")).contains("interval is empty"));
}

TEST_CASE("scripts see no addresses, and no warn") {
    const LogCapture log;
    auto vm = makeVm();
    auto& reported = publishReport(*vm);
    auto mod = environmentOf(*vm, "my_mod");
    REQUIRE(mod.run("test.report(tostring({})) test.report(tostring(print)) test.report(string.format('%p', {}))"
                    " test.report(('%s|%3d'):format(coroutine.create(print), 7))"
                    " test.report(tostring(setmetatable({}, {__tostring = function() return 'own' end})))"
                    " test.report(rawget(_ENV, 'warn') == nil) print({}, 1)",
                    "=my_mod"));
    CHECK(reported == std::vector<std::string>{"table: my_mod", "function: my_mod", "table: my_mod",
                                               "thread: my_mod|  7", "own", "true"});
    CHECK(log.contains("table: my_mod\t1"));
    REQUIRE(vm->runString("test.report(tostring({})) test.report(warn == nil)", "test"));
    CHECK(reported[reported.size() - 2] == "table: engine");
    CHECK(reported.back() == "true");
}
