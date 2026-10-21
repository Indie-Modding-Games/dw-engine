#include "budgeted_tables.hpp"

#include <cstdint>
#include <limits>

#include "budget.hpp"
#include "byte_charges.hpp"
#include "lua_errors.hpp"
#include "messages.hpp"
#include "metafields.hpp"

// table.insert and table.remove as ltablib.c has them (tinsert, tremove), charging the budget before they move
// elements. Lua errors unwind with longjmp: only trivially destructible values are alive in these functions.
namespace dwe::script::detail {
namespace {

// The most elements one table.move may copy, whatever budget is left.
constexpr lua_Integer MaxMovedElements = lua_Integer{1} << 24;

constexpr int TableArgument = 1;
constexpr int PositionArgument = 2;
constexpr int ComparatorArgument = 2;
constexpr int FirstArgument = 2;
constexpr int LastArgument = 3;
constexpr int InsertAtEndArguments = 2;
constexpr int InsertAtPositionArguments = 3;

void chargeMoves(lua_State* state, lua_Unsigned moves) {
    constexpr auto MostSteps = static_cast<lua_Unsigned>(std::numeric_limits<std::int64_t>::max());
    chargeBudget(state, static_cast<std::int64_t>(moves < MostSteps ? moves : MostSteps));
}

bool hasMetafield(lua_State* state, const char* name) {
    lua_pushstring(state, name);
    const bool found = lua_rawget(state, -2) != LUA_TNIL;
    lua_pop(state, 1);
    return found;
}

// The length of a table, or of a value whose metatable makes it behave like one (checktab and aux_getn in ltablib.c).
lua_Integer tableLength(lua_State* state) {
    const int type = lua_type(state, TableArgument);
    if (type != LUA_TTABLE) {
        const bool behavesAsTable = lua_getmetatable(state, TableArgument) != 0 && hasMetafield(state, IndexField) &&
                                    hasMetafield(state, NewIndexField) &&
                                    (type == LUA_TSTRING || hasMetafield(state, LengthField));
        if (!behavesAsTable) {
            luaL_checktype(state, TableArgument, LUA_TTABLE);
        }
        lua_pop(state, 1);
    }
    return luaL_len(state, TableArgument);
}

lua_Integer plusOne(lua_Integer value) {
    return static_cast<lua_Integer>(static_cast<lua_Unsigned>(value) + 1U);
}

int insert(lua_State* state) {
    const lua_Integer firstEmpty = plusOne(tableLength(state));
    lua_Integer position = firstEmpty;
    switch (lua_gettop(state)) {
    case InsertAtEndArguments:
        break;
    case InsertAtPositionArguments:
        position = luaL_checkinteger(state, PositionArgument);
        luaL_argcheck(state, static_cast<lua_Unsigned>(position) - 1U < static_cast<lua_Unsigned>(firstEmpty),
                      PositionArgument, messages::PositionOutOfBounds.data());
        chargeMoves(state, static_cast<lua_Unsigned>(firstEmpty) - static_cast<lua_Unsigned>(position));
        for (lua_Integer i = firstEmpty; i > position; --i) {
            lua_geti(state, TableArgument, i - 1);
            lua_seti(state, TableArgument, i);
        }
        break;
    default:
        return raiseError(state, {messages::WrongInsertArguments});
    }
    lua_seti(state, TableArgument, position);
    return 0;
}

int remove(lua_State* state) {
    const lua_Integer size = tableLength(state);
    lua_Integer position = luaL_optinteger(state, PositionArgument, size);
    if (position != size) {
        luaL_argcheck(state, static_cast<lua_Unsigned>(position) - 1U <= static_cast<lua_Unsigned>(size),
                      PositionArgument, messages::PositionOutOfBounds.data());
    }
    lua_geti(state, TableArgument, position);
    if (position < size) {
        chargeMoves(state, static_cast<lua_Unsigned>(size) - static_cast<lua_Unsigned>(position));
    }
    for (; position < size; ++position) {
        lua_geti(state, TableArgument, position + 1);
        lua_seti(state, TableArgument, position);
    }
    lua_pushnil(state);
    lua_seti(state, TableArgument, position);
    return 1;
}

// table.move, refusing ranges that no budget could pay for, and charging the budget for the rest. Lua's own
// table.move does the copy: it is upvalue 1.
int move(lua_State* state) {
    const lua_Integer first = luaL_checkinteger(state, FirstArgument);
    const lua_Integer last = luaL_checkinteger(state, LastArgument);
    if (last >= first) {
        if (first <= 0 ? last >= MaxMovedElements + first : last - first >= MaxMovedElements) {
            return raiseError(state, {messages::TableMoveTooLarge});
        }
        chargeMoves(state, static_cast<lua_Unsigned>(last - first) + 1U);
    }
    lua_pushvalue(state, lua_upvalueindex(1));
    lua_insert(state, 1);
    lua_call(state, lua_gettop(state) - 1, LUA_MULTRET);
    return lua_gettop(state);
}

// The comparisons a sort of count elements may make, for the budget: count times its number of bits.
std::int64_t sortComparisons(lua_Integer count) {
    std::int64_t bits = 1;
    for (lua_Integer rest = count; rest > 1; rest /= 2) {
        ++bits;
    }
    return static_cast<std::int64_t>(count) * bits;
}

// table.sort. Without a comparator, Lua's own sort compares in C, where the hook cannot run: it is charged up front
// for its comparisons, each as costly as comparing the longest text in the table. With one, each comparison runs Lua
// code, which the hook counts. Lua's own table.sort is upvalue 1.
int sort(lua_State* state) {
    if (lua_isnoneornil(state, ComparatorArgument)) {
        luaL_checktype(state, TableArgument, LUA_TTABLE);
        const lua_Integer count = luaL_len(state, TableArgument);
        if (count > 1) {
            chargeBudget(state, count);
            std::size_t longest = 0;
            for (lua_Integer index = 1; index <= count; ++index) {
                std::size_t length = 0;
                if (lua_rawgeti(state, TableArgument, index) == LUA_TSTRING) {
                    lua_tolstring(state, -1, &length);
                }
                longest = length > longest ? length : longest;
                lua_pop(state, 1);
            }
            chargeBudget(state, sortComparisons(count) * (1 + stepsForBytes(longest)));
        }
    }
    lua_pushvalue(state, lua_upvalueindex(1));
    lua_insert(state, 1);
    lua_call(state, lua_gettop(state) - 1, 0);
    return 0;
}

} // namespace

void installBudgetedTables(lua_State* state) {
    lua_getglobal(state, LUA_TABLIBNAME);
    lua_pushcfunction(state, insert);
    lua_setfield(state, -2, "insert");
    lua_pushcfunction(state, remove);
    lua_setfield(state, -2, "remove");
    lua_getfield(state, -1, "move");
    lua_pushcclosure(state, move, 1);
    lua_setfield(state, -2, "move");
    lua_getfield(state, -1, "sort");
    lua_pushcclosure(state, sort, 1);
    lua_setfield(state, -2, "sort");
    chargeForBytes(state, -1, "concat");
    chargeForBytes(state, -1, "unpack");
    lua_pop(state, 1);
}

} // namespace dwe::script::detail
