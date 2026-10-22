#include <dwe/assets/pack_manifest.hpp>
#include <dwe/script/hook_registry.hpp>

#include <doctest/doctest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "script_test_support.hpp"

using namespace dwe;
using script::ApiCall;
using script::ApiValue;
using script::testing::environmentOf;
using script::testing::errorOf;
using script::testing::LogCapture;
using script::testing::makeVm;
using script::testing::SmallLimits;
using script::testing::TestDeadline;
using script::testing::UnreachableInstructions;

namespace {

// Publishes test.log(text), which keeps what handlers log in order.
std::vector<std::string>& publishLog(script::ScriptVm& vm) {
    static std::vector<std::string> logged;
    logged.clear();
    REQUIRE(vm.api().publish("test", "log", [](const ApiCall& call) -> core::Result<ApiValue> {
        auto text = call.text(0);
        if (!text) {
            return std::unexpected{text.error()};
        }
        logged.emplace_back(*text);
        return ApiValue{};
    }));
    return logged;
}

} // namespace

TEST_CASE("events have a namespace and a name") {
    auto vm = makeVm();
    CHECK(vm->hooks().declare("field.map_entered"));
    CHECK(vm->hooks().isDeclared("field.map_entered"));
    for (const std::string_view bad : {"map_entered", "field.", ".map", "Field.map", "field.map.entered", ""}) {
        CAPTURE(bad);
        CHECK(errorOf(vm->hooks().declare(bad)).contains("is not an event name"));
    }
    CHECK(errorOf(vm->hooks().fire("field.unknown")) == "the event field.unknown is not declared");
}

TEST_CASE("handlers run in the order of the packs, then in the order each registered them, with the arguments") {
    auto vm = makeVm();
    auto& logged = publishLog(*vm);
    REQUIRE(vm->hooks().declare("field.map_entered"));
    auto first = environmentOf(*vm, "first");
    auto second = environmentOf(*vm, "second");
    REQUIRE(second.run("hooks.on('field.map_entered', function(map, index) test.log('second ' .. map .. index) end)",
                       "=second"));
    REQUIRE(first.run("hooks.on('field.map_entered', function(map) test.log('first a ' .. map) end)"
                      " hooks.on('field.map_entered', function(map) test.log('first b ' .. map) end)",
                      "=first"));
    const std::vector<ApiValue> arguments{std::string{"twna01"}, std::int64_t{3}};
    const auto report = vm->hooks().fire("field.map_entered", arguments);
    REQUIRE(report.has_value());
    CHECK(report->handlersRun == 3);
    CHECK(report->handlersFailed == 0);
    CHECK(logged == std::vector<std::string>{"first a twna01", "first b twna01", "second twna013"});
}

TEST_CASE("a pack's hook handlers share a budget per fired event and one per frame") {
    // A fire may spend 100000 instructions and a frame 250000: the third fire gets the 50000 left, the fourth none.
    constexpr script::ScriptLimits Limits{.instructionsPerCall = 1'000'000,
                                          .callDeadline = TestDeadline,
                                          .instructionsPerHook = 100'000,
                                          .hookDeadline = TestDeadline,
                                          .hookInstructionsPerFrame = 250'000,
                                          .hookTimePerFrame = TestDeadline};
    const LogCapture log;
    auto vm = makeVm(Limits);
    auto& logged = publishLog(*vm);
    REQUIRE(vm->hooks().declare("test.tick"));
    auto hot = environmentOf(*vm, "hot_mod");
    auto calm = environmentOf(*vm, "calm_mod");
    REQUIRE(hot.run("hooks.on('test.tick', function() while true do end end)"
                    " hooks.on('test.tick', function() test.log('hot') end)",
                    "=hot_mod"));
    REQUIRE(calm.run("hooks.on('test.tick', function() test.log('calm') end)", "=calm_mod"));
    for (int fire = 0; fire < 3; ++fire) {
        const auto report = vm->hooks().fire("test.tick");
        REQUIRE(report.has_value());
        CHECK(report->handlersRun == 2);
        CHECK(report->handlersSkipped == 1);
    }
    const auto spent = vm->hooks().fire("test.tick");
    REQUIRE(spent.has_value());
    CHECK(spent->handlersRun == 1);
    CHECK(spent->handlersSkipped == 2);
    CHECK(logged == std::vector<std::string>(4, "calm"));
    CHECK(LogCapture::count("mod hot_mod: its hook handlers spent their budget") == 1);
    vm->hooks().beginFrame();
    const auto next = vm->hooks().fire("test.tick");
    REQUIRE(next.has_value());
    CHECK(next->handlersRun == 2);
}

