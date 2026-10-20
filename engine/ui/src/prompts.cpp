#include <dwe/ui/prompts.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <string_view>
#include <variant>

namespace dwe::ui {
namespace {

using platform::GamepadAxis;
using platform::GamepadButton;
using platform::GamepadFamily;
using platform::KeyCode;
using platform::MouseButton;

// USB HID usage ids of the keys whose names are not a single printed character.
struct NamedKey {
    KeyCode key;
    std::string_view name;
};

constexpr std::array NamedKeys{
    NamedKey{.key = 40, .name = "enter"},
    NamedKey{.key = 41, .name = "escape"},
    NamedKey{.key = 42, .name = "backspace"},
    NamedKey{.key = 43, .name = "tab"},
    NamedKey{.key = 44, .name = "space"},
    NamedKey{.key = 45, .name = "minus"},
    NamedKey{.key = 46, .name = "equals"},
    NamedKey{.key = 47, .name = "left_bracket"},
    NamedKey{.key = 48, .name = "right_bracket"},
    NamedKey{.key = 49, .name = "backslash"},
    NamedKey{.key = 51, .name = "semicolon"},
    NamedKey{.key = 52, .name = "apostrophe"},
    NamedKey{.key = 53, .name = "grave"},
    NamedKey{.key = 54, .name = "comma"},
    NamedKey{.key = 55, .name = "period"},
    NamedKey{.key = 56, .name = "slash"},
    NamedKey{.key = 57, .name = "caps_lock"},
    NamedKey{.key = 73, .name = "insert"},
    NamedKey{.key = 74, .name = "home"},
    NamedKey{.key = 75, .name = "page_up"},
    NamedKey{.key = 76, .name = "delete"},
    NamedKey{.key = 77, .name = "end"},
    NamedKey{.key = 78, .name = "page_down"},
    NamedKey{.key = 79, .name = "right"},
    NamedKey{.key = 80, .name = "left"},
    NamedKey{.key = 81, .name = "down"},
    NamedKey{.key = 82, .name = "up"},
    NamedKey{.key = 88, .name = "keypad_enter"},
    NamedKey{.key = 224, .name = "left_ctrl"},
    NamedKey{.key = 225, .name = "left_shift"},
    NamedKey{.key = 226, .name = "left_alt"},
    NamedKey{.key = 228, .name = "right_ctrl"},
    NamedKey{.key = 229, .name = "right_shift"},
    NamedKey{.key = 230, .name = "right_alt"},
};

// The HID ranges of letters, digits and function keys.
constexpr KeyCode FirstLetter = 4;
constexpr KeyCode LastLetter = 29;
constexpr KeyCode FirstDigit = 30; // 1; 0 comes after 9
constexpr KeyCode LastDigit = 39;
constexpr KeyCode FirstFunction = 58; // F1 to F12
constexpr KeyCode LastFunction = 69;

constexpr std::string_view KeyPrefix = "key:";
constexpr std::string_view GamepadPrefix = "gamepad:";

// Where the labels are among the texts (localization/<language>/prompt).
constexpr std::string_view LabelKeyPrefix = "prompt.";
constexpr std::string_view FamilyKeyPrefix = "prompt.family.";
constexpr char ControlSeparator = ':';
constexpr char KeySeparator = '.';

std::string_view mouseButtonName(MouseButton button) {
    switch (button) {
    case MouseButton::Left:
        return "left";
    case MouseButton::Middle:
        return "middle";
    case MouseButton::Right:
        return "right";
    case MouseButton::Back:
        return "back";
    case MouseButton::Forward:
        return "forward";
    }
    return "left";
}

std::string_view gamepadButtonName(GamepadButton button) {
    switch (button) {
    case GamepadButton::South:
        return "south";
    case GamepadButton::East:
        return "east";
    case GamepadButton::West:
        return "west";
    case GamepadButton::North:
        return "north";
    case GamepadButton::Start:
        return "start";
    case GamepadButton::Back:
        return "back";
    case GamepadButton::Guide:
        return "guide";
    case GamepadButton::LeftStick:
        return "left_stick_click";
    case GamepadButton::RightStick:
        return "right_stick_click";
    case GamepadButton::LeftShoulder:
        return "left_shoulder";
    case GamepadButton::RightShoulder:
        return "right_shoulder";
    case GamepadButton::DpadUp:
        return "dpad_up";
    case GamepadButton::DpadDown:
        return "dpad_down";
    case GamepadButton::DpadLeft:
        return "dpad_left";
    case GamepadButton::DpadRight:
        return "dpad_right";
    }
    return "south";
}

std::string_view axisName(GamepadAxis axis) {
    switch (axis) {
    case GamepadAxis::LeftX:
    case GamepadAxis::LeftY:
        return "left_stick";
    case GamepadAxis::RightX:
    case GamepadAxis::RightY:
        return "right_stick";
    case GamepadAxis::LeftTrigger:
        return "left_trigger";
    case GamepadAxis::RightTrigger:
        return "right_trigger";
    }
    return "left_stick";
}

// A key that shows as its own name in capitals when the labels do not name it: a letter, a digit or F1 to F12.
bool printsItself(std::string_view key) {
    if (key.size() == 1) {
        return std::isalnum(static_cast<unsigned char>(key.front())) != 0;
    }
    return key.size() <= 3 && key.front() == 'f' &&
           std::ranges::all_of(key.substr(1), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
}

std::string capitals(std::string_view text) {
    std::string upper{text};
    for (char& c : upper) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return upper;
}

// The key of a control's label: "key:enter" is prompt.key.enter.
std::string labelKey(std::string_view control) {
    std::string key = std::format("{}{}", LabelKeyPrefix, control);
    std::ranges::replace(key, ControlSeparator, KeySeparator);
    return key;
}

} // namespace

std::string controlLabel(std::string_view control, GamepadFamily family, const localization::Catalog& texts) {
    if (control.starts_with(GamepadPrefix)) {
        const std::string_view button = control.substr(GamepadPrefix.size());
        if (auto found =
                texts.find(std::format("{}{}{}{}", FamilyKeyPrefix, gamepadFamilyName(family), KeySeparator, button))) {
            return localization::formatText(*found, {});
        }
    }
    if (auto found = texts.find(labelKey(control))) {
        return localization::formatText(*found, {});
    }
    if (control.starts_with(KeyPrefix) && printsItself(control.substr(KeyPrefix.size()))) {
        return capitals(control.substr(KeyPrefix.size()));
    }
    return std::string{control};
}

std::string keyControl(KeyCode key) {
    if (key >= FirstLetter && key <= LastLetter) {
        return std::format("{}{}", KeyPrefix, static_cast<char>('a' + (key - FirstLetter)));
    }
    if (key >= FirstDigit && key <= LastDigit) {
        return std::format("{}{}", KeyPrefix, key == LastDigit ? 0 : key - FirstDigit + 1);
    }
    if (key >= FirstFunction && key <= LastFunction) {
        return std::format("{}f{}", KeyPrefix, key - FirstFunction + 1);
    }
    for (const NamedKey& named : NamedKeys) {
        if (named.key == key) {
            return std::format("{}{}", KeyPrefix, named.name);
        }
    }
    return std::format("{}{}", KeyPrefix, key);
}

std::string gamepadButtonControl(GamepadButton button) {
    return std::format("{}{}", GamepadPrefix, gamepadButtonName(button));
}

std::string_view gamepadFamilyName(GamepadFamily family) {
    switch (family) {
    case GamepadFamily::Xbox:
        return "xbox";
    case GamepadFamily::PlayStation:
        return "playstation";
    case GamepadFamily::Nintendo:
        return "nintendo";
    case GamepadFamily::Steam:
        return "steam";
    case GamepadFamily::Generic:
        break;
    }
    return "generic";
}

std::string promptControl(const input::ActionMap& map, input::DeviceKind device, std::string_view action) {
    std::string fallback;
    for (const input::Binding& binding : map.bindings(action)) {
        if (device == input::DeviceKind::KeyboardMouse) {
            if (const auto* key = std::get_if<input::KeyBinding>(&binding)) {
                return keyControl(key->key);
            }
            if (const auto* mouse = std::get_if<input::MouseBinding>(&binding); mouse != nullptr && fallback.empty()) {
                fallback = std::format("mouse:{}", mouseButtonName(mouse->button));
            }
        } else {
            if (const auto* button = std::get_if<input::GamepadButtonBinding>(&binding)) {
                return gamepadButtonControl(button->button);
            }
            if (const auto* axis = std::get_if<input::GamepadAxisBinding>(&binding);
                axis != nullptr && fallback.empty()) {
                fallback = std::format("{}{}", GamepadPrefix, axisName(axis->axis));
            }
        }
    }
    return fallback;
}

std::string promptLabel(const input::ActionMap& map, input::DeviceKind device, GamepadFamily family,
                        std::string_view action, const localization::Catalog& texts) {
    const std::string control = promptControl(map, device, action);
    return control.empty() ? std::string{} : controlLabel(control, family, texts);
}

} // namespace dwe::ui
