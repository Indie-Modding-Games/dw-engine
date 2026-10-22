#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>

namespace dwe::platform {

// A key, identified by its position on the keyboard (its USB HID usage ID) rather than by the
// character it types, so bindings stay in the same place on every keyboard layout.
using KeyCode = std::uint16_t;

using GamepadId = std::uint32_t;

enum class MouseButton : std::uint8_t { Left, Middle, Right, Back, Forward };

constexpr std::size_t MouseButtonCount = 5;

// The maker's family of a gamepad. It decides which button prompts the game shows.
enum class GamepadFamily : std::uint8_t { Generic, Xbox, PlayStation, Nintendo, Steam };

// Buttons by position, not by label: South is A on Xbox, Cross on PlayStation and B on Nintendo.
enum class GamepadButton : std::uint8_t {
  South,
  East,
  West,
  North,
  Back,
  Guide,
  Start,
  LeftStick,
  RightStick,
  LeftShoulder,
  RightShoulder,
  DpadUp,
  DpadDown,
  DpadLeft,
  DpadRight,
};

constexpr std::size_t GamepadButtonCount = 15;

// Sticks go from -1 to 1 (down and right are positive); triggers go from 0 to 1.
enum class GamepadAxis : std::uint8_t { LeftX, LeftY, RightX, RightY, LeftTrigger, RightTrigger };

constexpr std::size_t GamepadAxisCount = 6;

struct QuitRequested {};

// The window stopped receiving input. Releases that happen while it is in the background never
// arrive, so whatever was held must be treated as released.
struct FocusLost {};

struct WindowResized {
  int width;
  int height;
};

struct KeyChanged {
  KeyCode key;
  bool down;
  bool repeat;
};

// The pointer's position over the window, in pixels of the drawable area (the same pixels as
// Window::pixelSize()), from the top left.
struct MouseMoved {
  float x;
  float y;
};

// The wheel turned: positive y is away from the player (scrolling up), positive x is to the right.
// In notches, which may be fractional on precise touchpads.
struct MouseWheelMoved {
  float x;
  float y;
};

struct MouseButtonChanged {
  MouseButton button;
  bool down;
};

struct GamepadConnected {
  GamepadId id;
  GamepadFamily family;
  std::string name;
};

struct GamepadDisconnected {
  GamepadId id;
};

struct GamepadButtonChanged {
  GamepadId id;
  GamepadButton button;
  bool down;
};

struct GamepadAxisMoved {
  GamepadId id;
  GamepadAxis axis;
  float value;
};

using Event = std::variant<QuitRequested, FocusLost, WindowResized, KeyChanged, MouseMoved,
                           MouseWheelMoved, MouseButtonChanged, GamepadConnected,
                           GamepadDisconnected, GamepadButtonChanged, GamepadAxisMoved>;

} // namespace dwe::platform
