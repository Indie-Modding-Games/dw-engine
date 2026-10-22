#include <dwe/assets/pack_manifest.hpp>
#include <dwe/script/mod_environment.hpp>

#include <doctest/doctest.h>
#include <sol/sol.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "script_test_support.hpp"

using namespace dwe;
using script::testing::assetId;
using script::testing::environmentOf;
using script::testing::errorOf;
using script::testing::LogCapture;
using script::testing::makeVm;
using script::testing::MemoryPacks;
using script::testing::packNamed;
using script::testing::publishReport;
using script::testing::SmallLimits;

TEST_CASE("a pack's environment has the safe standard library and nothing of the engine's") {
  auto vm = makeVm();
  auto& reported = publishReport(*vm);
  vm->bindCommand("give_item", [](std::string_view) {});
  REQUIRE(vm->runString("engine_secret = 42", "test"));
  auto mod = environmentOf(*vm, "my_mod");
  REQUIRE(mod.run(
      "test.report(type(string.format)) test.report(type(table.insert)) test.report(type(math.floor))"
      " test.report(type(utf8.char)) test.report(type(coroutine.wrap)) test.report(type(pcall))"
      " test.report(type(hooks.on)) test.report(_G == _ENV)",
      "=my_mod"));
  CHECK(reported == std::vector<std::string>{"function", "function", "function", "function",
                                             "function", "function", "function", "true"});
  for (const std::string_view name : {"engine_secret", "give_item", "io", "os", "debug", "package",
                                      "require", "load", "loadfile", "dofile"}) {
    CAPTURE(name);
    CHECK(errorOf(mod.run(std::string{"local x = "} + std::string{name}, "=my_mod"))
              .contains("unknown global"));
  }
  reported.clear();
  REQUIRE(mod.run("test.report(type(string.dump)) test.report(type(('').dump))", "=my_mod"));
  CHECK(reported == std::vector<std::string>{"nil", "nil"});
}

TEST_CASE("a pack's scripts cannot make globals, so their state lives in locals") {
  auto vm = makeVm();
  auto& reported = publishReport(*vm);
  auto mod = environmentOf(*vm, "my_mod");
  CHECK(errorOf(mod.run("counter = 1", "=my_mod")) ==
        "my_mod:1: cannot assign the global 'counter': declare it local");
  CHECK(errorOf(mod.run("function helper() end", "=my_mod"))
            .contains("cannot assign the global 'helper'"));
  CHECK(
      errorOf(mod.run("_G.counter = 1", "=my_mod")).contains("cannot assign the global 'counter'"));
  CHECK(errorOf(mod.run("_ENV[1] = true", "=my_mod")).contains("a key that is not a text"));
  CHECK(errorOf(mod.run("test.report(pirnt)", "=my_mod")) ==
        "my_mod:1: unknown global 'pirnt': a pack's scripts keep their variables in locals");
  REQUIRE(mod.run(
      "local counter = 1 local function bump() counter = counter + 1 end bump() test.report(counter)",
      "=my_mod"));
  CHECK(reported == std::vector<std::string>{"2"});
}

