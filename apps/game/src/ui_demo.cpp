#include "ui_demo.hpp"

#include <dwe/assets/file_system.hpp>
#include <dwe/assets/map_document.hpp>
#include <dwe/assets/map_table.hpp>
#include <dwe/assets/pak.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/fixed_step.hpp>
#include <dwe/core/log.hpp>
#include <dwe/input/action_map.hpp>
#include <dwe/input/input_state.hpp>
#include <dwe/localization/catalog.hpp>
#include <dwe/platform/locale.hpp>
#include <dwe/platform/platform.hpp>
#include <dwe/platform/window.hpp>
#include <dwe/render/renderer.hpp>
#include <dwe/rhi/device.hpp>
#include <dwe/ui/minimap.hpp>
#include <dwe/ui/ui.hpp>
#include <dwe/ui/ui_renderer.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <format>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "device_names.hpp"
#include "minimap_setting.hpp"

namespace dwe::app {
namespace {

constexpr double TickSeconds = 1.0 / 60.0;
constexpr double MaxFrameSeconds = 0.25;
constexpr std::chrono::milliseconds IdleWait{16};

#ifdef NDEBUG
constexpr bool Validation = false;
#else
constexpr bool Validation = true;
#endif

// The packs of the content folder and the team's packs from the source tree, mounted together. The
// opened archives must outlive the file system mounted from them.
struct MountedContent {
  assets::OpenedPacks opened;
  assets::VirtualFileSystem files;
};

core::Result<MountedContent> mountContent(const assets::FileSystem& disk,
                                          const std::filesystem::path& content) {
  std::vector<assets::FoundPack> found;
  for (const std::filesystem::path& folder : {content, std::filesystem::path{DWE_TEAM_PACKS}}) {
    auto packs = assets::findPacks(disk, folder);
    if (!packs) {
      return std::unexpected{packs.error()};
    }
    found.insert(found.end(), packs->packs.begin(), packs->packs.end());
  }
  // The content folder and the team's packs are the engine's own.
  auto opened = assets::openPacks(disk, found, assets::PackOrigin::Engine);
  if (!opened) {
    return std::unexpected{opened.error()};
  }
  auto files = assets::VirtualFileSystem::mount(opened->locations);
  if (!files) {
    return std::unexpected{files.error()};
  }
  return MountedContent{.opened = std::move(*opened), .files = std::move(*files)};
}

// A small walled area with a pillar, for running the demo without extracted content.
assets::MapDocument testMap() {
  assets::MapDocument map;
  map.id = "test";
  constexpr int Size = assets::MapCollision::Size;
  constexpr int First = 38;
  constexpr int Last = 62;
  constexpr int PillarFirst = 47;
  constexpr int PillarLast = 52;
  for (int row = 0; row < Size; ++row) {
    for (int column = 0; column < Size; ++column) {
      const bool inside = column >= First && column <= Last && row >= First && row <= Last;
      const bool pillar =
          column >= PillarFirst && column <= PillarLast && row >= PillarFirst && row <= PillarLast;
      if (!inside || pillar) {
        map.collision.setValue({.column = column, .row = row}, assets::MapCollision::BlockedBit);
      }
    }
  }
  return map;
}

// The key of the test map's name among the texts.
constexpr std::string_view TestMapNameKey = "dev.test_map";

struct LoadedMap {
  assets::MapDocument document;
  std::string name;    // for the log
  std::string nameKey; // for the player
};

LoadedMap loadMap(const assets::VirtualFileSystem* files, const std::string& id) {
  if (files != nullptr) {
    auto map = assets::loadMapDocument(*files, id);
    if (map) {
      std::string name = id;
      std::string nameKey;
      if (auto table = assets::loadMapTable(*files); table) {
        if (const assets::MapEntry* entry = table->find(id); entry != nullptr) {
          const std::string_view shown = table->displayName(*entry);
          if (!shown.empty()) {
            name = shown;
            nameKey = localization::mapNameKey(entry->loadingName);
          }
        }
      }
      return {.document = std::move(*map), .name = std::move(name), .nameKey = std::move(nameKey)};
    }
    core::logWarning("ui demo: map {}: {}; showing a test map", id, map.error().message);
  }
  return {.document = testMap(),
          .name = std::string{TestMapNameKey},
          .nameKey = std::string{TestMapNameKey}};
}

// A player that walks the map on its own, turning at walls, and a partner that follows its steps.
class Walkers {
public:
  explicit Walkers(const assets::MapDocument& map) : map_{&map} {
    if (const assets::MapWarp* warp = map.warp(0); warp != nullptr) {
      const glm::vec3 spawn = assets::warpSpawn(*warp).position;
      position_ = {spawn.x, spawn.z};
    }
    if (blocked(position_)) {
      position_ = firstFreeCell();
    }
    trail_ = {position_};
  }