TEST_CASE("a hot handler cannot push one frame's script time over its pack's frame budget") {
    constexpr auto FireTime = std::chrono::milliseconds{20};
    constexpr auto FrameTime = std::chrono::milliseconds{40};
    constexpr script::ScriptLimits Limits{.instructionsPerCall = UnreachableInstructions,
                                          .callDeadline = TestDeadline,
                                          .instructionsPerHook = UnreachableInstructions,
                                          .hookDeadline = FireTime,
                                          .hookInstructionsPerFrame = UnreachableInstructions,
                                          .hookTimePerFrame = FrameTime};
    // A fire that runs takes its 20 ms or what the frame has left, never less, so the frame's 40 ms let two fires run
    // at most, however loaded the machine is: the rest are skipped. Counting fires, not timing them, keeps the test
    // free of the load.
    constexpr int Fires = 20;
    constexpr std::size_t MostFiresRun = FrameTime / FireTime;
    auto vm = makeVm(Limits);
    REQUIRE(vm->hooks().declare("test.tick"));
    auto hot = environmentOf(*vm, "hot_mod");
    REQUIRE(hot.run("hooks.on('test.tick', function() while true do end end)", "=hot_mod"));
    std::size_t run = 0;
    std::size_t skipped = 0;
    for (int fire = 0; fire < Fires; ++fire) {
        const auto report = vm->hooks().fire("test.tick");
        REQUIRE(report.has_value());
        run += report->handlersRun;
        skipped += report->handlersSkipped;
    }
    CHECK(run <= MostFiresRun);
    CHECK(run + skipped == Fires);
}

TEST_CASE("a pack cannot flood the log: its lines have a budget of bytes, and a repeated error is counted") {
    // Each fire prints 50 lines of 1000 bytes: far more than the burst a pack may write at once.
    constexpr int Fires = 10;
    constexpr std::size_t MaxLinesKept = 80;
    const LogCapture log;
    auto vm = makeVm();
    REQUIRE(vm->hooks().declare("test.tick"));
    auto flood = environmentOf(*vm, "flood_mod");
    auto broken = environmentOf(*vm, "broken_mod");
    REQUIRE(
        flood.run("hooks.on('test.tick', function() for _ = 1, 50 do print(('x'):rep(1000)) end end)", "=flood_mod"));
    REQUIRE(broken.run("local fires = 0 hooks.on('test.tick', function() fires = fires + 1"
                       " if fires < 10 then error('always', 0) end error('other', 0) end)",
                       "=broken_mod"));
    for (int fire = 0; fire < Fires; ++fire) {
        REQUIRE(vm->hooks().fire("test.tick"));
    }
    const std::size_t kept = LogCapture::count("mod flood_mod: xxx");
    CHECK(kept > 0);
    CHECK(kept <= MaxLinesKept);
    CHECK(log.contains("mod flood_mod: writes more than"));
    CHECK(LogCapture::count("mod broken_mod: a handler of test.tick failed: always") == 1);
    CHECK(log.contains("mod broken_mod: the last error repeated 8 times"));
    CHECK(log.contains("mod broken_mod: a handler of test.tick failed: other"));
}

TEST_CASE("a pack pays nothing for the events it does not handle") {
    // Paying even a check interval per fire, 3000 fires would spend the pack's frame budget.
    constexpr int Fires = 3000;
    auto vm = makeVm();
    auto& logged = publishLog(*vm);
    REQUIRE(vm->hooks().declare("test.unhandled"));
    REQUIRE(vm->hooks().declare("test.handled"));
    auto mod = environmentOf(*vm, "my_mod");
    REQUIRE(mod.run("hooks.on('test.handled', function() test.log('ran') end)", "=my_mod"));
    for (int fire = 0; fire < Fires; ++fire) {
        REQUIRE(vm->hooks().fire("test.unhandled"));
    }
    const auto report = vm->hooks().fire("test.handled");
    REQUIRE(report.has_value());
    CHECK(report->handlersRun == 1);
    CHECK(report->handlersSkipped == 0);
    CHECK(logged == std::vector<std::string>{"ran"});
}

TEST_CASE("the repeats of an error still counted when the VM closes reach the log") {
    constexpr int Fires = 5;
    const LogCapture log;
    {
        auto vm = makeVm();
        REQUIRE(vm->hooks().declare("test.tick"));
        auto broken = environmentOf(*vm, "broken_mod");
        REQUIRE(broken.run("hooks.on('test.tick', function() error('always', 0) end)", "=broken_mod"));
        for (int fire = 0; fire < Fires; ++fire) {
            REQUIRE(vm->hooks().fire("test.tick"));
        }
        CHECK_FALSE(log.contains("repeated"));
    }
    CHECK(log.contains("mod broken_mod: the last error repeated 4 times"));
}

