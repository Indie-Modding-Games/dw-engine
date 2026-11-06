#include <dwe/assets/pack_manifest.hpp>
#include <dwe/script/api_registry.hpp>

#include <doctest/doctest.h>
#include <sol/sol.hpp>

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
using script::testing::makeVm;

namespace {

// story.describe(...): the calling pack and each argument's type and value, as text.
core::Result<ApiValue> describe(const ApiCall& call) {
  std::string text{call.pack()};
  for (const ApiValue& argument : call.arguments()) {
    text += '|';
    if (std::holds_alternative<std::monostate>(argument)) {
      text += "nil";
    } else if (const auto* flag = std::get_if<bool>(&argument)) {
      text += *flag ? "true" : "false";
    } else if (const auto* integer = std::get_if<std::int64_t>(&argument)) {
      text += "integer " + std::to_string(*integer);
    } else if (const auto* number = std::get_if<double>(&argument)) {
      text += "number " + std::to_string(*number);
    } else {
      text += "text " + std::get<std::string>(argument);
    }
  }
  return ApiValue{text};
}

} // namespace

TEST_CASE("a published function is called with the script's values and the calling pack") {
  auto vm = makeVm();
  REQUIRE(vm->api().publish("story", "describe", describe));
  REQUIRE(vm->runString("shown = story.describe(nil, true, 3, 2.5, 'agu')", "test"));
  CHECK(script::ScriptCall{*vm}.lua()["shown"].get<std::string>() ==
        "|nil|true|integer 3|number 2.500000|text agu");
  auto mod = environmentOf(*vm, "my_mod");
  REQUIRE(vm->api().publish("story", "twice", [](const ApiCall& call) -> core::Result<ApiValue> {
    auto value = call.integer(0);
    if (!value) {
      return std::unexpected{value.error()};
    }
    return ApiValue{*value * 2};
  }));
  REQUIRE(mod.run(
      "local shown = story.describe('x') local doubled = story.twice(21)"
      " if shown ~= 'my_mod|text x' or doubled ~= 42 or math.type(doubled) ~= 'integer' then"
      " error(shown .. ' ' .. doubled) end",
      "=my_mod"));
}

TEST_CASE("an API function's error is a Lua error at the calling line") {
  auto vm = makeVm();
  REQUIRE(vm->api().publish("story", "twice", [](const ApiCall& call) -> core::Result<ApiValue> {
    auto value = call.integer(0);
    if (!value) {
      return std::unexpected{value.error()};
    }
    return ApiValue{*value * 2};
  }));
  // The engine's globals add a traceback after the message.
  CHECK(errorOf(vm->runString("\nstory.twice('x')", "=test"))
            .starts_with("test:2: story.twice: argument 1 must be an integer\n"));
  auto mod = environmentOf(*vm, "my_mod");
  CHECK(
      errorOf(mod.run("story.twice({})", "=my_mod")) ==
      "my_mod:1: story.twice: argument 1 is a table; the API takes nil, booleans, numbers and texts");
  REQUIRE(vm->runString("ok, message = pcall(story.twice, 2.5) after = story.twice(2.0)", "=test"));
  CHECK(script::ScriptCall{*vm}.lua()["message"].get<std::string>() ==
        "story.twice: argument 1 must be an integer");
  CHECK(vm->number("after") == 4.0);
}

TEST_CASE("the API's arguments are read by type") {
  const std::vector<ApiValue> arguments{true, std::int64_t{7},    2.0,
                                        2.5,  std::string{"agu"}, std::monostate{}};
  const ApiCall call{"my_mod", arguments};
  CHECK(*call.boolean(0));
  CHECK(*call.integer(1) == 7);
  CHECK(*call.number(1) == 7.0);
  CHECK(*call.integer(2) == 2);
  CHECK(*call.number(3) == 2.5);
  CHECK(*call.text(4) == "agu");
  CHECK(errorOf(call.integer(3)) == "argument 4 must be an integer");
  CHECK(errorOf(call.text(5)) == "argument 6 must be a text");
  CHECK(errorOf(call.boolean(9)) == "argument 10 must be a boolean");
  CHECK(errorOf(call.number(4)) == "argument 5 must be a number");
}

TEST_CASE("namespaces and names are checked when they are published") {
  auto vm = makeVm();
  vm->bindCommand("give_item", [](std::string_view) {});
  const auto none = [](const ApiCall&) -> core::Result<ApiValue> { return ApiValue{}; };
  CHECK(errorOf(vm->api().publish("Story", "a", none)).contains("is not an API name"));
  CHECK(errorOf(vm->api().publish("story", "1a", none)).contains("is not an API name"));
  CHECK(errorOf(vm->api().publish("", "a", none)).contains("is not an API name"));
  CHECK(errorOf(vm->api().publish("hooks", "a", none)) ==
        "'hooks' is reserved: no API namespace can take it");
  for (const std::string_view taken : {"string", "math", "print", "pairs", "give_item"}) {
    CAPTURE(taken);
    CHECK(errorOf(vm->api().publish(taken, "a", none)).contains("would hide a global"));
  }
  REQUIRE(vm->api().publish("story", "a", none));
  CHECK(vm->api().isPublished("story", "a"));
  CHECK_FALSE(vm->api().isPublished("story", "b"));
  CHECK(errorOf(vm->api().publish("story", "a", none)) == "story.a is published already");
  CHECK(vm->api().publish("story", "b", none));
}

TEST_CASE("a function published after a pack's environment was made reaches it") {
  auto vm = makeVm();
  auto mod = environmentOf(*vm, "my_mod");
  REQUIRE(vm->api().publish("field", "map", [](const ApiCall&) -> core::Result<ApiValue> {
    return ApiValue{std::string{"twna01"}};
  }));
  REQUIRE(mod.run("if field.map() ~= 'twna01' then error('not published') end", "=my_mod"));
}

TEST_CASE("the engine's scripts cannot change a namespace either") {
  auto vm = makeVm();
  REQUIRE(vm->api().publish("story", "a",
                            [](const ApiCall&) -> core::Result<ApiValue> { return ApiValue{}; }));
  CHECK(errorOf(vm->runString("story.a = nil", "test")).contains("read-only"));
  CHECK(errorOf(vm->runString("setmetatable(story, nil)", "test")).contains("protected metatable"));
}
