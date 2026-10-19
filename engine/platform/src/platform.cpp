#include <dwe/core/log.hpp>
#include <dwe/platform/platform.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <format>
#include <limits>
#include <optional>
#include <utility>

namespace dwe::platform {
namespace {

// Valve's USB vendor ID, shared by the Steam Deck and the Steam Controller.
constexpr Uint16 ValveVendorId = 0x28DE;

GamepadFamily familyOf(SDL_Gamepad* gamepad) {
    if (SDL_GetGamepadVendor(gamepad) == ValveVendorId) {
        return GamepadFamily::Steam;
    }
    switch (SDL_GetGamepadType(gamepad)) {
    case SDL_GAMEPAD_TYPE_XBOX360:
    case SDL_GAMEPAD_TYPE_XBOXONE:
        return GamepadFamily::Xbox;
    case SDL_GAMEPAD_TYPE_PS3:
    case SDL_GAMEPAD_TYPE_PS4:
    case SDL_GAMEPAD_TYPE_PS5:
        return GamepadFamily::PlayStation;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
        return GamepadFamily::Nintendo;
    default:
        return GamepadFamily::Generic;
    }
}

std::optional<GamepadButton> buttonOf(Uint8 button) {
    switch (button) {
    case SDL_GAMEPAD_BUTTON_SOUTH:
        return GamepadButton::South;
    case SDL_GAMEPAD_BUTTON_EAST:
        return GamepadButton::East;
    case SDL_GAMEPAD_BUTTON_WEST:
        return GamepadButton::West;
    case SDL_GAMEPAD_BUTTON_NORTH:
        return GamepadButton::North;
    case SDL_GAMEPAD_BUTTON_BACK:
        return GamepadButton::Back;
    case SDL_GAMEPAD_BUTTON_GUIDE:
        return GamepadButton::Guide;
    case SDL_GAMEPAD_BUTTON_START:
        return GamepadButton::Start;
    case SDL_GAMEPAD_BUTTON_LEFT_STICK:
        return GamepadButton::LeftStick;
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
        return GamepadButton::RightStick;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
        return GamepadButton::LeftShoulder;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
        return GamepadButton::RightShoulder;
    case SDL_GAMEPAD_BUTTON_DPAD_UP:
        return GamepadButton::DpadUp;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
        return GamepadButton::DpadDown;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
        return GamepadButton::DpadLeft;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
        return GamepadButton::DpadRight;
    default:
        return std::nullopt;
    }
}

std::optional<GamepadAxis> axisOf(Uint8 axis) {
    switch (axis) {
    case SDL_GAMEPAD_AXIS_LEFTX:
        return GamepadAxis::LeftX;
    case SDL_GAMEPAD_AXIS_LEFTY:
        return GamepadAxis::LeftY;
    case SDL_GAMEPAD_AXIS_RIGHTX:
        return GamepadAxis::RightX;
    case SDL_GAMEPAD_AXIS_RIGHTY:
        return GamepadAxis::RightY;
    case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
        return GamepadAxis::LeftTrigger;
    case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
        return GamepadAxis::RightTrigger;
    default:
        return std::nullopt;
    }
}

std::optional<MouseButton> mouseButtonOf(Uint8 button) {
    switch (button) {
    case SDL_BUTTON_LEFT:
        return MouseButton::Left;
    case SDL_BUTTON_MIDDLE:
        return MouseButton::Middle;
    case SDL_BUTTON_RIGHT:
        return MouseButton::Right;
    case SDL_BUTTON_X1:
        return MouseButton::Back;
    case SDL_BUTTON_X2:
        return MouseButton::Forward;
    default:
        return std::nullopt;
    }
}

// SDL reports axes as 16-bit integers; the engine uses -1 to 1 for sticks and 0 to 1 for triggers.
float normalizedAxis(Sint16 value) {
    return std::max(static_cast<float>(value) / static_cast<float>(SDL_JOYSTICK_AXIS_MAX), -1.0F);
}

} // namespace

core::Result<Platform> Platform::create() {
    SDL_SetAppMetadata("dwe", "0.1.0", "dwe");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS)) {
        return core::fail(std::format("could not start SDL: {}", SDL_GetError()));
    }
    Platform platform;
    platform.active_ = true;
    const int version = SDL_GetVersion();
    const char* driver = SDL_GetCurrentVideoDriver();
    core::logInfo("SDL {}.{}.{} started (video driver: {})", SDL_VERSIONNUM_MAJOR(version),
                  SDL_VERSIONNUM_MINOR(version), SDL_VERSIONNUM_MICRO(version), driver != nullptr ? driver : "none");
    return platform;
}

