#pragma once

#include <dwe/input/action_map.hpp>
#include <dwe/platform/events.hpp>

#include <chrono>
#include <string_view>

namespace dwe::app {

// The logic runs at a fixed 60 ticks per second; rendering interpolates between ticks.
inline constexpr double TickSeconds = 1.0 / 60.0;
// A longer frame (a stall, a breakpoint) runs at most this much logic, so the game slows down instead of spiralling.
inline constexpr double MaxFrameSeconds = 0.25;
// The UI settings the game publishes for the HUD on a map change: how much the fade takes away (0 to 1), and the
// name of the map it goes to while the original shows it (empty otherwise).
inline constexpr std::string_view ScreenFadeSetting = "screen_fade";
inline constexpr std::string_view MapTitleSetting = "map_title";
// How long to sleep per loop while there is nothing to draw, about one display refresh.
inline constexpr std::chrono::milliseconds IdleWait{16};

#ifdef NDEBUG
inline constexpr bool Validation = false;
#else
inline constexpr bool Validation = true;
#endif

// How a run's log names the validation layer: on, unavailable (requested but not installed) or off (not requested).
constexpr const char* validationState(bool enabled) {
    if (enabled) {
        return "on";
    }
    return Validation ? "unavailable" : "off";
}

// Players may drop an SDL mapping file next to the game to support controllers SDL does not know yet.
void loadUserGamepadMappings();

// Whether key is bound to opening and closing the console, so it must reach the game even while the console has
// the keyboard.
bool togglesConsole(const input::ActionMap& map, platform::KeyCode key);

// Frames per second over the last half second, for the debug overlay.
struct FpsCounter {
    double windowStart = 0.0;
    int frames = 0;
    double framesPerSecond = 0.0;

    void frame(double now);
};

} // namespace dwe::app
