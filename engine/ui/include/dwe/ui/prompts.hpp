#pragma once

#include <dwe/core/error.hpp>
#include <dwe/input/action_map.hpp>
#include <dwe/input/input_state.hpp>
#include <dwe/platform/events.hpp>

#include <functional>
#include <map>
#include <string>
#include <string_view>

namespace dwe::ui {

// The labels of the controls in button prompts. They are player-facing text, so they come from the game pack
// (ui/common/prompts.yaml) in the player's language, like the menus; the engine only names the controls.
class PromptLabels {
public:
    // Reads the labels: a mapping with the optional sections `keys`, `mouse` and `gamepad` (control name → label)
    // and `families` (gamepad family → button name → label, for the buttons each family labels its own way).
    static core::Result<PromptLabels> parse(std::string_view yaml);

    // The label of a control (see promptControl) as a family of gamepads labels it. Letters, digits and function
    // keys without a label show as themselves ("A", "7", "F5"); any other control without one shows its name.
    [[nodiscard]] std::string label(std::string_view control, platform::GamepadFamily family) const;

private:
    // "key:enter", "mouse:left", "gamepad:dpad_up", and "playstation:south" for a family's own labels.
    std::map<std::string, std::string, std::less<>> labels_;
};

// The control that triggers an action on a kind of device, for button prompts: the first key (or mouse button)
// bound to it on keyboard and mouse, or the first gamepad button (or stick) — "key:enter", "mouse:left",
// "gamepad:south", "gamepad:left_stick". Empty when nothing on that device triggers the action.
[[nodiscard]] std::string promptControl(const input::ActionMap& map, input::DeviceKind device, std::string_view action);

// The label of the control that triggers an action: promptControl, then PromptLabels::label. Empty when nothing on
// that device triggers the action. The labels are words, never a maker's symbols (ADR 0014).
[[nodiscard]] std::string promptLabel(const input::ActionMap& map, input::DeviceKind device,
                                      platform::GamepadFamily family, std::string_view action,
                                      const PromptLabels& labels);

// The control names of a key ("key:a", "key:f5", "key:enter") and of a gamepad button ("gamepad:south").
[[nodiscard]] std::string keyControl(platform::KeyCode key);
[[nodiscard]] std::string gamepadButtonControl(platform::GamepadButton button);

// The name of a family of gamepads in the labels and in the scripts: "xbox", "playstation", "nintendo", "steam",
// "generic".
[[nodiscard]] std::string_view gamepadFamilyName(platform::GamepadFamily family);

} // namespace dwe::ui
