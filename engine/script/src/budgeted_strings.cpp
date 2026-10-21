#include "budgeted_strings.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "budget.hpp"
#include "byte_charges.hpp"
#include "lua_errors.hpp"
#include "messages.hpp"

namespace dwe::script::detail {
namespace {

constexpr int TextArgument = 1;
constexpr int CountArgument = 2;
constexpr int SeparatorArgument = 3;

// The longest text Lua can hold (MAX_SIZE in llimits.h, for a 64-bit lua_Integer).
constexpr auto MaxTextSize = static_cast<std::size_t>(LUA_MAXINTEGER);

constexpr std::array StringFunctionsByLength{"byte",     "char",    "format", "lower",  "pack",
                                             "packsize", "reverse", "sub",    "unpack", "upper"};
constexpr std::array Utf8FunctionsByLength{"char", "codepoint", "len", "offset"};

constexpr int OriginalUpvalue = 1;
constexpr int IteratorResult = 1;
constexpr int PositionArgument = 2;
constexpr int PositionResult = 1;
constexpr std::int64_t CodeStep = 1;

// string.rep as lstrlib.c (str_rep) has it, charging for every byte of the result before building it.
int repeatText(lua_State* state) {
    std::size_t length = 0;
    const char* text = luaL_checklstring(state, TextArgument, &length);
    const lua_Integer count = luaL_checkinteger(state, CountArgument);
    std::size_t separatorLength = 0;
    const char* separator = luaL_optlstring(state, SeparatorArgument, "", &separatorLength);
    if (count <= 0 || (length | separatorLength) == 0) {
        lua_pushliteral(state, "");
        return 1;
    }
    const auto copies = static_cast<std::size_t>(count);
    if (length > MaxTextSize - separatorLength || length + separatorLength > MaxTextSize / copies) {
        return raiseError(state, {messages::ResultTooLarge});
    }
    const std::size_t total = (copies * (length + separatorLength)) - separatorLength;
    chargeBudget(state, stepsForBytes(total));
    luaL_Buffer result;
    char* out = luaL_buffinitsize(state, &result, total);
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic): out is the buffer of total bytes.
    for (std::size_t copy = 1; copy < copies; ++copy) {
        std::memcpy(out, text, length);
        out += length;
        std::memcpy(out, separator, separatorLength);
        out += separatorLength;
    }
    // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    std::memcpy(out, text, length);
    luaL_pushresultsize(&result, total);
    return 1;
}

// A step of the iterator that utf8.codes gives: the original one (upvalue 1), then a charge for the bytes it went
// over, since it skips continuation bytes in a C loop. When there is no next character it went to the end.
int stepCodesCharged(lua_State* state) {
    std::size_t length = 0;
    luaL_checklstring(state, TextArgument, &length);
    const auto from = static_cast<lua_Unsigned>(lua_tointeger(state, PositionArgument));
    lua_pushvalue(state, lua_upvalueindex(OriginalUpvalue));
    lua_insert(state, 1);
    lua_call(state, lua_gettop(state) - 1, LUA_MULTRET);
    // Where the step stopped: the next character, or the end of the text when there is none.
    lua_Unsigned to = length;
    if (lua_isinteger(state, PositionResult) != 0) {
        to = static_cast<lua_Unsigned>(lua_tointeger(state, PositionResult));
    }
    std::int64_t steps = CodeStep;
    if (to > from) {
        steps += stepsForBytes(std::min(static_cast<std::size_t>(to - from), length));
    }
    chargeBudget(state, steps);
    return lua_gettop(state);
}

// utf8.codes: the original one (upvalue 1), with its iterator wrapped by stepCodesCharged.
int codesCharged(lua_State* state) {
    chargeBudget(state, CodeStep);
    lua_pushvalue(state, lua_upvalueindex(OriginalUpvalue));
    lua_insert(state, 1);
    lua_call(state, lua_gettop(state) - 1, LUA_MULTRET);
    lua_pushvalue(state, IteratorResult);
    lua_pushcclosure(state, stepCodesCharged, 1);
    lua_replace(state, IteratorResult);
    return lua_gettop(state);
}

} // namespace

void installBudgetedStrings(lua_State* state) {
    lua_getglobal(state, LUA_STRLIBNAME);
    for (const char* name : StringFunctionsByLength) {
        chargeForBytes(state, -1, name);
    }
    lua_pushcfunction(state, repeatText);
    lua_setfield(state, -2, "rep");
    lua_pop(state, 1);
    lua_getglobal(state, LUA_UTF8LIBNAME);
    for (const char* name : Utf8FunctionsByLength) {
        chargeForBytes(state, -1, name);
    }
    lua_getfield(state, -1, "codes");
    lua_pushcclosure(state, codesCharged, 1);
    lua_setfield(state, -2, "codes");
    lua_pop(state, 1);
}

} // namespace dwe::script::detail