  void tick(double seconds) {
    constexpr float Speed = 250.0F;
    constexpr double TurnEvery = 4.0;
    constexpr std::size_t PartnerDelay = 40;
    sinceTurn_ += seconds;
    const glm::vec2 step = direction() * (Speed * static_cast<float>(seconds));
    if (sinceTurn_ > TurnEvery ||
        blocked(position_ + (direction() * assets::CollisionCellSize * 0.5F))) {
      turn();
      sinceTurn_ = 0.0;
    } else {
      position_ += step;
    }
    trail_.push_back(position_);
    if (trail_.size() > PartnerDelay) {
      trail_.pop_front();
    }
  }

  [[nodiscard]] ui::MinimapMarker player() const {
    const glm::vec2 facing = direction();
    return {.x = position_.x, .z = position_.y, .facingX = facing.x, .facingZ = facing.y};
  }

  [[nodiscard]] ui::MinimapMarker partner() const {
    const glm::vec2 at = trail_.front();
    const glm::vec2 toward = trail_.size() > 1 ? trail_[1] - at : glm::vec2{0.0F, -1.0F};
    return {.x = at.x, .z = at.y, .facingX = toward.x, .facingZ = toward.y};
  }

private:
  [[nodiscard]] glm::vec2 direction() const {
    constexpr std::array<glm::vec2, 4> Directions{glm::vec2{0.0F, -1.0F}, glm::vec2{1.0F, 0.0F},
                                                  glm::vec2{0.0F, 1.0F}, glm::vec2{-1.0F, 0.0F}};
    return Directions.at(heading_);
  }

  // Turns right, or left, or back: whichever way is open first.
  void turn() {
    constexpr std::array<std::size_t, 3> Turns{1, 3, 2};
    for (const std::size_t turn : Turns) {
      const std::size_t heading = (heading_ + turn) % 4;
      const std::size_t saved = heading_;
      heading_ = heading;
      if (!blocked(position_ + (direction() * assets::CollisionCellSize * 0.5F))) {
        return;
      }
      heading_ = saved;
    }
  }

  [[nodiscard]] bool blocked(glm::vec2 at) const {
    return map_->collision.isBlocked(assets::collisionCellAt({at.x, 0.0F, at.y}));
  }

  [[nodiscard]] glm::vec2 firstFreeCell() const {
    for (int row = 0; row < assets::MapCollision::Size; ++row) {
      for (int column = 0; column < assets::MapCollision::Size; ++column) {
        const assets::CollisionCell cell{.column = column, .row = row};
        if (!map_->collision.isBlocked(cell)) {
          const glm::vec3 center = assets::collisionCellCenter(cell);
          return {center.x, center.z};
        }
      }
    }
    return {0.0F, 0.0F};
  }

  const assets::MapDocument* map_;
  glm::vec2 position_{0.0F};
  std::size_t heading_ = 0;
  double sinceTurn_ = 0.0;
  std::deque<glm::vec2> trail_{glm::vec2{0.0F}};
};

// Drives a virtual gamepad from the lines appended to a file:
//   attach <xbox|playstation|nintendo|steam|generic>   plugs a gamepad of that family (replacing
//   the last one) tap <button>                                        presses a button for a few
//   frames press <button> / release <button> axis <axis> <-1..1>
// Buttons: south, east, west, north, back, guide, start, left_stick, right_stick, left_shoulder,
// right_shoulder, dpad_up, dpad_down, dpad_left, dpad_right. Axes: left_x, left_y, right_x,
// right_y, left_trigger, right_trigger.
class GamepadScript {
public:
  GamepadScript(platform::Platform& platform, std::filesystem::path path)
      : platform_{&platform}, path_{std::move(path)} {}

