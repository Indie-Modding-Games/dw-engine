#include <dwe/assets/content_check.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/pak.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/fixed_step.hpp>
#include <dwe/core/log.hpp>
#include <dwe/debug/debug_ui.hpp>
#include <dwe/input/action_map.hpp>
#include <dwe/input/input_state.hpp>
#include <dwe/platform/platform.hpp>
#include <dwe/platform/window.hpp>
#include <dwe/render/renderer.hpp>
#include <dwe/rhi/device.hpp>
#include <dwe/script/script_vm.hpp>

#include <SDL3/SDL_gamepad.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <print>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

#include "test_scene.hpp"

namespace {

using namespace dwe;

constexpr double TickSeconds = 1.0 / 60.0;
constexpr double MaxFrameSeconds = 0.25;
// How long to sleep per loop while there is nothing to draw, about one display refresh.
constexpr std::chrono::milliseconds IdleWait{16};

#ifdef NDEBUG
constexpr bool Validation = false;
#else
constexpr bool Validation = true;
#endif

struct Options {
    // Where the debug overlay's asset viewer looks for packs.
    std::filesystem::path content = "content/base";
    // Set by --check-content: open every asset of the folder's packs and quit, without a window.
    bool checkContent = false;
    // Development check: make and destroy a texture every frame (render.md).
    bool textureChurn = false;
};

// Reads `dwe-game [--content <folder>] [--check-content <folder>] [--texture-churn]`.
std::optional<Options> parseOptions(std::span<char* const> arguments) {
    Options options;
    for (std::size_t index = 1; index < arguments.size(); ++index) {
        const std::string_view argument = arguments[index];
        if ((argument == "--content" || argument == "--check-content") && index + 1 < arguments.size()) {
            options.content = arguments[++index];
            options.checkContent = options.checkContent || argument == "--check-content";
        } else if (argument == "--texture-churn") {
            options.textureChurn = true;
        } else {
            core::logError("unknown argument '{}'; usage: dwe-game [--content <folder>] [--check-content <folder>] "
                           "[--texture-churn]",
                           argument);
            return std::nullopt;
        }
    }
    return options;
}

// Mounts the packs of a content folder and opens every asset the stack provides with the engine's readers. Prints
// one line per asset that fails; exits with success only when there is none.
int checkContentFolder(const std::filesystem::path& folder) {
    const assets::DiskFileSystem disk;
    const auto found = assets::findPacks(disk, folder);
    if (!found || found->packs.empty()) {
        core::logError("no packs in {}: {}", assets::pathText(folder),
                       found ? "no pack.yaml nor .pak" : found.error().message);
        return EXIT_FAILURE;
    }
    const auto opened = assets::openPacks(disk, found->packs);
    if (!opened) {
        core::logError("cannot open the packs of {}: {}", assets::pathText(folder), opened.error().message);
        return EXIT_FAILURE;
    }
    const auto stack = assets::VirtualFileSystem::mount(opened->locations);
    if (!stack) {
        core::logError("cannot mount {}: {}", assets::pathText(folder), stack.error().message);
        return EXIT_FAILURE;
    }
    const auto problems = assets::checkContent(*stack);
    for (const assets::ContentProblem& problem : problems) {
        std::println("{}: {}", problem.file, problem.message);
    }
    std::println("{} assets in {} packs, {} cannot be opened", stack->ids().size(), found->packs.size(),
                 problems.size());
    return problems.empty() ? EXIT_SUCCESS : EXIT_FAILURE;
}

// Players may drop an SDL mapping file next to the game to support controllers SDL does not know yet.
void loadUserGamepadMappings() {
    const std::filesystem::path path = "gamecontrollerdb.txt";
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        return;
    }
    const int added = SDL_AddGamepadMappingsFromFile(path.c_str());
    if (added < 0) {
        core::logWarning("could not read {}: {}", path.string(), SDL_GetError());
    } else {
        core::logInfo("loaded {} gamepad mappings from {}", added, path.string());
    }
}

// Whether key is bound to opening and closing the console, so it must reach the game even while the console has
// the keyboard.
bool togglesConsole(const input::ActionMap& map, platform::KeyCode key) {
    return std::ranges::any_of(map.bindings("debug_console"), [key](const input::Binding& binding) {
        const auto* keyBinding = std::get_if<input::KeyBinding>(&binding);
        return keyBinding != nullptr && keyBinding->key == key;
    });
}

struct FpsCounter {
    double windowStart = 0.0;
    int frames = 0;
    double framesPerSecond = 0.0;

    void frame(double now) {
        ++frames;
        if (now - windowStart >= 0.5) {
            framesPerSecond = frames / (now - windowStart);
            frames = 0;
            windowStart = now;
        }
    }
};

