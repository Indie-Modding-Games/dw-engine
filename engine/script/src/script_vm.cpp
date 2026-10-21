#include <dwe/assets/file_system.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/script/api_version.hpp>
#include <dwe/script/script_vm.hpp>

#include <sol/sol.hpp>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "address_free_text.hpp"
#include "budgeted_coroutines.hpp"
#include "budgeted_error_handlers.hpp"
#include "budgeted_strings.hpp"
#include "budgeted_tables.hpp"
#include "locked_tables.hpp"
#include "lua_errors.hpp"
#include "messages.hpp"
#include "mod_tables.hpp"
#include "print_to_log.hpp"
#include "script_assets.hpp"
#include "string_patterns.hpp"
#include "vm_state.hpp"

namespace dwe::script {
namespace {

using detail::errorMessage;
using detail::raiseError;

// The one option of collectgarbage that scripts have.
constexpr const char* GarbageCountOption = "count";
constexpr int GarbageOptionArgument = 1;

// collectgarbage limited to reading the memory in use: the other options would let a script stop the collector or
// change how it runs. A pack's call sees only its own bytes, so it learns nothing of the other packs; the engine's
// calls see every byte Lua holds, which is what LUA_GCCOUNT reports.
int countGarbage(lua_State* state) {
    const char* option = luaL_optstring(state, GarbageOptionArgument, GarbageCountOption);
    if (std::string_view{option} != GarbageCountOption) {
        return raiseError(state, {messages::GarbageOption, option, messages::GarbageOptionEnd});
    }
    constexpr double BytesPerKilobyte = 1024.0;
    void* userData = nullptr;
    lua_getallocf(state, &userData);
    const auto& memory = *static_cast<const detail::Memory*>(userData);
    const std::size_t used = memory.owner == detail::EngineMemory ? memory.used : memory.usedByOwner[memory.owner];
    lua_pushnumber(state, static_cast<double>(used) / BytesPerKilobyte);
    return 1;
}

// The standard library as it is before any script runs, kept for packs' environments: the engine's globals change
// as modules bind their functions and the debug console runs code.
int snapshotGlobals(lua_State* state) {
    auto* reference = static_cast<int*>(lua_touserdata(state, 1));
    lua_newtable(state);
    const int pristine = lua_gettop(state);
    lua_pushglobaltable(state);
    detail::copyGlobals(state, pristine + 1, pristine);
    lua_pop(state, 1);
    lua_pushnil(state);
    lua_setfield(state, pristine, LUA_GNAME);
    *reference = luaL_ref(state, LUA_REGISTRYINDEX);
    return 0;
}

int newRegistryTable(lua_State* state) {
    auto* reference = static_cast<int*>(lua_touserdata(state, 1));
    lua_newtable(state);
    *reference = luaL_ref(state, LUA_REGISTRYINDEX);
    return 0;
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
        luaL_checkstack(state, 2, messages::PathTooDeep);
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

std::filesystem::file_time_type modifiedTime(const std::filesystem::path& path) {
    std::error_code error;
    const auto time = std::filesystem::last_write_time(path, error);
    return error ? std::filesystem::file_time_type{} : time;
}

// Runs source with the engine's globals, as one call into Lua.
core::Result<void> runWithGlobals(detail::VmState& vm, std::string_view source, std::string_view name) {
    const detail::CallScope scope{vm};
    // Text only: precompiled bytecode is not checked by Lua and could break out of the sandbox.
    sol::protected_function_result result =
        vm.lua.safe_script(source, sol::script_pass_on_error, std::string{name}, sol::load_mode::text);
    // Not through sol::error: without exceptions, the MSVC library's std::runtime_error keeps only a pointer to the
    // text it was built from, which is gone by the time what() reads it.
    if (!result.valid()) {
        return core::fail(std::string{errorMessage(vm.state(), result.stack_index())});
    }
    return {};
}

// Whose globals an asset runs with: a mod's environment or the engine's (runAsset).
enum class AssetRunner : std::uint8_t { Mod, Engine };

// The asset's text now, when it differs from what ran. An engine asset is checked again as runAsset checks it.
std::optional<detail::ScriptAsset> changedAsset(const detail::WatchedAsset& watched,
                                                AssetRunner runner = AssetRunner::Mod) {
    auto script = runner == AssetRunner::Engine ? detail::readEngineScriptAsset(*watched.files, watched.id)
                                                : detail::readScriptAsset(*watched.files, watched.id);
    if (!script || detail::hashText(script->text) == watched.textHash) {
        return std::nullopt;
    }
    return std::move(*script);
}

void reloadGlobalFiles(ScriptVm& vm, const std::vector<detail::WatchedFile>& files) {
    for (const detail::WatchedFile& file : files) {
        if (modifiedTime(file.path) == file.modified) {
            continue;
        }
        if (auto reloaded = vm.runFile(file.path); reloaded) {
            core::logInfo("{}", core::formatMessage(messages::Reloaded, assets::pathText(file.path)));
        } else {
            core::logError("{}", core::formatMessage(messages::ReloadFailed, assets::pathText(file.path),
                                                     detail::clippedScriptText(reloaded.error().message)));
        }
    }
}

void reloadGlobalAssets(detail::VmState& vm) {
    // By index, never by reference: a script that runs may watch another asset, which grows the list.
    // NOLINTNEXTLINE(modernize-loop-convert)
    for (std::size_t index = 0; index < vm.assets.size(); ++index) {
        auto script = changedAsset(vm.assets[index], AssetRunner::Engine);
        if (!script) {
            continue;
        }
        vm.assets[index].textHash = detail::hashText(script->text);
        const assets::AssetId asset = vm.assets[index].id;
        const std::string& id = asset.str();
        if (auto reloaded = runWithGlobals(vm, script->text, detail::assetChunkName(script->pack, asset)); reloaded) {
            core::logInfo("{}", core::formatMessage(messages::Reloaded, id));
        } else {
            core::logError("{}", core::formatMessage(messages::ReloadFailed, id,
                                                     detail::clippedScriptText(reloaded.error().message)));
        }
    }
}

// Runs every script of a pack again, in order, in a fresh environment, which replaces the old one only if all of
// them run. By index, never by reference: a script may call an API function that makes another pack's environment,
// which may move the mods.
core::Result<void> rerunMod(detail::VmState& vm, std::size_t index) {
    const std::string pack = vm.mods[index].pack;
    const detail::MemoryOwner owner = vm.mods[index].memoryOwner;
    auto tables = detail::makeModTables(vm, pack);
    if (!tables) {
        return std::unexpected{tables.error()};
    }
    for (std::size_t asset = 0; asset < vm.mods[index].assets.size(); ++asset) {
        const detail::WatchedAsset watched = vm.mods[index].assets[asset];
        auto script = detail::readScriptAsset(*watched.files, watched.id);
        auto ran = script ? detail::runInEnvironment(vm, *tables, owner, script->text,
                                                     detail::assetChunkName(pack, watched.id))
                          : core::Result<void>{std::unexpected{script.error()}};
        if (!ran) {
            detail::releaseModTables(vm, *tables);
            return ran;
        }
        vm.mods[index].assets[asset].textHash = detail::hashText(script->text);
    }
    detail::releaseModTables(vm, vm.mods[index].tables);
    vm.mods[index].tables = *tables;
    return {};
}

bool hasChangedAsset(const detail::ModState& mod) {
    return std::ranges::any_of(mod.assets, [](const auto& watched) { return changedAsset(watched).has_value(); });
}

void reloadMods(detail::VmState& vm) {
    for (std::size_t index = 0; index < vm.mods.size(); ++index) {
        if (!hasChangedAsset(vm.mods[index])) {
            continue;
        }
        const std::string pack = vm.mods[index].pack;
        if (auto reloaded = rerunMod(vm, index); reloaded) {
            core::logInfo("{}", core::formatMessage(messages::ModReloaded, pack));
        } else {
            // Its texts as they are now, so the same edit is not tried again on every call.
            for (detail::WatchedAsset& watched : vm.mods[index].assets) {
                if (auto script = changedAsset(watched); script) {
                    watched.textHash = detail::hashText(script->text);
                }
            }
            core::logError("{}", core::formatMessage(messages::ModReloadFailed, pack,
                                                     detail::clippedScriptText(reloaded.error().message)));
        }
    }
}

} // namespace

core::Result<std::unique_ptr<ScriptVm>> ScriptVm::create(const ScriptLimits& limits) {
    auto vm = std::make_unique<detail::VmState>(limits);
    sol::state& lua = vm->lua;
    lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math, sol::lib::utf8,
                       sol::lib::coroutine);
    // Loading code from files or strings would escape the sandbox and the instruction budget.
    lua["dofile"] = sol::lua_nil;
    lua["loadfile"] = sol::lua_nil;
    lua["load"] = sol::lua_nil;
    lua["string"]["dump"] = sol::lua_nil;
    // Warnings would go to stderr outside the log and its limits.
    lua["warn"] = sol::lua_nil;
    lua_State* state = vm->state();
    lua_register(state, "collectgarbage", countGarbage);
    lua_register(state, "print", detail::printToLog);
    detail::installBudgetedPatterns(state);
    detail::installBudgetedStrings(state);
    detail::installAddressFreeText(state);
    detail::installBudgetedTables(state);
    detail::installBudgetedCoroutines(state);
    detail::installBudgetedErrorHandlers(state);
    detail::refuseFinalizers(state);
    detail::freezeStringMetatable(state);
    if (auto snapshot = detail::runProtected(state, snapshotGlobals, &vm->pristineGlobals); !snapshot) {
        return std::unexpected{snapshot.error()};
    }
    if (auto made = detail::runProtected(state, newRegistryTable, &vm->engineNamespaces); !made) {
        return std::unexpected{made.error()};
    }
    return std::unique_ptr<ScriptVm>{new ScriptVm{std::move(vm)}};
}

ScriptVm::ScriptVm(std::unique_ptr<detail::VmState> vm) : vm_{std::move(vm)} {}

ScriptVm::~ScriptVm() {
    if (vm_) {
        vm_->packLogs.flush();
    }
}

core::Result<void> ScriptVm::runFile(const std::filesystem::path& path) {
    // Binary, as Lua reads a file itself: the same bytes on every platform (Lua takes \r\n line ends).
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        return core::fail(core::formatMessage(messages::CannotOpenScript, assets::pathText(path)));
    }
    std::stringstream source;
    source << file.rdbuf();

