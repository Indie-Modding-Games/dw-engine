#include "string_patterns.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <new>
#include <string_view>
#include <type_traits>

#include "budget.hpp"
#include "lua_errors.hpp"
#include "messages.hpp"
#include "pattern_matcher.hpp"

// The string library's pattern functions as lstrlib.c has them (str_find_aux, gmatch, str_gsub), on PatternMatcher.
// Lua errors unwind with longjmp: only trivially destructible values are alive in these functions.
namespace dwe::script::detail {
namespace {

// The characters that make a pattern more than plain text (SPECIALS in lstrlib.c).
constexpr std::string_view PatternSpecials = "^$*+?.([%-";
constexpr char Anchor = '^';
constexpr char Escape = '%';

constexpr int SubjectArgument = 1;
constexpr int PatternArgument = 2;
constexpr int StartArgument = 3;
constexpr int PlainArgument = 4;
constexpr int ReplacementArgument = 3;
constexpr int MaxReplacementsArgument = 4;

std::string_view checkText(lua_State* state, int argument) {
    std::size_t length = 0;
    const char* text = luaL_checklstring(state, argument, &length);
    return {text, length};
}

// A position argument, which counts from 1 and from the end when negative, as an offset from the start
// (posrelatI in lstrlib.c, minus 1).
std::size_t startOffset(lua_Integer position, std::size_t length) {
    if (position > 0) {
        return static_cast<std::size_t>(position) - 1;
    }
    if (position == 0 || position < -static_cast<lua_Integer>(length)) {
        return 0;
    }
    return length + static_cast<std::size_t>(position);
}

bool hasSpecials(std::string_view pattern) {
    return pattern.find_first_of(PatternSpecials) != std::string_view::npos;
}

// Where text first appears in subject, or nullptr (lmemfind in lstrlib.c).
const char* findPlain(lua_State* state, std::string_view subject, std::string_view text) {
    if (text.empty()) {
        return subject.data();
    }
    if (text.size() > subject.size()) {
        return nullptr;
    }
    const char* position = subject.data();
    std::size_t candidates = subject.size() - text.size() + 1;
    while (candidates > 0) {
        const auto* found = static_cast<const char*>(std::memchr(position, text.front(), candidates));
        if (found == nullptr) {
            return nullptr;
        }
        chargeBudget(state, 1 + static_cast<std::int64_t>((static_cast<std::size_t>(found - position) + text.size()) /
                                                          BytesPerStep));
        if (std::memcmp(found + 1, text.data() + 1, text.size() - 1) == 0) {
            return found;
        }
        candidates -= static_cast<std::size_t>(found + 1 - position);
        position = found + 1;
    }
    return nullptr;
}

int pushFail(lua_State* state) {
    lua_pushnil(state);
    return 1;
}

int pushRange(lua_State* state, std::string_view subject, const char* start, const char* end) {
    lua_pushinteger(state, static_cast<lua_Integer>(start - subject.data()) + 1);
    lua_pushinteger(state, static_cast<lua_Integer>(end - subject.data()));
    return 2;
}

int findPattern(lua_State* state, std::string_view subject, std::size_t start, std::string_view pattern, bool find) {
    const bool anchored = !pattern.empty() && pattern.front() == Anchor;
    if (anchored) {
        pattern.remove_prefix(1);
    }
    PatternMatcher matcher{state, subject, pattern};
    const char* position = subject.data() + start;
    do {
        if (const char* end = matcher.matchAt(position); end != nullptr) {
            if (find) {
                pushRange(state, subject, position, end);
                return matcher.pushCaptures(nullptr, nullptr) + 2;
            }
            return matcher.pushCaptures(position, end);
        }
    } while (position++ < matcher.subjectEnd() && !anchored);
    return pushFail(state);
}

int findOrMatch(lua_State* state, bool find) {
    const std::string_view subject = checkText(state, SubjectArgument);
    const std::string_view pattern = checkText(state, PatternArgument);
    const std::size_t start = startOffset(luaL_optinteger(state, StartArgument, 1), subject.size());
    if (start > subject.size()) {
        return pushFail(state);
    }
    if (find && (lua_toboolean(state, PlainArgument) != 0 || !hasSpecials(pattern))) {
        const char* found = findPlain(state, subject.substr(start), pattern);
        return found != nullptr ? pushRange(state, subject, found, found + pattern.size()) : pushFail(state);
    }
    return findPattern(state, subject, start, pattern, find);
}

int find(lua_State* state) {
    return findOrMatch(state, true);
}

int matchText(lua_State* state) {
    return findOrMatch(state, false);
}

// The iterator that gmatch returns keeps its subject and pattern as upvalues 1 and 2, and this state as upvalue 3.
struct MatchIteration {
    PatternMatcher matcher;
    const char* position = nullptr;
    const char* lastMatchEnd = nullptr;
};

static_assert(std::is_trivially_destructible_v<MatchIteration>);

constexpr int IterationUpvalue = 3;

int nextMatch(lua_State* state) {
    auto* iteration = static_cast<MatchIteration*>(lua_touserdata(state, lua_upvalueindex(IterationUpvalue)));
    iteration->matcher.setState(state);
    for (const char* position = iteration->position; position <= iteration->matcher.subjectEnd(); ++position) {
        const char* end = iteration->matcher.matchAt(position);
        if (end != nullptr && end != iteration->lastMatchEnd) {
            iteration->position = end;
            iteration->lastMatchEnd = end;
            return iteration->matcher.pushCaptures(position, end);
        }
    }
    return 0;
}

int iterateMatches(lua_State* state) {
    const std::string_view subject = checkText(state, SubjectArgument);
    const std::string_view pattern = checkText(state, PatternArgument);
    // Past the end, the iterator finds nothing.
    const std::size_t start =
        std::min(startOffset(luaL_optinteger(state, StartArgument, 1), subject.size()), subject.size() + 1);
    lua_settop(state, PatternArgument);
    void* memory = lua_newuserdatauv(state, sizeof(MatchIteration), 0);
    new (memory) MatchIteration{
        .matcher = PatternMatcher{state, subject, pattern},
        .position = subject.data() + start,
        .lastMatchEnd = nullptr,
    };
    lua_pushcclosure(state, nextMatch, IterationUpvalue);
    return 1;
}

// A replacement text, with %0 to %9 standing for the captures and %% for '%' (add_s in lstrlib.c).
void addReplacementText(lua_State* state, luaL_Buffer& buffer, PatternMatcher& matcher, const char* start,
                        const char* end) {
    std::size_t length = 0;
    const char* text = lua_tolstring(state, ReplacementArgument, &length);
    const char* escape = nullptr;
    while ((escape = static_cast<const char*>(std::memchr(text, Escape, length))) != nullptr) {
        luaL_addlstring(&buffer, text, static_cast<std::size_t>(escape - text));
        const char code = *(escape + 1);
        if (code == Escape) {
            luaL_addchar(&buffer, code);
        } else if (code == '0') {
            luaL_addlstring(&buffer, start, static_cast<std::size_t>(end - start));
        } else if (std::isdigit(static_cast<unsigned char>(code)) != 0) {
            matcher.addCapture(buffer, code - '1', start, end);
        } else {
            raiseError(state, {messages::BadReplacementEscape});
        }
        length -= static_cast<std::size_t>(escape + 2 - text);
        text = escape + 2;
    }
    luaL_addlstring(&buffer, text, length);
}

// Adds what replaces one match; returns whether it changed the text (add_value in lstrlib.c).
bool addReplacement(lua_State* state, luaL_Buffer& buffer, PatternMatcher& matcher, const char* start, const char* end,
                    int replacementType) {
    if (replacementType == LUA_TFUNCTION) {
        lua_pushvalue(state, ReplacementArgument);
        const int count = matcher.pushCaptures(start, end);
        lua_call(state, count, 1);
    } else if (replacementType == LUA_TTABLE) {
        matcher.pushCapture(0, start, end);
        lua_gettable(state, ReplacementArgument);
    } else {
        addReplacementText(state, buffer, matcher, start, end);
        return true;
    }
    if (lua_toboolean(state, -1) == 0) {
        lua_pop(state, 1);
        luaL_addlstring(&buffer, start, static_cast<std::size_t>(end - start));
        return false;
    }
    if (lua_isstring(state, -1) == 0) {
        raiseError(state, {messages::BadReplacementValue, luaL_typename(state, -1), messages::BadReplacementValueEnd});
    }
    luaL_addvalue(&buffer);
    return true;
}

int substitute(lua_State* state) {
    const std::string_view subject = checkText(state, SubjectArgument);
    std::string_view pattern = checkText(state, PatternArgument);
    const int replacementType = lua_type(state, ReplacementArgument);
    const lua_Integer maxReplacements =
        luaL_optinteger(state, MaxReplacementsArgument, static_cast<lua_Integer>(subject.size()) + 1);
    if (replacementType != LUA_TNUMBER && replacementType != LUA_TSTRING && replacementType != LUA_TFUNCTION &&
        replacementType != LUA_TTABLE) {
        luaL_typeerror(state, ReplacementArgument, messages::ReplacementTypes);
    }
    luaL_Buffer buffer;
    luaL_buffinit(state, &buffer);
    const bool anchored = !pattern.empty() && pattern.front() == Anchor;
    if (anchored) {
        pattern.remove_prefix(1);
    }
    PatternMatcher matcher{state, subject, pattern};
    const char* position = subject.data();
    const char* lastMatchEnd = nullptr;
    lua_Integer replacements = 0;
    bool changed = false;
    while (replacements < maxReplacements) {
        const char* end = matcher.matchAt(position);
        if (end != nullptr && end != lastMatchEnd) {
            ++replacements;
            changed = addReplacement(state, buffer, matcher, position, end, replacementType) || changed;
            position = end;
            lastMatchEnd = end;
        } else if (position < matcher.subjectEnd()) {
            luaL_addchar(&buffer, *position++);
        } else {
            break;
        }
        if (anchored) {
            break;
        }
    }
    if (changed) {
        luaL_addlstring(&buffer, position, static_cast<std::size_t>(matcher.subjectEnd() - position));
        luaL_pushresult(&buffer);
    } else {
        lua_pushvalue(state, SubjectArgument);
    }
    lua_pushinteger(state, replacements);
    return 2;
}

} // namespace

void installBudgetedPatterns(lua_State* state) {
    lua_getglobal(state, LUA_STRLIBNAME);
    lua_pushcfunction(state, find);
    lua_setfield(state, -2, "find");
    lua_pushcfunction(state, matchText);
    lua_setfield(state, -2, "match");
    lua_pushcfunction(state, iterateMatches);
    lua_setfield(state, -2, "gmatch");
    lua_pushcfunction(state, substitute);
    lua_setfield(state, -2, "gsub");
    lua_pop(state, 1);
}

} // namespace dwe::script::detail
