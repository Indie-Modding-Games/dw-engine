#include <dwe/script/mod_environment.hpp>
#include <dwe/script/script_coroutine.hpp>

#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "script_test_support.hpp"

using namespace dwe;
using script::ApiCall;
using script::ApiValue;
using script::BudgetStop;
using script::CoroutineFailed;
using script::CoroutineFinished;
using script::CoroutineStep;
using script::CoroutineWaiting;
using script::ScriptCoroutine;
using script::ScriptLimits;
using script::ScriptModule;
using script::testing::assetId;
using script::testing::environmentOf;
using script::testing::errorOf;
using script::testing::makeVm;
using script::testing::MemoryPacks;
using script::testing::publishReport;
using script::testing::TestLimits;

namespace {

int& publishWait(script::ScriptVm& vm) {
  static int calls = 0;
  calls = 0;
  REQUIRE(
      vm.api().publishWaiting("test", "wait", [](const ApiCall& call) -> core::Result<ApiValue> {
        ++calls;
        return call.arguments().empty() ? ApiValue{} : call.arguments().front();
      }));
  return calls;
}

struct Story {
  MemoryPacks packs;
  std::unique_ptr<script::ScriptVm> vm;
  std::vector<std::string>* reported;
  int* waits;

  explicit Story(std::string_view module, const ScriptLimits& limits = TestLimits)
      : vm{makeVm(limits)}, reported{&publishReport(*vm)}, waits{&publishWait(*vm)} {
    packs.addPack("my_mod");
    packs.addFile("my_mod", "scripts/story.lua", module);
    packs.mount();
  }

  [[nodiscard]] ScriptModule load() const {
    auto environment = environmentOf(*vm, "my_mod");
    auto module = environment.loadModule(packs.files(), assetId("scripts/story"));
    REQUIRE_MESSAGE(module.has_value(), module.error().message);
    return std::move(*module);
  }
};

ScriptCoroutine sectionOf(const ScriptModule& module, std::int64_t id) {
  const std::vector<ApiValue> path{std::string{"sections"}, id};
  auto coroutine = module.coroutine(path);
  REQUIRE_MESSAGE(coroutine.has_value(), coroutine.error().message);
  return std::move(*coroutine);
}

CoroutineWaiting waiting(const CoroutineStep& step) {
  const auto* held = std::get_if<CoroutineWaiting>(&step);
  REQUIRE_MESSAGE(held != nullptr, "the coroutine did not wait");
  return *held;
}

CoroutineFailed failed(const CoroutineStep& step) {
  const auto* held = std::get_if<CoroutineFailed>(&step);
  REQUIRE_MESSAGE(held != nullptr, "the coroutine did not fail");
  return *held;
}

ApiValue finished(const CoroutineStep& step) {
  const auto* held = std::get_if<CoroutineFinished>(&step);
  REQUIRE_MESSAGE(held != nullptr, "the coroutine did not finish");
  return held->value;
}

} // namespace

TEST_CASE("published values are read-only constants of their namespace") {
  auto vm = makeVm();
  auto& reported = publishReport(*vm);
  REQUIRE(vm->api().publishValue("test", "ended", ApiValue{true}));
  REQUIRE(vm->api().publishValue("test", "player", ApiValue{std::int64_t{0xFD}}));
  CHECK(vm->api().isPublished("test", "ended"));
  CHECK(errorOf(vm->api().publishValue("test", "ended", ApiValue{false})) ==
        "test.ended is published already");
  auto mod = environmentOf(*vm, "my_mod");
  REQUIRE(mod.run("test.report(test.ended) test.report(test.player)", "mod"));
  REQUIRE(vm->runString("test.report(test.player)", "engine"));
  CHECK(reported == std::vector<std::string>{"true", "253", "253"});
  CHECK_FALSE(mod.run("test.ended = false", "mod"));
}

