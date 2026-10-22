#pragma once

#include <dwe/core/error.hpp>
#include <dwe/input/action_map.hpp>
#include <dwe/input/input_state.hpp>
#include <dwe/localization/catalog.hpp>
#include <dwe/platform/events.hpp>

#include <string>
#include <string_view>

namespace dwe::ui {

// The label of a control (see promptControl) as a family of gamepads labels it. Labels are
// player-facing text, so they come from the texts in the player's language
// (localization/<language>/prompt): "key:enter" is the key prompt.key.enter, and a family's own
// label of a gamepad button, such as "gamepad:south" on a PlayStation pad, is
// prompt.family.playstation.south, looked up first. Letters, digits and function keys without a
// label show as themselves ("A", "7", "F5"); any other control without one shows its name.
[[nodiscard]] std::string controlLabel(std::string_view control, platform::GamepadFamily family,
                                       const localization::Catalog& texts);

// The control that triggers an action on a kind of device, for button prompts: the first key (or
// mouse button) bound to it on keyboard and mouse, or the first gamepad button (or stick) —
// "key:enter", "mouse:left", "gamepad:south", "gamepad:left_stick". Empty when nothing on that
// device triggers the action.
[[nodiscard]] std::string promptControl(const input::ActionMap& map, input::DeviceKind device,
                                        std::string_view action);

// The label of the control that triggers an action: promptControl, then controlLabel. Empty when
// nothing on that device triggers the action. The labels are words, never a maker's symbols (ADR
// 0014).
[[nodiscard]] std::string promptLabel(const input::ActionMap& map, input::DeviceKind device,
                                      platform::GamepadFamily family, std::string_view action,
                                      const localization::Catalog& texts);

// The control names of a key ("key:a", "key:f5", "key:enter") and of a gamepad button
// ("gamepad:south").
[[nodiscard]] std::string keyControl(platform::KeyCode key);
[[nodiscard]] std::string gamepadButtonControl(platform::GamepadButton button);

// The name of a family of gamepads in the labels and in the scripts: "xbox", "playstation",
// "nintendo", "steam", "generic".
[[nodiscard]] std::string_view gamepadFamilyName(platform::GamepadFamily family);

} // namespace dwe::ui
