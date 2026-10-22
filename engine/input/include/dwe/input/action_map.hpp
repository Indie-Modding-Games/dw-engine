#pragma once

#include <dwe/platform/events.hpp>

#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace dwe::input {

struct KeyBinding {
  platform::KeyCode key;
};

struct MouseBinding {
  platform::MouseButton button;
};

struct GamepadButtonBinding {
  platform::GamepadButton button;
};

// One half of a gamepad axis: direction is 1 for the positive half (right, down, a pulled trigger)
// and -1 for the negative half.
struct GamepadAxisBinding {
  platform::GamepadAxis axis;
  float direction;
};

// One physical control that drives an action.
using Binding = std::variant<KeyBinding, MouseBinding, GamepadButtonBinding, GamepadAxisBinding>;

// Which physical controls drive each action. The game only ever asks about actions ("confirm",
// "move_left"), never about keys or buttons, so every action works with every device and can be
// rebound.
class ActionMap {
public:
  void bind(std::string_view action, Binding binding);

  // Removes every binding of an action.
  void clear(std::string_view action);

  // The bindings of an action; empty if the action has none.
  [[nodiscard]] const std::vector<Binding>& bindings(std::string_view action) const;

  struct Entry {
    std::string action;
    std::vector<Binding> bindings;
  };

  [[nodiscard]] const std::vector<Entry>& entries() const { return entries_; }

private:
  std::vector<Entry> entries_;
};

// The bindings a new player starts with: keyboard and mouse, and the same layout on every gamepad.
ActionMap defaultActionMap();

} // namespace dwe::input