TEST_CASE("a published value may have an upper-case name, and a function may not") {
  auto vm = makeVm();
  auto& reported = publishReport(*vm);
  REQUIRE(vm->api().publishValue("test", "ENDED_2", ApiValue{true}));
  CHECK_FALSE(vm->api().publishValue("test", "Ended", ApiValue{true}));
  CHECK_FALSE(vm->api().publishValue("test", "2ENDED", ApiValue{true}));
  CHECK_FALSE(vm->api().publish(
      "test", "ENDED", [](const ApiCall&) -> core::Result<ApiValue> { return ApiValue{}; }));
  REQUIRE(vm->runString("test.report(test.ENDED_2)", "engine"));
  CHECK(reported == std::vector<std::string>{"true"});
  CHECK(vm->api().publishedNames("test") == std::vector<std::string>{"ENDED_2", "report"});
  CHECK(vm->api().publishedNames("other").empty());
}

TEST_CASE("a waiting function gives its result to the resumer and gets the next resume's value") {
  Story story{"return { sections = { [81] = function()\n"
              "  test.report(test.wait(1))\n"
              "  return test.wait('two')\n"
              "end } }"};
  const ScriptModule module = story.load();
  ScriptCoroutine section = sectionOf(module, 81);
  CHECK(waiting(section.resume()).value == ApiValue{std::int64_t{1}});
  CHECK(waiting(section.resume(ApiValue{std::string{"back"}})).value ==
        ApiValue{std::string{"two"}});
  CHECK(*story.reported == std::vector<std::string>{"back"});
  CHECK(finished(section.resume(ApiValue{true})) == ApiValue{true});
  CHECK(section.finished());
  CHECK(failed(section.resume()).message == "the coroutine already finished");
}

TEST_CASE(
    "a wait tells how deep in the coroutine's calls it was made; tail calls and metamethods add nothing") {
  Story story{
      "local function inner() return test.wait() end\n"
      "local function middle() inner() end\n"
      "local probe = setmetatable({}, { __index = function() local value = test.wait() return value end })\n"
      "return { sections = { [1] = function() test.wait() middle() local _ = probe.x return inner() end } }"};
  const ScriptModule module = story.load();
  ScriptCoroutine section = sectionOf(module, 1);
  CHECK(waiting(section.resume()).callDepth == 0);
  CHECK(waiting(section.resume()).callDepth == 2);
  CHECK(waiting(section.resume()).callDepth == 0);
  CHECK(waiting(section.resume()).callDepth == 0);
  CHECK(std::holds_alternative<CoroutineFinished>(section.resume()));
}

TEST_CASE("only the coroutine the engine steps can wait, and refused waits never run") {
  Story story{"return { sections = {\n"
              "  [1] = function() coroutine.wrap(function() test.wait() end)() end,\n"
              "  [2] = function() coroutine.yield() end,\n"
              "  [3] = function() return {} end,\n"
              "} }"};
  const ScriptModule module = story.load();
  auto mod = environmentOf(*story.vm, "my_mod");
  CHECK(errorOf(mod.run("test.wait()", "mod"))
            .contains("test.wait waits, so only a section the engine runs"));
  ScriptCoroutine own = sectionOf(module, 1);
  CHECK(
      failed(own.resume()).message.contains("test.wait waits, so only a section the engine runs"));
  CHECK(*story.waits == 0);
  ScriptCoroutine yielding = sectionOf(module, 2);
  CHECK(failed(yielding.resume()).message.contains("yielded without a waiting function"));
  CHECK(yielding.finished());
  ScriptCoroutine table = sectionOf(module, 3);
  CHECK(failed(table.resume()).message ==
        "the coroutine returned a table; it may return nil, a boolean, a number "
        "or a text");
}

