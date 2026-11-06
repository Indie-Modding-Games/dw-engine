#pragma once

#include <dwe/input/action_map.hpp>
#include <dwe/platform/events.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace dwe::input {

// The kind of device the player used last. It decides which button prompts the game shows.
enum class DeviceKind : std::uint8_t { KeyboardMouse, Gamepad };

struct ConnectedGamepad {
  platform::GamepadId id;
  platform::GamepadFamily family;
  std::string name;
};

struct Vector2 {
  float x;
  float y;
};

// What the player is doing, in actions. It is fed with the platform's events and read by the game
// once per logic tick. Every connected gamepad drives the same actions.
class InputState {
public:
  // deadzone is the part of each stick half, from the center, that is ignored.
  explicit InputState(ActionMap map, float deadzone = 0.2F);

  void handle(const platform::Event& event);

  // Closes a logic tick: what was pressed or released during it stops being reported.
  void endTick();

  // How strongly an action is held, from 0 to 1. Buttons and keys are 0 or 1; sticks and triggers
  // are analog.
  [[nodiscard]] float value(std::string_view action) const;

  [[nodiscard]] bool isDown(std::string_view action) const;

  // True during the tick in which the action went down, even if it was released before the tick
  // ran.
  [[nodiscard]] bool wasPressed(std::string_view action) const;

  // True during the tick in which the action went down from a mouse button (a click).
  [[nodiscard]] bool wasClicked(std::string_view action) const;

  // True during the tick in which the action went down from a key or a gamepad's control, not a
  // mouse button: a confirm that means the cursor's entry, whatever the pointer is on.
  [[nodiscard]] bool wasPressedByKeyOrPad(std::string_view action) const;

  // Where the pointer is in the window, in pixels as MouseMoved gives them; nothing until the
  // platform reported it.
  [[nodiscard]] std::optional<Vector2> pointer() const;

  // True during the tick in which the player moved the pointer: a report of where it already was
  // (a window that gains the focus reports it) does not count.
  [[nodiscard]] bool pointerMoved() const { return pointerMoved_; }

  // Keeps where the pointer is from a move the input does not handle (one the UI keeps for
  // itself), so a click that follows lands where the pointer is. It is not a move of the player's.
  void notePointer(const platform::MouseMoved& moved);

  // A direction from four actions, with a length of at most 1.
  [[nodiscard]] Vector2 vector(std::string_view left, std::string_view right, std::string_view up,
                               std::string_view down) const;

  [[nodiscard]] DeviceKind lastDevice() const { return lastDevice_; }

  // The family of the gamepad used last; meaningful when lastDevice() is Gamepad.
  [[nodiscard]] platform::GamepadFamily lastGamepadFamily() const { return lastGamepadFamily_; }

  [[nodiscard]] const std::vector<ConnectedGamepad>& gamepads() const { return gamepads_; }

  // True during the tick in which a gamepad was disconnected, so the game can pause.
  [[nodiscard]] bool gamepadLost() const { return gamepadLost_; }

  [[nodiscard]] ActionMap& actionMap() { return map_; }
  [[nodiscard]] const ActionMap& actionMap() const { return map_; }

private:
  struct GamepadState {
    platform::GamepadId id{};
    std::array<bool, platform::GamepadButtonCount> buttons{};
    std::array<float, platform::GamepadAxisCount> axes{};
  };

  [[nodiscard]] float bindingValue(const Binding& binding) const;
  void notePressed(const Binding& changed, bool byMouse);
  GamepadState* findGamepad(platform::GamepadId id);
  void useGamepad(platform::GamepadId id);

  ActionMap map_;
  float deadzone_;

  std::unordered_set<platform::KeyCode> keysDown_;
  std::array<bool, platform::MouseButtonCount> mouseButtons_{};
  std::vector<GamepadState> gamepadStates_;
  std::vector<ConnectedGamepad> gamepads_;

  std::unordered_set<std::string> pressedThisTick_;
  // The actions of pressedThisTick_ that a mouse button pressed, and those a key or a gamepad did.
  std::unordered_set<std::string> clickedThisTick_;
  std::unordered_set<std::string> keyOrPadThisTick_;
  bool pointerMoved_ = false;
  DeviceKind lastDevice_ = DeviceKind::KeyboardMouse;
  // Where the pointer was last reported, to tell a real move from a report of where it already was.
  std::optional<platform::MouseMoved> lastPointer_;
  platform::GamepadFamily lastGamepadFamily_ = platform::GamepadFamily::Generic;
  bool gamepadLost_ = false;
};

} // namespace dwe::input
