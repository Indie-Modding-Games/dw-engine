#include "pack_random.hpp"

#include <array>
#include <bit>
#include <cstdint>

#include "lua_errors.hpp"
#include "messages.hpp"

namespace dwe::script::detail {
namespace {

constexpr int StateUpvalue = 1;
constexpr int SeedUpvalue = 2;
constexpr int LowArgument = 1;
constexpr int HighArgument = 2;
constexpr int SeedArgument = 1;

// FNV-1a, 64 bits: the seed of a pack's generator from its id.
constexpr std::uint64_t FnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t FnvPrime = 1099511628211ULL;

// splitmix64, which fills the generator's state from one seed.
constexpr std::uint64_t SplitMixIncrement = 0x9e3779b97f4a7c15ULL;
constexpr std::uint64_t SplitMixFirst = 0xbf58476d1ce4e5b9ULL;
constexpr std::uint64_t SplitMixSecond = 0x94d049bb133111ebULL;
constexpr int SplitMixShiftFirst = 30;
constexpr int SplitMixShiftSecond = 27;
constexpr int SplitMixShiftLast = 31;

// xoshiro256**.
constexpr std::uint64_t ScrambleMultiplier = 5;
constexpr int ScrambleRotation = 7;
constexpr std::uint64_t ScrambleFinal = 9;
constexpr int StateShift = 17;
constexpr int StateRotation = 45;

// A float in [0, 1) takes the top 53 bits.
constexpr int FloatDropBits = 11;
constexpr double FloatScale = 0x1.0p-53;

using GeneratorState = std::array<std::uint64_t, 4>;

std::uint64_t seedOf(std::string_view pack) {
    std::uint64_t hash = FnvOffset;
    for (const char c : pack) {
        hash = (hash ^ static_cast<unsigned char>(c)) * FnvPrime;
    }
    return hash;
}

void seed(GeneratorState& state, std::uint64_t value) {
    for (std::uint64_t& word : state) {
        value += SplitMixIncrement;
        std::uint64_t mixed = value;
        mixed = (mixed ^ (mixed >> SplitMixShiftFirst)) * SplitMixFirst;
        mixed = (mixed ^ (mixed >> SplitMixShiftSecond)) * SplitMixSecond;
        word = mixed ^ (mixed >> SplitMixShiftLast);
    }
}

std::uint64_t next(GeneratorState& state) {
    const std::uint64_t result = std::rotl(state[1] * ScrambleMultiplier, ScrambleRotation) * ScrambleFinal;
    const std::uint64_t shifted = state[1] << StateShift;
    state[2] ^= state[0];
    state[3] ^= state[1];
    state[1] ^= state[2];
    state[0] ^= state[3];
    state[2] ^= shifted;
    state[3] = std::rotl(state[3], StateRotation);
    return result;
}

// A random value in [0, limit], without bias: masked draws until one fits (project in lmathlib.c).
std::uint64_t project(GeneratorState& state, std::uint64_t limit) {
    if ((limit & (limit + 1)) == 0) {
        return next(state) & limit;
    }
    const std::uint64_t mask = std::bit_ceil(limit + 1) - 1;
    std::uint64_t value = next(state) & mask;
    while (value > limit) {
        value = next(state) & mask;
    }
    return value;
}

GeneratorState& stateOf(lua_State* state) {
    return *static_cast<GeneratorState*>(lua_touserdata(state, lua_upvalueindex(StateUpvalue)));
}

int random(lua_State* state) {
    GeneratorState& generator = stateOf(state);
    lua_Integer low = 1;
    lua_Integer high = 0;
    switch (lua_gettop(state)) {
    case 0:
        lua_pushnumber(state, static_cast<double>(next(generator) >> FloatDropBits) * FloatScale);
        return 1;
    case 1:
        high = luaL_checkinteger(state, LowArgument);
        if (high == 0) {
            lua_pushinteger(state, static_cast<lua_Integer>(next(generator)));
            return 1;
        }
        break;
    case 2:
        low = luaL_checkinteger(state, LowArgument);
        high = luaL_checkinteger(state, HighArgument);
        break;
    default:
        return raiseError(state, {messages::WrongRandomArguments});
    }
    luaL_argcheck(state, low <= high, HighArgument, messages::EmptyRandomInterval);
    const std::uint64_t span = static_cast<std::uint64_t>(high) - static_cast<std::uint64_t>(low);
    const std::uint64_t drawn = project(generator, span) + static_cast<std::uint64_t>(low);
    lua_pushinteger(state, static_cast<lua_Integer>(drawn));
    return 1;
}

int randomSeed(lua_State* state) {
    GeneratorState& generator = stateOf(state);
    if (lua_isnone(state, SeedArgument)) {
        seed(generator, static_cast<std::uint64_t>(lua_tointeger(state, lua_upvalueindex(SeedUpvalue))));
    } else {
        seed(generator, static_cast<std::uint64_t>(luaL_checkinteger(state, SeedArgument)));
    }
    return 0;
}

} // namespace

void installPackRandom(lua_State* state, int mathTable, std::string_view pack) {
    mathTable = lua_absindex(state, mathTable);
    const std::uint64_t packSeed = seedOf(pack);
    auto* generator = static_cast<GeneratorState*>(lua_newuserdatauv(state, sizeof(GeneratorState), 0));
    seed(*generator, packSeed);
    const int generatorIndex = lua_gettop(state);
    lua_pushvalue(state, generatorIndex);
    lua_pushinteger(state, static_cast<lua_Integer>(packSeed));
    lua_pushcclosure(state, random, 2);
    lua_setfield(state, mathTable, "random");
    lua_pushvalue(state, generatorIndex);
    lua_pushinteger(state, static_cast<lua_Integer>(packSeed));
    lua_pushcclosure(state, randomSeed, 2);
    lua_setfield(state, mathTable, "randomseed");
    lua_pop(state, 1);
}

} // namespace dwe::script::detail
