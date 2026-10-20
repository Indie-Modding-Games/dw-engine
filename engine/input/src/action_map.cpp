#include <dwe/input/action_map.hpp>
#include <dwe/input/keys.hpp>

#include <algorithm>
#include <iterator>

namespace dwe::input {

using platform::GamepadAxis;
using platform::GamepadButton;
using platform::MouseButton;

void ActionMap::bind(std::string_view action, Binding binding) {
    auto entry = std::ranges::find(entries_, action, &Entry::action);
    if (entry == entries_.end()) {
        entries_.push_back(Entry{.action = std::string{action}, .bindings = {}});
        entry = std::prev(entries_.end());
    }
    entry->bindings.push_back(binding);
}

void ActionMap::clear(std::string_view action) {
    std::erase_if(entries_, [action](const Entry& entry) { return entry.action == action; });
}

const std::vector<Binding>& ActionMap::bindings(std::string_view action) const {
    static const std::vector<Binding> none;
    auto entry = std::ranges::find(entries_, action, &Entry::action);
    return entry == entries_.end() ? none : entry->bindings;
}

ActionMap defaultActionMap() {
    ActionMap map;

    map.bind("confirm", KeyBinding{keys::Return});
    map.bind("confirm", KeyBinding{keys::Space});
    map.bind("confirm", MouseBinding{MouseButton::Left});
    map.bind("confirm", GamepadButtonBinding{GamepadButton::South});

    map.bind("cancel", KeyBinding{keys::Escape});
    map.bind("cancel", KeyBinding{keys::Backspace});
    map.bind("cancel", MouseBinding{MouseButton::Right});
    map.bind("cancel", GamepadButtonBinding{GamepadButton::East});

    map.bind("menu", KeyBinding{keys::Tab});
    map.bind("menu", GamepadButtonBinding{GamepadButton::Start});

    map.bind("move_left", KeyBinding{keys::A});
    map.bind("move_left", KeyBinding{keys::Left});
    map.bind("move_left", GamepadButtonBinding{GamepadButton::DpadLeft});
    map.bind("move_left", GamepadAxisBinding{.axis = GamepadAxis::LeftX, .direction = -1.0F});

    map.bind("move_right", KeyBinding{keys::D});
    map.bind("move_right", KeyBinding{keys::Right});
    map.bind("move_right", GamepadButtonBinding{GamepadButton::DpadRight});
    map.bind("move_right", GamepadAxisBinding{.axis = GamepadAxis::LeftX, .direction = 1.0F});

    map.bind("move_up", KeyBinding{keys::W});
    map.bind("move_up", KeyBinding{keys::Up});
    map.bind("move_up", GamepadButtonBinding{GamepadButton::DpadUp});
    map.bind("move_up", GamepadAxisBinding{.axis = GamepadAxis::LeftY, .direction = -1.0F});

    map.bind("move_down", KeyBinding{keys::S});
    map.bind("move_down", KeyBinding{keys::Down});
    map.bind("move_down", GamepadButtonBinding{GamepadButton::DpadDown});
    map.bind("move_down", GamepadAxisBinding{.axis = GamepadAxis::LeftY, .direction = 1.0F});

    map.bind("debug_console", KeyBinding{keys::Grave});
    map.bind("debug_console", KeyBinding{keys::F1});

    // Development views (dwe-game --map): fade to a time of day, show the next map.
    map.bind("debug_time_day", KeyBinding{keys::Digit1});
    map.bind("debug_time_dusk", KeyBinding{keys::Digit2});
    map.bind("debug_time_night", KeyBinding{keys::Digit3});
    map.bind("debug_next_map", KeyBinding{keys::N});

    return map;
}

} // namespace dwe::input
