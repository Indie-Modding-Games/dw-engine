#pragma once

#include <dwe/platform/events.hpp>

#include <optional>
#include <string_view>

namespace dwe::app {

// The names scripted runs (the UI demo's gamepad file, autotest routes) use for controls.

// south, east, west, north, back, guide, start, left_stick, right_stick, left_shoulder,
// right_shoulder, dpad_up, dpad_down, dpad_left, dpad_right.
std::optional<platform::GamepadButton> gamepadButtonNamed(std::string_view name);

// left_x, left_y, right_x, right_y, left_trigger, right_trigger.
std::optional<platform::GamepadAxis> gamepadAxisNamed(std::string_view name);

// xbox, playstation, nintendo, steam, generic.
std::optional<platform::GamepadFamily> gamepadFamilyNamed(std::string_view name);

// Keys by their name on a US layout: a to z, 0 to 9, return, escape, backspace, tab, space, left,
// right, up, down, left_shift, right_shift.
std::optional<platform::KeyCode> keyNamed(std::string_view name);

} // namespace dwe::app
