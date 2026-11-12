#pragma once

#include <dwe/core/error.hpp>
#include <dwe/platform/events.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

union SDL_Event;
struct SDL_Gamepad;
struct SDL_Joystick;
struct SDL_Cursor;

namespace dwe::platform {

// Sees every event as SDL delivers it, before it is translated. Only for tools that need SDL's own
// events, such as the debug UI.
using RawEventObserver = std::function<void(const SDL_Event& event)>;

// The operating system services the engine needs: events, gamepads and time. There is one Platform
// for the whole program; it shuts SDL down when it is destroyed.
class Platform {
public:
  static core::Result<Platform> create();

  Platform(Platform&& other) noexcept;
  Platform& operator=(Platform&& other) noexcept;
  Platform(const Platform&) = delete;
  Platform& operator=(const Platform&) = delete;
  ~Platform();

  // Translates the pending events and appends them to events. Connects new gamepads as they appear.
  void pollEvents(std::vector<Event>& events, const RawEventObserver& observer = {});

  // Sleeps until an event arrives or the timeout passes, leaving the event to be polled. For idle
  // loops, such as while the window is minimized.
  void waitForEvents(std::chrono::milliseconds timeout) const;

  // Vibrates a gamepad; low and high are the strengths of the two motors, from 0 to 1.
  void rumble(GamepadId gamepad, float low, float high, std::chrono::milliseconds duration);

  // Installs an RGBA cursor of size (width, height), with the click point at (hotX, hotY) from the
  // top-left, as the window's pointer. `rgba` must be width*height*4 straight (not premultiplied)
  // bytes. The game uses this to let the current UI skin draw its own mouse cursor; the OS arrow
  // is replaced, so it stays on top of every document (modals included) without the UI stacking it.
  core::Result<void> setColorCursor(std::span<const std::byte> rgba, int width, int height,
                                    int hotX, int hotY);

  // Returns the pointer to the system default arrow, undoing setColorCursor.
  void clearColorCursor();

  // Re-asserts the skin's colour cursor on the current window (a no-op if none is installed). The
  // Windows backend can drop the cached cursor on a window event (focus, resize, fullscreen), and
  // re-issuing the stored one each frame is a cheap way to keep the skin's cursor visible through
  // those transitions.
  void reapplyColorCursor();

  // Seconds since the platform started, from a monotonic clock.
  [[nodiscard]] double nowSeconds() const;

  // Plugs in a software gamepad of the given family, for tests and scripted runs without a physical
  // one. It goes through the same path as a real gamepad: pollEvents reports it connected and
  // reports its buttons and axes.
  core::Result<GamepadId> attachVirtualGamepad(GamepadFamily family);

  // Unplugs a gamepad made by attachVirtualGamepad.
  void detachVirtualGamepad(GamepadId gamepad);

  // Presses or releases a button of a virtual gamepad.
  void setVirtualGamepadButton(GamepadId gamepad, GamepadButton button, bool down);

  // Moves an axis of a virtual gamepad: -1 to 1 for sticks, 0 to 1 for triggers.
  void setVirtualGamepadAxis(GamepadId gamepad, GamepadAxis axis, float value);

private:
  Platform() = default;

  bool active_ = false;
  std::unordered_map<GamepadId, SDL_Gamepad*> gamepads_;
  std::unordered_map<GamepadId, SDL_Joystick*> virtualGamepads_;
  // The colour cursor last installed by setColorCursor (owned here): freed on the next set and on
  // destruction. SDL_SetCursor(nullptr) restores the system arrow.
  SDL_Cursor* customCursor_ = nullptr;
};

// The folder where the program keeps the user's own files, such as saves, created if it does not
// exist: the system's place for application data ($XDG_DATA_HOME/<organization>/<application>/ on
// Linux, %APPDATA% on Windows). It does not need a Platform.
core::Result<std::filesystem::path> userDataFolder(std::string_view organization,
                                                   std::string_view application);

} // namespace dwe::platform
