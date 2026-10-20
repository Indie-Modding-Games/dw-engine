#include "game.hpp"

#include <dwe/assets/asset_id.hpp>
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
#include <dwe/ui/minimap.hpp>
#include <dwe/ui/ui.hpp>
#include <dwe/ui/ui_renderer.hpp>

#include <SDL3/SDL_events.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "autotest.hpp"
#include "common.hpp"
#include "content.hpp"
#include "field_scene.hpp"
#include "frame_stats.hpp"

namespace dwe::app {
namespace {

constexpr std::string_view MenuDocument = "pause_menu";
// Smaller changes of the map change's fade are not worth telling the UI.
constexpr double FadeEpsilon = 1e-4;
// Frames left out of the steady frame times after a map change: the new map's textures upload in the next frames.
constexpr int MapChangeSettleFrames = 5;

std::string describe(const ui::UiValue& value) {
    if (const auto* flag = std::get_if<bool>(&value)) {
        return *flag ? "true" : "false";
    }
    if (const auto* number = std::get_if<double>(&value)) {
        return std::format("{}", *number);
    }
    if (const auto* text = std::get_if<std::string>(&value)) {
        return *text;
    }
    return "-";
}

// The menu's settings, published to the UI so its pages show the current state.
struct Settings {
    bool fullscreen = false;
    double uiScale = 1.0;
    bool vsync = true;
};

// Applies a command of the UI's documents and publishes the resulting state. Returns false for quit.
bool apply(const ui::UiCommand& command, ui::Ui& ui, platform::Window& window, Settings& settings) {
    core::logInfo("game: command {} = {}", command.name, describe(command.value));
    if (command.name == "quit") {
        return false;
    }
    if (command.name == "open_menu") {
        ui.show(MenuDocument, true);
    } else if (command.name == "minimap_visible" && std::holds_alternative<bool>(command.value)) {
        ui.setSetting("minimap_visible", command.value);
    } else if (command.name == "minimap_size" && std::holds_alternative<std::string>(command.value)) {
        ui.setSetting("minimap_size", command.value);
    } else if (command.name == "fullscreen" && std::holds_alternative<bool>(command.value)) {
        if (auto changed = window.setFullscreen(std::get<bool>(command.value)); changed) {
            settings.fullscreen = std::get<bool>(command.value);
        } else {
            core::logWarning("game: {}", changed.error().message);
        }
        ui.setSetting("fullscreen", settings.fullscreen);
    } else if (command.name == "ui_scale" && std::holds_alternative<double>(command.value)) {
        ui.setScale(static_cast<float>(std::get<double>(command.value)));
        settings.uiScale = ui.scale();
        ui.setSetting("ui_scale", settings.uiScale);
    } else if (command.name == "vsync") {
        // The device picks its present mode when it is created (--no-vsync); the menu keeps showing the real one.
        core::logInfo("game: vsync cannot change while running yet");
        ui.setSetting("vsync", settings.vsync);
    }
    return true;
}

ui::MinimapMarker markerOf(const game::field::CharacterState& character) {
    return {.x = character.position.x,
            .z = character.position.z,
            .facingX = std::sin(character.yaw),
            .facingZ = std::cos(character.yaw)};
}

core::Result<void> loadDocuments(ui::Ui& ui) {
    for (const auto& [name, id] :
         {std::pair{"hud", "ui/hud/document"}, std::pair{"pause_menu", "ui/pause_menu/document"}}) {
        auto asset = assets::AssetId::parse(id);
        if (!asset) {
            return std::unexpected{asset.error()};
        }
        if (auto loaded = ui.loadDocument(name, *asset); !loaded) {
            return loaded;
        }
    }
    return {};
}

// The mouse on the ground (R2.2): a click walks to the point around walls (click-to-move), and holding the button
// walks towards the pointer for as long as it is held, like a stick, which also reaches exits at the edge of the
// screen that no click can point at.
class MouseSteering {
public:
    // Takes the events the UI did not.
    void handle(const platform::Event& event, const FieldScene& scene) {
        if (const auto* moved = std::get_if<platform::MouseMoved>(&event)) {
            pointer_ = {moved->x, moved->y};
        } else if (const auto* button = std::get_if<platform::MouseButtonChanged>(&event);
                   button != nullptr && button->button == platform::MouseButton::Left) {
            held_ = button->down;
            heldTicks_ = 0;
            if (held_) {
                target_ = scene.pickGround(pointer_);
            }
        }
    }