TEST_CASE("an error in a handler is logged with its pack and the other handlers still run") {
    const LogCapture log;
    auto vm = makeVm(SmallLimits);
    auto& logged = publishLog(*vm);
    REQUIRE(vm->hooks().declare("story.day_passed"));
    auto broken = environmentOf(*vm, "broken_mod");
    auto endless = environmentOf(*vm, "endless_mod");
    auto fine = environmentOf(*vm, "fine_mod");
    REQUIRE(broken.run("hooks.on('story.day_passed', function() error('oops') end)", "=broken_mod"));
    REQUIRE(endless.run("hooks.on('story.day_passed', function() while true do end end)", "=endless_mod"));
    REQUIRE(fine.run("hooks.on('story.day_passed', function() test.log('fine') end)", "=fine_mod"));
    const auto report = vm->hooks().fire("story.day_passed");
    REQUIRE(report.has_value());
    CHECK(report->handlersRun == 3);
    CHECK(report->handlersFailed == 2);
    CHECK(logged == std::vector<std::string>{"fine"});
    CHECK(log.contains("mod broken_mod: a handler of story.day_passed failed: broken_mod:1: oops"));
    CHECK(log.contains("mod endless_mod: a handler of story.day_passed failed:"));
    CHECK(log.contains("ran too long"));
}

TEST_CASE("a pack can only handle declared events with functions") {
    auto vm = makeVm();
    REQUIRE(vm->hooks().declare("field.map_entered"));
    auto mod = environmentOf(*vm, "my_mod");
    CHECK(errorOf(mod.run("hooks.on('field.map_left', function() end)", "=my_mod")) ==
          "my_mod:1: there is no event 'field.map_left' to handle");
    CHECK(errorOf(mod.run("hooks.on('field.map_entered', 'not a function')", "=my_mod")).contains("function expected"));
}

TEST_CASE("a hook fired from inside a script's call shares that call's budget") {
    auto vm = makeVm(SmallLimits);
    REQUIRE(vm->hooks().declare("story.nested"));
    script::HookRegistry hooks = vm->hooks();
    REQUIRE(vm->api().publish("story", "fire", [hooks](const ApiCall&) mutable -> core::Result<ApiValue> {
        auto report = hooks.fire("story.nested");
        if (!report) {
            return std::unexpected{report.error()};
        }
        return ApiValue{static_cast<std::int64_t>(report->handlersFailed)};
    }));
    auto mod = environmentOf(*vm, "my_mod");
    REQUIRE(mod.run("hooks.on('story.nested', function() for i = 1, 300000 do end end)", "=my_mod"));
    // Each nested handler would finish with a budget of its own; sharing one, the loop is stopped.
    CHECK(errorOf(mod.run("while true do story.fire() end", "=my_mod")).contains("ran too long"));
}

TEST_CASE("reloading a pack replaces its handlers, and a broken edit keeps the previous ones") {
    const LogCapture log;
    auto vm = makeVm();
    auto& logged = publishLog(*vm);
    REQUIRE(vm->hooks().declare("story.day_passed"));
    script::testing::MemoryPacks packs;
    packs.addPack("my_mod");
    packs.addFile("my_mod", "scripts/main.lua", "hooks.on('story.day_passed', function() test.log('v1') end)");
    const auto& files = packs.mount();
    REQUIRE(vm->runModScript(files, script::testing::assetId("scripts/main")));
    REQUIRE(vm->hooks().fire("story.day_passed"));
    CHECK(logged == std::vector<std::string>{"v1"});

    packs.addFile("my_mod", "scripts/main.lua", "hooks.on('story.day_passed', function() test.log('v2') end)");
    vm->reloadChanged();
    logged.clear();
    REQUIRE(vm->hooks().fire("story.day_passed"));
    CHECK(logged == std::vector<std::string>{"v2"});

    packs.addFile("my_mod", "scripts/main.lua",
                  "hooks.on('story.day_passed', function() test.log('v3') end) error('half done')");
    vm->reloadChanged();
    CHECK(log.contains("could not reload the scripts of the pack 'my_mod', which keeps its previous ones"));
    logged.clear();
    REQUIRE(vm->hooks().fire("story.day_passed"));
    CHECK(logged == std::vector<std::string>{"v2"});
}