  // Runs the lines added since the last frame and releases taps that are due.
  void frame() {
    constexpr int TapFrames = 6;
    if (gamepad_) {
      for (auto tap = taps_.begin(); tap != taps_.end();) {
        if (--tap->second <= 0) {
          platform_->setVirtualGamepadButton(*gamepad_, tap->first, false);
          tap = taps_.erase(tap);
        } else {
          ++tap;
        }
      }
    }
    for (const std::string& line : newLines()) {
      std::istringstream words{line};
      std::string verb;
      std::string name;
      words >> verb >> name;
      if (verb == "attach") {
        attach(name);
        continue;
      }
      if (!gamepad_) {
        attach("generic");
      }
      if (!gamepad_) {
        return;
      }
      const auto button = gamepadButtonNamed(name);
      if ((verb == "tap" || verb == "press") && button) {
        platform_->setVirtualGamepadButton(*gamepad_, *button, true);
        if (verb == "tap") {
          taps_.emplace_back(*button, TapFrames);
        }
      } else if (verb == "release" && button) {
        platform_->setVirtualGamepadButton(*gamepad_, *button, false);
      } else if (const auto axis = gamepadAxisNamed(name); verb == "axis" && axis) {
        float value = 0.0F;
        words >> value;
        platform_->setVirtualGamepadAxis(*gamepad_, *axis, value);
      } else {
        core::logWarning("ui demo: unknown gamepad command '{}'", line);
      }
    }
  }

private:
  void attach(std::string_view name) {
    const auto family = gamepadFamilyNamed(name);
    if (!family) {
      core::logWarning("ui demo: unknown gamepad family '{}'", name);
      return;
    }
    if (gamepad_) {
      platform_->detachVirtualGamepad(*gamepad_);
      gamepad_.reset();
      taps_.clear();
    }
    auto gamepad = platform_->attachVirtualGamepad(*family);
    if (!gamepad) {
      core::logError("ui demo: {}", gamepad.error().message);
      return;
    }
    gamepad_ = *gamepad;
    core::logInfo("ui demo: virtual gamepad attached ({})", name);
  }

  std::vector<std::string> newLines() {
    std::vector<std::string> lines;
    std::ifstream file{path_};
    if (!file) {
      return lines;
    }
    file.seekg(offset_);
    std::string line;
    while (std::getline(file, line)) {
      if (file.eof()) {
        break; // a line still being written
      }
      offset_ = file.tellg();
      if (!line.empty()) {
        lines.push_back(line);
      }
    }
    return lines;
  }