TEST_CASE("each resume has its own instruction budget") {
  ScriptLimits limits = TestLimits;
  limits.instructionsPerResume = 20'000;
  Story story{"return { sections = {\n"
              "  [1] = function() for step = 1, 50 do for i = 1, 5000 do end test.wait() end end,\n"
              "  [2] = function() test.wait() for i = 1, 100000 do end end,\n"
              "} }",
              limits};
  const ScriptModule module = story.load();
  ScriptCoroutine paced = sectionOf(module, 1);
  for (int step = 0; step < 50; ++step) {
    REQUIRE(std::holds_alternative<CoroutineWaiting>(paced.resume()));
  }
  CHECK(std::holds_alternative<CoroutineFinished>(paced.resume()));
  ScriptCoroutine runaway = sectionOf(module, 2);
  REQUIRE(std::holds_alternative<CoroutineWaiting>(runaway.resume()));
  const CoroutineFailed stopped = failed(runaway.resume());
  CHECK(stopped.stop == BudgetStop::Instructions);
  CHECK(stopped.message.contains("ran too long"));
  CHECK(runaway.finished());
}

TEST_CASE("the time ceiling of a resume does not count the engine work of published functions") {
  // A map's entry section loads models and places characters without waiting; on a busy machine
  // that engine work took longer than the whole ceiling of the resume and failed the section.
  static constexpr std::chrono::milliseconds EngineWork{60};
  ScriptLimits limits = TestLimits;
  limits.instructionsPerResume = script::testing::UnreachableInstructions;
  limits.resumeDeadline = std::chrono::milliseconds{20};
  Story story{"return { sections = {\n"
              "  [1] = function()\n"
              "    test.load() test.load() test.load()\n"
              "    local placed = 0 for slot = 1, 1000 do placed = placed + 1 end\n"
              "    test.report(placed)\n"
              "  end,\n"
              "  [2] = function() test.load() while true do end end,\n"
              "} }",
              limits};
  REQUIRE(story.vm->api().publish("test", "load", [](const ApiCall&) {
    // Busy, so the thread's processor time runs as well as the steady clock.
    const auto started = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - started < EngineWork) {
    }
    return core::Result<ApiValue>{ApiValue{}};
  }));
  const ScriptModule module = story.load();
  ScriptCoroutine entry = sectionOf(module, 1);
  const CoroutineStep placed = entry.resume();
  if (const auto* stopped = std::get_if<CoroutineFailed>(&placed)) {
    FAIL_CHECK(stopped->message);
  }
  CHECK(std::holds_alternative<CoroutineFinished>(placed));
  CHECK(*story.reported == std::vector<std::string>{"1000"});
  // The script's own time still counts: a loop after the engine work stops at the ceiling.
  ScriptCoroutine runaway = sectionOf(module, 2);
  CHECK(failed(runaway.resume()).stop == BudgetStop::TimeCeiling);
}

TEST_CASE("errors and missing functions name the module") {
  Story story{"return { sections = { [1] = function()\n  error('boom')\nend } }"};
  const ScriptModule module = story.load();
  const std::vector<ApiValue> missing{std::string{"sections"}, std::int64_t{99}};
  CHECK(errorOf(module.coroutine(missing)) ==
        "my_mod:scripts/story has no function at sections.99");
  ScriptCoroutine section = sectionOf(module, 1);
  const CoroutineFailed error = failed(section.resume());
  CHECK(error.message == "my_mod:scripts/story:2: boom");
  CHECK(error.stop == BudgetStop::None);
}

TEST_CASE("dropping a waiting coroutine closes its to-be-closed variables") {
  Story story{
      "return { sections = { [1] = function()\n"
      "  local guard <close> = setmetatable({}, { __close = function() test.report('closed') end })\n"
      "  test.wait()\n"
      "end } }"};
  const ScriptModule module = story.load();
  {
    ScriptCoroutine section = sectionOf(module, 1);
    REQUIRE(std::holds_alternative<CoroutineWaiting>(section.resume()));
    CHECK(story.reported->empty());
  }
  CHECK(*story.reported == std::vector<std::string>{"closed"});
}

