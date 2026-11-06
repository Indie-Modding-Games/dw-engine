#include "byte_charges.hpp"

#include "budget.hpp"

namespace dwe::script::detail {
namespace {

constexpr int OriginalUpvalue = 1;

// One step per value, and the bytes of the texts among them.
std::int64_t stepsForValues(lua_State* state, int first, int last) {
  std::int64_t steps = 0;
  for (int index = first; index <= last; ++index) {
    std::size_t length = 0;
    if (lua_type(state, index) == LUA_TSTRING) {
      lua_tolstring(state, index, &length);
    }
    steps += 1 + stepsForBytes(length);
  }
  return steps;
}

// Charges for the results of the call, both when it returns at once and when it resumes after a
// yield.
int chargeResults(lua_State* state, int /*status*/, lua_KContext /*context*/) {
  const int results = lua_gettop(state);
  chargeBudget(state, stepsForValues(state, 1, results));
  return results;
}

int callCharged(lua_State* state) {
  chargeBudget(state, stepsForValues(state, 1, lua_gettop(state)));
  lua_pushvalue(state, lua_upvalueindex(OriginalUpvalue));
  lua_insert(state, 1);
  lua_callk(state, lua_gettop(state) - 1, LUA_MULTRET, 0, chargeResults);
  return chargeResults(state, LUA_OK, 0);
}

} // namespace

void chargeForBytes(lua_State* state, int library, const char* name) {
  library = lua_absindex(state, library);
  lua_getfield(state, library, name);
  lua_pushcclosure(state, callCharged, 1);
  lua_setfield(state, library, name);
}

} // namespace dwe::script::detail
