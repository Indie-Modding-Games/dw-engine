#include "api_functions.hpp"

#include <dwe/core/message.hpp>

#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include "locked_tables.hpp"
#include "lua_errors.hpp"
#include "messages.hpp"
#include "number_text.hpp"

namespace dwe::script::detail {
namespace {

constexpr int FunctionUpvalue = 1;
constexpr int PackUpvalue = 2;

bool isApiType(int type) {
  return type == LUA_TNIL || type == LUA_TBOOLEAN || type == LUA_TNUMBER || type == LUA_TSTRING;
}

const PublishedFunction& publishedFunction(lua_State* state) {
  return *static_cast<const PublishedFunction*>(
      lua_touserdata(state, lua_upvalueindex(FunctionUpvalue)));
}

std::string_view callingPack(lua_State* state) {
  std::size_t length = 0;
  const char* pack = lua_tolstring(state, lua_upvalueindex(PackUpvalue), &length);
  return {pack, length};
}

ApiValue readArgument(lua_State* state, int index) {
  switch (lua_type(state, index)) {
  case LUA_TBOOLEAN:
    return lua_toboolean(state, index) != 0;
  case LUA_TNUMBER:
    if (lua_isinteger(state, index) != 0) {
      return static_cast<std::int64_t>(lua_tointeger(state, index));
    }
    return static_cast<double>(lua_tonumber(state, index));
  case LUA_TSTRING: {
    std::size_t length = 0;
    const char* text = lua_tolstring(state, index, &length);
    return std::string{text, length};
  }
  default:
    return std::monostate{};
  }
}

// What a published function gave, pushed in a protected call: pushing may run out of memory.
struct CallOutcome {
  const core::Result<ApiValue>* result;
  std::string_view functionName;
};

int pushOutcome(lua_State* state) {
  const auto* outcome = static_cast<const CallOutcome*>(lua_touserdata(state, 1));
  if (outcome->result->has_value()) {
    pushApiValue(state, **outcome->result);
    return 1;
  }
  const std::string& message = outcome->result->error().message;
  lua_pushlstring(state, outcome->functionName.data(), outcome->functionName.size());
  lua_pushlstring(state, messages::ApiFunctionErrorSeparator.data(),
                  messages::ApiFunctionErrorSeparator.size());
  lua_pushlstring(state, message.data(), message.size());
  lua_concat(state, 3);
  return 1;
}

// Calls the published function with the arguments at 1..count and pushes its result, or its error
// message. Returns whether it failed. It never raises a Lua error, because its C++ values need
// their destructors.
bool runPublished(lua_State* state, int count) {
  const PublishedFunction& function = publishedFunction(state);
  std::vector<ApiValue> arguments;
  arguments.reserve(static_cast<std::size_t>(count));
  for (int index = 1; index <= count; ++index) {
    arguments.push_back(readArgument(state, index));
  }
  const core::Result<ApiValue> result = function.run(ApiCall{callingPack(state), arguments});
  CallOutcome outcome{.result = &result, .functionName = function.qualifiedName};
  lua_pushcfunction(state, pushOutcome);
  lua_pushlightuserdata(state, &outcome);
  if (lua_pcall(state, 1, 1, 0) != LUA_OK) {
    return true;
  }
  return !result.has_value();
}

int raiseBadArgument(lua_State* state, int index) {
  const NumberText number{index};
  return raiseError(state, {publishedFunction(state).qualifiedName, messages::BadApiArgument,
                            number.text(), messages::BadApiArgumentType,
                            luaL_typename(state, index), messages::BadApiArgumentEnd});
}

// The Lua function behind an API function. Upvalue 1 points to the PublishedFunction, upvalue 2 is
// the calling pack.
int callPublished(lua_State* state) {
  const int count = lua_gettop(state);
  for (int index = 1; index <= count; ++index) {
    if (!isApiType(lua_type(state, index))) {
      return raiseBadArgument(state, index);
    }
  }
  // Where the script called from, for an error; and room for runPublished's protected call.
  luaL_where(state, 1);
  luaL_checkstack(state, 3, nullptr);
  if (runPublished(state, count)) {
    lua_concat(state, 2);
    return lua_error(state);
  }
  return 1;
}

void pushApiFunction(lua_State* state, PublishedFunction& function, std::string_view pack) {
  // The function outlives every closure: the VM owns both.
  lua_pushlightuserdata(state, &function);
  lua_pushlstring(state, pack.data(), pack.size());
  lua_pushcclosure(state, callPublished, 2);
}

} // namespace

void pushApiValue(lua_State* state, const ApiValue& value) {
  std::visit(
      [state](const auto& held) {
        using Held = std::decay_t<decltype(held)>;
        if constexpr (std::is_same_v<Held, std::monostate>) {
          lua_pushnil(state);
        } else if constexpr (std::is_same_v<Held, bool>) {
          lua_pushboolean(state, held ? 1 : 0);
        } else if constexpr (std::is_same_v<Held, std::int64_t>) {
          lua_pushinteger(state, static_cast<lua_Integer>(held));
        } else if constexpr (std::is_same_v<Held, double>) {
          lua_pushnumber(state, static_cast<lua_Number>(held));
        } else {
          lua_pushlstring(state, held.data(), held.size());
        }
      },
      value);
}

void addApiFunction(lua_State* state, int environment, int namespaces, PublishedFunction& function,
                    std::string_view pack) {
  environment = lua_absindex(state, environment);
  namespaces = lua_absindex(state, namespaces);
  lua_pushlstring(state, function.apiNamespace.data(), function.apiNamespace.size());
  if (lua_rawget(state, namespaces) != LUA_TTABLE) {
    lua_pop(state, 1);
    lua_newtable(state);
    lua_pushlstring(state, function.apiNamespace.data(), function.apiNamespace.size());
    lua_pushvalue(state, -2);
    lua_rawset(state, namespaces);
    lua_pushlstring(state, function.apiNamespace.data(), function.apiNamespace.size());
    pushReadOnlyView(state, -2);
    lua_rawset(state, environment);
  }
  lua_pushlstring(state, function.name.data(), function.name.size());
  pushApiFunction(state, function, pack);
  lua_rawset(state, -3);
  lua_pop(state, 1);
}

} // namespace dwe::script::detail
