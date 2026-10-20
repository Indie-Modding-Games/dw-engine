#include "common.hpp"

#include <dwe/core/log.hpp>

#include <SDL3/SDL_gamepad.h>

#include <algorithm>
#include <filesystem>
#include <system_error>
#include <variant>

namespace dwe::app {

// SDL takes the file name as UTF-8 chars, which path::c_str() is not on Windows (wchar_t).
void loadUserGamepadMappings() {
    constexpr const char* FileName = "gamecontrollerdb.txt";
    std::error_code error;
    if (!std::filesystem::exists(FileName, error)) {
        return;
    }
    const int added = SDL_AddGamepadMappingsFromFile(FileName);
    if (added < 0) {
        core::logWarning("could not read {}: {}", FileName, SDL_GetError());
    } else {
        core::logInfo("loaded {} gamepad mappings from {}", added, FileName);
    }
}

bool togglesConsole(const input::ActionMap& map, platform::KeyCode key) {
    return std::ranges::any_of(map.bindings("debug_console"), [key](const input::Binding& binding) {
        const auto* keyBinding = std::get_if<input::KeyBinding>(&binding);
        return keyBinding != nullptr && keyBinding->key == key;
    });
}

void FpsCounter::frame(double now) {
    ++frames;
    if (now - windowStart >= 0.5) {
        framesPerSecond = frames / (now - windowStart);
        frames = 0;
        windowStart = now;
    }
}

} // namespace dwe::app
