#include "address_free_text.hpp"

#include <string_view>

#include "memory.hpp"
#include "metafields.hpp"

namespace dwe::script::detail {
namespace {

constexpr int ValueArgument = 1;
constexpr int FormatArgument = 1;
constexpr int OriginalUpvalue = 1;
constexpr char Conversion = '%';
constexpr char PointerConversion = 'p';
constexpr char TextConversion = 's';
// What may come between '%' and its conversion: flags, width and precision (L_FMTFLAGSX and digits in lstrlib.c).
constexpr std::string_view OwnerSeparator = ": ";
constexpr std::string_view ConversionModifiers = "-+ #0123456789.";

// The name scripts see as the owner of the running call.
std::string_view ownerName(lua_State* state) {
    void* userData = nullptr;
    lua_getallocf(state, &userData);
    const auto& memory = *static_cast<const Memory*>(userData);
    return memory.ownerNames[memory.owner];
}

// Whether a value's text has no address in it: a plain value, or one with __tostring.
bool hasOwnText(lua_State* state, int index) {
    const int type = lua_type(state, index);
    if (type == LUA_TNIL || type == LUA_TBOOLEAN || type == LUA_TNUMBER || type == LUA_TSTRING) {
        return true;
    }
    const bool hasToString = luaL_getmetafield(state, index, ToStringField) != LUA_TNIL;
    if (hasToString) {
        lua_pop(state, 1);
    }
    return hasToString;
}

int addressFreeToString(lua_State* state) {
    luaL_checkany(state, ValueArgument);
    pushAddressFreeText(state, ValueArgument);
    return 1;
}

// string.format with %p and %s giving address-free texts: the format and the arguments are rewritten before Lua's own
// format runs (upvalue 1). Everything lives on the Lua stack, since a Lua error unwinds with longjmp.
int addressFreeFormat(lua_State* state) {
    std::size_t length = 0;
    const char* format = luaL_checklstring(state, FormatArgument, &length);
    const int arguments = lua_gettop(state);
    luaL_Buffer rewritten;
    luaL_buffinit(state, &rewritten);
    int argument = FormatArgument;
    std::size_t at = 0;
    while (at < length) {
        const char c = format[at++];
        luaL_addchar(&rewritten, c);
        if (c != Conversion) {
            continue;
        }
        if (at < length && format[at] == Conversion) {
            luaL_addchar(&rewritten, format[at++]);
            continue;
        }
        while (at < length && ConversionModifiers.contains(format[at])) {
            luaL_addchar(&rewritten, format[at++]);
        }
        if (at >= length) {
            break;
        }
        const char conversion = format[at++];
        ++argument;
        const bool replaced = argument <= arguments && (conversion == PointerConversion ||
                                                        (conversion == TextConversion && !hasOwnText(state, argument)));
        if (replaced) {
            pushAddressFreeText(state, argument);
            lua_replace(state, argument);
        }
        luaL_addchar(&rewritten, replaced ? TextConversion : conversion);
    }
    luaL_pushresult(&rewritten);
    lua_replace(state, FormatArgument);
    lua_pushvalue(state, lua_upvalueindex(OriginalUpvalue));
    lua_insert(state, 1);
    lua_call(state, arguments, 1);
    return 1;
}

} // namespace

void pushAddressFreeText(lua_State* state, int index) {
    index = lua_absindex(state, index);
    if (hasOwnText(state, index)) {
        luaL_tolstring(state, index, nullptr);
        return;
    }
    // The name is the __name metafield, as luaL_tolstring has it, or the type's.
    const int name = luaL_getmetafield(state, index, NameField);
    if (name != LUA_TSTRING) {
        if (name != LUA_TNIL) {
            lua_pop(state, 1);
        }
        lua_pushstring(state, luaL_typename(state, index));
    }
    const std::string_view owner = ownerName(state);
    lua_pushlstring(state, OwnerSeparator.data(), OwnerSeparator.size());
    lua_pushlstring(state, owner.data(), owner.size());
    lua_concat(state, 3);
}

void installAddressFreeText(lua_State* state) {
    lua_pushcfunction(state, addressFreeToString);
    lua_setglobal(state, "tostring");
    lua_getglobal(state, LUA_STRLIBNAME);
    lua_getfield(state, -1, "format");
    lua_pushcclosure(state, addressFreeFormat, 1);
    lua_setfield(state, -2, "format");
    lua_pop(state, 1);
}

} // namespace dwe::script::detail
