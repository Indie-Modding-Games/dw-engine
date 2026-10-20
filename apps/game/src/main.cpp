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
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <print>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

#include "map_preview.hpp"
#include "model_viewer.hpp"
#include "test_scene.hpp"
#include "ui_demo.hpp"

namespace {

using namespace dwe;

constexpr double TickSeconds = 1.0 / 60.0;
constexpr std::array TestClearColor{0.08F, 0.10F, 0.14F, 1.0F};
// Black, like the bars around a map that is smaller than the window.
constexpr std::array MapClearColor{0.0F, 0.0F, 0.0F, 1.0F};
constexpr double MaxFrameSeconds = 0.25;
// How long to sleep per loop while there is nothing to draw, about one display refresh.
constexpr std::chrono::milliseconds IdleWait{16};

#ifdef NDEBUG
constexpr bool Validation = false;
#else
constexpr bool Validation = true;
#endif

constexpr std::string_view Usage =
    "dwe-game [--content <folder>] [--check-content <folder>] [--texture-churn] "
    "[--size <width>x<height>] [--map <id>[,<id>...] [--time day|dusk|night] [--focus <x>,<z>]] "
    "[--ui-demo [--virtual-gamepad <file>]] [--model-viewer [character id...]]";

struct Options {
    // Where the debug overlay's asset viewer and the map preview look for packs.
    std::filesystem::path content = "content/base";
    // Set by --check-content: open every asset of the folder's packs and quit, without a window.
    bool checkContent = false;
    // Development check: make and destroy a texture every frame (render.md).
    bool textureChurn = false;
    int width = 1280;
    int height = 720;
    // Maps to show instead of the test scene (app::MapPreview), until the field scene exists: the first, and the
    // others in turn with debug_next_map.
    std::vector<std::string> maps;
    render::TimeOfDay time = render::TimeOfDay::Day;
    std::optional<glm::vec2> focus;
    // Runs the UI demo instead of the game (--ui-demo), with --content, --map and --size, and this file of virtual
    // gamepad commands.
    bool uiDemo = false;
    std::optional<std::filesystem::path> virtualGamepad;
    // Show these characters of the game pack (`--model-viewer [id...]`) instead of the test scene.
    bool modelViewer = false;
    std::vector<std::string> characters;
};

// Reads two numbers separated by `separator`, such as 1280x720.
std::optional<std::array<int, 2>> parsePair(std::string_view text, char separator) {
    std::array<int, 2> values{};
    const char* begin = std::to_address(text.begin());
    const char* end = std::to_address(text.end());
    auto first = std::from_chars(begin, end, values[0]);
    if (first.ec != std::errc{} || first.ptr == end || *first.ptr != separator) {
        return std::nullopt;
    }
    auto second = std::from_chars(first.ptr + 1, end, values[1]);
    if (second.ec != std::errc{} || second.ptr != end) {
        return std::nullopt;
    }
    return values;
}

std::optional<render::TimeOfDay> parseTime(std::string_view text) {
    if (text == "day") {
        return render::TimeOfDay::Day;
    }
    if (text == "dusk") {
        return render::TimeOfDay::Dusk;
    }
    if (text == "night") {
        return render::TimeOfDay::Night;
    }
    return std::nullopt;
}

// Reads one option that takes a value; false if the value is not valid for it or the option is unknown.
bool readOption(Options& options, std::string_view argument, std::string_view value) {
    if (argument == "--content" || argument == "--check-content") {
        options.content = value;
        options.checkContent = options.checkContent || argument == "--check-content";
        return true;
    }
    if (argument == "--virtual-gamepad") {
        options.virtualGamepad = value;
        return true;
    }
    if (argument == "--map") {
        options.maps.clear();
        for (const auto part : std::views::split(value, ',')) {
            options.maps.emplace_back(std::string_view{part});
        }
        return std::ranges::none_of(options.maps, &std::string::empty);
    }
    if (argument == "--size") {
        const auto size = parsePair(value, 'x');
        if (!size || (*size)[0] <= 0 || (*size)[1] <= 0) {
            return false;
        }
        options.width = (*size)[0];
        options.height = (*size)[1];
        return true;
    }
    if (argument == "--time") {
        const auto time = parseTime(value);
        options.time = time.value_or(render::TimeOfDay::Day);
        return time.has_value();
    }
    if (argument == "--focus") {
        const auto focus = parsePair(value, ',');
        if (!focus) {
            return false;
        }
        options.focus = glm::vec2{static_cast<float>((*focus)[0]), static_cast<float>((*focus)[1])};
        return true;
    }
    return false;
}

// Reads the command line (see Usage).
std::optional<Options> parseOptions(std::span<char* const> arguments) {
    Options options;
    std::size_t index = 1;
    while (index < arguments.size()) {
        const std::string_view argument = arguments[index];
        if (argument == "--model-viewer") {
            options.modelViewer = true;
            ++index;
            while (index < arguments.size() && !std::string_view{arguments[index]}.starts_with("--")) {
                options.characters.emplace_back(arguments[index]);
                ++index;
            }
            continue;
        }
        if (argument == "--texture-churn" || argument == "--ui-demo") {
            options.textureChurn = options.textureChurn || argument == "--texture-churn";
            options.uiDemo = options.uiDemo || argument == "--ui-demo";
            ++index;
            continue;
        }
        if (index + 1 >= arguments.size() || !readOption(options, argument, arguments[index + 1])) {
            core::logError("bad argument '{}'; usage: {}", argument, Usage);
            return std::nullopt;
        }
        index += 2;
    }
    if (options.modelViewer && options.characters.empty()) {
        options.characters = {"models/tamer", "models/partner"};
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
    auto window = platform::Window::create({.title = "dwe", .width = options.width, .height = options.height});
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
    // Until the field scene exists, the frame shows the render foundation's test scene, or the model viewer.
    std::unique_ptr<app::TestScene> testScene;
    std::unique_ptr<app::ModelViewer> modelViewer;
    if (options.modelViewer) {
        auto viewer = app::ModelViewer::create(**renderer, options.content, DWE_GAME_PACK, options.characters);
        if (!viewer) {
            core::logError("{}", viewer.error().message);
            return EXIT_FAILURE;
        }
        modelViewer = std::move(*viewer);
    } else {
        auto scene = app::TestScene::create(**renderer, options.textureChurn);
        if (!scene) {
            core::logError("{}", scene.error().message);
            return EXIT_FAILURE;
        }
        testScene = std::move(*scene);
    }
    std::unique_ptr<app::MapPreview> mapPreview;
    if (!options.maps.empty()) {
        auto preview = app::MapPreview::create(
            **renderer,
            {.content = options.content, .mapIds = options.maps, .time = options.time, .focus = options.focus});
        if (!preview) {
            core::logError("{}", preview.error().message);
            return EXIT_FAILURE;
        }
        mapPreview = std::move(*preview);
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
            if (mapPreview && !(*debugUi)->capturesKeyboard()) {
                mapPreview->handle(event);
            }
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
            if (mapPreview) {
                mapPreview->tick(input, TickSeconds);
            }
            input.endTick();
        }
        (*scripts)->reloadChanged();

        std::array<render::Drawer*, 2> drawers{};
        std::span<render::Drawer* const> sceneDrawers;
        std::span<render::Drawer* const> uiDrawers;
        if (mapPreview) {
            mapPreview->update(frameSeconds);
            drawers = {&mapPreview->backgroundDrawer(), &mapPreview->layerDrawer()};
            sceneDrawers = drawers;
        } else if (modelViewer) {
            modelViewer->update(now - start, (*renderer)->targetExtent());
            drawers = {&modelViewer->opaqueDrawer(), &modelViewer->translucentDrawer()};
            sceneDrawers = drawers;
        } else {
            testScene->update(now - start);
            drawers = {&testScene->sceneDrawer(), &testScene->uiDrawer()};
            sceneDrawers = std::span{drawers}.first(1);
            uiDrawers = std::span{drawers}.last(1);
        }
        const debug::FrameStats stats{
            .framesPerSecond = fps.framesPerSecond,
            .frameMilliseconds = frameSeconds * 1000.0,
            .ticksThisFrame = ticks,
        };
        const debug::Marker marker{
            .x = static_cast<float>((*scripts)->number("marker.x").value_or(0.0)),
            .y = static_cast<float>((*scripts)->number("marker.y").value_or(0.0)),
            // The map preview draws its own marker in the world.
            .radius = mapPreview ? 0.0F : static_cast<float>((*scripts)->number("marker.radius").value_or(0.0)),
        };
        const bool drawn = (*renderer)->renderFrame({
            .clearColor = mapPreview ? MapClearColor : TestClearColor,
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
    if (options->uiDemo) {
        app::UiDemoOptions demo{.content = options->content,
                                .width = options->width,
                                .height = options->height,
                                .virtualGamepad = options->virtualGamepad};
        if (!options->maps.empty()) {
            demo.map = options->maps.front();
        }
        return app::runUiDemo(demo);
    }
    return options->checkContent ? checkContentFolder(options->content) : run(*options);
}
