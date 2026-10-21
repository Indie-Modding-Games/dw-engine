#include <dwe/core/message.hpp>
#include <dwe/script/api_registry.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <utility>
#include <variant>

#include "api_functions.hpp"
#include "api_names.hpp"
#include "hooks_table.hpp"
#include "messages.hpp"
#include "vm_state.hpp"

namespace dwe::script {
namespace {

std::unexpected<core::Error> badArgument(std::size_t index, std::string_view expected) {
    return core::fail(core::formatMessage(messages::ApiArgumentMustBe, index + 1, expected));
}

// Whether a whole double fits in an int64: the bounds are -2^63 inclusive and 2^63 exclusive.
bool fitsInteger(double number) {
    constexpr double Limit = 9223372036854775808.0;
    return number >= -Limit && number < Limit && std::trunc(number) == number;
}

bool isOtherGlobal(lua_State* state, int globals, int namespaces, std::string_view name) {
    lua_pushlstring(state, name.data(), name.size());
    const bool isNamespace = lua_rawget(state, namespaces) != LUA_TNIL;
    lua_pop(state, 1);
    lua_pushlstring(state, name.data(), name.size());
    const bool isGlobal = lua_rawget(state, globals) != LUA_TNIL;
    lua_pop(state, 1);
    return isGlobal && !isNamespace;
}

// Whether a namespace would take the name of a global that is not an API namespace: one of the engine's globals or
// of the standard library that packs' environments copy.
struct NamespaceCheck {
    const detail::VmState* vm;
    std::string_view apiNamespace;
    bool taken;
};

int checkNamespace(lua_State* state) {
    auto* check = static_cast<NamespaceCheck*>(lua_touserdata(state, 1));
    lua_pushglobaltable(state);
    lua_rawgeti(state, LUA_REGISTRYINDEX, check->vm->engineNamespaces);
    lua_rawgeti(state, LUA_REGISTRYINDEX, check->vm->pristineGlobals);
    const int pristine = lua_gettop(state);
    const int engineNamespaces = pristine - 1;
    const int globals = pristine - 2;
    check->taken = isOtherGlobal(state, globals, engineNamespaces, check->apiNamespace) ||
                   isOtherGlobal(state, pristine, engineNamespaces, check->apiNamespace);
    return 0;
}

// Adds a published function to the engine's globals and to every pack's environment.
struct Publication {
    detail::VmState* vm;
    detail::PublishedFunction* function;
};

int publishInLua(lua_State* state) {
    auto* publication = static_cast<Publication*>(lua_touserdata(state, 1));
    detail::VmState& vm = *publication->vm;
    lua_pushglobaltable(state);
    lua_rawgeti(state, LUA_REGISTRYINDEX, vm.engineNamespaces);
    detail::addApiFunction(state, -2, -1, *publication->function, {});
    lua_pop(state, 2);
    for (detail::ModState& mod : vm.mods) {
        lua_rawgeti(state, LUA_REGISTRYINDEX, mod.tables.environment);
        lua_rawgeti(state, LUA_REGISTRYINDEX, mod.tables.namespaces);
        detail::addApiFunction(state, -2, -1, *publication->function, mod.pack);
        lua_pop(state, 2);
    }
    return 0;
}

} // namespace

core::Result<bool> ApiCall::boolean(std::size_t index) const {
    const ApiValue* value = index < arguments_.size() ? &arguments_[index] : nullptr;
    if (const bool* held = value != nullptr ? std::get_if<bool>(value) : nullptr; held != nullptr) {
        return *held;
    }
    return badArgument(index, messages::BooleanKind);
}

core::Result<std::int64_t> ApiCall::integer(std::size_t index) const {
    const ApiValue* value = index < arguments_.size() ? &arguments_[index] : nullptr;
    if (value != nullptr) {
        if (const auto* held = std::get_if<std::int64_t>(value); held != nullptr) {
            return *held;
        }
        if (const auto* number = std::get_if<double>(value); number != nullptr && fitsInteger(*number)) {
            return static_cast<std::int64_t>(*number);
        }
    }
    return badArgument(index, messages::IntegerKind);
}

core::Result<double> ApiCall::number(std::size_t index) const {
    const ApiValue* value = index < arguments_.size() ? &arguments_[index] : nullptr;
    if (value != nullptr) {
        if (const auto* held = std::get_if<double>(value); held != nullptr) {
            return *held;
        }
        if (const auto* integer = std::get_if<std::int64_t>(value); integer != nullptr) {
            return static_cast<double>(*integer);
        }
    }
    return badArgument(index, messages::NumberKind);
}

core::Result<std::string_view> ApiCall::text(std::size_t index) const {
    const ApiValue* value = index < arguments_.size() ? &arguments_[index] : nullptr;
    if (const auto* held = value != nullptr ? std::get_if<std::string>(value) : nullptr; held != nullptr) {
        return std::string_view{*held};
    }
    return badArgument(index, messages::TextKind);
}

core::Result<void> ApiRegistry::publish(std::string_view apiNamespace, std::string_view name, ApiFunction function) {
    if (!detail::isApiName(apiNamespace) || !detail::isApiName(name)) {
        return core::fail(core::formatMessage(messages::BadApiName, apiNamespace, name));
    }
    if (apiNamespace == detail::HooksGlobal) {
        return core::fail(core::formatMessage(messages::ReservedNamespace, apiNamespace));
    }
    if (isPublished(apiNamespace, name)) {
        return core::fail(core::formatMessage(messages::AlreadyPublished, apiNamespace, name));
    }
    NamespaceCheck check{.vm = vm_, .apiNamespace = apiNamespace, .taken = false};
    if (auto checked = detail::runProtected(vm_->state(), checkNamespace, &check); !checked) {
        return core::fail(core::formatMessage(messages::CannotPublish, detail::qualifiedApiName(apiNamespace, name),
                                              checked.error().message));
    }
    if (check.taken) {
        return core::fail(core::formatMessage(messages::NamespaceTakesGlobal, apiNamespace));
    }
    // Listed before Lua gets closures that point to it, so it lives as long as they do even if adding fails halfway.
    detail::PublishedFunction& published =
        *vm_->functions.emplace_back(std::make_unique<detail::PublishedFunction>(detail::PublishedFunction{
            .apiNamespace = std::string{apiNamespace},
            .name = std::string{name},
            .qualifiedName = detail::qualifiedApiName(apiNamespace, name),
            .run = std::move(function),
        }));
    Publication publication{.vm = vm_, .function = &published};
    if (auto added = detail::runProtected(vm_->state(), publishInLua, &publication); !added) {
        return core::fail(core::formatMessage(messages::CannotPublish, published.qualifiedName, added.error().message));
    }
    return {};
}

bool ApiRegistry::isPublished(std::string_view apiNamespace, std::string_view name) const {
    return std::ranges::any_of(vm_->functions, [&](const std::unique_ptr<detail::PublishedFunction>& function) {
        return function->apiNamespace == apiNamespace && function->name == name;
    });
}

} // namespace dwe::script
