#include "device_names.hpp"

#include <dwe/input/keys.hpp>

#include <array>
#include <utility>

namespace dwe::app {

std::optional<platform::GamepadButton> gamepadButtonNamed(std::string_view name) {
  using enum platform::GamepadButton;
  constexpr std::array<std::pair<std::string_view, platform::GamepadButton>, 15> Names{{
      {"south", South},
      {"east", East},
      {"west", West},
      {"north", North},
      {"back", Back},
      {"guide", Guide},
      {"start", Start},
      {"left_stick", LeftStick},
      {"right_stick", RightStick},
      {"left_shoulder", LeftShoulder},
      {"right_shoulder", RightShoulder},
      {"dpad_up", DpadUp},
      {"dpad_down", DpadDown},
      {"dpad_left", DpadLeft},
      {"dpad_right", DpadRight},
  }};
  for (const auto& [known, button] : Names) {
    if (known == name) {
      return button;
    }
  }
  return std::nullopt;
}

std::optional<platform::GamepadAxis> gamepadAxisNamed(std::string_view name) {
  using enum platform::GamepadAxis;
  constexpr std::array<std::pair<std::string_view, platform::GamepadAxis>, 6> Names{{
      {"left_x", LeftX},
      {"left_y", LeftY},
      {"right_x", RightX},
      {"right_y", RightY},
      {"left_trigger", LeftTrigger},
      {"right_trigger", RightTrigger},
  }};
  for (const auto& [known, axis] : Names) {
    if (known == name) {
      return axis;
    }
  }
  return std::nullopt;
}

std::optional<platform::GamepadFamily> gamepadFamilyNamed(std::string_view name) {
  using enum platform::GamepadFamily;
  constexpr std::array<std::pair<std::string_view, platform::GamepadFamily>, 5> Names{{
      {"xbox", Xbox},
      {"playstation", PlayStation},
      {"nintendo", Nintendo},
      {"steam", Steam},
      {"generic", Generic},
  }};
  for (const auto& [known, family] : Names) {
    if (known == name) {
      return family;
    }
  }
  return std::nullopt;
}

std::optional<platform::KeyCode> keyNamed(std::string_view name) {
  // USB HID usage IDs: letters from 4, digits 1-9 from 30 and 0 at 39.
  constexpr platform::KeyCode FirstLetter = 4;
  constexpr platform::KeyCode FirstDigit = 30;
  constexpr platform::KeyCode Zero = 39;
  if (name.size() == 1 && name[0] >= 'a' && name[0] <= 'z') {
    return static_cast<platform::KeyCode>(FirstLetter + (name[0] - 'a'));
  }
  if (name.size() == 1 && name[0] >= '1' && name[0] <= '9') {
    return static_cast<platform::KeyCode>(FirstDigit + (name[0] - '1'));
  }
  if (name == "0") {
    return Zero;
  }
  namespace keys = input::keys;
  constexpr std::array<std::pair<std::string_view, platform::KeyCode>, 19> Names{{
      {"return", keys::Return},
      {"escape", keys::Escape},
      {"backspace", keys::Backspace},
      {"tab", keys::Tab},
      {"space", keys::Space},
      {"left", keys::Left},
      {"right", keys::Right},
      {"up", keys::Up},
      {"down", keys::Down},
      {"left_ctrl", keys::LeftCtrl},
      {"right_ctrl", keys::RightCtrl},
      {"left_shift", keys::LeftShift},
      {"right_shift", keys::RightShift},
      {"grave", keys::Grave},
      {"f1", keys::F1},
      {"f10", keys::F10},
      {"f12", keys::F12},
      {"delete", keys::Delete},
      {"home", keys::Home},
  }};
  for (const auto& [known, key] : Names) {
    if (known == name) {
      return key;
    }
  }
  return std::nullopt;
}

} // namespace dwe::app
