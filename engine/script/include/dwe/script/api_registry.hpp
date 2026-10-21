#pragma once

#include <dwe/core/error.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace dwe::script {

namespace detail {
struct VmState;
}

// A value passed between C++ and the scripts through the API: nil, a boolean, an integer, a number or a text.
using ApiValue = std::variant<std::monostate, bool, std::int64_t, double, std::string>;

// One call of an API function from a script.
class ApiCall {
public:
    ApiCall(std::string_view pack, std::span<const ApiValue> arguments) : pack_{pack}, arguments_{arguments} {}

    // The pack whose script calls, which names the content it adds (`pack:thing`). Empty for the engine's own scripts.
    [[nodiscard]] std::string_view pack() const { return pack_; }

    [[nodiscard]] std::span<const ApiValue> arguments() const { return arguments_; }

    // The argument at index, from 0, as one type. An integer is also a number, and a number without a fraction an
    // integer. The error names the argument as the script counts it, from 1.
    [[nodiscard]] core::Result<bool> boolean(std::size_t index) const;
    [[nodiscard]] core::Result<std::int64_t> integer(std::size_t index) const;
    [[nodiscard]] core::Result<double> number(std::size_t index) const;
    [[nodiscard]] core::Result<std::string_view> text(std::size_t index) const;

private:
    std::string_view pack_;
    std::span<const ApiValue> arguments_;
};

// A function scripts can call. Its error becomes a Lua error in the calling script, after the function's name.
using ApiFunction = std::function<core::Result<ApiValue>(const ApiCall& call)>;

// Where the modules that own a part of the game publish it to the scripts, by namespace: publish("story",
// "give_item", ...) gives `story.give_item(...)` to the engine's globals and to every pack's environment, now and
// later. A namespace is a read-only table: scripts cannot change it, and in a pack's environment each call knows the
// pack that made it. Get one from ScriptVm::api(); it must not outlive the VM.
class ApiRegistry {
public:
    explicit ApiRegistry(detail::VmState& vm) : vm_{&vm} {}

    // Publishes a function. Namespaces and names have 1 to 64 characters of a-z, 0-9 and _, starting with a letter;
    // a namespace cannot take the name of a global, such as a standard library or `hooks`, and a function is
    // published once.
    core::Result<void> publish(std::string_view apiNamespace, std::string_view name, ApiFunction function);

    [[nodiscard]] bool isPublished(std::string_view apiNamespace, std::string_view name) const;

private:
    detail::VmState* vm_;
};

} // namespace dwe::script