int run(const Options& options) {
    auto platform = platform::Platform::create();
    if (!platform) {
        core::logError("{}", platform.error().message);
        return EXIT_FAILURE;
    }
    auto window = platform::Window::create({.title = "dwe", .width = 1280, .height = 720});
    if (!window) {
        core::logError("{}", window.error().message);
        return EXIT_FAILURE;
    }
    auto device = rhi::Device::create(*window, {.validation = Validation, .vsync = true});
    if (!device) {
        core::logError("{}", device.error().message);
        return EXIT_FAILURE;
    }
    auto debugUi = debug::DebugUi::create(*window, **device);
    if (!debugUi) {
        core::logError("{}", debugUi.error().message);
        return EXIT_FAILURE;
    }
    (*debugUi)->mountContent(options.content);
    auto renderer = render::Renderer::create(**device);
    if (!renderer) {
        core::logError("{}", renderer.error().message);
        return EXIT_FAILURE;
    }
    // Until the field scene exists, the frame shows the render foundation's test scene.
    auto testScene = app::TestScene::create(**renderer, options.textureChurn);
    if (!testScene) {
        core::logError("{}", testScene.error().message);
        return EXIT_FAILURE;
    }
    loadUserGamepadMappings();

    input::InputState input{input::defaultActionMap()};
    auto scripts = script::ScriptVm::create();
    if (!scripts) {
        core::logError("{}", scripts.error().message);
        return EXIT_FAILURE;
    }
    (*scripts)->bindInput(input);
    if (auto loaded = (*scripts)->runFile(std::filesystem::path{DWE_GAME_SCRIPTS} / "main.lua"); !loaded) {
        core::logError("{}", loaded.error().message);
    }

    core::FixedStep step{TickSeconds, MaxFrameSeconds};
    FpsCounter fps;
    std::vector<platform::Event> events;
    const double start = platform->nowSeconds();
    double previous = start;
    bool running = true;

    while (running) {
        events.clear();
        platform->pollEvents(events, [&](const SDL_Event& raw) { (*debugUi)->handleRawEvent(raw); });
        for (const platform::Event& event : events) {
            if (std::holds_alternative<platform::QuitRequested>(event)) {
                running = false;
            }
            // While the console is being typed into, key presses belong to it, except the one that closes it.
            // Releases still reach the game, so a key held when the console opened does not stay down.
            const auto* key = std::get_if<platform::KeyChanged>(&event);
            if (key != nullptr && key->down && (*debugUi)->capturesKeyboard() &&
                !togglesConsole(input.actionMap(), key->key)) {
                continue;
            }
            // Clicks on the overlay are for it, not for the game.
            const auto* button = std::get_if<platform::MouseButtonChanged>(&event);
            if (button != nullptr && button->down && (*debugUi)->capturesMouse()) {
                continue;
            }
            input.handle(event);
        }

        const double now = platform->nowSeconds();
        const double frameSeconds = now - previous;
        previous = now;
        fps.frame(now);

        const int ticks = step.advance(frameSeconds);
        for (int tick = 0; tick < ticks; ++tick) {
            if (input.wasPressed("debug_console")) {
                (*debugUi)->toggle();
            }
            const std::array arguments{TickSeconds};
            if (auto result = (*scripts)->call("tick", arguments); !result) {
                core::logError("{}", result.error().message);
            }
            input.endTick();
        }
        (*scripts)->reloadChanged();

        (*testScene)->update(now - start);
        const std::array<render::Drawer*, 1> sceneDrawers{&(*testScene)->sceneDrawer()};
        const std::array<render::Drawer*, 1> uiDrawers{&(*testScene)->uiDrawer()};
        const debug::FrameStats stats{
            .framesPerSecond = fps.framesPerSecond,
            .frameMilliseconds = frameSeconds * 1000.0,
            .ticksThisFrame = ticks,
        };
        const debug::Marker marker{
            .x = static_cast<float>((*scripts)->number("marker.x").value_or(0.0)),
            .y = static_cast<float>((*scripts)->number("marker.y").value_or(0.0)),
            .radius = static_cast<float>((*scripts)->number("marker.radius").value_or(0.0)),
        };
        const bool drawn = (*renderer)->renderFrame({
            .clearColor = {0.08F, 0.10F, 0.14F, 1.0F},
            .scene = sceneDrawers,
            .ui = uiDrawers,
            .overlay = [&](const rhi::Frame& frame) { (*debugUi)->draw(frame, stats, input, **scripts, marker); },
        });
        // No frame means nothing to draw into, as with a window minimized to zero size. Sleep until something
        // happens instead of spinning. A minimized window is not skipped up front: some drivers only finish the
        // frames already presented when the next one is, and waiting on those first would never return.
        if (!drawn) {
            platform->waitForEvents(IdleWait);
        }
    }
    (*device)->waitIdle();
    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char** argv) {
    const auto options = parseOptions(std::span{argv, static_cast<std::size_t>(argc)});
    if (!options) {
        return EXIT_FAILURE;
    }
    return options->checkContent ? checkContentFolder(options->content) : run(*options);
}
