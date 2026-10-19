#include <dwe/platform/window.hpp>

#include <SDL3/SDL.h>

#include <format>
#include <utility>

namespace dwe::platform {

core::Result<Window> Window::create(const WindowDesc& desc) {
    SDL_Window* window = SDL_CreateWindow(desc.title.c_str(), desc.width, desc.height,
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

} // namespace dwe::platform