    // The click-to-move target set since the last call.
    std::optional<glm::vec3> take() { return std::exchange(target_, std::nullopt); }

    // Once per logic tick: while the button has been held for a moment, the direction towards the pointer, as a
    // stick (game::field::FieldInput).
    std::optional<glm::vec2> steer(const FieldScene& scene, glm::vec2 windowSize) {
        if (!held_ || ++heldTicks_ < HoldTicks) {
            return std::nullopt;
        }
        const auto ground = scene.pickGround(pointer_);
        if (!ground) {
            return std::nullopt;
        }
        const game::field::Field& field = scene.field();
        glm::vec3 toPointer = *ground - field.player().position;
        // At the edge of the window the player walks off that edge, even past the pointer: exits are often there,
        // under ground that the camera never shows.
        const float band = EdgeBand * std::min(windowSize.x, windowSize.y);
        const bool atEdge = pointer_.x < band || pointer_.y < band || pointer_.x > windowSize.x - band ||
                            pointer_.y > windowSize.y - band;
        if (const auto centre = scene.pickGround(windowSize * 0.5F); atEdge && centre) {
            toPointer = *ground - *centre;
        }
        const float length = std::hypot(toPointer.x, toPointer.z);
        if (length < DeadRadius) {
            return glm::vec2{0.0F};
        }
        const glm::vec3 forward = field.cameraForward();
        const glm::vec2 direction{toPointer.x / length, toPointer.z / length};
        return glm::vec2{glm::dot(direction, glm::vec2{-forward.z, forward.x}),
                         glm::dot(direction, glm::vec2{forward.x, forward.z})};
    }

    // A press taken by the UI (a click on the menu) must not leave the ground held.
    void release() {
        held_ = false;
        target_.reset();
    }

private:
    // A press longer than this (a quarter of a second) steers instead of only setting a target.
    static constexpr int HoldTicks = 15;
    // Closer to the pointer than this, the player stops instead of turning on the spot.
    static constexpr float DeadRadius = 30.0F;
    // The edge of the window, as a fraction of its smaller side.
    static constexpr float EdgeBand = 0.04F;