TEST_CASE("scripts cannot resume or close a section's coroutine, so its wait holds") {
  Story story{"local captured\n"
              "hooks.on('test.tick', function()\n"
              "  test.report(select(2, pcall(coroutine.resume, captured)))\n"
              "  test.report(select(2, pcall(coroutine.close, captured)))\n"
              "  test.report(coroutine.status(captured))\n"
              "end)\n"
              "return { sections = { [1] = function()\n"
              "  captured = coroutine.running()\n"
              "  test.wait()\n"
              "  test.report('after')\n"
              "end } }"};
  REQUIRE(story.vm->hooks().declare("test.tick"));
  const ScriptModule module = story.load();
  ScriptCoroutine section = sectionOf(module, 1);
  REQUIRE(std::holds_alternative<CoroutineWaiting>(section.resume()));
  REQUIRE(story.vm->hooks().fire("test.tick"));
  REQUIRE(story.reported->size() == 3);
  CHECK((*story.reported)[0].contains("coroutine.resume: the engine runs this coroutine"));
  CHECK((*story.reported)[1].contains("coroutine.close: the engine runs this coroutine"));
  CHECK((*story.reported)[2] == "suspended");
  CHECK(std::holds_alternative<CoroutineFinished>(section.resume()));
  CHECK(story.reported->back() == "after");
  CHECK(*story.waits == 1);
}

TEST_CASE("a wait may pass through pcall, but not through a C function that cannot yield") {
  Story story{
      "return { sections = {\n"
      "  [1] = function() test.report(select(2, pcall(test.wait, 5))) end,\n"
      "  [2] = function() table.sort({ 3, 1, 2 }, function(a, b) test.wait() return a < b end) end,\n"
      "  [3] = function() string.gsub('ab', '.', function() test.wait() end) end,\n"
      "} }"};
  const ScriptModule module = story.load();
  ScriptCoroutine protectedWait = sectionOf(module, 1);
  CHECK(waiting(protectedWait.resume()).value == ApiValue{std::int64_t{5}});
  CHECK(std::holds_alternative<CoroutineFinished>(protectedWait.resume(ApiValue{std::int64_t{7}})));
  CHECK(*story.reported == std::vector<std::string>{"7"});
  for (const std::int64_t id : {2, 3}) {
    ScriptCoroutine callback = sectionOf(module, id);
    CHECK(failed(callback.resume())
              .message.contains("test.wait waits, so only a section the engine runs"));
  }
  CHECK(*story.waits == 1);
}

TEST_CASE(
    "a to-be-closed variable that never ends is stopped by the budget when its coroutine is dropped") {
  Story story{"return { sections = {\n"
              "  [1] = function()\n"
              "    local guard <close> = setmetatable({}, {\n"
              "      __close = function() test.report('closing') while true do end end })\n"
              "    test.wait()\n"
              "  end,\n"
              "  [2] = function() return 2 end,\n"
              "} }"};
  const ScriptModule module = story.load();
  {
    ScriptCoroutine section = sectionOf(module, 1);
    REQUIRE(std::holds_alternative<CoroutineWaiting>(section.resume()));
  }
  CHECK(*story.reported == std::vector<std::string>{"closing"});
  CHECK(story.vm->lastBudgetStop() == BudgetStop::Instructions);
  ScriptCoroutine next = sectionOf(module, 2);
  CHECK(finished(next.resume()) == ApiValue{std::int64_t{2}});
}

TEST_CASE("a section that runs out of memory fails without a budget stop") {
  ScriptLimits limits = TestLimits;
  limits.instructionsPerResume = 1'000'000'000;
  Story story{"return { sections = { [1] = function()\n"
              "  local kept = {}\n"
              "  for i = 1, 1000 do kept[i] = string.rep('x', 1024 * 1024) .. i end\n"
              "end } }",
              limits};
  const ScriptModule module = story.load();
  ScriptCoroutine section = sectionOf(module, 1);
  const CoroutineFailed error = failed(section.resume());
  CHECK_MESSAGE(error.message.contains("memory"), error.message);
  CHECK(error.stop == BudgetStop::None);
}

