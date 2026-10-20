#pragma once

#include <dwe/core/error.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace sol {
class state; // NOLINT(readability-identifier-naming): a third-party type, named by its library.
}

namespace dwe::input {
class InputState;
}

namespace dwe::script {

// What a script may use, so a broken or hostile mod cannot hang or exhaust the game.
struct ScriptLimits {
    std::size_t memoryBytes = std::size_t{64} * 1024 * 1024;
    // Lua instructions one call may run before it is stopped.
    std::uint64_t instructionsPerCall = 10'000'000;
};

// A sandboxed Lua 5.5 state. Scripts get the safe standard libraries (base without file loading, string, table,
// math, utf8, coroutine) and the engine API, never io, os, package or native modules.
class ScriptVm {
public:
    static core::Result<std::unique_ptr<ScriptVm>> create(const ScriptLimits& limits = {});

    ScriptVm(const ScriptVm&) = delete;
    ScriptVm& operator=(const ScriptVm&) = delete;
    ScriptVm(ScriptVm&&) = delete;
    ScriptVm& operator=(ScriptVm&&) = delete;
    ~ScriptVm();

    // Runs a script file and remembers it, so reloadChanged() can run it again when it is edited.
    core::Result<void> runFile(const std::filesystem::path& path);

    // Runs Lua source; name identifies it in error messages.
    core::Result<void> runString(std::string_view source, std::string_view name);

    // Runs again every remembered file whose modification time changed. A file that fails keeps the state it had.
    void reloadChanged();

    // Calls a global function with numeric arguments. Calling a function that does not exist does nothing.
    core::Result<void> call(std::string_view function, std::span<const double> arguments = {});

    // Reads a number from a global or a field of global tables, as a dotted path like "marker.x".
    [[nodiscard]] std::optional<double> number(std::string_view path);

    // Exposes the input as the global table `input` (value, down, pressed, vector).
    void bindInput(const input::InputState& input);

    // The underlying state, for modules that register their own API.
    sol::state& lua();

    // Gives the next call into Lua a full instruction budget. Modules that call Lua functions through lua() call it
    // before each call, as call() does; otherwise their calls share what is left of the previous one's budget.
    void startCall();

private:
    struct Impl;

    explicit ScriptVm(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

} // namespace dwe::script
