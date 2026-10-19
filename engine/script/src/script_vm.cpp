#include <dwe/core/log.hpp>
#include <dwe/script/script_vm.hpp>

#include <sol/sol.hpp>

#include <algorithm>
#include <cstdlib>
#include <format>
#include <fstream>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::script {
namespace {

// The longest line a script may print, in bytes.
constexpr std::size_t MaxPrintBytes = 4096;

// The most elements one table.move may copy.
constexpr lua_Integer MaxMovedElements = lua_Integer{1} << 24;

// How often, in instructions, the budget of the running call is checked.
constexpr int InstructionCheckInterval = 10'000;

struct Memory {
    std::size_t used = 0;
    std::size_t limit = 0;
};

// Lua's allocator, with a ceiling. Returning nullptr makes Lua raise a memory error in the script.
void* allocate(void* userData, void* block, std::size_t oldSize, std::size_t newSize) {
    auto* memory = static_cast<Memory*>(userData);
    // When block is null, oldSize holds the kind of object being created, not a size.
    const std::size_t previous = block != nullptr ? oldSize : 0;
    if (newSize == 0) {
        std::free(block);
        memory->used -= previous;
        return nullptr;
    }
    if (newSize > previous && memory->used - previous + newSize > memory->limit) {
        return nullptr;
    }
    void* resized = std::realloc(block, newSize);
    if (resized != nullptr) {
        memory->used = memory->used - previous + newSize;
    }
    return resized;
}

// The instructions the running call may still execute, checked by a Lua hook every InstructionCheckInterval
// instructions.
struct Budget {
    lua_State* state = nullptr;
    std::uint64_t perCall = 0;
    std::int64_t left = 0;

    void reset();
};

void checkBudget(lua_State* state, lua_Debug* /*debug*/) {
    auto* budget = *static_cast<Budget**>(lua_getextraspace(state));
    budget->left -= InstructionCheckInterval;
    if (budget->left <= 0) {
        // From now on every instruction fails, so a script that catches the error with pcall cannot keep running:
        // the first instruction it runs outside the pcall fails too.
        lua_sethook(state, checkBudget, LUA_MASKCOUNT, 1);
        luaL_error(state, "the script ran too long and was stopped");
    }
}

void Budget::reset() {
    left = static_cast<std::int64_t>(perCall);
    lua_sethook(state, checkBudget, LUA_MASKCOUNT, InstructionCheckInterval);
}

// collectgarbage limited to reading the memory in use: the other options would let a script stop the collector or
// change how it runs.
int countGarbage(lua_State* state) {
    const char* option = luaL_optstring(state, 1, "count");
    if (std::string_view{option} != "count") {
        return luaL_error(state, "collectgarbage('%s') is not available to scripts", option);
    }
    constexpr double BytesPerKilobyte = 1024.0;
    const double kilobytes = lua_gc(state, LUA_GCCOUNT) + (lua_gc(state, LUA_GCCOUNTB) / BytesPerKilobyte);
    lua_pushnumber(state, kilobytes);
    return 1;
}

// print, written to the log. The line is built in a Lua buffer because a __tostring metamethod may raise a Lua error,
// which unwinds with longjmp and would skip the destructor of a C++ string.
int printToLog(lua_State* state) {
    const int count = lua_gettop(state);
    luaL_Buffer line;
    luaL_buffinit(state, &line);
    for (int i = 1; i <= count; ++i) {
        if (i > 1) {
            luaL_addchar(&line, '\t');
        }
        luaL_tolstring(state, i, nullptr);
        luaL_addvalue(&line);
    }
    luaL_pushresult(&line);
    std::size_t length = 0;
    const char* text = lua_tolstring(state, -1, &length);
    // The log keeps its lines outside the script's memory limit, so a line is cut at a size no message needs.
    if (length > MaxPrintBytes) {
        core::logInfo("lua: {} [{} more bytes]", std::string_view{text, MaxPrintBytes}, length - MaxPrintBytes);
    } else {
        core::logInfo("lua: {}", std::string_view{text, length});
    }
    return 0;
}

// table.move, refusing ranges that would keep the C loop inside it running far past the instruction budget, which
// only counts Lua instructions.
int boundedTableMove(lua_State* state) {
    const lua_Integer first = luaL_checkinteger(state, 2);
    const lua_Integer last = luaL_checkinteger(state, 3);
    if (last >= first && (first <= 0 ? last >= MaxMovedElements + first : last - first >= MaxMovedElements)) {
        return luaL_error(state, "table.move: the range is too large");
    }
    lua_pushvalue(state, lua_upvalueindex(1));
    lua_insert(state, 1);
    lua_call(state, lua_gettop(state) - 1, LUA_MULTRET);
    return lua_gettop(state);
}

// The engine reads and calls into Lua only inside a protected call, with raw access that runs no metamethods: an
// error raised outside one (a failed allocation, a script's __index) would make Lua abort the program.

struct GlobalCall {
    std::string_view function;
    std::span<const double> arguments;
};

int callGlobal(lua_State* state) {
    const auto* request = static_cast<const GlobalCall*>(lua_touserdata(state, 1));
    lua_pushglobaltable(state);
    lua_pushlstring(state, request->function.data(), request->function.size());
    if (lua_rawget(state, -2) != LUA_TFUNCTION) {
        return 0;
    }
    for (const double argument : request->arguments) {
        lua_pushnumber(state, argument);
    }
    lua_call(state, static_cast<int>(request->arguments.size()), 0);
    return 0;
}

struct NumberLookup {
    std::string_view path;
    std::optional<double> result;
};

int lookUpNumber(lua_State* state) {
    auto* lookup = static_cast<NumberLookup*>(lua_touserdata(state, 1));
    lua_pushglobaltable(state);
    std::string_view rest = lookup->path;
    while (true) {
        const std::size_t dot = rest.find('.');
        const std::string_view key = rest.substr(0, dot);
        lua_pushlstring(state, key.data(), key.size());
        luaL_checkstack(state, 2, "the path is too deep");
        const int type = lua_rawget(state, -2);
        if (dot == std::string_view::npos) {
            if (type == LUA_TNUMBER) {
                lookup->result = lua_tonumber(state, -1);
            }
            return 0;
        }
        if (type != LUA_TTABLE) {
            return 0;
        }
        rest.remove_prefix(dot + 1);
    }
}

// The message of a failed protected call, without converting the error object: a conversion could run Lua code.
std::string_view errorMessage(lua_State* state) {
    if (lua_type(state, -1) != LUA_TSTRING) {
        return "the error is not a string";
    }
    std::size_t length = 0;
    const char* text = lua_tolstring(state, -1, &length);
    return {text, length};
}

struct WatchedFile {
    std::filesystem::path path;
    std::filesystem::file_time_type modified;
};

std::filesystem::file_time_type modifiedTime(const std::filesystem::path& path) {
    std::error_code error;
    const auto time = std::filesystem::last_write_time(path, error);
    return error ? std::filesystem::file_time_type{} : time;
}

} // namespace