Platform::Platform(Platform&& other) noexcept
    : active_{std::exchange(other.active_, false)}, gamepads_{std::move(other.gamepads_)} {}

Platform& Platform::operator=(Platform&& other) noexcept {
    std::swap(active_, other.active_);
    std::swap(gamepads_, other.gamepads_);
    return *this;
}

Platform::~Platform() {
    if (!active_) {
        return;
    }
    for (const auto& [id, gamepad] : gamepads_) {
        SDL_CloseGamepad(gamepad);
    }
    SDL_Quit();
}

void Platform::waitForEvents(std::chrono::milliseconds timeout) {
    SDL_WaitEventTimeout(nullptr, static_cast<Sint32>(timeout.count()));
}

void Platform::pollEvents(std::vector<Event>& events, const RawEventObserver& observer) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (observer) {
            observer(event);
        }
        switch (event.type) {
        case SDL_EVENT_QUIT:
            events.emplace_back(QuitRequested{});
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            events.emplace_back(FocusLost{});
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            events.emplace_back(WindowResized{event.window.data1, event.window.data2});
            break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            events.emplace_back(KeyChanged{static_cast<KeyCode>(event.key.scancode), event.key.down, event.key.repeat});
            break;
        case SDL_EVENT_MOUSE_MOTION:
            events.emplace_back(MouseMoved{event.motion.x, event.motion.y});
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (auto button = mouseButtonOf(event.button.button)) {
                events.emplace_back(MouseButtonChanged{*button, event.button.down});
            }
            break;
        case SDL_EVENT_GAMEPAD_ADDED: {
            if (gamepads_.contains(event.gdevice.which)) {
                break;
            }
            SDL_Gamepad* gamepad = SDL_OpenGamepad(event.gdevice.which);
            if (gamepad == nullptr) {
                core::logWarning("could not open gamepad {}: {}", event.gdevice.which, SDL_GetError());
                break;
            }
            const char* name = SDL_GetGamepadName(gamepad);
            gamepads_[event.gdevice.which] = gamepad;
            events.emplace_back(GamepadConnected{event.gdevice.which, familyOf(gamepad), name ? name : "Gamepad"});
            break;
        }
        case SDL_EVENT_GAMEPAD_REMOVED:
            if (auto found = gamepads_.find(event.gdevice.which); found != gamepads_.end()) {
                SDL_CloseGamepad(found->second);
                gamepads_.erase(found);
                events.emplace_back(GamepadDisconnected{event.gdevice.which});
            }
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
            if (auto button = buttonOf(event.gbutton.button)) {
                events.emplace_back(GamepadButtonChanged{event.gbutton.which, *button, event.gbutton.down});
            }
            break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            if (auto axis = axisOf(event.gaxis.axis)) {
                events.emplace_back(GamepadAxisMoved{event.gaxis.which, *axis, normalizedAxis(event.gaxis.value)});
            }
            break;
        default:
            break;
        }
    }
}

void Platform::rumble(GamepadId gamepad, float low, float high, std::chrono::milliseconds duration) {
    auto found = gamepads_.find(gamepad);
    if (found == gamepads_.end()) {
        return;
    }
    auto strength = [](float value) {
        return static_cast<Uint16>(std::clamp(value, 0.0F, 1.0F) *
                                   static_cast<float>(std::numeric_limits<Uint16>::max()));
    };
    SDL_RumbleGamepad(found->second, strength(low), strength(high), static_cast<Uint32>(duration.count()));
}

double Platform::nowSeconds() const {
    return static_cast<double>(SDL_GetTicksNS()) / static_cast<double>(SDL_NS_PER_SECOND);
}

} // namespace dwe::platform
