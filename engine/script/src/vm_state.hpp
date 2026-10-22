#pragma once

#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>
#include <dwe/script/api_registry.hpp>
#include <dwe/script/script_vm.hpp>

#include <sol/sol.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "budget.hpp"
#include "memory.hpp"
#include "pack_logs.hpp"

namespace dwe::script::detail {

// A script file on disk that reloadChanged() runs again when it is edited.
struct WatchedFile {
  std::filesystem::path path;
  std::filesystem::file_time_type modified;
};

// A script asset that reloadChanged() runs again when its text changes. The file system must
// outlive the VM.
struct WatchedAsset {
  const assets::VirtualFileSystem* files = nullptr;
  assets::AssetId id;
  std::size_t textHash = 0;
};

enum class PublishedKind : std::uint8_t { Function, WaitingFunction, Value };

// A function of the API, at a stable address: the Lua closures that call it point to it.
struct PublishedFunction {
  std::string apiNamespace;
  std::string name;
  // apiNamespace.name, as errors name it.
  std::string qualifiedName;
  ApiFunction run;
  PublishedKind kind = PublishedKind::Function;
  ApiValue value;
};

// The Lua side of a pack's environment, as references in the Lua registry.
struct ModTables {
  // The environment: the mod's globals.
  int environment = LUA_NOREF;
  // The tables behind the read-only API namespaces of the environment, by namespace name.
  int namespaces = LUA_NOREF;
  // The mod's hook handlers: event name -> list of functions.
  int handlers = LUA_NOREF;
};

// What a call into Lua may spend.
struct CallBudget {
  std::int64_t instructions = 0;
  std::chrono::steady_clock::duration time{};
};

// What the hook handlers of a pack may still spend in the frame of frameNumber.
struct FrameBudget {
  std::uint64_t frameNumber = 0;
  CallBudget left;
};

struct ModState {
  std::string pack;
  // Who pays for the memory its calls reserve.
  MemoryOwner memoryOwner = EngineMemory;
  ModTables tables;
  // The assets it ran, in order, so a reload runs them again.
  std::vector<WatchedAsset> assets;
  FrameBudget hookFrame;
  // When the log last said its handlers were skipped.
  std::optional<std::chrono::steady_clock::time_point> hookBudgetReported;
};

// Everything the VM and its API handles (ApiRegistry, HookRegistry, ModEnvironment) share.
struct VmState {
  Memory memory;
  Budget budget;
  // Before lua, which holds the print functions that point to it.
  PackLogs packLogs;
  sol::state lua;
  // How many engine calls into Lua are running, one inside another: only the outermost one refills
  // the budget.
  int callDepth = 0;
  std::vector<WatchedFile> files;
  std::vector<WatchedAsset> assets;
  // The standard library as it was before any script ran: mod environments copy it.
  int pristineGlobals = LUA_NOREF;
  // The tables behind the API namespaces of the engine's own globals, by namespace name.
  int engineNamespaces = LUA_NOREF;
  std::vector<std::unique_ptr<PublishedFunction>> functions;
  std::set<std::string, std::less<>> events;
  // In the order they were made, which is the order their hook handlers run in.
  std::vector<ModState> mods;
  // What the hook handlers of a pack may spend per fired event and per frame.
  CallBudget hookBudget;
  CallBudget hookFrameBudget;
  CallBudget resumeBudget;
  // The frame running now; HookRegistry::beginFrame starts the next one. It starts at 1, so a new
  // pack's FrameBudget, of frame 0, is refilled before its first hook runs.
  std::uint64_t frameNumber = 1;

  explicit VmState(const ScriptLimits& limits);

  [[nodiscard]] lua_State* state() { return lua.lua_state(); }

  // The mod of a pack, or nullptr.
  ModState* findMod(std::string_view pack);
};

// Starts an engine call into Lua whose new memory owner pays for: the outermost call gets a full
// budget (a call's, or the one given) and charges its allocations to it, nested ones share what is
// left of it. Gives the owner that paid before.
MemoryOwner enterCall(VmState& vm, MemoryOwner owner,
                      const std::optional<CallBudget>& budget = std::nullopt);

// Ends a call that enterCall started.
void leaveCall(VmState& vm, MemoryOwner previousOwner);

// An engine call into Lua, as enterCall and leaveCall. Given a budget, a nested call (a hook fired
// from inside a script's call) gets no more than it, nor than the outer call has left; what it
// spends comes out of the outer call.
class CallScope {
public:
  explicit CallScope(VmState& vm, MemoryOwner owner = EngineMemory,
                     const std::optional<CallBudget>& budget = std::nullopt);
  CallScope(const CallScope&) = delete;
  CallScope& operator=(const CallScope&) = delete;
  CallScope(CallScope&&) = delete;
  CallScope& operator=(CallScope&&) = delete;
  ~CallScope();

  // The instructions the call spent so far, with what it paid up front.
  [[nodiscard]] std::int64_t spent() const;

private:
  // What the outer call had when a nested one with its own budget started.
  struct OuterBudget {
    std::int64_t left = 0;
    std::chrono::steady_clock::time_point deadline;
    lua_State* running = nullptr;
  };

  VmState* vm_;
  std::optional<OuterBudget> outer_;
  MemoryOwner previousOwner_;
  // The budget's instructions when the call started, before its up-front payment.
  std::int64_t granted_ = 0;
};

// Runs body inside a protected call, with request as its only argument (a light userdata): a Lua
// error, such as running out of memory or a script's own error, becomes the result's error. The
// body must leave nothing that needs a destructor alive while it may raise.
core::Result<void> runProtected(lua_State* state, lua_CFunction body, void* request);

// The hash that tells whether a script's text changed.
std::size_t hashText(std::string_view text);

// Remembers a script asset with the hash of its text, replacing what was remembered of it.
void rememberAsset(std::vector<WatchedAsset>& assets, const assets::VirtualFileSystem& files,
                   const assets::AssetId& id, std::string_view text);

} // namespace dwe::script::detail
