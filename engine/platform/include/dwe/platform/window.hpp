#pragma once

#include <dwe/core/error.hpp>

#include <optional>
#include <string>

struct SDL_Window;

namespace dwe::platform {

struct WindowDesc {
  std::string title;
  int width = 1280;
  int height = 720;
};

struct PixelSize {
  int width;
  int height;
};

// A resizable window that the renderer draws into with Vulkan.
class Window {
public:
  static core::Result<Window> create(const WindowDesc& desc);

  Window(Window&& other) noexcept;
  Window& operator=(Window&& other) noexcept;
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;
  ~Window();

  // The size of the drawable area in pixels, which differs from the window size on high-density
  // displays.
  [[nodiscard]] PixelSize pixelSize() const;

  // The size of the window in window units, the units setSize takes.
  [[nodiscard]] PixelSize windowSize() const;

  // Asks for a new size of the window (not fullscreen), in window units. The window manager may
  // change or ignore it; the size it gets arrives as a WindowResized event.
  core::Result<void> setSize(int width, int height);

  // The part of the window's display that windows may use (without panels and docks), in window
  // units; nothing if the system does not say.
  [[nodiscard]] std::optional<PixelSize> usableDisplaySize() const;

  // Switches between the window and borderless fullscreen on its display. The new size arrives as a
  // WindowResized event.
  core::Result<void> setFullscreen(bool fullscreen);
  [[nodiscard]] bool isFullscreen() const;

  // Whether the keyboard types text into the window (TextTyped events); off by default. A system
  // may show an on-screen keyboard while it is on.
  core::Result<void> setTextInput(bool on);

  [[nodiscard]] SDL_Window* sdlWindow() const { return window_; }

private:
  explicit Window(SDL_Window* window) : window_{window} {}

  SDL_Window* window_ = nullptr;
};

} // namespace dwe::platform