    auto known = std::ranges::find(vm_->files, path, &detail::WatchedFile::path);
    if (known == vm_->files.end()) {
        vm_->files.push_back(detail::WatchedFile{.path = path, .modified = modifiedTime(path)});
    } else {
        known->modified = modifiedTime(path);
    }
    return runWithGlobals(*vm_, source.str(), "@" + assets::pathText(path));
}

core::Result<void> ScriptVm::runString(std::string_view source, std::string_view name) {
    return runWithGlobals(*vm_, source, name);
}

core::Result<void> ScriptVm::runAsset(const assets::VirtualFileSystem& files, const assets::AssetId& id) {
    auto script = detail::readEngineScriptAsset(files, id);
    if (!script) {
        return std::unexpected{script.error()};
    }
    detail::rememberAsset(vm_->assets, files, id, script->text);
    return runWithGlobals(*vm_, script->text, detail::assetChunkName(script->pack, id));
}

core::Result<ModEnvironment> ScriptVm::modEnvironment(const assets::PackManifest& pack) {
    if (auto compatible = assets::checkPackApi(pack, LuaApiVersion); !compatible) {
        return std::unexpected{compatible.error()};
    }
    if (auto valid = assets::checkPackId(pack.id); !valid) {
        return std::unexpected{valid.error()};
    }
    const auto known = std::ranges::find(vm_->mods, pack.id, &detail::ModState::pack);
    if (known != vm_->mods.end()) {
        return ModEnvironment{*vm_, static_cast<std::size_t>(known - vm_->mods.begin())};
    }
    auto tables = detail::makeModTables(*vm_, pack.id);
    if (!tables) {
        return core::fail(core::formatMessage(messages::CannotMakeEnvironment, pack.id, tables.error().message));
    }
    vm_->mods.push_back(detail::ModState{.pack = pack.id,
                                         .memoryOwner = vm_->memory.addOwner(pack.id),
                                         .tables = *tables,
                                         .assets = {},
                                         .hookFrame = {},
                                         .hookBudgetReported = {}});
    return ModEnvironment{*vm_, vm_->mods.size() - 1};
}