  platform::Platform* platform_;
  std::filesystem::path path_;
  std::streamoff offset_ = 0;
  std::optional<platform::GamepadId> gamepad_;
  std::vector<std::pair<platform::GamepadButton, int>> taps_;
};

// Logs where the entries of the menu's page are, for scripted clicks.
void logMenuLayout(const ui::Ui& ui) {
  constexpr std::array Entries{"continue",     "open-minimap", "open-options", "quit",
                               "minimap-mode", "minimap-size", "minimap-back", "display-mode",
                               "ui-scale",     "vsync",        "options-back"};
  std::string line;
  for (const std::string_view entry : Entries) {
    const auto center = ui.elementCenter("pause_menu", entry);
    if (center && (center->x > 0.0F || center->y > 0.0F)) {
      line += std::format(" {}@{:.0f},{:.0f}", entry, center->x, center->y);
    }
  }
  core::logInfo("ui demo: page {}:{}", ui.elementText("pause_menu", "title"), line);
}

std::string describe(const ui::UiValue& value) {
  if (const auto* flag = std::get_if<bool>(&value)) {
    return *flag ? "true" : "false";
  }
  if (const auto* number = std::get_if<double>(&value)) {
    return std::to_string(*number);
  }
  if (const auto* text = std::get_if<std::string>(&value)) {
    return *text;
  }
  return "-";
}

// What the UI costs the CPU each frame (markers, layout and recording its draws), logged every few
// seconds.
class UiCost {
public:
  void add(double seconds, std::size_t draws) {
    total_ += seconds;
    worst_ = std::max(worst_, seconds);
    draws_ = draws;
    if (++frames_ == ReportFrames) {
      constexpr double Milliseconds = 1000.0;
      core::logInfo("ui demo: ui cost {:.3f} ms per frame on average, {:.3f} ms at worst, {} draws",
                    total_ / static_cast<double>(frames_) * Milliseconds, worst_ * Milliseconds,
                    draws_);
      *this = UiCost{};
    }
  }

private:
  static constexpr int ReportFrames = 300;
  double total_ = 0.0;
  double worst_ = 0.0;
  std::size_t draws_ = 0;
  int frames_ = 0;
};

struct Settings {
  bool fullscreen = false;
  double uiScale = 1.0;
};

// Applies a menu command and publishes the resulting state. Returns false for quit.
bool apply(const ui::UiCommand& command, ui::Ui& ui, platform::Window& window, Settings& settings) {
  core::logInfo("ui demo: command {} = {}", command.name, describe(command.value));
  if (command.name == "quit") {
    return false;
  }
  if (command.name == MinimapSetting || command.name == LegacyMinimapVisibleSetting) {
    ui.setSetting(MinimapSetting, std::string{minimapModeOf(command.value)});
  } else if (command.name == "minimap_size" && std::holds_alternative<std::string>(command.value)) {
    ui.setSetting("minimap_size", command.value);
  } else if (command.name == "fullscreen" && std::holds_alternative<bool>(command.value)) {
    if (auto changed = window.setFullscreen(std::get<bool>(command.value)); changed) {
      settings.fullscreen = std::get<bool>(command.value);
    } else {
      core::logWarning("ui demo: {}", changed.error().message);
    }
    ui.setSetting("fullscreen", settings.fullscreen);
  } else if (command.name == "ui_scale" && std::holds_alternative<double>(command.value)) {
    ui.setScale(static_cast<float>(std::get<double>(command.value)));
    settings.uiScale = ui.scale();
    ui.setSetting("ui_scale", settings.uiScale);
  } else if (command.name == "vsync") {
    // The device picks its present mode when it is created; the menu keeps showing the real one.
    core::logInfo("ui demo: vsync cannot change while running yet");
    ui.setSetting("vsync", true);
  }
  return true;
}

} // namespace

int runUiDemo(const UiDemoOptions& options) {
  auto platform = platform::Platform::create();
  if (!platform) {
    core::logError("{}", platform.error().message);
    return EXIT_FAILURE;
  }
  auto window = platform::Window::create(
      {.title = "dwe ui demo", .width = options.width, .height = options.height});
  if (!window) {
    core::logError("{}", window.error().message);
    return EXIT_FAILURE;
  }
  auto device = rhi::Device::create(*window, {.validation = Validation, .vsync = true});
  if (!device) {
    core::logError("{}", device.error().message);
    return EXIT_FAILURE;
  }
  auto renderer = render::Renderer::create(**device);
  if (!renderer) {
    core::logError("{}", renderer.error().message);
    return EXIT_FAILURE;
  }
  const assets::DiskFileSystem disk;
  auto content = mountContent(disk, options.content);
  if (!content) {
    core::logError("ui demo: {}", content.error().message);
    return EXIT_FAILURE;
  }
  auto uiRenderer = ui::UiRenderer::create(**renderer);
  if (!uiRenderer) {
    core::logError("{}", uiRenderer.error().message);
    return EXIT_FAILURE;
  }
  auto texts = localization::Catalog::load(
      content->files, localization::pickLanguage(localization::availableLanguages(content->files),
                                                 platform::systemLanguage()));
  if (!texts) {
    core::logError("ui demo: {}", texts.error().message);
    return EXIT_FAILURE;
  }
  const platform::PixelSize size = window->pixelSize();
  auto ui = ui::Ui::create({
      .files = &content->files,
      .renderer = uiRenderer->get(),
      .texts = &*texts,
      .width = size.width,
      .height = size.height,
      .scale = 1.0F,
      .hotReload = true,
      .clock = [&platform] { return platform->nowSeconds(); },
      .scripts = {},
  });
  if (!ui) {
    core::logError("{}", ui.error().message);
    return EXIT_FAILURE;
  }
  for (const auto& [name, id] :
       {std::pair{"hud", "ui/hud/document"}, std::pair{"pause_menu", "ui/pause_menu/document"}}) {
    auto asset = assets::AssetId::parse(id);
    if (!asset) {
      core::logError("ui demo: {}", asset.error().message);
      return EXIT_FAILURE;
    }
    if (auto loaded = (*ui)->loadDocument(name, *asset); !loaded) {
      core::logError("ui demo: {}", loaded.error().message);
      return EXIT_FAILURE;
    }
  }
  Settings settings;
  (*ui)->setSetting(MinimapSetting, std::string{MinimapAuto});
  (*ui)->setSetting("minimap_size", std::string{"medium"});
  (*ui)->setSetting("fullscreen", settings.fullscreen);
  (*ui)->setSetting("ui_scale", settings.uiScale);
  (*ui)->setSetting("vsync", true);
  (*ui)->setMenuDocument("pause_menu");
  (*ui)->show("hud");

  const LoadedMap map = loadMap(&content->files, options.map);
  (*ui)->setMinimap(ui::minimapGridOf(map.document), map.nameKey);
  Walkers walkers{map.document};
  core::logInfo("ui demo: map {} ({})", map.document.id, map.name);

  std::optional<GamepadScript> gamepadScript;
  if (options.virtualGamepad) {
    gamepadScript.emplace(*platform, *options.virtualGamepad);
  }

  input::InputState input{input::defaultActionMap()};
  core::FixedStep step{TickSeconds, MaxFrameSeconds};
  std::vector<platform::Event> events;
  double previous = platform->nowSeconds();
  UiCost cost;
  std::string focused;
  std::string page;
  bool running = true;
  while (running) {
    if (gamepadScript) {
      gamepadScript->frame();
    }
    events.clear();
    platform->pollEvents(events);
    for (const platform::Event& event : events) {
      if (std::holds_alternative<platform::QuitRequested>(event)) {
        running = false;
      }
      if (!(*ui)->handleEvent(event)) {
        input.handle(event);
      }
    }

    const double now = platform->nowSeconds();
    const double frameSeconds = now - previous;
    previous = now;
    const int ticks = step.advance(frameSeconds);
    for (int tick = 0; tick < ticks; ++tick) {
      (*ui)->tick(input, TickSeconds);
      if (!(*ui)->isVisible("pause_menu")) {
        walkers.tick(TickSeconds);
      }
      input.endTick();
    }
    for (const ui::UiCommand& command : (*ui)->takeCommands()) {
      running = apply(command, **ui, *window, settings) && running;
    }
    if (std::string current = (*ui)->focusedElementId("pause_menu"); current != focused) {
      focused = std::move(current);
      core::logInfo("ui demo: focus {}", focused.empty() ? "-" : focused);
    }
    (*ui)->reloadChanged();
    const double uiStart = platform->nowSeconds();
    (*ui)->setMinimapMarkers(walkers.player(), walkers.partner());
    (*ui)->update();
    cost.add(platform->nowSeconds() - uiStart, (*uiRenderer)->drawCount());
    if (std::string shown =
            (*ui)->isVisible("pause_menu") ? (*ui)->elementText("pause_menu", "title") : "";
        shown != page) {
      page = std::move(shown);
      if (!page.empty()) {
        logMenuLayout(**ui);
      }
    }

    const std::array<render::Drawer*, 1> uiDrawers{uiRenderer->get()};
    const bool drawn = (*renderer)->renderFrame({
        .clearColor = {0.10F, 0.12F, 0.16F, 1.0F},
        .scene = {},
        .ui = uiDrawers,
        .overlay = nullptr,
    });
    if (!drawn) {
      platform->waitForEvents(IdleWait);
    }
  }
  // The locals go in reverse order: the UI releases its textures into the UI renderer's renderer
  // before either.
  (*device)->waitIdle();
  return EXIT_SUCCESS;
}

} // namespace dwe::app