struct ScriptVm::Impl {
    Memory memory;
    Budget budget;
    sol::state lua;
    std::vector<WatchedFile> files;

    explicit Impl(const ScriptLimits& limits)
        : memory{0, limits.memoryBytes}, budget{nullptr, limits.instructionsPerCall, 0},
          lua{sol::default_at_panic, allocate, &memory} {
        budget.state = lua.lua_state();
    }

    core::Result<void> run(std::string_view source, std::string_view name) {
        budget.reset();
        // Text only: precompiled bytecode is not checked by Lua and could break out of the sandbox.
        sol::protected_function_result result =
            lua.safe_script(source, sol::script_pass_on_error, std::string{name}, sol::load_mode::text);
        if (!result.valid()) {
            sol::error error = result;
            return core::fail(error.what());
        }
        return {};
    }
};

core::Result<std::unique_ptr<ScriptVm>> ScriptVm::create(const ScriptLimits& limits) {
    auto impl = std::make_unique<Impl>(limits);
    sol::state& lua = impl->lua;
    // Coroutines copy the main thread's extra space and hook when they are created, so they share the budget.
    *static_cast<Budget**>(lua_getextraspace(lua.lua_state())) = &impl->budget;
    impl->budget.reset();

    lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math, sol::lib::utf8,
                       sol::lib::coroutine);
    // Loading code from files or strings would escape the sandbox and the instruction budget.
    lua["dofile"] = sol::lua_nil;
    lua["loadfile"] = sol::lua_nil;
    lua["load"] = sol::lua_nil;
    lua["string"]["dump"] = sol::lua_nil;
    lua_State* state = lua.lua_state();
    lua_register(state, "collectgarbage", countGarbage);
    lua_register(state, "print", printToLog);
    lua_getglobal(state, "table");
    lua_getfield(state, -1, "move");
    lua_pushcclosure(state, boundedTableMove, 1);
    lua_setfield(state, -2, "move");
    lua_pop(state, 1);

    return std::unique_ptr<ScriptVm>{new ScriptVm{std::move(impl)}};
}

ScriptVm::ScriptVm(std::unique_ptr<Impl> impl) : impl_{std::move(impl)} {}

ScriptVm::~ScriptVm() = default;

core::Result<void> ScriptVm::runFile(const std::filesystem::path& path) {
    std::ifstream file{path};
    if (!file) {
        return core::fail(std::format("could not open the script {}", path.string()));
    }
    std::stringstream source;
    source << file.rdbuf();

    auto known = std::ranges::find(impl_->files, path, &WatchedFile::path);
    if (known == impl_->files.end()) {
        impl_->files.push_back(WatchedFile{path, modifiedTime(path)});
    } else {
        known->modified = modifiedTime(path);
    }
    return impl_->run(source.str(), "@" + path.string());
}

core::Result<void> ScriptVm::runString(std::string_view source, std::string_view name) {
    return impl_->run(source, name);
}

void ScriptVm::reloadChanged() {
    for (const WatchedFile& file : std::vector<WatchedFile>{impl_->files}) {
        if (modifiedTime(file.path) == file.modified) {
            continue;
        }
        if (auto reloaded = runFile(file.path); reloaded) {
            core::logInfo("reloaded {}", file.path.string());
        } else {
            core::logError("could not reload {}: {}", file.path.string(), reloaded.error().message);
        }
    }
}

core::Result<void> ScriptVm::call(std::string_view function, std::span<const double> arguments) {
    lua_State* state = impl_->lua.lua_state();
    GlobalCall request{.function = function, .arguments = arguments};
    impl_->budget.reset();
    lua_pushcfunction(state, callGlobal);
    lua_pushlightuserdata(state, &request);
    if (lua_pcall(state, 1, 0, 0) != LUA_OK) {
        std::string message{errorMessage(state)};
        lua_pop(state, 1);
        return core::fail(std::format("{}: {}", function, message));
    }
    return {};
}

std::optional<double> ScriptVm::number(std::string_view path) {
    lua_State* state = impl_->lua.lua_state();
    NumberLookup lookup{.path = path, .result = std::nullopt};
    impl_->budget.reset();
    lua_pushcfunction(state, lookUpNumber);
    lua_pushlightuserdata(state, &lookup);
    if (lua_pcall(state, 1, 0, 0) != LUA_OK) {
        lua_pop(state, 1);
        return std::nullopt;
    }
    return lookup.result;
}

sol::state& ScriptVm::lua() {
    return impl_->lua;
}

} // namespace dwe::script