core::Result<void> ScriptVm::runModScript(const assets::VirtualFileSystem& files, const assets::AssetId& id) {
    const assets::AssetSource* source = files.resolve(id);
    if (source == nullptr) {
        return core::fail(core::formatMessage(messages::NoScriptAsset, id.str()));
    }
    const auto pack = std::ranges::find(files.packs(), source->packId,
                                        [](const assets::MountedPack& mounted) { return mounted.manifest.id; });
    if (pack == files.packs().end()) {
        return core::fail(core::formatMessage(messages::NoScriptAsset, id.str()));
    }
    auto environment = modEnvironment(pack->manifest);
    if (!environment) {
        return std::unexpected{environment.error()};
    }
    return environment->runAsset(files, id);
}

ApiRegistry ScriptVm::api() {
    return ApiRegistry{*vm_};
}

HookRegistry ScriptVm::hooks() {
    return HookRegistry{*vm_};
}

void ScriptVm::reloadChanged() {
    reloadGlobalFiles(*this, std::vector<detail::WatchedFile>{vm_->files});
    reloadGlobalAssets(*vm_);
    reloadMods(*vm_);
}

core::Result<void> ScriptVm::call(std::string_view function, std::span<const double> arguments) {
    GlobalCall request{.function = function, .arguments = arguments};
    const detail::CallScope scope{*vm_};
    if (auto called = detail::runProtected(vm_->state(), callGlobal, &request); !called) {
        return core::fail(core::formatMessage(messages::CallFailed, function, called.error().message));
    }
    return {};
}

std::optional<double> ScriptVm::number(std::string_view path) {
    NumberLookup lookup{.path = path, .result = std::nullopt};
    const detail::CallScope scope{*vm_};
    if (!detail::runProtected(vm_->state(), lookUpNumber, &lookup)) {
        return std::nullopt;
    }
    return lookup.result;
}

ScriptCall::ScriptCall(ScriptVm& vm)
    : vm_{vm.vm_.get()}, previousOwner_{detail::enterCall(*vm_, detail::EngineMemory)} {}

ScriptCall::~ScriptCall() {
    detail::leaveCall(*vm_, previousOwner_);
}

sol::state& ScriptCall::lua() const {
    return vm_->lua;
}

} // namespace dwe::script
