#include "dev_views.hpp"

#include <dwe/assets/content_check.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/pak.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/content/disc_install.hpp>
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

#include <array>
#include <cstdlib>
#include <memory>
#include <print>
#include <span>
#include <variant>
#include <vector>

#include "common.hpp"
#include "map_preview.hpp"
#include "model_viewer.hpp"
#include "test_scene.hpp"

namespace dwe::app {
namespace {

constexpr std::array TestClearColor{0.08F, 0.10F, 0.14F, 1.0F};
// Black, like the bars around a map that is smaller than the window.
constexpr std::array MapClearColor{0.0F, 0.0F, 0.0F, 1.0F};

} // namespace

bool wantsDevView(const Options& options) {
  return options.testScene || options.modelViewer || !options.maps.empty();
}

int checkContentFolder(const std::filesystem::path& folder) {
  const assets::DiskFileSystem disk;
  const auto found = assets::findPacks(disk, folder);
  if (!found || found->packs.empty()) {
    core::logError("no packs in {}: {}", assets::pathText(folder),
                   found ? "no pack.yaml nor .pak" : found.error().message);
    return EXIT_FAILURE;
  }
  // A content folder holds the engine's own packs.
  const auto opened = assets::openPacks(disk, found->packs, assets::PackOrigin::Engine);
  if (!opened) {
    core::logError("cannot open the packs of {}: {}", assets::pathText(folder),
                   opened.error().message);
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
  std::println("{} assets in {} packs, {} cannot be opened", stack->ids().size(),
               found->packs.size(), problems.size());
  return problems.empty() ? EXIT_SUCCESS : EXIT_FAILURE;
}

int runDevView(const Options& options) {
  auto platform = platform::Platform::create();
  if (!platform) {
    core::logError("{}", platform.error().message);
    return EXIT_FAILURE;
  }
  auto window =
      platform::Window::create({.title = "dwe", .width = options.width, .height = options.height});
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
  // The frame shows the render foundation's test scene, or the model viewer, under the map preview
  // if any.
  std::unique_ptr<TestScene> testScene;
  std::unique_ptr<ModelViewer> modelViewer;
  if (options.modelViewer) {
    auto viewer =
        ModelViewer::create(**renderer, options.content, dwe::content::teamPacksFolder() / "game",
                            options.characters, {.chibi = options.chibi, .yaw = options.viewerYaw});
    if (!viewer) {
      core::logError("{}", viewer.error().message);
      return EXIT_FAILURE;
    }
    modelViewer = std::move(*viewer);
  } else {
    auto scene = TestScene::create(**renderer, options.textureChurn);
    if (!scene) {
      core::logError("{}", scene.error().message);
      return EXIT_FAILURE;
    }
    testScene = std::move(*scene);
  }
  std::unique_ptr<MapPreview> mapPreview;
  if (!options.maps.empty()) {
    auto preview =
        MapPreview::create(**renderer, {.content = options.content,
                                        .mapIds = options.maps,
                                        .time = options.time.value_or(render::TimeOfDay::Day),
                                        .focus = options.focus});
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
#ifdef DWE_GAME_SCRIPTS
  if (auto loaded = (*scripts)->runFile(std::filesystem::path{DWE_GAME_SCRIPTS} / "main.lua");
      !loaded) {
    core::logError("{}", loaded.error().message);
  }
#endif

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
      // While the console is being typed into, key presses belong to it, except the one that closes
      // it. Releases still reach the game, so a key held when the console opened does not stay
      // down.
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
        // The map preview draws its own marker in the world; the model viewer, none.
        .radius = mapPreview || modelViewer
                      ? 0.0F
                      : static_cast<float>((*scripts)->number("marker.radius").value_or(0.0)),
    };
    const bool drawn = (*renderer)->renderFrame({
        .clearColor = mapPreview ? MapClearColor : TestClearColor,
        .scene = sceneDrawers,
        .ui = uiDrawers,
        .overlay =
            [&](const rhi::Frame& frame) {
              (*debugUi)->draw(frame, stats, input, **scripts, marker);
            },
    });
    // No frame means nothing to draw into, as with a window minimized to zero size. Sleep until
    // something happens instead of spinning. A minimized window is not skipped up front: some
    // drivers only finish the frames already presented when the next one is, and waiting on those
    // first would never return.
    if (!drawn) {
      platform->waitForEvents(IdleWait);
    }
  }
  (*device)->waitIdle();
  return EXIT_SUCCESS;
}

} // namespace dwe::app
