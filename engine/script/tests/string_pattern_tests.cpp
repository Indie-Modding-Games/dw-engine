#include <doctest/doctest.h>
#include <lua.hpp>
#include <sol/sol.hpp>

#include <array>
#include <chrono>
#include <memory>
#include <string>
#include <string_view>

#include "script_test_support.hpp"

using namespace dwe;
using script::testing::errorOf;
using script::testing::makeVm;
using script::testing::QuickStop;
using script::testing::SmallLimits;
using script::testing::stopTime;

namespace {

// Evaluates an expression and keeps its results, or its error, as text in the global `result`.
std::string parityChunk(std::string_view expression) {
    return std::string{"local function show(...) local parts = {} for i = 1, select('#', ...) do"
                       " local v = select(i, ...) parts[#parts + 1] = type(v) .. ':' .. tostring(v) end"
                       " return table.concat(parts, ',') end"
                       " local function collect(iterator) local out = {} for a, b in iterator do"
                       " out[#out + 1] = tostring(a) .. '|' .. tostring(b) end return table.concat(out, ';') end"
                       " local ok, shown = pcall(function() return show("} +
           std::string{expression} + ") end) result = (ok and 'ok ' or 'error ') .. tostring(shown)";
}

// Lua's own string library, to compare with.
class ReferenceLua {
public:
    ReferenceLua() : state_{luaL_newstate(), lua_close} { luaL_openlibs(state_.get()); }

    std::string evaluate(std::string_view expression) {
        const std::string chunk = parityChunk(expression);
        lua_State* state = state_.get();
        REQUIRE(luaL_loadbufferx(state, chunk.data(), chunk.size(), "=parity", "t") == LUA_OK);
        REQUIRE(lua_pcall(state, 0, 0, 0) == LUA_OK);
        lua_getglobal(state, "result");
        std::size_t length = 0;
        const char* text = lua_tolstring(state, -1, &length);
        std::string result{text, length};
        lua_pop(state, 1);
        return result;
    }

private:
    std::unique_ptr<lua_State, void (*)(lua_State*)> state_;
};

std::string evaluate(script::ScriptVm& vm, std::string_view expression) {
    REQUIRE(vm.runString(parityChunk(expression), "=parity"));
    return script::ScriptCall{vm}.lua()["result"].get<std::string>();
}

} // namespace

