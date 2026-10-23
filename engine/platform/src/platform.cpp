#include <dwe/core/assert.hpp>
#include <dwe/core/log.hpp>
#include <dwe/platform/platform.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <utility>

#include "messages.hpp"

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

// The inverse of buttonOf: a virtual gamepad has SDL's buttons in SDL's order.
int sdlButtonOf(GamepadButton button) {
  constexpr std::array<SDL_GamepadButton, GamepadButtonCount> Buttons{
      SDL_GAMEPAD_BUTTON_SOUTH,          SDL_GAMEPAD_BUTTON_EAST,
      SDL_GAMEPAD_BUTTON_WEST,           SDL_GAMEPAD_BUTTON_NORTH,
      SDL_GAMEPAD_BUTTON_BACK,           SDL_GAMEPAD_BUTTON_GUIDE,
      SDL_GAMEPAD_BUTTON_START,          SDL_GAMEPAD_BUTTON_LEFT_STICK,
      SDL_GAMEPAD_BUTTON_RIGHT_STICK,    SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,
      SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_DPAD_UP,
      SDL_GAMEPAD_BUTTON_DPAD_DOWN,      SDL_GAMEPAD_BUTTON_DPAD_LEFT,
      SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
  };
  return Buttons.at(static_cast<std::size_t>(button));
}

int sdlAxisOf(GamepadAxis axis) {
  constexpr std::array<SDL_GamepadAxis, GamepadAxisCount> Axes{
      SDL_GAMEPAD_AXIS_LEFTX,  SDL_GAMEPAD_AXIS_LEFTY,        SDL_GAMEPAD_AXIS_RIGHTX,
      SDL_GAMEPAD_AXIS_RIGHTY, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
  };
  return Axes.at(static_cast<std::size_t>(axis));
}

struct UsbIds {
  Uint16 vendor;
  Uint16 product;
};

UsbIds virtualGamepadIds(GamepadFamily family) {
  switch (family) {
  case GamepadFamily::Xbox:
    return {.vendor = 0x045E, .product = 0x0B12}; // Xbox Series controller
  case GamepadFamily::PlayStation:
    return {.vendor = 0x054C, .product = 0x0CE6}; // DualSense
  case GamepadFamily::Nintendo:
    return {.vendor = 0x057E, .product = 0x2009}; // Switch Pro Controller
  case GamepadFamily::Steam:
    return {.vendor = ValveVendorId, .product = 0x1205}; // Steam Deck
  case GamepadFamily::Generic:
    break;
  }
  return {.vendor = 0, .product = 0};
}

// How many drawable pixels one window coordinate covers in a window; 1 when the window is unknown.
float pixelDensity(SDL_WindowID id) {
  SDL_Window* window = SDL_GetWindowFromID(id);
  const float density = window != nullptr ? SDL_GetWindowPixelDensity(window) : 0.0F;
  return density > 0.0F ? density : 1.0F;
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
                SDL_VERSIONNUM_MINOR(version), SDL_VERSIONNUM_MICRO(version),
                driver != nullptr ? driver : "none");
  return platform;
}

Platform::Platform(Platform&& other) noexcept
    : active_{std::exchange(other.active_, false)}, gamepads_{std::move(other.gamepads_)},
      virtualGamepads_{std::move(other.virtualGamepads_)} {}

Platform& Platform::operator=(Platform&& other) noexcept {
  std::swap(active_, other.active_);
  std::swap(gamepads_, other.gamepads_);
  std::swap(virtualGamepads_, other.virtualGamepads_);
  return *this;
}

Platform::~Platform() {
  if (!active_) {
    return;
  }
  for (const auto& [id, gamepad] : gamepads_) {
    SDL_CloseGamepad(gamepad);
  }
  for (const auto& [id, joystick] : virtualGamepads_) {
    SDL_CloseJoystick(joystick);
    SDL_DetachVirtualJoystick(id);
  }
  SDL_Quit();
}