TEST_CASE("a pack cannot load another pack's script as a module") {
  auto vm = makeVm();
  MemoryPacks packs;
  packs.addPack("my_mod");
  packs.addPack("other_mod");
  packs.addFile("other_mod", "scripts/story.lua", "return {}");
  const auto& files = packs.mount();
  auto mod = environmentOf(*vm, "my_mod");
  CHECK(errorOf(mod.loadModule(files, assetId("scripts/story"))) ==
        "the script scripts/story comes from the pack 'other_mod', not from 'my_mod'");
}

TEST_CASE("a coroutine can get another module's function as its first argument") {
  auto vm = makeVm();
  std::vector<std::string>& reported = publishReport(*vm);
  publishWait(*vm);
  MemoryPacks packs;
  packs.addEnginePack("base");
  packs.addPack("my_mod");
  packs.addFile("base", "scripts/story.lua",
                "return { sections = { [1] = function()\n"
                "  test.report('original')\n"
                "  test.wait()\n"
                "  return true\n"
                "end } }");
  packs.addFile("my_mod", "scripts/patch.lua",
                "return { script = 7, sections = { [1] = function(original)\n"
                "  test.report('before')\n"
                "  local result = original()\n"
                "  test.report('after')\n"
                "  return result\n"
                "end, [3] = function(original)\n"
                "  test.report(original == nil and 'no original' or 'original')\n"
                "end, name = function() end }, [2] = 'not a section' }");
  const auto& files = packs.mount();
  auto baseModule = environmentOf(*vm, "base").loadModule(files, assetId("scripts/story"));
  REQUIRE_MESSAGE(baseModule.has_value(), baseModule.error().message);
  auto patch = environmentOf(*vm, "my_mod").loadModule(files, assetId("scripts/patch"));
  REQUIRE_MESSAGE(patch.has_value(), patch.error().message);

  const std::vector<ApiValue> scriptPath{std::string{"script"}};
  CHECK(patch->value(scriptPath).value_or(ApiValue{}) == ApiValue{std::int64_t{7}});
  const std::vector<ApiValue> missing{std::string{"nothing"}, std::string{"here"}};
  CHECK(patch->value(missing).value_or(ApiValue{false}) == ApiValue{});
  const std::vector<ApiValue> sectionsPath{std::string{"sections"}};
  CHECK(errorOf(patch->value(sectionsPath)) ==
        "my_mod:scripts/patch has a table at sections; it may hold nil, a boolean, a number or a "
        "text there");
  CHECK(patch->functionKeys(sectionsPath).value_or(std::vector<std::int64_t>{}) ==
        std::vector<std::int64_t>{1, 3});
  CHECK(patch->functionKeys(scriptPath).value_or(std::vector<std::int64_t>{0}).empty());

  const std::vector<ApiValue> first{std::string{"sections"}, std::int64_t{1}};
  auto wrapped = patch->coroutine(first, {.module = &*baseModule, .path = first});
  REQUIRE_MESSAGE(wrapped.has_value(), wrapped.error().message);
  // The original waits inside the patch's coroutine; a plain call is one level deeper.
  CHECK(waiting(wrapped->resume()).callDepth == 1);
  CHECK(reported == std::vector<std::string>{"before", "original"});
  CHECK(finished(wrapped->resume()) == ApiValue{true});
  CHECK(reported == std::vector<std::string>{"before", "original", "after"});

  reported.clear();
  const std::vector<ApiValue> third{std::string{"sections"}, std::int64_t{3}};
  auto alone = patch->coroutine(third, {.module = &*baseModule, .path = third});
  REQUIRE_MESSAGE(alone.has_value(), alone.error().message);
  CHECK(std::holds_alternative<CoroutineFinished>(alone->resume()));
  CHECK(reported == std::vector<std::string>{"no original"});
}