TEST_CASE("the budgeted pattern functions give what Lua's own give") {
    ReferenceLua reference;
    auto vm = makeVm();
    const auto expressions = std::to_array<std::string_view>({
        R"lua(string.find("hello world", "o w"))lua",
        R"lua(string.find("hello world", "o", 6))lua",
        R"lua(string.find("hello", "l", -2))lua",
        R"lua(string.find("hello", "l", -100))lua",
        R"lua(string.find("hello", "", 10))lua",
        R"lua(string.find("hello", "", 6))lua",
        R"lua(string.find("a.b", ".", 1, true))lua",
        R"lua(string.find("a+b", "+", 1, true))lua",
        R"lua(string.find("hello", "(l)(l)"))lua",
        R"lua(string.find("hello", "()ll()"))lua",
        R"lua(("hello"):find("l+"))lua",
        R"lua(string.find("a\0b\0c", "b\0c"))lua",
        R"lua(string.match("key = value", "(%w+)%s*=%s*(%w+)"))lua",
        R"lua(string.match("  trim me  ", "^%s*(.-)%s*$"))lua",
        R"lua(string.match("x [[nested] list] y", "%b[]"))lua",
        R"lua(string.match("THE (quick) fox", "%f[%a]%a+", 5))lua",
        R"lua(string.match("abc", "((a)(b))"))lua",
        R"lua(string.match("aaa", "a-b"))lua",
        R"lua(string.match("aaab", "a-b"))lua",
        R"lua(string.match("hello", ".-(l+)(.*)"))lua",
        R"lua(string.match("2024-01-15", "(%d+)-(%d+)-(%d+)"))lua",
        R"lua(string.match("abcabc", "(abc)%1"))lua",
        R"lua(string.match("[a]", "[%]]"))lua",
        R"lua(string.match("a-b", "[a%-]+"))lua",
        R"lua(string.match("x]y", "[]]"))lua",
        R"lua(string.match("^x", "^^x"))lua",
        R"lua(string.match("x$", "x$$"))lua",
        R"lua(string.match("abc", "[^a]+"))lua",
        R"lua(string.match("ABC123def", "[A-Z]+"))lua",
        R"lua(string.match("ABC123def", "%u+%d+(%l?)"))lua",
        R"lua(string.match("a\0b", "%z"))lua",
        R"lua(string.match("tab\there", "%c"))lua",
        R"lua(string.match("-12.5e3", "^[+-]?%d+%.?%d*[eE]?%d*$"))lua",
        R"lua(string.match("hello", "()", 3))lua",
        R"lua(string.match("hello", "x*"))lua",
        R"lua(string.match("", ".?"))lua",
        R"lua(string.gsub("hello world", "o", "0"))lua",
        R"lua(string.gsub("hello world", "(%w+)", "<%1>"))lua",
        R"lua(string.gsub("hello", "", "-"))lua",
        R"lua(string.gsub("abc", "%w", "%0%0"))lua",
        R"lua(string.gsub("abc", "%w", "%%"))lua",
        R"lua(string.gsub("abc", "b", 7))lua",
        R"lua(string.gsub("hello world", "%w+", {hello = "HI", world = false}))lua",
        R"lua(string.gsub("hello world", "%w+", function(word) return word:upper() end))lua",
        R"lua(string.gsub("hello world", "%w+", function() return nil end))lua",
        R"lua(string.gsub("hello world", "(%w+) (%w+)", "%2 %1"))lua",
        R"lua(string.gsub("hello", "l", "L", 1))lua",
        R"lua(string.gsub("hello", "l", "L", 0))lua",
        R"lua(string.gsub("abc", "^a", "X"))lua",
        R"lua(string.gsub("abc", "()", "%1"))lua",
        R"lua(string.gsub("abc", "%w", {a = 1}))lua",
        R"lua(collect(("one two  three"):gmatch("%a+")))lua",
        R"lua(collect(("k=v, a=b"):gmatch("(%w+)=(%w+)")))lua",
        R"lua(collect(("abc"):gmatch("")))lua",
        R"lua(collect(("abcd"):gmatch(".", 3)))lua",
        R"lua(collect(("abcd"):gmatch(".", 10)))lua",
        R"lua(collect(("^a^a"):gmatch("^a")))lua",
        R"lua(string.find("a", "%"))lua",
        R"lua(string.find("a", "[a"))lua",
        R"lua(string.find("a", "[%"))lua",
        R"lua(string.find("a", "(a"))lua",
        R"lua(string.find("a", "a)"))lua",
        R"lua(string.match("a", "%1"))lua",
        R"lua(string.match("aa", "(a%1)"))lua",
        R"lua(string.gsub("a", "a", "%2"))lua",
        R"lua(string.gsub("a", "a", "%x"))lua",
        R"lua(string.gsub("a", "a", "x%"))lua",
        R"lua(string.gsub("a", "a", true))lua",
        R"lua(string.gsub("a", "(a", "x"))lua",
        R"lua(string.gsub("abc", "%w", {a = {}}))lua",
        R"lua(string.match("a", "%f"))lua",
        R"lua(string.match("a", "%b"))lua",
        R"lua(string.match("a", "%ba"))lua",
        R"lua(string.match("a", ("()"):rep(33)))lua",
        R"lua(string.match(("a"):rep(300), ("a?"):rep(300) .. ("a"):rep(300)))lua",
        R"lua(string.find({}, "a"))lua",
        R"lua(string.find("a"))lua",
    });
    for (const std::string_view expression : expressions) {
        CAPTURE(expression);
        CHECK(evaluate(*vm, expression) == reference.evaluate(expression));
    }
}

TEST_CASE("patterns that backtrack for too long are stopped by the budget") {
    auto vm = makeVm(SmallLimits);
    const auto sources = std::to_array<std::string_view>({
        R"lua(string.find(("a"):rep(5000), ("a*"):rep(12) .. "b"))lua",
        R"lua(string.match(("a"):rep(5000), ("a-"):rep(12) .. "b"))lua",
        R"lua(for _ in (("a"):rep(5000)):gmatch(("a*"):rep(12) .. "b") do end)lua",
        R"lua(string.gsub(("a"):rep(5000), ("a*"):rep(12) .. "b", ""))lua",
        R"lua(string.find(("a"):rep(5000), ("(a*)"):rep(8) .. "%8%7b"))lua",
        R"lua(string.find(("("):rep(20000), "%b()"))lua",
        R"lua(string.find(("a"):rep(2000000), ("a"):rep(1000000) .. "b", 1, true))lua",
        R"lua(local s = ("a"):rep(3000) while true do pcall(string.find, s, ".-.-.-b") end)lua",
    });
    for (const std::string_view source : sources) {
        CAPTURE(source);
        CHECK(stopTime(*vm, source) < QuickStop);
    }
}

TEST_CASE("strings' methods use the budgeted patterns") {
    auto vm = makeVm(SmallLimits);
    CHECK(stopTime(*vm, R"lua(local s = ("a"):rep(5000) s:find(("a*"):rep(12) .. "b"))lua") < QuickStop);
}

TEST_CASE("after a stopped pattern, the next call has a full budget") {
    auto vm = makeVm(SmallLimits);
    CHECK_FALSE(vm->runString(R"lua(string.find(("a"):rep(5000), ("a*"):rep(12) .. "b"))lua", "test"));
    REQUIRE(vm->runString(R"lua(found = string.find(("a"):rep(5000) .. "b", "a*b"))lua", "test"));
    CHECK(script::ScriptCall{*vm}.lua()["found"].get<int>() == 1);
}