void Platform::waitForEvents(std::chrono::milliseconds timeout) const {
  DWE_ASSERT(active_);
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
      events.emplace_back(WindowResized{.width = event.window.data1, .height = event.window.data2});
      break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
      events.emplace_back(KeyChanged{.key = static_cast<KeyCode>(event.key.scancode),
                                     .down = event.key.down,
                                     .repeat = event.key.repeat});
      break;
    case SDL_EVENT_TEXT_INPUT:
      events.emplace_back(TextTyped{.text = event.text.text});
      break;
    case SDL_EVENT_MOUSE_MOTION: {
      // SDL reports the pointer in window coordinates, which are not pixels on high-density
      // displays.
      const float density = pixelDensity(event.motion.windowID);
      events.emplace_back(MouseMoved{.x = event.motion.x * density, .y = event.motion.y * density});
      break;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
      const float sign = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0F : 1.0F;
      events.emplace_back(MouseWheelMoved{.x = event.wheel.x * sign, .y = event.wheel.y * sign});
      break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
      if (auto button = mouseButtonOf(event.button.button)) {
        events.emplace_back(MouseButtonChanged{.button = *button, .down = event.button.down});
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
      events.emplace_back(GamepadConnected{.id = event.gdevice.which,
                                           .family = familyOf(gamepad),
                                           .name = name != nullptr ? name : "Gamepad"});
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
        events.emplace_back(GamepadButtonChanged{
            .id = event.gbutton.which, .button = *button, .down = event.gbutton.down});
      }
      break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
      if (auto axis = axisOf(event.gaxis.axis)) {
        events.emplace_back(GamepadAxisMoved{
            .id = event.gaxis.which, .axis = *axis, .value = normalizedAxis(event.gaxis.value)});
      }
      break;
    default:
      break;
    }
  }
}

void Platform::rumble(GamepadId gamepad, float low, float high,
                      std::chrono::milliseconds duration) {
  auto found = gamepads_.find(gamepad);
  if (found == gamepads_.end()) {
    return;
  }
  auto strength = [](float value) {
    return static_cast<Uint16>(std::clamp(value, 0.0F, 1.0F) *
                               static_cast<float>(std::numeric_limits<Uint16>::max()));
  };
  SDL_RumbleGamepad(found->second, strength(low), strength(high),
                    static_cast<Uint32>(duration.count()));
}

core::Result<GamepadId> Platform::attachVirtualGamepad(GamepadFamily family) {
  DWE_ASSERT(active_);
  SDL_VirtualJoystickDesc desc;
  SDL_INIT_INTERFACE(&desc);
  desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
  desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
  desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
  // SDL tells the families apart by USB ids, so the virtual gamepad borrows those of a common model
  // of each.
  const auto [vendor, product] = virtualGamepadIds(family);
  desc.vendor_id = vendor;
  desc.product_id = product;
  desc.name = "dwe virtual gamepad";
  const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
  if (id == 0) {
    return core::fail(std::format("could not attach a virtual gamepad: {}", SDL_GetError()));
  }
  SDL_Joystick* joystick = SDL_OpenJoystick(id);
  if (joystick == nullptr) {
    SDL_DetachVirtualJoystick(id);
    return core::fail(std::format("could not open the virtual gamepad: {}", SDL_GetError()));
  }
  virtualGamepads_[id] = joystick;
  return id;
}

void Platform::detachVirtualGamepad(GamepadId gamepad) {
  auto found = virtualGamepads_.find(gamepad);
  if (found == virtualGamepads_.end()) {
    return;
  }
  SDL_CloseJoystick(found->second);
  SDL_DetachVirtualJoystick(gamepad);
  virtualGamepads_.erase(found);
}

void Platform::setVirtualGamepadButton(GamepadId gamepad, GamepadButton button, bool down) {
  if (auto found = virtualGamepads_.find(gamepad); found != virtualGamepads_.end()) {
    SDL_SetJoystickVirtualButton(found->second, sdlButtonOf(button), down);
  }
}

void Platform::setVirtualGamepadAxis(GamepadId gamepad, GamepadAxis axis, float value) {
  if (auto found = virtualGamepads_.find(gamepad); found != virtualGamepads_.end()) {
    const float limited = std::clamp(value, -1.0F, 1.0F);
    SDL_SetJoystickVirtualAxis(
        found->second, sdlAxisOf(axis),
        static_cast<Sint16>(limited * static_cast<float>(SDL_JOYSTICK_AXIS_MAX)));
  }
}

double Platform::nowSeconds() const {
  DWE_ASSERT(active_);
  return static_cast<double>(SDL_GetTicksNS()) / static_cast<double>(SDL_NS_PER_SECOND);
}

core::Result<std::filesystem::path> userDataFolder(std::string_view organization,
                                                   std::string_view application) {
  char* folder =
      SDL_GetPrefPath(std::string{organization}.c_str(), std::string{application}.c_str());
  if (folder == nullptr) {
    return core::fail(std::format(messages::NoUserDataFolder, SDL_GetError()));
  }
  // SDL gives UTF-8, which a path only reads as such from char8_t (on Windows a char path is the
  // ANSI code page).
  const std::u8string text{reinterpret_cast<const char8_t*>(folder)};
  SDL_free(folder);
  return std::filesystem::path{text};
}

} // namespace dwe::platform
