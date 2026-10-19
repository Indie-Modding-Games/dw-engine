#include <dwe/input/input_state.hpp>

#include <algorithm>
#include <cmath>
#include <type_traits>
#include <utility>

namespace dwe::input {

using namespace platform;

namespace {

// Down is any value past the middle, so a half-pulled trigger or a stick pushed halfway counts as held.
constexpr float DownThreshold = 0.5F;

constexpr std::size_t index(auto value) {
    return static_cast<std::size_t>(std::to_underlying(value));
}

// How far one half of an axis is pushed, from 0 to 1, with the deadzone removed.
float axisHalf(float raw, float direction, float deadzone) {
    const float half = std::max(0.0F, raw * direction);
    return std::clamp((half - deadzone) / (1.0F - deadzone), 0.0F, 1.0F);
}

template <typename T>
bool sameControl(const Binding& binding, const T& changed) {
    const T* candidate = std::get_if<T>(&binding);
    if (candidate == nullptr) {
        return false;
    }
    if constexpr (std::is_same_v<T, KeyBinding>) {
        return candidate->key == changed.key;
    } else if constexpr (std::is_same_v<T, MouseBinding>) {
        return candidate->button == changed.button;
    } else if constexpr (std::is_same_v<T, GamepadButtonBinding>) {
        return candidate->button == changed.button;
    } else {
        return candidate->axis == changed.axis && candidate->direction == changed.direction;
    }
}

} // namespace

InputState::InputState(ActionMap map, float deadzone) : map_{std::move(map)}, deadzone_{deadzone} {}

void InputState::handle(const Event& event) {
    if (const auto* key = std::get_if<KeyChanged>(&event)) {
        if (key->repeat) {
            return;
        }
        lastDevice_ = DeviceKind::KeyboardMouse;
        if (key->down) {
            keysDown_.insert(key->key);
            notePressed(KeyBinding{key->key});
        } else {
            keysDown_.erase(key->key);
        }
    } else if (const auto* mouse = std::get_if<MouseButtonChanged>(&event)) {
        lastDevice_ = DeviceKind::KeyboardMouse;
        mouseButtons_[index(mouse->button)] = mouse->down;
        if (mouse->down) {
            notePressed(MouseBinding{mouse->button});
        }
    } else if (std::holds_alternative<FocusLost>(event)) {
        keysDown_.clear();
        mouseButtons_.fill(false);
        for (GamepadState& state : gamepadStates_) {
            state.buttons.fill(false);
            state.axes.fill(0.0F);
        }
    } else if (std::holds_alternative<MouseMoved>(event)) {
        lastDevice_ = DeviceKind::KeyboardMouse;
    } else if (const auto* connected = std::get_if<GamepadConnected>(&event)) {
        if (findGamepad(connected->id) == nullptr) {
            gamepadStates_.push_back(GamepadState{.id = connected->id});
            gamepads_.push_back(
                ConnectedGamepad{.id = connected->id, .family = connected->family, .name = connected->name});
        }
    } else if (const auto* disconnected = std::get_if<GamepadDisconnected>(&event)) {
        std::erase_if(gamepadStates_, [&](const GamepadState& state) { return state.id == disconnected->id; });
        std::erase_if(gamepads_, [&](const ConnectedGamepad& pad) { return pad.id == disconnected->id; });
        gamepadLost_ = true;
    } else if (const auto* button = std::get_if<GamepadButtonChanged>(&event)) {
        GamepadState* state = findGamepad(button->id);
        if (state == nullptr) {
            return;
        }
        useGamepad(button->id);
        state->buttons[index(button->button)] = button->down;
        if (button->down) {
            notePressed(GamepadButtonBinding{button->button});
        }
    } else if (const auto* axis = std::get_if<GamepadAxisMoved>(&event)) {
        GamepadState* state = findGamepad(axis->id);
        if (state == nullptr) {
            return;
        }
        const float before = state->axes[index(axis->axis)];
        state->axes[index(axis->axis)] = axis->value;
        if (std::abs(axis->value) > deadzone_) {
            useGamepad(axis->id);
        }
        // Crossing the threshold counts as a press of that half of the axis, measured as isDown() measures it.
        for (const float direction : {1.0F, -1.0F}) {
            if (axisHalf(before, direction, deadzone_) < DownThreshold &&
                axisHalf(axis->value, direction, deadzone_) >= DownThreshold) {
                notePressed(GamepadAxisBinding{.axis = axis->axis, .direction = direction});
            }
        }
    }
}

void InputState::endTick() {
    pressedThisTick_.clear();
    gamepadLost_ = false;
}

float InputState::value(std::string_view action) const {
    float strongest = 0.0F;
    for (const Binding& binding : map_.bindings(action)) {
        strongest = std::max(strongest, bindingValue(binding));
    }
    return strongest;
}

bool InputState::isDown(std::string_view action) const {
    return value(action) >= DownThreshold;
}

bool InputState::wasPressed(std::string_view action) const {
    return pressedThisTick_.contains(std::string{action});
}

Vector2 InputState::vector(std::string_view left, std::string_view right, std::string_view up,
                           std::string_view down) const {
    Vector2 direction{.x = value(right) - value(left), .y = value(down) - value(up)};
    const float length = std::hypot(direction.x, direction.y);
    if (length > 1.0F) {
        direction.x /= length;
        direction.y /= length;
    }
    return direction;
}

float InputState::bindingValue(const Binding& binding) const {
    if (const auto* key = std::get_if<KeyBinding>(&binding)) {
        return keysDown_.contains(key->key) ? 1.0F : 0.0F;
    }
    if (const auto* mouse = std::get_if<MouseBinding>(&binding)) {
        return mouseButtons_[index(mouse->button)] ? 1.0F : 0.0F;
    }
    float strongest = 0.0F;
    for (const GamepadState& state : gamepadStates_) {
        if (const auto* button = std::get_if<GamepadButtonBinding>(&binding)) {
            strongest = std::max(strongest, state.buttons[index(button->button)] ? 1.0F : 0.0F);
        } else if (const auto* axis = std::get_if<GamepadAxisBinding>(&binding)) {
            strongest = std::max(strongest, axisHalf(state.axes[index(axis->axis)], axis->direction, deadzone_));
        }
    }
    return strongest;
}

void InputState::notePressed(const Binding& changed) {
    for (const ActionMap::Entry& entry : map_.entries()) {
        const bool bound = std::ranges::any_of(entry.bindings, [&](const Binding& binding) {
            return std::visit([&](const auto& control) { return sameControl(binding, control); }, changed);
        });
        if (bound) {
            pressedThisTick_.insert(entry.action);
        }
    }
}

InputState::GamepadState* InputState::findGamepad(GamepadId id) {
    auto found = std::ranges::find(gamepadStates_, id, &GamepadState::id);
    return found == gamepadStates_.end() ? nullptr : &*found;
}

void InputState::useGamepad(GamepadId id) {
    lastDevice_ = DeviceKind::Gamepad;
    auto pad = std::ranges::find(gamepads_, id, &ConnectedGamepad::id);
    if (pad != gamepads_.end()) {
        lastGamepadFamily_ = pad->family;
    }
}

} // namespace dwe::input