TEST_CASE("a pack cannot unlock its environment or the tables the engine shares") {
  auto vm = makeVm();
  auto& reported = publishReport(*vm);
  auto mod = environmentOf(*vm, "my_mod");
  struct Escape {
    std::string_view source;
    std::string_view error;
  };
  constexpr std::string_view LockedRawset = "rawset cannot change a table locked by the engine";
  constexpr std::string_view ProtectedMetatable = "cannot change a protected metatable";
  constexpr std::string_view ReadOnly = "this table of the engine is read-only";
  // getmetatable gives a text in place of a locked metatable, so there is no metatable to change,
  // nor its fields.
  constexpr std::string_view NoMetatable = "attempt to index a string value";
  for (const auto& [source, error] : {
           Escape{.source = "rawset(_ENV, 'counter', 1)", .error = LockedRawset},
           Escape{.source = "setmetatable(_ENV, nil)", .error = ProtectedMetatable},
           Escape{.source = "getmetatable(_ENV).__newindex = nil", .error = NoMetatable},
           Escape{.source = "rawset(test, 'report', print)", .error = LockedRawset},
           Escape{.source = "test.report = print", .error = ReadOnly},
           Escape{.source = "test.extra = 1", .error = ReadOnly},
           Escape{.source = "setmetatable(test, nil)", .error = ProtectedMetatable},
           Escape{.source = "getmetatable('').__index.find = nil",
                  .error = "attempt to index a nil value"},
           Escape{.source = "rawset(hooks, 'on', print)", .error = LockedRawset},
           Escape{.source = "hooks.on = print", .error = ReadOnly},
       }) {
    CAPTURE(source);
    const std::string message = errorOf(mod.run(source, "=my_mod"));
    CHECK_MESSAGE(message.contains(error), message);
  }
  REQUIRE(mod.run(
      "test.report(getmetatable(test)) test.report(getmetatable('')) test.report(getmetatable(_ENV))"
      " test.report(rawget(test, 'report'))",
      "=my_mod"));
  CHECK(reported == std::vector<std::string>{"locked by the engine", "locked by the engine",
                                             "locked by the engine", "nil"});
}

TEST_CASE("one pack cannot read or change another pack's state or the engine's") {
  auto vm = makeVm();
  auto& reported = publishReport(*vm);
  auto first = environmentOf(*vm, "first");
  auto second = environmentOf(*vm, "second");
  REQUIRE(
      first.run("string.upper = nil math.pi = 3 table.secret = 'first' coroutine = nil", "=first"));
  REQUIRE(second.run("test.report(type(string.upper)) test.report(tostring(math.pi > 3))"
                     " test.report(table.secret) test.report(type(coroutine))",
                     "=second"));
  CHECK(reported == std::vector<std::string>{"function", "true", "nil", "table"});
  REQUIRE(vm->runString("engine_upper = type(string.upper) engine_pi = math.pi", "test"));
  CHECK(script::ScriptCall{*vm}.lua()["engine_upper"].get<std::string>() == "function");
  CHECK(vm->number("engine_pi") > 3.0);
}

TEST_CASE("the environment of a pack is made once") {
  auto vm = makeVm();
  auto& reported = publishReport(*vm);
  auto first = environmentOf(*vm, "my_mod");
  REQUIRE(first.run("string.mine = 'kept'", "=my_mod"));
  auto again = environmentOf(*vm, "my_mod");
  CHECK(again.pack() == "my_mod");
  REQUIRE(again.run("test.report(string.mine)", "=my_mod"));
  CHECK(reported == std::vector<std::string>{"kept"});
}

TEST_CASE("a pack written for another major version of the API gets no environment") {
  auto vm = makeVm();
  auto pack = packNamed("old_mod");
  pack.api = assets::ApiRequirement{.major = 0, .minimumMinor = std::nullopt};
  CHECK(errorOf(vm->modEnvironment(pack)) ==
        "pack 'old_mod': the scripts need the Lua API 0.x, and this engine has 1.1.0");
  pack.api = assets::ApiRequirement{.major = 1, .minimumMinor = std::nullopt};
  CHECK(vm->modEnvironment(pack).has_value());
}

TEST_CASE("a pack's scripts are stopped like the engine's: loops, pcall, coroutines and memory") {
  auto vm = makeVm(SmallLimits);
  auto mod = environmentOf(*vm, "my_mod");
  for (
      const std::string_view source : {
          "while true do end",
          "local function spin() while true do end end while true do pcall(spin) end",
          "local spin = coroutine.wrap(function() while true do coroutine.yield() end end) while true do spin() end",
          "local ok, message = coroutine.resume(coroutine.create(function() while true do end end)) error(message)",
          "local ok = pcall(error) while true do ok = coroutine.resume(coroutine.create(function() end)) end",
          R"lua(string.find(("a"):rep(5000), ("a*"):rep(12) .. "b"))lua",
      }) {
    CAPTURE(source);
    CHECK(errorOf(mod.run(source, "=my_mod")).contains("ran too long"));
  }
  CHECK(mod.run("local fine = 1 + 1", "=my_mod"));
}

