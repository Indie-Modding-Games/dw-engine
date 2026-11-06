#include "budgeted_coroutines.hpp"

#include <cstdint>
#include <string_view>

#include "budget.hpp"
#include "lua_errors.hpp"
#include "messages.hpp"

namespace dwe::script::detail {
namespace {

constexpr int CoroutineArgument = 1;
constexpr int OriginalUpvalue = 1;
constexpr int ThreadUpvalue = 1;
constexpr int ResumeUpvalue = 2;
constexpr int CreateUpvalue = 1;
constexpr std::int64_t ResumeStep = 1;
constexpr const char* EngineThreadsField = "dwe.engine_threads";
constexpr std::string_view ResumeName = "coroutine.resume";
constexpr std::string_view CloseName = "coroutine.close";

// A section's thread reached through coroutine.running could otherwise be resumed or closed by a
// script, which would skip its wait.
bool isEngineThread(lua_State* state, int index) {
  if (lua_type(state, index) != LUA_TTHREAD) {
    return false;
  }
  index = lua_absindex(state, index);
  lua_getfield(state, LUA_REGISTRYINDEX, EngineThreadsField);
  lua_pushvalue(state, index);
  const bool engine = lua_rawget(state, -2) != LUA_TNIL;
  lua_pop(state, 2);
  return engine;
}

// coroutine.resume. Upvalue 1 is Lua's own, which this one calls after arming the coroutine's hook;
// while it runs, the budget knows the coroutine as the running thread, so an allocation that
// empties the budget stops it. Arming restarts the hook's count, so the instructions the coroutine
// may run before its first check are paid up front.
int resumeCharged(lua_State* state) {
  if (isEngineThread(state, CoroutineArgument)) {
    return raiseError(state, {ResumeName, messages::EngineCoroutine});
  }
  chargeBudget(state, CoroutineCheckInterval);
  lua_State* coroutine = lua_tothread(state, CoroutineArgument);
  Budget& budget = budgetOf(state);
  lua_State* resumer = budget.running;
  if (coroutine != nullptr) {
    budget.arm(coroutine);
    budget.running = coroutine;
  }
  lua_pushvalue(state, lua_upvalueindex(OriginalUpvalue));
  lua_insert(state, 1);
  const int status = lua_pcall(state, lua_gettop(state) - 1, LUA_MULTRET, 0);
  budget.running = resumer;
  if (status != LUA_OK) {
    return lua_error(state);
  }
  chargeBudget(state, ResumeStep);
  return lua_gettop(state);
}

// The function coroutine.wrap gives, as lcorolib.c (luaB_auxwrap) has it, resuming through
// resumeCharged. Upvalue 1 is the coroutine, upvalue 2 resumeCharged.
int resumeWrapped(lua_State* state) {
  const int arguments = lua_gettop(state);
  lua_pushvalue(state, lua_upvalueindex(ResumeUpvalue));
  lua_pushvalue(state, lua_upvalueindex(ThreadUpvalue));
  lua_rotate(state, 1, 2);
  lua_call(state, arguments + 1, LUA_MULTRET);
  if (lua_toboolean(state, 1) != 0) {
    lua_remove(state, 1);
    return lua_gettop(state);
  }
  lua_settop(state, 2);
  lua_State* coroutine = lua_tothread(state, lua_upvalueindex(ThreadUpvalue));
  int status = lua_status(coroutine);
  if (status != LUA_OK && status != LUA_YIELD) {
    // Closes its to-be-closed variables; their error, if any, replaces the coroutine's.
    status = lua_closethread(coroutine, state);
    lua_pop(state, 1);
    lua_xmove(coroutine, state, 1);
  }
  if (status != LUA_ERRMEM && lua_type(state, -1) == LUA_TSTRING) {
    luaL_where(state, 1);
    lua_insert(state, -2);
    lua_concat(state, 2);
  }
  return lua_error(state);
}

// coroutine.wrap. Upvalue 1 is coroutine.create, upvalue 2 resumeCharged.
int wrapCharged(lua_State* state) {
  lua_pushvalue(state, lua_upvalueindex(CreateUpvalue));
  lua_insert(state, 1);
  lua_call(state, lua_gettop(state) - 1, 1);
  lua_pushvalue(state, lua_upvalueindex(ResumeUpvalue));
  lua_pushcclosure(state, resumeWrapped, 2);
  return 1;
}

// coroutine.close. Upvalue 1 is Lua's own.
int closeGuarded(lua_State* state) {
  if (isEngineThread(state, CoroutineArgument)) {
    return raiseError(state, {CloseName, messages::EngineCoroutine});
  }
  lua_pushvalue(state, lua_upvalueindex(OriginalUpvalue));
  lua_insert(state, 1);
  lua_call(state, lua_gettop(state) - 1, LUA_MULTRET);
  return lua_gettop(state);
}

} // namespace

void markEngineThread(lua_State* state, int index) {
  index = lua_absindex(state, index);
  lua_getfield(state, LUA_REGISTRYINDEX, EngineThreadsField);
  lua_pushvalue(state, index);
  lua_pushboolean(state, 1);
  lua_rawset(state, -3);
  lua_pop(state, 1);
}

void installBudgetedCoroutines(lua_State* state) {
  lua_newtable(state);
  lua_newtable(state);
  lua_pushliteral(state, "k");
  lua_setfield(state, -2, "__mode");
  lua_setmetatable(state, -2);
  lua_setfield(state, LUA_REGISTRYINDEX, EngineThreadsField);
  lua_getglobal(state, LUA_COLIBNAME);
  const int library = lua_gettop(state);
  lua_getfield(state, library, "close");
  lua_pushcclosure(state, closeGuarded, 1);
  lua_setfield(state, library, "close");
  lua_getfield(state, library, "resume");
  lua_pushcclosure(state, resumeCharged, 1);
  lua_getfield(state, library, "create");
  lua_pushvalue(state, -2);
  lua_pushcclosure(state, wrapCharged, 2);
  lua_setfield(state, library, "wrap");
  lua_setfield(state, library, "resume");
  lua_pop(state, 1);
}

} // namespace dwe::script::detail