    glm::vec2 pointer_{0.0F};
    bool held_ = false;
    int heldTicks_ = 0;
    std::optional<glm::vec3> target_;
};

} // namespace

int runGame(const Options& options) {
    auto platform = platform::Platform::create();
    if (!platform) {
        core::logError("{}", platform.error().message);
        return EXIT_FAILURE;
    }
    // After the platform, so its virtual gamepad is unplugged before the platform goes.
    std::unique_ptr<Autotest> autotest;
    if (options.autotest) {
        auto loaded = Autotest::load(*options.autotest, options.shots, options.skipSteps);
        if (!loaded) {
            core::logError("autotest: {}", loaded.error().message);
            return EXIT_FAILURE;
        }
        autotest = std::move(*loaded);
    }
    auto window = platform::Window::create({.title = "dwe", .width = options.width, .height = options.height});
    if (!window) {
        core::logError("{}", window.error().message);
        return EXIT_FAILURE;
    }
    Settings settings{.fullscreen = false, .uiScale = 1.0, .vsync = options.vsync};
    if (options.fullscreen) {
        if (auto changed = window->setFullscreen(true); changed) {
            settings.fullscreen = true;
        } else {
            core::logWarning("game: {}", changed.error().message);
        }
    }
    auto device = rhi::Device::create(*window, {.validation = Validation, .vsync = options.vsync});
    if (!device) {
        core::logError("{}", device.error().message);
        return EXIT_FAILURE;
    }
    core::logInfo("game: validation layer {}, vsync {}", validationState((*device)->validationEnabled()),
                  options.vsync ? "on" : "off");
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
    auto content = GameContent::mount(options.content, options.mods);
    if (!content) {
        core::logError("game: {}", content.error().message);
        return EXIT_FAILURE;
    }
    const assets::VirtualFileSystem& files = (*content)->files();
    auto uiRenderer = ui::UiRenderer::create(**renderer);
    if (!uiRenderer) {
        core::logError("{}", uiRenderer.error().message);
        return EXIT_FAILURE;
    }
    const platform::PixelSize size = window->pixelSize();
    auto ui = ui::Ui::create({
        .files = &files,
        .renderer = uiRenderer->get(),
        .width = size.width,
        .height = size.height,
        .scale = 1.0F,
        .hotReload = false,
        .clock = [&platform] { return platform->nowSeconds(); },
        .scripts = {},
    });
    if (!ui) {
        core::logError("{}", ui.error().message);
        return EXIT_FAILURE;
    }
    if (auto loaded = loadDocuments(**ui); !loaded) {
        core::logError("game: {}", loaded.error().message);
        return EXIT_FAILURE;
    }
    (*ui)->setSetting("minimap_visible", true);
    (*ui)->setSetting("minimap_size", std::string{"medium"});
    (*ui)->setSetting("fullscreen", settings.fullscreen);
    (*ui)->setSetting("ui_scale", settings.uiScale);
    (*ui)->setSetting("vsync", settings.vsync);
    // How dark the map change has made the screen; the HUD draws it.
    double screenFade = 0.0;
    (*ui)->setSetting("screen_fade", screenFade);
    (*ui)->setMenuDocument(MenuDocument);
    (*ui)->show("hud");

    auto scene = FieldScene::create(
        **renderer, files, {.startMap = options.startMap, .startExit = options.startExit, .time = options.time});
    if (!scene) {
        core::logError("game: {}", scene.error().message);
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

    FrameStats frameStats;
    const AutotestHost host{
        .platform = &*platform,
        .window = &*window,
        .renderer = renderer->get(),
        .device = device->get(),
        .scene = scene->get(),
        .ui = ui->get(),
        .input = &input,
        .stats = &frameStats,
    };

    core::FixedStep step{TickSeconds, MaxFrameSeconds};
    FpsCounter fps;
    MouseSteering mouse;
    std::vector<platform::Event> events;
    std::uint64_t ticksRun = 0;
    std::uint32_t mapSerial = 0;
    double previous = platform->nowSeconds();
    bool running = true;
    debug::FrameStats overlayStats{};
    // Built once; the frame takes it by reference, so drawing it allocates nothing.
    const auto drawOverlay = [&](const rhi::Frame& frame) {
        (*debugUi)->draw(frame, overlayStats, input, **scripts, {.x = 0.0F, .y = 0.0F, .radius = 0.0F});
    };

    while (running) {
        events.clear();
        if (autotest) {
            autotest->frame(host, ticksRun, platform->nowSeconds(), events);
            if (autotest->finished()) {
                break;
            }
        }
        platform->pollEvents(events, [&](const SDL_Event& raw) { (*debugUi)->handleRawEvent(raw); });
        for (const platform::Event& event : events) {
            if (std::holds_alternative<platform::QuitRequested>(event)) {
                running = false;
                if (autotest) {
                    autotest->fail("the window was closed");
                }
            }
            const auto* key = std::get_if<platform::KeyChanged>(&event);
            if (key != nullptr && key->down && (*debugUi)->capturesKeyboard() &&
                !togglesConsole(input.actionMap(), key->key)) {
                continue;
            }
            const auto* button = std::get_if<platform::MouseButtonChanged>(&event);
            if (button != nullptr && button->down && (*debugUi)->capturesMouse()) {
                continue;
            }
            if ((*ui)->handleEvent(event)) {
                if (button != nullptr) {
                    mouse.release();
                }
                continue;
            }
            input.handle(event);
            if (!(*ui)->capturesInput()) {
                mouse.handle(event, **scene);
            }
        }

        const double now = platform->nowSeconds();
        const double frameSeconds = now - previous;
        previous = now;
        fps.frame(now);
        // The first half second warms up (pipelines, the first uploads): it is not the game's steady pace.
        if (constexpr std::uint64_t WarmUpTicks = 30; ticksRun < WarmUpTicks) {
            frameStats.skipNext(1);
        }
        // Frame times are only kept for the autotest's report; a normal session would grow them forever.
        const bool steady = autotest ? frameStats.add(frameSeconds) : true;
        // A slow frame says when it happened, to tell a hitch of the game from a load or a resize. At 60 Hz with
        // vsync about half the frames take a little over one tick, so only one and a half ticks count as slow.
        if (constexpr double SlowFrameSeconds = 1.5 * TickSeconds; frameSeconds > SlowFrameSeconds && ticksRun > 0) {
            core::logInfo("game: slow frame, {:.1f} ms, at tick {}{}", frameSeconds * 1000.0, ticksRun,
                          steady ? "" : " (expected: load, resize or capture)");
        }

        const int ticks = step.advance(frameSeconds);
        for (int tick = 0; tick < ticks; ++tick) {
            if (input.wasPressed("debug_console")) {
                (*debugUi)->toggle();
            }
            (*ui)->tick(input, TickSeconds);
            game::field::FieldInput intent;
            if (!(*ui)->capturesInput()) {
                const input::Vector2 move = input.vector("move_left", "move_right", "move_up", "move_down");
                // Up gives a negative y; the field's y points away from the camera.
                intent.stick = {move.x, -move.y};
                intent.run = !input.isDown("walk");
                intent.moveTarget = mouse.take();
                if (const auto held =
                        mouse.steer(**scene, glm::vec2{static_cast<float>((*renderer)->targetExtent().width),
                                                       static_cast<float>((*renderer)->targetExtent().height)});
                    held && move.x == 0.0F && move.y == 0.0F) {
                    intent.stick = *held;
                }
                // Escape, the right button or East drops a click-to-move target.
                intent.cancelTarget = input.wasPressed("cancel");
            } else {
                mouse.release();
            }
            // The menu pauses the field.
            if (!(*ui)->isVisible(MenuDocument)) {
                (*scene)->tick(intent, static_cast<float>(TickSeconds));
            }
            input.endTick();
            ++ticksRun;
        }

        for (const ui::UiCommand& command : (*ui)->takeCommands()) {
            running = apply(command, **ui, *window, settings) && running;
        }
        if ((*scene)->mapSerial() != mapSerial) {
            mapSerial = (*scene)->mapSerial();
            (*ui)->setMinimap(ui::minimapGridOf((*scene)->map()), (*scene)->mapName());
            if (const auto& change = (*scene)->lastMapChange(); change && autotest) {
                autotest->mapChanged(*change);
            }
            frameStats.skipNext(MapChangeSettleFrames);
        }
        const game::field::Field& field = (*scene)->field();
        (*ui)->setMinimapMarkers(markerOf(field.player()), markerOf(field.partner()));
        if (const double fade = field.fadeLevel(); std::abs(fade - screenFade) > FadeEpsilon) {
            screenFade = fade;
            (*ui)->setSetting("screen_fade", screenFade);
        }

        const bool paused = (*ui)->isVisible(MenuDocument);
        (*scene)->update(frameSeconds, static_cast<float>(step.alpha()), paused);
        (*ui)->update();

        overlayStats = {
            .framesPerSecond = fps.framesPerSecond,
            .frameMilliseconds = frameSeconds * 1000.0,
            .ticksThisFrame = ticks,
        };
        const std::array<render::Drawer*, 1> uiDrawers{uiRenderer->get()};
        const bool drawn = (*renderer)->renderFrame({
            .clearColor = {0.0F, 0.0F, 0.0F, 1.0F},
            .scene = (*scene)->drawers(),
            .ui = uiDrawers,
            .overlay = std::ref(drawOverlay),
        });
        if (!drawn) {
            platform->waitForEvents(IdleWait);
        }
    }
    (*device)->waitIdle();
    if (!autotest) {
        // The locals go in reverse order: the scene and the UI release their textures into the renderer before it.
        return EXIT_SUCCESS;
    }
    autotest->summarize(host);
    // Shut down before judging the run, so that what teardown reports (leaks, objects destroyed in use) counts: the
    // scene and the UI release their textures into the renderer before it, and the device goes last.
    const bool validationActive = (*device)->validationEnabled();
    scene->reset();
    scripts->reset();
    ui->reset();
    uiRenderer->reset();
    renderer->reset();
    debugUi->reset();
    device->reset();
    return autotest->verdict(validationActive);
}

} // namespace dwe::app
