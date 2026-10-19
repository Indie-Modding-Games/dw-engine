#pragma once

#include <dwe/core/error.hpp>
#include <dwe/platform/events.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

union SDL_Event;
struct SDL_Gamepad;

namespace dwe::platform {

// Sees every event as SDL delivers it, before it is translated. Only for tools that need SDL's own events, such as
// the debug UI.
using RawEventObserver = std::function<void(const SDL_Event& event)>;

// The operating system services the engine needs: events, gamepads and time. There is one Platform for the whole
// program; it shuts SDL down when it is destroyed.
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

    // Sleeps until an event arrives or the timeout passes, leaving the event to be polled. For idle loops, such as
    // while the window is minimized.
    void waitForEvents(std::chrono::milliseconds timeout) const;

    // Vibrates a gamepad; low and high are the strengths of the two motors, from 0 to 1.
    void rumble(GamepadId gamepad, float low, float high, std::chrono::milliseconds duration);

    // Seconds since the platform started, from a monotonic clock.
    [[nodiscard]] double nowSeconds() const;

private:
    Platform() = default;

    bool active_ = false;
    std::unordered_map<GamepadId, SDL_Gamepad*> gamepads_;
};

} // namespace dwe::platform
