#pragma once

#include <dwe/core/error.hpp>

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

    // The size of the drawable area in pixels, which differs from the window size on high-density displays.
    [[nodiscard]] PixelSize pixelSize() const;

    [[nodiscard]] SDL_Window* sdlWindow() const { return window_; }

private:
    explicit Window(SDL_Window* window) : window_{window} {}

    SDL_Window* window_ = nullptr;
};

} // namespace dwe::platform
