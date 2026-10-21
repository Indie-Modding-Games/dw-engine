#include <dwe/input/input_state.hpp>
#include <dwe/script/script_vm.hpp>

#include <sol/sol.hpp>

#include <string>
#include <tuple>

#include "vm_state.hpp"

namespace dwe::script {
namespace {

std::string familyName(platform::GamepadFamily family) {
    switch (family) {
    case platform::GamepadFamily::Xbox:
        return "xbox";
    case platform::GamepadFamily::PlayStation:
        return "playstation";
    case platform::GamepadFamily::Nintendo:
        return "nintendo";
    case platform::GamepadFamily::Steam:
        return "steam";
    case platform::GamepadFamily::Generic:
        break;
    }
    return "generic";
}

} // namespace

void ScriptVm::bindInput(const input::InputState& input) {
    const input::InputState* state = &input;
    sol::table api = vm_->lua.create_named_table("input");
    api["value"] = [state](const std::string& action) { return state->value(action); };
    api["down"] = [state](const std::string& action) { return state->isDown(action); };
    api["pressed"] = [state](const std::string& action) { return state->wasPressed(action); };
    api["vector"] = [state](const std::string& left, const std::string& right, const std::string& up,
                            const std::string& down) {
        const input::Vector2 direction = state->vector(left, right, up, down);
        return std::make_tuple(direction.x, direction.y);
    };
    api["device"] = [state]() -> std::string {
        return state->lastDevice() == input::DeviceKind::Gamepad ? "gamepad" : "keyboard_mouse";
    };
    api["gamepad_family"] = [state]() { return familyName(state->lastGamepadFamily()); };
}

} // namespace dwe::script