TEST_CASE("a pack's scripts cannot take more memory than the VM has") {
  auto vm = makeVm({.memoryBytes = std::size_t{4} * 1024 * 1024,
                    .instructionsPerCall = 100'000'000,
                    .callDeadline = script::testing::TestDeadline});
  auto mod = environmentOf(*vm, "my_mod");
  CHECK(errorOf(mod.run("local t = {} for i = 1, 100000000 do t[i] = ('x'):rep(64) .. i end",
                        "=my_mod"))
            .contains("not enough memory"));
  CHECK(
      errorOf(mod.run("local s = ('x'):rep(1000000000)", "=my_mod")).contains("not enough memory"));
  CHECK(mod.run("local fine = 1 + 1", "=my_mod"));
}

TEST_CASE("a pack prints to the log with its id") {
  const LogCapture log;
  auto vm = makeVm();
  auto mod = environmentOf(*vm, "my_mod");
  REQUIRE(mod.run("print('hello', 1)", "=my_mod"));
  CHECK(log.contains("mod my_mod: hello\t1"));
}

TEST_CASE("a pack's scripts are read from the packs by asset id") {
  auto vm = makeVm();
  auto& reported = publishReport(*vm);
  MemoryPacks packs;
  packs.addPack("my_mod", "api: \"1.x\"\n");
  packs.addPack("other_mod");
  packs.addFile("my_mod", "scripts/main.lua", "local name = 'main' test.report(name)");
  packs.addFile("my_mod", "scripts/broken.lua", "local x = 1\nerror('broken')");
  packs.addFile("my_mod", "scripts/data.yaml", "test.report('yaml')");
  packs.addFile("other_mod", "scripts/other.lua", "test.report('other')");
  const auto& files = packs.mount();
  REQUIRE(vm->runModScript(files, assetId("scripts/main")));
  REQUIRE(vm->runModScript(files, assetId("scripts/other")));
  CHECK(reported == std::vector<std::string>{"main", "other"});
  CHECK(errorOf(vm->runModScript(files, assetId("scripts/broken"))) ==
        "my_mod:scripts/broken:2: broken");
  CHECK(errorOf(vm->runModScript(files, assetId("scripts/data"))).contains("is not a Lua script"));
  CHECK(errorOf(vm->runModScript(files, assetId("scripts/missing"))) ==
        "no pack has the script scripts/missing");
  auto mod = environmentOf(*vm, "my_mod");
  CHECK(errorOf(mod.runAsset(files, assetId("scripts/other"))) ==
        "the script scripts/other comes from the pack 'other_mod', not from 'my_mod'");
}

TEST_CASE(
    "the engine's scripts can also be read from the packs, and both reload when their text changes") {
  auto vm = makeVm();
  auto& reported = publishReport(*vm);
  MemoryPacks packs;
  packs.addEnginePack("game");
  packs.addFile("game", "scripts/dev.lua", "value = 1");
  packs.addPack("my_mod");
  packs.addFile("my_mod", "scripts/main.lua", "test.report('first')");
  const auto& files = packs.mount();
  REQUIRE(vm->runAsset(files, assetId("scripts/dev")));
  REQUIRE(vm->runModScript(files, assetId("scripts/main")));
  vm->reloadChanged();
  CHECK(vm->number("value") == 1.0);
  CHECK(reported == std::vector<std::string>{"first"});

  packs.addFile("game", "scripts/dev.lua", "value = 2");
  packs.addFile("my_mod", "scripts/main.lua", "test.report('second')");
  vm->reloadChanged();
  CHECK(vm->number("value") == 2.0);
  CHECK(reported == std::vector<std::string>{"first", "second"});
}
