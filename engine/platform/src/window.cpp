#include <dwe/platform/window.hpp>

#include <SDL3/SDL.h>

#include <format>
#include <utility>

namespace dwe::platform {

core::Result<Window> Window::create(const WindowDesc& desc) {
  SDL_Window* window =
      SDL_CreateWindow(desc.title.c_str(), desc.width, desc.height,
                       SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (window == nullptr) {
    return core::fail(std::format("could not create the window: {}", SDL_GetError()));
  }
  return Window{window};
}

Window::Window(Window&& other) noexcept : window_{std::exchange(other.window_, nullptr)} {}

Window& Window::operator=(Window&& other) noexcept {
  std::swap(window_, other.window_);
  return *this;
}

Window::~Window() {
  if (window_ != nullptr) {
    SDL_DestroyWindow(window_);
  }
}

PixelSize Window::pixelSize() const {
  PixelSize size{.width = 0, .height = 0};
  SDL_GetWindowSizeInPixels(window_, &size.width, &size.height);
  return size;
}

PixelSize Window::windowSize() const {
  PixelSize size{.width = 0, .height = 0};
  SDL_GetWindowSize(window_, &size.width, &size.height);
  return size;
}

core::Result<void> Window::setSize(int width, int height) {
  if (width <= 0 || height <= 0) {
    return core::fail(std::format("bad window size {}x{}", width, height));
  }
  if (!SDL_SetWindowSize(window_, width, height)) {
    return core::fail(std::format("could not resize the window: {}", SDL_GetError()));
  }
  return {};
}

std::optional<PixelSize> Window::usableDisplaySize() const {
  const SDL_DisplayID display = SDL_GetDisplayForWindow(window_);
  SDL_Rect bounds{};
  if (display == 0 || !SDL_GetDisplayUsableBounds(display, &bounds)) {
    return std::nullopt;
  }
  return PixelSize{.width = bounds.w, .height = bounds.h};
}

core::Result<void> Window::setFullscreen(bool fullscreen) {
  if (!SDL_SetWindowFullscreen(window_, fullscreen)) {
    return core::fail(std::format("could not change the window mode: {}", SDL_GetError()));
  }
  return {};
}

bool Window::isFullscreen() const {
  return (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
}

} // namespace dwe::platform
