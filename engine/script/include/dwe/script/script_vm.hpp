#pragma once

#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/pack_manifest.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>
#include <dwe/script/api_registry.hpp>
#include <dwe/script/budget_stop.hpp>
#include <dwe/script/hook_registry.hpp>
#include <dwe/script/mod_environment.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace sol {
// NOLINTNEXTLINE(readability-identifier-naming): a third-party type, named by its library.
class state;
} // namespace sol

namespace dwe::input {
class InputState;
}

namespace dwe::localization {
class Catalog;
}

namespace dwe::script {

namespace detail {
struct VmState;
}

inline constexpr std::size_t MebiByte = std::size_t{1024} * 1024;

// What a script may use, so a broken or hostile mod cannot hang or exhaust the game. A call that
// runs out of instructions or time is stopped with an error; one that runs out of memory gets a
// memory error. The instructions decide where a runaway script stops, the same on every machine;
// each time limit is a loose ceiling, for the work the instruction count does not see (BudgetStop).
struct ScriptLimits {
  // All the memory of the Lua state. An eighth of it is kept for the engine's own work outside
  // script calls.
  std::size_t memoryBytes = 64 * MebiByte;
  // What the scripts of one pack may hold at once, so one pack cannot starve the others.
  std::size_t packMemoryBytes = 16 * MebiByte;
  // The largest single block (a string, a table's array) a script call may reserve. It keeps short
  // the C work of one operation the instruction hook cannot interrupt, such as comparing two
  // strings.
  std::size_t largestBlockBytes = 4 * MebiByte;
  // Each time ceiling is about ten times what its instructions take in a release build (100 to 200
  // million Lua instructions a second), so that the instructions, not the clock, stop a runaway
  // script. Lua instructions one call may run before it is stopped. C library functions charge for
  // the bytes they handle.
  std::uint64_t instructionsPerCall = 10'000'000;
  // The time ceiling of one call, whatever it spends its time on.
  std::chrono::milliseconds callDeadline{1000};
  // What the handlers of one pack may spend on one fired event (HookRegistry::fire), together:
  // hooks run inside the frame, so they get far less than a call.
  std::uint64_t instructionsPerHook = 250'000;
  std::chrono::milliseconds hookDeadline{20};
  // What all the hook handlers of one pack may spend in one frame (HookRegistry::beginFrame). Once
  // it is spent, the pack's handlers are skipped until the next frame.
  std::uint64_t hookInstructionsPerFrame = 500'000;
  std::chrono::milliseconds hookTimePerFrame{40};
  // A story section runs a few thousand instructions between two waits: one that never waits must
  // stop within a frame, not after a call's second.
  std::uint64_t instructionsPerResume = 200'000;
  std::chrono::milliseconds resumeDeadline{20};
};

// A sandboxed Lua 5.5 state. Scripts get the safe standard libraries (base without file loading,
// string, table, math, utf8, coroutine) and the engine API, never io, os, package or native
// modules. The engine's own scripts (the debug console, the dev views) share its globals; each
// pack's scripts run in their own ModEnvironment, which sees only the standard library and the
// versioned API (api_version.hpp) that modules publish through api() and hooks().
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

  // Runs a script asset (`scripts/main` for scripts/main.lua) with the engine's globals and
  // remembers it, so reloadChanged() runs it again when its text changes. The file system must
  // outlive the VM.
  core::Result<void> runAsset(const assets::VirtualFileSystem& files, const assets::AssetId& id);

  // The environment of a pack's scripts, made the first time. Refused when the pack needs another
  // major version of the API.
  core::Result<ModEnvironment> modEnvironment(const assets::PackManifest& pack);

  // Runs a script asset in the environment of the pack that provides it.
  core::Result<void> runModScript(const assets::VirtualFileSystem& files,
                                  const assets::AssetId& id);

  // Where modules publish functions to the scripts.
  [[nodiscard]] ApiRegistry api();

  // Where modules declare and fire the events that packs' scripts handle.
  [[nodiscard]] HookRegistry hooks();

  // Runs Lua source; name identifies it in error messages.
  core::Result<void> runString(std::string_view source, std::string_view name);

  // Runs again every remembered file whose modification time changed, and every remembered asset
  // whose text changed. A file that fails keeps the state it had. A pack whose script changed gets
  // a fresh environment where all its scripts run again, in order; if one fails, the pack keeps its
  // previous environment and handlers.
  void reloadChanged();

  // Calls a global function with numeric arguments. Calling a function that does not exist does
  // nothing.
  core::Result<void> call(std::string_view function, std::span<const double> arguments = {});

  // Reads a number from a global or a field of global tables, as a dotted path like "marker.x".
  [[nodiscard]] std::optional<double> number(std::string_view path);

  // Exposes the input as the global table `input` (value, down, pressed, vector).
  void bindInput(const input::InputState& input);

  // Exposes the player-facing texts: the global function t(key[, arguments]) gives the text of a
  // key in the player's language with its placeholders filled from a table ({count = 3}), as in the
  // UI's documents; texts.language() gives the language. The catalog must outlive the VM.
  void bindTexts(const localization::Catalog& catalog);
  // Exposes a global function of one text argument, for the developer console's commands:
  // bindCommand("text_font",
  // ...) makes `text_font("original")` call `run` with "original". Another argument type is a Lua
  // error.
  void bindCommand(std::string_view name, std::function<void(std::string_view)> run);

  // Why the budget stopped a script in the latest outermost call (a run, a call, a ScriptCall, a
  // fired event), if it did: a hook handler stopped inside a call that went on counts too. None
  // once the next outermost call starts.
  [[nodiscard]] BudgetStop lastBudgetStop() const;

private:
  friend class ScriptCall;

  explicit ScriptVm(std::unique_ptr<detail::VmState> vm);

  std::unique_ptr<detail::VmState> vm_;
};

// The only way into the VM's Lua state from outside the script module: while one lives, the scripts
// it runs have the limits of a call (instructions, time, memory). The outermost one gives the call
// a full budget; nested ones, such as a script calling an API function that fires an event, share
// what is left of it.
class ScriptCall {
public:
  explicit ScriptCall(ScriptVm& vm);
  ScriptCall(const ScriptCall&) = delete;
  ScriptCall& operator=(const ScriptCall&) = delete;
  ScriptCall(ScriptCall&&) = delete;
  ScriptCall& operator=(ScriptCall&&) = delete;
  ~ScriptCall();

  // The underlying state, for modules that register their own API or call Lua functions; valid
  // while this lives.
  [[nodiscard]] sol::state& lua() const;

private:
  detail::VmState* vm_;
  std::size_t previousOwner_;
};

} // namespace dwe::script
