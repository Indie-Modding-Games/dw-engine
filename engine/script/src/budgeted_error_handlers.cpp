#include "budgeted_error_handlers.hpp"

#include "budget.hpp"

namespace dwe::script::detail {
namespace {

constexpr int OriginalUpvalue = 1;
constexpr int HandlerUpvalue = 1;
constexpr int HandlerArgument = 2;

// The message handler xpcall gets: the script's handler, upvalue 1, unless the budget ran out. Then the error stays
// as it is.
int handleWithinBudget(lua_State* state) {
    if (budgetOf(state).left <= 0) {
        return 1;
    }
    lua_pushvalue(state, lua_upvalueindex(HandlerUpvalue));
    lua_insert(state, 1);
    lua_call(state, lua_gettop(state) - 1, 1);
    return 1;
}

// What xpcall returns, both when the call returns at once and when it resumes after f yielded.
int returnAll(lua_State* state, int /*status*/, lua_KContext /*context*/) {
    return lua_gettop(state);
}

// xpcall(f, handler, ...). Upvalue 1 is Lua's own. Called with a continuation, so f may yield inside a coroutine.
int xpcallWithinBudget(lua_State* state) {
    luaL_checktype(state, HandlerArgument, LUA_TFUNCTION);
    lua_pushvalue(state, HandlerArgument);
    lua_pushcclosure(state, handleWithinBudget, 1);
    lua_replace(state, HandlerArgument);
    lua_pushvalue(state, lua_upvalueindex(OriginalUpvalue));
    lua_insert(state, 1);
    lua_callk(state, lua_gettop(state) - 1, LUA_MULTRET, 0, returnAll);
    return returnAll(state, LUA_OK, 0);
}

} // namespace

void installBudgetedErrorHandlers(lua_State* state) {
    lua_getglobal(state, "xpcall");
    lua_pushcclosure(state, xpcallWithinBudget, 1);
    lua_setglobal(state, "xpcall");
}

} // namespace dwe::script::detail
