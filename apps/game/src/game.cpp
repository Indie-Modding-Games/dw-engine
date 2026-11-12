#include "game.hpp"

#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/character_model.hpp>
#include <dwe/audio/output.hpp>
#include <dwe/content/game_content.hpp>
#include <dwe/core/fixed_step.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/log_ring.hpp>
#include <dwe/core/message.hpp>
#include <dwe/debug/debug_ui.hpp>
#include <dwe/game/partner/field_animation.hpp>
#include <dwe/game/save/slots.hpp>
#include <dwe/input/action_map.hpp>
#include <dwe/input/input_state.hpp>
#include <dwe/input/keys.hpp>
#include <dwe/localization/catalog.hpp>
#include <dwe/platform/locale.hpp>
#include <dwe/platform/platform.hpp>
#include <dwe/platform/window.hpp>
#include <dwe/render/renderer.hpp>
#include <dwe/render/screen_fade.hpp>
#include <dwe/render/upscale_cache.hpp>
#include <dwe/rhi/device.hpp>
#include <dwe/screens/screen_settings.hpp>
#include <dwe/script/script_vm.hpp>
#include <dwe/session/session_sound.hpp>
#include <dwe/ui/ui.hpp>
#include <dwe/ui/ui_renderer.hpp>

#include <SDL3/SDL_events.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include "app_flow.hpp"
#include "autotest.hpp"
#include "battle_speed.hpp"
#include "bug_report.hpp"
#include "bug_reporter.hpp"
#include "chibi_style.hpp"
#include "common.hpp"
#include "content_mount.hpp"
#include "control_log.hpp"
#include "control_socket.hpp"
#include "dev_assets_banner.hpp"
#include "dev_tools.hpp"
#include "display_settings.hpp"
#include "field_scene.hpp"
#include "first_run_screen.hpp"
#include "frame_stats.hpp"
#include "game_audio.hpp"
#include "game_documents.hpp"
#include "graphics_options.hpp"
#include "gym_batch.hpp"
#include "hd_scenery_setting.hpp"
#include "language.hpp"
#include "map_debug_overlay.hpp"
#include "medal_figure.hpp"
#include "medal_sheet.hpp"
#include "messages.hpp"
#include "minimap_setting.hpp"
#include "movie_screen.hpp"
#include "new_game_interview.hpp"
#include "new_game_scene_rules.hpp"
#include "partner_portrait.hpp"
#include "pause_box.hpp"
#include "play_session.hpp"
#include "player_chart.hpp"
#include "remote_control.hpp"
#include "save_figure.hpp"
#include "save_indicator.hpp"
#include "screenshot.hpp"
#include "slow_frame_log.hpp"
#include "state_trace.hpp"
#include "text_font_setting.hpp"
#include "title_options.hpp"
#include "title_screen.hpp"
#include "ui_skin_setting.hpp"
#include "user_settings.hpp"
#include "versus_screen.hpp"
#include "volume_setting.hpp"
#include "walk_control.hpp"

namespace dwe::app {
namespace {

// Smaller changes of the map change's fade are not worth telling the UI.
constexpr double FadeEpsilon = 1e-4;

// Tells the HUD what the map change shows: how much the fade takes away and the key of the map name
// it shows, which the HUD writes in the player's language.
class MapChangeView {
public:
  explicit MapChangeView(ui::Ui& ui) : ui_(&ui) {
    ui_->setSetting(ScreenFadeSetting, fade_);
    ui_->setSetting(MapTitleKeySetting, titleKey_);
  }

  // Publishes the field's fade and map name key when they changed.
  void show(const FieldScene& scene, float fade) {
    showFade(fade);
    const auto named = scene.field().mapNameShown();
    showTitleKey(named ? scene.nameKeyOf(*named) : std::string{});
  }

private:
  void showFade(double fade) {
    if (std::abs(fade - fade_) > FadeEpsilon) {
      fade_ = fade;
      ui_->setSetting(ScreenFadeSetting, fade_);
    }
  }

  void showTitleKey(std::string titleKey) {
    if (titleKey != titleKey_) {
      titleKey_ = std::move(titleKey);
      ui_->setSetting(MapTitleKeySetting, titleKey_);
    }
  }

  ui::Ui* ui_;
  double fade_ = 0.0;
  std::string titleKey_;
};

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

// Applies a command of the UI's documents and publishes the resulting state. Returns false for
// quit.
bool apply(const ui::UiCommand& command, ui::Ui& ui, platform::Window& window, rhi::Device& device,
           Settings& settings, DisplayChanger& display, LanguageChanger& language,
           TextFontChanger& textFont, UiSkinChanger& skin, MinimapChanger& minimap) {
  core::logInfo("game: command {} = {}", command.name, describe(command.value));
  if (command.name == "quit") {
    return false;
  }
  if (command.name == "open_menu") {
    ui.show(MenuDocument, true);
  } else if (command.name == "open_evolutions") {
    // The compact partner window's Evoluciones tab (ui/partner_menu): the evolution tree lives on
    // the pause menu's player page, so this hides the partner and hands the way in over. The
    // one-shot setting tells the document to open on the tree and to go back here on Cancel
    // (pause_menu/logic.lua: on_show, back).
    ui.setSetting("pause_menu.evolutions", true);
    ui.hide("partner_menu");
    ui.show(MenuDocument, true);
  } else if (command.name == "pause_menu.evolutions" &&
             std::holds_alternative<bool>(command.value)) {
    // The document consumed its one-shot: the next plain pause opens as usual.
    ui.setSetting("pause_menu.evolutions", std::get<bool>(command.value));
  } else if (command.name == LanguageCommand &&
             std::holds_alternative<std::string>(command.value)) {
    language.change(std::get<std::string>(command.value));
  } else if (command.name == MinimapSetting || command.name == LegacyMinimapVisibleSetting) {
    ui.setSetting(MinimapSetting, std::string{minimap.change(command.value)});
  } else if (command.name == MinimapSizeSetting) {
    ui.setSetting(MinimapSizeSetting, std::string{display.changeMinimapSize(command.value)});
  } else if (command.name == FullscreenSetting && std::holds_alternative<bool>(command.value)) {
    if (auto changed = window.setFullscreen(std::get<bool>(command.value)); changed) {
      settings.fullscreen = std::get<bool>(command.value);
      display.keepFullscreen(settings.fullscreen);
    } else {
      core::logWarning("game: {}", changed.error().message);
    }
    ui.setSetting(FullscreenSetting, settings.fullscreen);
  } else if (command.name == UiScaleSetting && std::holds_alternative<double>(command.value)) {
    ui.setScale(static_cast<float>(std::get<double>(command.value)));
    settings.uiScale = ui.scale();
    display.keepUiScale(ui.scale());
    ui.setSetting(UiScaleSetting, settings.uiScale);
  } else if (command.name == ui::TextFontSetting &&
             std::holds_alternative<std::string>(command.value)) {
    textFont.change(std::get<std::string>(command.value));
  } else if (command.name == ui::UiSkinSetting &&
             std::holds_alternative<std::string>(command.value)) {
    skin.change(std::get<std::string>(command.value));
  } else if (command.name == VsyncSetting && std::holds_alternative<bool>(command.value)) {
    // The swapchain is rebuilt with the new present mode before the next frame.
    settings.vsync = std::get<bool>(command.value);
    device.setVsync(settings.vsync);
    display.keepVsync(settings.vsync);
    core::logInfo("game: vsync {}", settings.vsync ? "on" : "off");
    ui.setSetting(VsyncSetting, settings.vsync);
  }
  return true;
}

core::Result<void> loadDocuments(ui::Ui& ui) {
  for (const auto& [name, id] : GameDocuments) {
    auto asset = assets::AssetId::parse(id);
    if (!asset) {
      return std::unexpected{asset.error()};
    }
    if (auto loaded = ui.loadDocument(name, *asset); !loaded) {
      return loaded;
    }
  }
  // Warns about a skin's classes for a document the game does not load.
  static_cast<void>(ui.checkSkinDocuments());
  return {};
}

// The mouse on the ground (R2.2): a click walks to the point around walls (click-to-move), and
// holding the button walks towards the pointer for as long as it is held, like a stick, which also
// reaches exits at the edge of the screen that no click can point at.
class MouseSteering {
public:
  // Every move of the pointer, also those the UI takes: the UI keeps the first move after a resize
  // for itself, and a click there must still land where the pointer is, not where it was before.
  void pointerMoved(const platform::MouseMoved& moved) { pointer_ = {moved.x, moved.y}; }

  // Takes the button events the UI did not. `orders` says whether a press is a walk order now
  // (PlaySession::walksByPointer): a press while a textbox, a scene or a screen holds the player is
  // theirs (the confirm that closes a page), and neither walks to where it fell nor steers while
  // held, then or once they let go.
  void handle(const platform::Event& event, const FieldScene& scene, bool orders) {
    if (const auto* button = std::get_if<platform::MouseButtonChanged>(&event);
        button != nullptr && button->button == platform::MouseButton::Left) {
      held_ = button->down && orders;
      heldTicks_ = 0;
      // The release keeps the press's target: a click is a press and a release in the same frame.
      if (button->down) {
        target_ = held_ ? scene.pickGround(pointer_) : std::nullopt;
      }
    }
  }

  // The click-to-move target set since the last call.
  std::optional<glm::vec3> take() { return std::exchange(target_, std::nullopt); }

  // Once per logic tick: while the button has been held for a moment, the direction towards the
  // pointer, as a stick (game::field::FieldInput).
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
    // At the edge of the window the player walks off that edge, even past the pointer: exits are
    // often there, under ground that the camera never shows.
    const float band = EdgeBand * std::min(windowSize.x, windowSize.y);
    const bool atEdge = pointer_.x < band || pointer_.y < band ||
                        pointer_.x > windowSize.x - band || pointer_.y > windowSize.y - band;
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

  // Whether a click that walks to `target` also presses confirm where the player stands: only near
  // him (on him, the machine or the character in front of him). A click farther away only walks,
  // or walking off a confirm cell (a save machine) would ask its question again with every click.
  static bool pressesInPlace(glm::vec3 target, glm::vec3 player) {
    return std::hypot(target.x - player.x, target.z - player.z) <= PressRadius;
  }

private:
  // Two cells of the collision grid.
  static constexpr float PressRadius = 200.0F;
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

// Makes a catalog the one localization::tr reads while it lives, and clears it before the catalog
// goes.
class CatalogInUse {
public:
  explicit CatalogInUse(const localization::Catalog& texts) { localization::useCatalog(&texts); }
  CatalogInUse(const CatalogInUse&) = delete;
  CatalogInUse& operator=(const CatalogInUse&) = delete;
  CatalogInUse(CatalogInUse&&) = delete;
  CatalogInUse& operator=(CatalogInUse&&) = delete;
  ~CatalogInUse() { localization::useCatalog(nullptr); }
};

// The desktop's own pointer and keyboard; the gamepad is not among them, as an autotest's is a
// virtual one.
bool isPointerOrKeyEvent(const SDL_Event& event) {
  switch (event.type) {
  case SDL_EVENT_KEY_DOWN:
  case SDL_EVENT_KEY_UP:
  case SDL_EVENT_TEXT_INPUT:
  case SDL_EVENT_MOUSE_MOTION:
  case SDL_EVENT_MOUSE_BUTTON_DOWN:
  case SDL_EVENT_MOUSE_BUTTON_UP:
  case SDL_EVENT_MOUSE_WHEEL:
    return true;
  default:
    return false;
  }
}

// Where main.c calls playMovie for each moment.
int movieCallSite(MovieMoment moment) {
  switch (moment) {
  case MovieMoment::Boot:
    return 3427;
  case MovieMoment::NewGame:
    return 3442;
  case MovieMoment::ClearedLoad:
    return 3476;
  case MovieMoment::Ending:
    return 3527;
  }
  return 0;
}

bool isPointerOrKey(const platform::Event& event) {
  return std::holds_alternative<platform::KeyChanged>(event) ||
         std::holds_alternative<platform::MouseMoved>(event) ||
         std::holds_alternative<platform::MouseButtonChanged>(event) ||
         std::holds_alternative<platform::MouseWheelMoved>(event);
}

// A route's load_save: the save copied as slot_01 into a folder of its own, which the run uses as
// its saves folder, so the run loads it as any slot and never writes next to the original.
core::Result<std::filesystem::path> stageRouteSave(const std::filesystem::path& save) {
  const std::filesystem::path folder =
      std::filesystem::temp_directory_path() /
      std::format("dwe-route-save-{}", std::chrono::steady_clock::now().time_since_epoch().count());
  std::error_code error;
  std::filesystem::create_directories(folder, error);
  if (!error) {
    std::filesystem::copy_file(
        save, folder / game::save::slotFileName({.kind = game::save::SlotKind::Manual, .index = 1}),
        std::filesystem::copy_options::overwrite_existing, error);
  }
  if (error) {
    return core::fail(std::format("load_save {}: {}", save.string(), error.message()));
  }
  return folder;
}

// Ends the process once the game is torn down when the sound device's open is still stuck (a sound
// server that never answered): SDL_Quit would wait for that thread forever, and so would the
// process's exit, so the platform is never shut down and the process ends at once with its code.
class StuckAudioExit {
public:
  explicit StuckAudioExit(const int& exitCode) : exitCode_{&exitCode} {}
  StuckAudioExit(const StuckAudioExit&) = delete;
  StuckAudioExit& operator=(const StuckAudioExit&) = delete;
  StuckAudioExit(StuckAudioExit&&) = delete;
  StuckAudioExit& operator=(StuckAudioExit&&) = delete;
  ~StuckAudioExit() {
    if (!audio::deviceOpenStuck()) {
      return;
    }
    core::logWarning("{}", messages::AudioStuckAtExit);
    std::fflush(stdout);
    std::fflush(stderr);
    std::_Exit(*exitCode_);
  }

private:
  const int* exitCode_;
};

// Where a frame's time went, in the order the loop runs them; the slow frame's line names them.
class FramePhases {
public:
  enum Phase : std::uint8_t { Ticks, Update, Ui, Render, Count };

  void start(double now) {
    last_ = now;
    seconds_.fill(0.0);
  }
  void mark(Phase phase, double now) {
    seconds_.at(phase) = now - last_;
    last_ = now;
  }
  [[nodiscard]] std::string text() const {
    return std::format("ticks {:.1f} ms, update {:.1f} ms, ui {:.1f} ms, render {:.1f} ms",
                       seconds_.at(Ticks) * 1000.0, seconds_.at(Update) * 1000.0,
                       seconds_.at(Ui) * 1000.0, seconds_.at(Render) * 1000.0);
  }

private:
  double last_ = 0.0;
  std::array<double, Count> seconds_{};
};

} // namespace

int runGame(const Options& requested) {
  // The log's last lines, for the bug reports.
  const std::shared_ptr<core::LogRing> logTail = core::keepLogTail(ReportLogLines);
  Options options = requested;
  auto platform = platform::Platform::create();
  if (!platform) {
    core::logError("{}", platform.error().message);
    return EXIT_FAILURE;
  }
  // The process's exit code, for StuckAudioExit; the error returns leave it a failure.
  int exitCode = EXIT_FAILURE;
  // Before the platform shuts SDL down: a sound device open that never returned would make it wait
  // forever.
  const StuckAudioExit stuckAudioExit{exitCode};
  audio::Audio sound = openGameAudio();
  // The saves folder a route's load_save made, removed when the run ends.
  std::optional<std::filesystem::path> routeSaves;
  // After the platform, so its virtual gamepad is unplugged before the platform goes.
  std::unique_ptr<Autotest> autotest;
  if (options.autotest) {
    auto loaded = Autotest::load(*options.autotest, options.shots, options.skipSteps);
    if (!loaded) {
      core::logError("autotest: {}", loaded.error().message);
      return EXIT_FAILURE;
    }
    autotest = std::move(*loaded);
    // Like the opening's questions and the movies, the opening's chain is left out of an
    // autotest: its routes start on a free field, unless one says new_game: original
    // (--new-game-chain still wins).
    if (!options.newGameChain) {
      options.newGameChain = autotest->newGameChain().value_or(session::NewGameChain::Seeded);
    }
    if (const auto& save = autotest->loadSave()) {
      auto staged = stageRouteSave(*save);
      if (!staged) {
        core::logError("autotest: {}", staged.error().message);
        return EXIT_FAILURE;
      }
      core::logInfo("autotest: loading {}", save->string());
      routeSaves = *staged;
      options.saves = *staged;
      options.load = game::save::SlotId{.kind = game::save::SlotKind::Manual, .index = 1};
      options.newGame = false;
    }
  }
  std::optional<StateTrace> trace;
  if (options.trace) {
    auto opened = StateTrace::open(*options.trace);
    if (!opened) {
      core::logError("trace: {}", opened.error().message);
      return EXIT_FAILURE;
    }
    trace = std::move(*opened);
  }
  // The debug control channel (remote_control.hpp); nothing of it exists without --control.
  std::unique_ptr<ControlSocket> controlSocket;
  // The warnings and errors its log_tail gives, kept from here on.
  std::shared_ptr<ControlLog> controlLog;
  if (options.control) {
    controlLog = keepControlLog();
    auto opened = ControlSocket::open(*options.control);
    if (!opened) {
      core::logError("{}", opened.error().message);
      return EXIT_FAILURE;
    }
    controlSocket = std::move(*opened);
  }
  // The saves folder the control channel's last load made, removed when the game ends.
  struct StagedSaves {
    std::optional<std::filesystem::path> folder;
    StagedSaves() = default;
    StagedSaves(const StagedSaves&) = delete;
    StagedSaves& operator=(const StagedSaves&) = delete;
    StagedSaves(StagedSaves&&) = delete;
    StagedSaves& operator=(StagedSaves&&) = delete;
    ~StagedSaves() {
      if (folder) {
        std::error_code ignored;
        std::filesystem::remove_all(*folder, ignored);
      }
    }
  } controlSaves;
  // An autotest neither reads nor writes the player's settings: its language is --lang or the
  // system's. Nor does a game driven by the control channel, which a tool starts next to the
  // player's own.
  const std::optional<std::filesystem::path> settingsFile =
      autotest || options.control ? std::nullopt : userSettingsFile();
  UserSettings userSettings = settingsFile ? loadUserSettings(*settingsFile) : UserSettings{};
  // Read before the window opens: its mode, vsync and the UI's scale are the player's from the
  // first frame (display_settings.hpp).
  const DisplayStart display = startDisplay(options.fullscreen, options.vsync, userSettings);
  DisplayChanger displayChanger{userSettings, settingsFile};
  auto window =
      platform::Window::create({.title = "dwe", .width = options.width, .height = options.height});
  if (!window) {
    core::logError("{}", window.error().message);
    return EXIT_FAILURE;
  }
  Settings settings{.fullscreen = false, .uiScale = 1.0, .vsync = display.vsync};
  if (display.fullscreen) {
    if (auto changed = window->setFullscreen(true); changed) {
      settings.fullscreen = true;
    } else {
      core::logWarning("game: {}", changed.error().message);
    }
  }
  auto device = rhi::Device::create(*window, {.validation = Validation, .vsync = display.vsync});
  if (!device) {
    core::logError("{}", device.error().message);
    return EXIT_FAILURE;
  }
  core::logInfo("game: validation layer {}, vsync {}",
                validationState((*device)->validationEnabled()), display.vsync ? "on" : "off");
  // The first start asks for the player's disc and reads it before anything mounts the content
  // (first_run_screen.hpp).
  if (needsFirstRun(options)) {
    auto installed = runFirstRun(*platform, *window, **device, options.language);
    if (!installed) {
      core::logError("game: {}", installed.error().message);
      return EXIT_FAILURE;
    }
    if (!*installed) {
      core::logInfo("game: the first start ended without a disc");
      exitCode = EXIT_SUCCESS;
      return exitCode;
    }
    options.content = **installed;
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
  auto content = mountGameContent(options);
  if (!content) {
    core::logError("game: {}", content.error().message);
    return EXIT_FAILURE;
  }
  const assets::VirtualFileSystem& files = (*content)->files();
  if (options.tuning) {
    (*debugUi)->openTuning();
  }
  const std::vector<std::string> languages = localization::availableLanguages(files);
  auto language =
      startLanguage(languages, options.language, userSettings.language, platform::systemLanguage());
  if (!language) {
    core::logError("game: {}", language.error().message);
    return EXIT_FAILURE;
  }
  auto texts = localization::Catalog::load(files, *language);
  if (!texts) {
    core::logError("game: {}", texts.error().message);
    return EXIT_FAILURE;
  }
  const CatalogInUse catalogInUse{*texts};
  auto uiRenderer = ui::UiRenderer::create(**renderer);
  if (!uiRenderer) {
    core::logError("{}", uiRenderer.error().message);
    return EXIT_FAILURE;
  }
  // The enhanced graphics: the saved choices, with --graphics on top for this run only.
  GraphicsOptions startGraphics = startGraphicsOptions(userSettings);
  if (options.graphics) {
    startGraphics = withGraphicsFlag(startGraphics, *options.graphics).value_or(startGraphics);
  }
  // HD scenery's and the models' upscales, kept under the user's data like the settings (not by
  // autotests).
  std::optional<std::filesystem::path> upscaleFolder;
  if (settingsFile) {
    if (const auto data =
            platform::userDataFolder(content::UserDataOrganization, content::UserDataApplication)) {
      upscaleFolder = *data / "cache" / "upscale";
    }
  }
  const auto upscaleCache = std::make_shared<const render::UpscaleCache>(upscaleFolder);
  // The menus' 3D figures take the models' texture filter and the cache before their first model.
  const auto prepareFigure = [&](render::ModelRenderer& models) {
    prepareModels(models, startGraphics, upscaleCache);
  };
  auto portrait = PartnerPortrait::create(**renderer);
  if (portrait) {
    prepareFigure((*portrait)->models());
    if (auto bound = (*uiRenderer)->bindTexture("runtime/partner-portrait", (*portrait)->texture());
        !bound) {
      core::logError("Status portrait: {}", bound.error().message);
      return EXIT_FAILURE;
    }
  } else {
    core::logWarning("Status portrait: {}", portrait.error().message);
  }
  const auto raiseId = assets::AssetId::parse("data/raise");
  auto portraitRaise = assets::loadRaiseTable(files, *raiseId);
  if (!portraitRaise)
    core::logWarning("Status portrait: {}", portraitRaise.error().message);
  std::string portraitSpecies;
  // The partner's animation names (models/partner.yaml): the same clip ids for every species.
  auto portraitClips = assets::loadCharacterModel(files, *assets::AssetId::parse("models/partner"));
  if (!portraitClips)
    core::logWarning("Status portrait: {}", portraitClips.error().message);
  auto playerPortrait = PartnerPortrait::create(**renderer);
  if (playerPortrait) {
    prepareFigure((*playerPortrait)->models());
    if (auto bound =
            (*uiRenderer)->bindTexture("runtime/player-portrait", (*playerPortrait)->texture());
        !bound) {
      core::logError("Player portrait: {}", bound.error().message);
      return EXIT_FAILURE;
    }
    if (const auto* placement = portraitRaise ? portraitRaise->find("boys") : nullptr; placement) {
      if (auto loaded = (*playerPortrait)->load(files, "boys", *placement); !loaded)
        core::logWarning("Player portrait: {}", loaded.error().message);
    } else {
      core::logWarning("Player portrait: missing tamer placement in raise table");
    }
  } else {
    core::logWarning("Player portrait: {}", playerPortrait.error().message);
  }
  // The save indicator's figure (save_indicator.hpp), from the player's content; without it the
  // indicator shows its text alone.
  auto saveFigure = SaveFigure::create(**renderer);
  if (saveFigure) {
    prepareFigure((*saveFigure)->models());
    if (auto bound =
            (*uiRenderer)->bindTexture(std::string{SaveFigureTexture}, (*saveFigure)->texture());
        !bound) {
      core::logError("Save figure: {}", bound.error().message);
      return EXIT_FAILURE;
    }
    if (auto loaded = (*saveFigure)->load(files); !loaded) {
      core::logWarning("Save figure: {}", loaded.error().message);
    }
  } else {
    core::logWarning("Save figure: {}", saveFigure.error().message);
  }
  // The medal that turns on the medals page (medal_figure.hpp); without it the page shows none.
  auto medalFigure = MedalFigure::create(**renderer);
  if (medalFigure) {
    prepareFigure((*medalFigure)->models());
    if (auto bound =
            (*uiRenderer)->bindTexture(std::string{MedalFigureTexture}, (*medalFigure)->texture());
        !bound) {
      core::logError("Medal figure: {}", bound.error().message);
      return EXIT_FAILURE;
    }
    if (auto loaded = (*medalFigure)->load(files); !loaded) {
      core::logWarning("Medal figure: {}", loaded.error().message);
    }
  } else {
    core::logWarning("Medal figure: {}", medalFigure.error().message);
  }
  // The map change's fade, subtracted from the scene before the UI is drawn (fade.c: renderFade).
  std::optional<render::ScreenFade> screenFade;
  if (auto fade = render::ScreenFade::create(**renderer)) {
    screenFade.emplace(std::move(*fade));
  } else {
    core::logError("{}", fade.error().message);
    return EXIT_FAILURE;
  }
  // The original's movies (docs/architecture/peliculas.md). None in an autotest, whose routes start
  // where they say, unless its route asks for them (movies: true).
  std::optional<MovieRules> movieRules;
  std::unique_ptr<MovieScreen> movies;
  if (!autotest || autotest->playsMovies()) {
    movieRules = loadMovieRules(files, options.ruleset);
    if (auto screen = MovieScreen::create(**renderer, sound); screen && movieRules) {
      movies = std::move(*screen);
    } else if (!screen) {
      core::logWarning("movies: {}", screen.error().message);
    }
  }
  // The new game's opening questions before its movie (main.c: newGameScene); none in an autotest,
  // as the movies, unless its route asks for them.
  const std::optional<NewGameSceneRules> newGameScene =
      autotest && !autotest->asksNewGameScene() ? std::nullopt
                                                : loadNewGameSceneRules(files, options.ruleset);
  // The opening plays the system sounds before any game exists (sound.c: playSound, bank 0).
  session::SessionSound openingSound{sound, files};
  const platform::PixelSize size = window->pixelSize();
  auto ui = ui::Ui::create({
      .files = &files,
      .renderer = uiRenderer->get(),
      .texts = &*texts,
      .width = size.width,
      .height = size.height,
      .scale = display.uiScale,
      .hotReload = options.tuning,
      .clock = [&platform] { return platform->nowSeconds(); },
      .scripts = {},
      // The current skin installs its own mouse cursor as the system pointer (so it stays on top of
      // every screen); a skin without a cursor returns the arrow to the system.
      .cursor =
          [&platform](std::optional<ui::UiCursor> cursor) {
            if (!cursor) {
              platform->clearColorCursor();
              return;
            }
            if (auto set = platform->setColorCursor(
                    std::span<const std::byte>{cursor->image.pixels},
                    static_cast<int>(cursor->image.width), static_cast<int>(cursor->image.height),
                    static_cast<int>(cursor->hotX), static_cast<int>(cursor->hotY));
                !set) {
              core::logWarning("skin cursor: {}", set.error().message);
            }
          },
  });
  if (!ui) {
    core::logError("{}", ui.error().message);
    return EXIT_FAILURE;
  }
  settings.uiScale = (*ui)->scale();
  // La caja de nombres del prólogo oculta sus documentos en su destructor: debe destruirse
  // antes que la UI (los locales van en orden inverso).
  std::unique_ptr<NewGameInterview> interview;
  // The skin goes on before the documents load, so they are styled once.
  applyStartUiSkin(**ui, startUiSkin(options.uiSkin, userSettings.uiSkin));
  UiSkinChanger uiSkinChanger{**ui, userSettings, settingsFile};
  if (auto loaded = loadDocuments(**ui); !loaded) {
    core::logError("game: {}", loaded.error().message);
    return EXIT_FAILURE;
  }
  loadPlayerChart(**ui, files);
  loadPlayerMedalsAndCards(**ui, files);
  LanguageChanger languageChanger{*texts, **ui, userSettings, settingsFile};
  MenuSounds menuSounds{sound, files};
  PauseBox pauseBox;
  (*ui)->setSetting(LanguageSetting, texts->language());
  // The original look's title screen asks for the language before each "Press Start" screen
  // (title.language_first; an addition, as a remaster's); an autotest only when its route asks.
  (*ui)->setSetting("title.language_first", autotest == nullptr || autotest->asksLanguageFirst());
  MinimapChanger minimapChanger{userSettings, settingsFile};
  GraphicsChanger graphics{userSettings, settingsFile, startGraphics};
  // The title's pictures, smoothed with the texture filter «upscaled», keep what the upscaler made
  // where the models' textures do.
  (*ui)->setUpscaleCache(upscaleCache);
  graphics.publish(**ui);
  // The music's and the effects' volumes: the saved steps, applied to the audio from the start.
  VolumeChanger volumes{userSettings, settingsFile, sound};
  volumes.publish(**ui);
  HdSceneryChanger hdScenery{userSettings, settingsFile};
  WalkControlChanger walkControl{userSettings, settingsFile};
  ChibiModeChanger chibiMode{userSettings, settingsFile};
  (*ui)->setSetting(ChibiStyleSetting, std::string{chibiModeName(chibiMode.mode())});
  // An autotest walks with the device of its route (it reads no settings file).
  if (autotest) {
    walkControl.use(autotest->walksWithMouse() ? WalkControl::Mouse : WalkControl::Keys, nullptr);
  }
  (*ui)->setSetting(WalkControlSetting, std::string{walkControlName(walkControl.control())});
  // Quick battles (battle_speed.hpp): the saved speed; an autotest reads none, so it fights at the
  // original's.
  BattleSpeedChanger battleSpeed{userSettings, settingsFile};
  (*ui)->setSetting(BattleSpeedSetting, std::string{battleSpeedName(battleSpeed.speed())});
  std::optional<bool> battleFastShown;
  // Batch training (gym_batch.hpp): the saved number of sessions; an autotest reads none, so it
  // trains one session a course, as the original.
  const auto gymSessions = [&userSettings] {
    return std::clamp(userSettings.gymSessions.value_or(GymSessionsMin), GymSessionsMin,
                      GymSessionsMax);
  };
  (*ui)->setSetting(GymSessionsSetting, static_cast<double>(gymSessions()));
  (*ui)->setSetting(DifficultySetting,
                    userSettings.difficulty.value_or(std::string{DifficultyNormal}));
  (*ui)->setSetting(AutosaveSetting, userSettings.autosave.value_or(true));
  // The options only a game applies, for the title's options page (title_options.hpp).
  publishGameToggles(**ui, userSettings);
  (*ui)->setSetting(MinimapSetting, std::string{startMinimapMode(userSettings.minimap)});
  (*ui)->setSetting(MinimapSizeSetting, std::string{display.minimapSize});
  (*ui)->setSetting(WeatherSetting, userSettings.weather.value_or(true));
  (*ui)->setSetting(SeasonsSetting, userSettings.seasons.value_or(true));
  (*ui)->setSetting(HdScenerySetting, hdScenery.on());
  (*ui)->setSetting(FullscreenSetting, settings.fullscreen);
  (*ui)->setSetting(UiScaleSetting, settings.uiScale);
  (*ui)->setSetting(VsyncSetting, settings.vsync);
  (*ui)->setSetting(SaveIndicatorFigureSetting, saveFigure && (*saveFigure)->loaded());
  TextFontChanger textFontChanger{**ui, userSettings, settingsFile};
  if (const auto font = startTextFont(options.textFont, userSettings.textFont)) {
    (*ui)->setTextFont(*font);
  }
  (*ui)->setSetting(ui::TextFontSetting, std::string{ui::textFontName((*ui)->textFont())});
  MapChangeView mapChangeView{**ui};
  (*ui)->setMenuDocument(MenuDocument);
  if (options.devAssets) {
    if (auto shown = showDevAssetsBanner(**ui); !shown) {
      core::logError("game: {}", shown.error().message);
      return EXIT_FAILURE;
    }
  }
  loadUserGamepadMappings();

  input::InputState input{input::defaultActionMap()};
  auto scripts = script::ScriptVm::create();
  if (!scripts) {
    core::logError("{}", scripts.error().message);
    return EXIT_FAILURE;
  }
  (*scripts)->bindInput(input);
  (*scripts)->bindTexts(*texts);
  // The console's text_font("original"|"modern"), the main menu's setting; applied between frames,
  // like a menu's command, so the UI never changes fonts while a frame draws it.
  std::optional<std::string> consoleTextFont;
  (*scripts)->bindCommand(ui::TextFontSetting, [&consoleTextFont](std::string_view name) {
    consoleTextFont = std::string{name};
  });
  // The console's and the routes' hd_scenery("on"|"off"), the options row's setting, applied
  // between frames in the same way.
  std::optional<bool> consoleHdScenery;
  (*scripts)->bindCommand(HdScenerySetting, [&consoleHdScenery](std::string_view value) {
    consoleHdScenery = value != "off";
  });
  // The console's and the routes' scenery_quality("smooth"|"upscaled"), antialiasing("on"|"off")
  // and texture_filter("nearest"|"smooth"|"upscaled"), the sharpness settings
  // (graphics_options.hpp), applied between frames in the same way.
  std::vector<ui::UiCommand> consoleGraphics;
  for (const std::string_view name : {SceneryQualitySetting, TextureFilterSetting}) {
    (*scripts)->bindCommand(name, [&consoleGraphics, name](std::string_view value) {
      consoleGraphics.push_back({.name = std::string{name}, .value = std::string{value}});
    });
  }
  (*scripts)->bindCommand(AntialiasingSetting, [&consoleGraphics](std::string_view value) {
    consoleGraphics.push_back(
        {.name = std::string{AntialiasingSetting}, .value = ui::UiValue{value != "off"}});
  });
  // The console's and the routes' battle_speed("normal"|"key"|"fast"), the options row's setting
  // (battle_speed.hpp), applied between frames in the same way.
  std::optional<std::string> consoleBattleSpeed;
  (*scripts)->bindCommand(BattleSpeedSetting, [&consoleBattleSpeed](std::string_view value) {
    consoleBattleSpeed = std::string{value};
  });
  // The console's ui_skin("<id>"), the menus' skin setting, applied between frames in the same way.
  std::optional<std::string> consoleUiSkin;
  (*scripts)->bindCommand(ui::UiSkinSetting, [&consoleUiSkin](std::string_view id) {
    consoleUiSkin = std::string{id};
  });

  // The field map's debug overlay (map_debug_overlay.hpp): F3 (debug_map_overlay), the console's
  // map_overlay("on"|"off"|"toggle") and the control channel's overlay turn it on and off.
  bool mapOverlay = false;
  debug::OverlayShapes mapOverlayShapes;
  const auto showMapOverlay = [&mapOverlay](std::optional<bool> on) {
    mapOverlay = on.value_or(!mapOverlay);
    core::logInfo("game: map overlay {}", mapOverlay ? "on" : "off");
    return mapOverlay;
  };
  (*scripts)->bindCommand(MapOverlayCommand, [showMapOverlay](std::string_view value) {
    showMapOverlay(value == "on"    ? std::optional<bool>{true}
                   : value == "off" ? std::optional<bool>{false}
                                    : std::nullopt);
  });

  SessionCommands sessionCommands;
  sessionCommands.bind(**scripts);
  // The partner's status for the autotest's checks, refreshed every frame of a run.
  screens::ScreenSettings partnerStatus;

  AppFlow<PlaySession> flow{[&](const PlayStart& start) {
    return PlaySession::create({.options = &options,
                                .keepsSaves = !autotest,
                                .files = &files,
                                .device = device->get(),
                                .renderer = renderer->get(),
                                .ui = ui->get(),
                                .sound = &sound,
                                .debugUi = debugUi->get(),
                                .userSettings = &userSettings,
                                .settingsFile = settingsFile,
                                .upscaleCache = upscaleCache},
                               start);
  }};
  TitleScreen title{files, options, !autotest, **ui};
  // «Versus en línea» from the title's menu (versus_screen.hpp): no game behind it.
  VersusScreen versus{files, options, !autotest, **ui, renderer->get(), &sound};
  // A new game asks its rules (an addition); an autotest only when its route asks.
  title.setAsksRules(autotest == nullptr || autotest->asksNewGameRules());
  // What waits for the movie that plays: the title, or a game to start (main.c: main).
  std::optional<PlayStart> startAfterMovie;
  // The new game that waits for the opening's questions.
  std::optional<PlayStart> startAfterInterview;
  bool titleAfterMovie = false;
  bool moviePlayed = false;
  // The ticks the "Press Start" screen has shown, and whether the boot movie plays again after them
  // (runLandingScreen: a countdown from its start that only Start leaves, whatever else is pressed;
  // the main menu, its slot lists and the language box never go back to the movie); not when it
  // could not play.
  std::uint32_t titleIdleTicks = 0;
  // The movie that plays as soon as the current one is over (movieAfter: the boot movie after the
  // ending's), before the title.
  std::optional<MovieMoment> nextMovie;
  bool replayBootMovie = true;
  const auto playMovie = [&](MovieMoment moment) {
    if (!movies) {
      return false;
    }
    // The music stops before a movie and starts again after it (main.c:3441 finalizeMusic,
    // playMovie, initializeMusic): a game playing under the movie holds its music until afterMovie.
    core::logDebug("[port:movie] playMovie (main.c:{}): movie {} '{}'", movieCallSite(moment),
                   static_cast<int>(moment), movieRules->of(moment).file);
    movies->setFillWindow(userSettings.widescreenPictures.value_or(WidescreenPicturesDefault));
    if (!movies->play(files, movieRules->of(moment))) {
      return false;
    }
    if (PlaySession* play = flow.playing()) {
      play->holdMusicForMovie(true);
    }
    title.hide();
    moviePlayed = true;
    return true;
  };
  // The game starts at the title screen, with no game; --new-game, --load, --start and an autotest
  // whose route does not ask for the main menu start playing.
  if ((autotest && !autotest->startsAtMainMenu()) || options.newGame || options.load ||
      options.startMap) {
    sessionCommands.drop(autotest.get());
    if (auto started = flow.play({.load = options.load,
                                  .backup = false,
                                  .saveTo = std::nullopt,
                                  .choice = autotest ? autotest->newGameNames() : std::nullopt});
        !started) {
      core::logError("game: {}", started.error().message);
      return EXIT_FAILURE;
    }
    if (const auto moment = flow.playing()->takeMovieRequest()) {
      playMovie(*moment);
    }
  } else if (playMovie(MovieMoment::Boot)) {
    titleAfterMovie = true;
  } else if (autotest && !autotest->startsAtBootScreen()) {
    // A route starts at the main menu itself, unless it asks for the screen before it.
    title.show();
  } else {
    title.showBoot();
  }

  BugReporter bugReporter{bugReportsFolder(options), logTail};
  // Whether the player asked for a bug report this frame (report_bug).
  bool reportAsked = false;

  FrameStats frameStats;
  SlowFrameLog slowFrames;
  MinimapCover minimapCover;
  AutotestHost host{
      .platform = &*platform,
      .window = &*window,
      .renderer = renderer->get(),
      .device = device->get(),
      .scene = nullptr,
      .ui = ui->get(),
      .input = &input,
      .stats = &frameStats,
      .scripts = scripts->get(),
      .partner = &partnerStatus,
      .story = nullptr,
      .saves = options.saves,
  };

  core::FixedStep step{TickSeconds, MaxFrameSeconds};
  FpsCounter fps;
  MouseSteering mouse;
  std::vector<platform::Event> events;
  std::uint64_t ticksRun = 0;
  double previous = platform->nowSeconds();
  // Where the last frame's time went, for the slow frame's line: the ticks, the session's update,
  // the UI's layout and the renderer.
  FramePhases phases;
  bool running = true;
  std::optional<PlayStart> loadAfterIndicator;
  double loadingIndicatorSeconds = 0.0;
  bool loadingIndicator = false;
  debug::FrameStats overlayStats{};
  // Built once; the frame takes it by reference, so drawing it allocates nothing.
  const auto drawOverlay = [&](const rhi::Frame& frame) {
    (*debugUi)->draw(frame, overlayStats, input, **scripts, {.x = 0.0F, .y = 0.0F, .radius = 0.0F});
  };
  const auto startPlaying = [&](const PlayStart& start) {
    sessionCommands.drop(autotest.get());
    if (auto started = flow.play(start); !started) {
      core::logError("game: {}", started.error().message);
      title.startFailed(start, flow.saveFailed());
    } else {
      // The load is not game time: the next frame must not run it as a burst of ticks.
      step.reset();
      previous = platform->nowSeconds();
      // A finished game's movie plays before its field (main.c: main, MAIN_STATE 2).
      if (const auto moment = flow.playing()->takeMovieRequest()) {
        playMovie(*moment);
      }
    }
  };
  // After a movie: the game that waited for it, or the title.
  const auto afterMovie = [&] {
    core::logDebug("[port:movie] playMovie (main.c:3443): the movie is over");
    if (PlaySession* play = flow.playing()) {
      play->holdMusicForMovie(false);
    }
    if (const auto next = std::exchange(nextMovie, std::nullopt); next && playMovie(*next)) {
      // main.c: main goes round its loop: the boot movie, then the "Press Start" screen.
      titleIdleTicks = 0;
      return;
    }
    if (startAfterMovie) {
      startPlaying(*std::exchange(startAfterMovie, std::nullopt));
    } else if (std::exchange(titleAfterMovie, false)) {
      // After the boot movie, the "Press Start" screen (main.c: main, runLandingScreen).
      title.showBoot();
    }
    titleIdleTicks = 0;
  };
  // A new game: its movie, then its first map (main.c: main, MAIN_STATE 0).
  const auto beginNewGame = [&](const PlayStart& start) {
    if (playMovie(MovieMoment::NewGame)) {
      startAfterMovie = start;
    } else {
      startPlaying(start);
    }
  };
  // A new game asks the opening's questions first (main.c: main: newGameScene).
  const auto askNewGame = [&](const PlayStart& start) {
    if (!newGameScene) {
      beginNewGame(start);
      return;
    }
    NewGameSceneRules sceneRules = *newGameScene;
    sceneRules.naming.length = nameLength(sceneRules.naming, userSettings.longNames.value_or(true));
    auto made = NewGameInterview::create(**renderer, **ui, files, std::move(sceneRules),
                                         &openingSound, [&](render::ModelRenderer& models) {
                                           prepareModels(models, graphics.options(), upscaleCache);
                                         });
    if (!made) {
      core::logWarning("new game scene: {}; the game starts without the questions",
                       made.error().message);
      beginNewGame(start);
      return;
    }
    core::logDebug("[port:story] newGameScene (main.c:3727): the opening's questions");
    title.hide();
    interview = std::move(*made);
    startAfterInterview = start;
    // Building the scene is not game time.
    step.reset();
    previous = platform->nowSeconds();
  };
  std::array<render::Drawer*, 1> movieDrawers{movies ? &movies->drawer() : nullptr};

  // The control channel's view of the game and what it may do to it.
  std::unique_ptr<RemoteControl> control;
  std::vector<platform::KeyChanged> controlKeys;
  if (controlSocket) {
    // A movie, the opening's questions or the title over the game; empty when the game shows.
    const auto modeOver = [&]() -> std::string {
      if (movies && movies->playing()) {
        return "movie";
      }
      if (interview) {
        return "interview";
      }
      return flow.playing() == nullptr ? "title" : "";
    };
    const auto status = [&, modeOver] {
      ControlStatus now;
      now.textures = (*renderer)->bindless().used();
      now.gpuBytes = (*device)->allocatedBytes();
      now.overlay = mapOverlay;
      now.clients = controlSocket->clients();
      now.mode = modeOver();
      if (const PlaySession* play = flow.playing()) {
        const TraceFrame frame = play->traceFrame(ticksRun, input);
        now.map = frame.map;
        if (now.mode.empty()) {
          now.mode = frame.battle ? "battle" : "field";
        }
        if (const auto& preview = play->scene().preview()) {
          now.preview = preview->map;
        }
      }
      return now;
    };
    // A game from a save or on a map, built as the title's start builds one. The host's functions
    // outlive this block, so they take status and playFrom by value.
    const auto playFrom = [&](const PlayStart& start) -> core::Result<void> {
      sessionCommands.drop(autotest.get());
      title.hide();
      if (auto started = flow.play(start); !started) {
        return core::fail(std::format("load: {}", started.error().message));
      }
      step.reset();
      previous = platform->nowSeconds();
      return {};
    };
    control = std::make_unique<RemoteControl>(
        ControlHost{
            .status = status,
            .state = [&]() -> std::optional<std::string> {
              const PlaySession* play = flow.playing();
              if (play == nullptr) {
                return std::nullopt;
              }
              return traceLine(play->traceFrame(ticksRun, input));
            },
            .runScript = [&](std::string_view code) -> core::Result<void> {
              return (*scripts)->runString(code, "control");
            },
            .load = [&, playFrom](const ControlRequest& request) -> core::Result<void> {
              if (request.path) {
                auto staged = stageRouteSave(*request.path);
                if (!staged) {
                  return std::unexpected{staged.error()};
                }
                if (controlSaves.folder) {
                  std::error_code ignored;
                  std::filesystem::remove_all(*controlSaves.folder, ignored);
                }
                controlSaves.folder = *staged;
                options.saves = *staged;
                options.startMap.reset();
                options.newGame = false;
                const game::save::SlotId slot{.kind = game::save::SlotKind::Manual, .index = 1};
                options.load = slot;
                core::logInfo("control: loading {}", request.path->string());
                return playFrom({.load = slot,
                                 .backup = false,
                                 .saveTo = std::nullopt,
                                 .choice = std::nullopt});
              }
              // A map, or a map and the exit to enter by: "twna01" or "twna01,2".
              const std::size_t comma = request.text.find(',');
              int exit = 0;
              if (comma != std::string::npos) {
                const std::string_view digits = std::string_view{request.text}.substr(comma + 1);
                const auto parsed =
                    std::from_chars(digits.data(), digits.data() + digits.size(), exit);
                if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size()) {
                  return core::fail(
                      std::format("load: '{}': expected <map>[,<exit>]", request.text));
                }
              }
              options.startMap = request.text.substr(0, comma);
              options.startExit = exit;
              options.load.reset();
              core::logInfo("control: a new game on {}", request.text);
              return playFrom({.load = std::nullopt,
                               .backup = false,
                               .saveTo = std::nullopt,
                               .choice = std::nullopt});
            },
            .screenshot =
                [&](const std::filesystem::path& path, ControlShotDone done) {
                  std::error_code ignored;
                  if (path.has_parent_path()) {
                    std::filesystem::create_directories(path.parent_path(), ignored);
                  }
                  (*renderer)->captureNextFrame(
                      [path, done = std::move(done)](core::Result<rhi::CapturedImage> captured) {
                        if (!captured) {
                          done(std::unexpected{captured.error()});
                          return;
                        }
                        if (auto written = writeScreenshot(std::move(*captured), path); !written) {
                          done(std::unexpected{written.error()});
                          return;
                        }
                        done(path.string());
                      });
                },
            .trace = [&](const std::optional<std::filesystem::path>& path) -> core::Result<void> {
              if (!path) {
                trace.reset();
                return {};
              }
              auto opened = StateTrace::open(*path);
              if (!opened) {
                return std::unexpected{opened.error()};
              }
              trace = std::move(*opened);
              return {};
            },
            .quit = [&] { running = false; },
            .rescan = [&]() -> core::Result<void> { return (*content)->rescan(); },
            .reloadMap = [&](std::optional<std::int32_t> exit) -> core::Result<ControlReload> {
              PlaySession* play = flow.playing();
              if (play == nullptr) {
                return core::fail("no game is being played");
              }
              if (auto editable = play->mapEditable(); !editable) {
                return std::unexpected{editable.error()};
              }
              // A map editor saved new files: the packs are listed again first.
              if (auto rescanned = (*content)->rescan(); !rescanned) {
                return std::unexpected{rescanned.error()};
              }
              // ... and the texts read again, for the lines a story patch brings or changes.
              if (auto reread = texts->setLanguage(std::string{texts->language()}); !reread) {
                return std::unexpected{reread.error()};
              }
              if (auto reloaded = play->reloadMap(exit); !reloaded) {
                return std::unexpected{reloaded.error()};
              }
              return ControlReload{.map = play->scene().field().mapId(), .exit = exit};
            },
            .storyState = [&]() -> core::Result<std::string> {
              const PlaySession* play = flow.playing();
              if (play == nullptr) {
                return core::fail("no game is being played");
              }
              return storyStateJson(play->storyStateView());
            },
            .overlay = showMapOverlay,
            .live =
                [&, modeOver] {
                  const PlaySession* play = flow.playing();
                  ControlLive live = play != nullptr ? play->live() : ControlLive{};
                  if (std::string over = modeOver(); !over.empty()) {
                    live.mode = std::move(over);
                  }
                  live.log = controlLog->newest();
                  return live;
                },
            .previewMap = [&](const ControlRequest& request) -> core::Result<ControlPreview> {
              PlaySession* play = flow.playing();
              if (play == nullptr) {
                return core::fail("no game is being played");
              }
              auto previewed = play->previewMap(request.text, request.patch, request.pack);
              if (!previewed) {
                return std::unexpected{previewed.error()};
              }
              return ControlPreview{.map = request.text, .reentered = *previewed};
            },
            .previewClear = [&]() -> core::Result<ControlPreviewCleared> {
              PlaySession* play = flow.playing();
              if (play == nullptr) {
                return ControlPreviewCleared{.map = {}, .cleared = false};
              }
              auto cleared = play->clearPreview();
              if (!cleared) {
                return std::unexpected{cleared.error()};
              }
              return ControlPreviewCleared{.map = play->scene().field().mapId(),
                                           .cleared = *cleared};
            },
            .logTail = [&](std::optional<std::uint64_t> since) { return controlLog->after(since); },
            .storySet = [&](const StoryWrite& write) -> core::Result<void> {
              PlaySession* play = flow.playing();
              if (play == nullptr) {
                return core::fail("no game is being played");
              }
              return play->storySet(write);
            },
            .storyRun = [&](const StoryRun& run) -> core::Result<void> {
              PlaySession* play = flow.playing();
              if (play == nullptr) {
                return core::fail("no game is being played");
              }
              return play->storyRun(run);
            },
        },
        options.controlPaused);
  }
  // A route run with the control channel ends without closing the game: the channel goes on.
  bool routeOver = false;
  // Whether the pad's Select opened the pause menu on its options page: a settings press closes it
  // then, and only then.
  bool optionsDirect = false;

  while (running) {
    events.clear();
    if (autotest && !routeOver) {
      const PlaySession* play = flow.playing();
      partnerStatus = play != nullptr ? play->partnerStatus() : screens::ScreenSettings{};
      host.scene = play != nullptr ? &play->scene() : nullptr;
      host.story = play != nullptr ? play->storyState() : nullptr;
      autotest->frame(host, ticksRun, platform->nowSeconds(), events);
      if (autotest->finished()) {
        if (!control) {
          break;
        }
        routeOver = true;
        core::logInfo("control: the route is over; the game stays up for the control channel");
      }
    }
    if (control) {
      for (const ControlLine& line : controlSocket->receive()) {
        control->receive(line.client, line.text);
      }
      for (const std::uint64_t gone : controlSocket->takeGone()) {
        control->forget(gone);
      }
      for (const ControlReply& reply : control->takeReplies()) {
        controlSocket->send(reply.client, reply.line);
      }
    }
    // During an autotest the route is the only player: the desktop's own mouse and keyboard
    // (someone using the computer, the pointer resting over the window) would move the pointer
    // under a click or a hold.
    const std::size_t injected = events.size();
    platform->pollEvents(events, [&](const SDL_Event& raw) {
      if (!autotest || !isPointerOrKeyEvent(raw)) {
        (*debugUi)->handleRawEvent(raw);
      }
    });
    if (autotest) {
      const auto real = std::ranges::remove_if(
          events.begin() + static_cast<std::ptrdiff_t>(injected), events.end(), isPointerOrKey);
      events.erase(real.begin(), real.end());
    }
    // The keyboard's text this frame, for a naming box (Window::setTextInput).
    std::string typed;
    bool typedEnter = false;
    for (const platform::Event& event : events) {
      if (const auto* text = std::get_if<platform::TextTyped>(&event)) {
        typed += text->text;
        continue;
      }
      if (const auto* key = std::get_if<platform::KeyChanged>(&event);
          key != nullptr && key->key == input::keys::Return && key->down && !key->repeat) {
        typedEnter = true;
      }
      if (const auto* moved = std::get_if<platform::MouseMoved>(&event)) {
        mouse.pointerMoved(*moved);
      }
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
        // The input still knows where the pointer is, for the textboxes' and the naming box's
        // clicks.
        if (const auto* moved = std::get_if<platform::MouseMoved>(&event)) {
          input.notePointer(*moved);
        }
        continue;
      }
      input.handle(event);
      if (!(*ui)->capturesInput() && flow.playing() != nullptr) {
        mouse.handle(event, flow.playing()->scene(),
                     walkControl.control() == WalkControl::Mouse &&
                         flow.playing()->walksByPointer());
      }
    }

    // A naming box open takes the keyboard's text as well as its keys.
    const bool takesText = (interview && interview->takesText()) ||
                           (flow.playing() != nullptr && flow.playing()->takesText()) ||
                           (flow.playing() == nullptr && versus.takesText());
    if (auto switched = window->setTextInput(takesText); !switched) {
      core::logDebug("game: {}", switched.error().message);
    }
    if (interview && interview->takesText()) {
      interview->type(typed, typedEnter);
    } else if (flow.playing() != nullptr && flow.playing()->takesText()) {
      flow.playing()->type(typed, typedEnter);
    } else if (flow.playing() == nullptr && versus.takesText()) {
      versus.type(typed, typedEnter);
    }

    const double now = platform->nowSeconds();
    const double frameSeconds = now - previous;
    previous = now;
    fps.frame(now);
    // The first half second warms up (pipelines, the first uploads): it is not the game's steady
    // pace.
    if (constexpr std::uint64_t WarmUpTicks = 30; ticksRun < WarmUpTicks) {
      frameStats.skipNext(1);
    }
    // Frame times are only kept for the autotest's report; a normal session would grow them
    // forever.
    const bool steady = autotest ? frameStats.add(frameSeconds) : true;
    // A slow frame says when it happened, to tell a hitch of the game from a load or a resize. At
    // 60 Hz with vsync about half the frames take a little over one tick, so only one and a half
    // ticks count as slow.
    if (constexpr double SlowFrameSeconds = 1.5 * TickSeconds;
        frameSeconds > SlowFrameSeconds && ticksRun > 0) {
      if (auto left = slowFrames.slow(frameSeconds * 1000.0, ticksRun)) {
        core::logInfo("game: slow frame, {:.1f} ms, at tick {}{} ({}){}", frameSeconds * 1000.0,
                      ticksRun, steady ? "" : " (expected: load, resize or capture)", phases.text(),
                      *left);
      }
    }
    phases.start(now);

    int ticks = step.advance(frameSeconds);
    // What is drawn between ticks (the characters' clips, fades, the scenes' effects) moves on by
    // the logic's time, capped as the ticks are, never by more: a stall, a load or a slow machine
    // would otherwise play the animations ahead of the clock and the field.
    double gameSeconds = step.takenSeconds();
    // A quick battle (battle_speed.hpp) runs each of the clock's ticks as two of the fight's, and
    // what is drawn between them moves on as fast; the sound and the play time keep the clock's.
    const int ticksPerTick = flow.playing() != nullptr
                                 ? battleSpeed.ticksPerTick(flow.playing()->battleSpeedMoment())
                                 : 1;
    ticks *= ticksPerTick;
    gameSeconds *= ticksPerTick;
    // The battle document's x2 plate, while x2 is switched on.
    if (const bool fast = battleSpeed.speed() != BattleSpeed::Normal && battleSpeed.fastSwitched();
        fast != battleFastShown) {
      (*ui)->setSetting(BattleFastSetting, fast);
      battleFastShown = fast;
    }
    // The control channel's pause: only the ticks it advances run, and the frame lasts as long as
    // they do, so what moves with the frame's time moves with them.
    if (control) {
      ticks = control->ticksThisFrame(ticks);
      if (control->paused()) {
        gameSeconds = ticks * TickSeconds;
      }
    }
    if (autotest && !routeOver) {
      if (const int steered = autotest->ticksThisFrame(ticks); steered < ticks) {
        ticks = steered;
        gameSeconds = ticks * TickSeconds;
      }
    }
    sound.update(ticks * TickSeconds / ticksPerTick);
    for (int tick = 0; tick < ticks; ++tick) {
      if (autotest && !routeOver) {
        autotest->feedTick(ticksRun, input);
      }
      if (control) {
        controlKeys.clear();
        control->beforeTick(ticksRun, controlKeys);
        for (const platform::KeyChanged& change : controlKeys) {
          input.handle(change);
        }
      }
      // The developer tools (dev_tools.hpp): while the option is off their keys do nothing, and
      // turning it off closes the console and the developer menu. --tuning asks for the console.
      const bool devTools = userSettings.devTools.value_or(DevToolsDefault) || options.tuning;
      if (!devTools && (*debugUi)->visible()) {
        (*debugUi)->toggle();
      }
      if (PlaySession* play = flow.playing();
          play != nullptr && play->devMenuOpen() && (!devTools || input.wasPressed("dev_menu"))) {
        play->toggleDevMenu();
      } else if (play != nullptr && devTools && input.wasPressed("dev_menu")) {
        play->toggleDevMenu();
      }
      if (devTools && input.wasPressed("debug_console")) {
        (*debugUi)->toggle();
      }
      if (devTools && input.wasPressed("debug_map_overlay")) {
        showMapOverlay(std::nullopt);
      }
      reportAsked = input.wasPressed("report_bug") || reportAsked;
      // The original has no pause menu on the title, over a movie, the opening's questions, a
      // naming box or a gym course: there `menu` skips the movie (Start, mov.c: MOV_playMovie),
      // puts the cursor on OK or ends the session (the American cancel, trn_*.c), and must not
      // open it too.
      const bool menuHeld = flow.playing() == nullptr || (movies && movies->playing()) ||
                            interview || flow.playing()->holdsMenu();
      (*ui)->setMenuDocument(menuHeld ? std::string_view{} : MenuDocument);
      // The walking state's tick (tamer.c:520): the game menu opens only while the player walks
      // freely, with no script, fade or dialogue box running; utils2.c: handlePause: no pause
      // during a fade (FADE_PROTECTION). The pad's Select options open the same menu and follow
      // both.
      const bool menuShut = !menuHeld && !flow.playing()->menuShutBy().empty();
      const bool fadeProtected = !menuHeld && flow.playing()->fadeProtected();
      (*ui)->setMenuShut(menuShut);
      // Select/Back on a pad: the game menu's options page, straight away
      // (docs/modulos/apps/game.md, «Opciones en partida»). From Start's pause it ends the pause
      // first; with the menu open it switches it to the options page (or closes it when Select
      // opened it there); over another menu or where the menu is held it does nothing.
      const bool settingsAsked =
          !menuHeld && input.wasPressed("settings") &&
          ((*ui)->isVisible(MenuDocument) || (!(*ui)->hasModal() && !menuShut));
      // The menu was closed by another path: the next settings press opens it again, not closes it.
      if (!(*ui)->isVisible(MenuDocument)) {
        optionsDirect = false;
      }
      // Start's pause freezes everything but the box, menus and battle included (handlePause).
      if (const PauseStep pause =
              pauseBox.tick(input.wasPressed("pause") || (settingsAsked && pauseBox.paused()),
                            !menuHeld && !fadeProtected);
          pause.opened || pause.closed || pause.paused) {
        if (pause.opened) {
          (*ui)->show(PauseBoxDocument);
          menuSounds.play(ui::UiCue::Confirm);
        } else if (pause.closed) {
          (*ui)->hide(PauseBoxDocument);
        }
        if (pause.paused) {
          input.endTick();
          ++ticksRun;
          continue;
        }
      }
      if (flow.playing() != nullptr && input.wasPressed("menu")) {
        publishPlayerPoint(**ui, flow.playing()->scene());
        flow.playing()->preparePauseMenu();
      }
      if (settingsAsked) {
        const bool open = (*ui)->isVisible(MenuDocument) && optionsDirect;
        if (open) {
          (*ui)->hide(MenuDocument);
          optionsDirect = false;
        } else {
          // A menu another key opened (Tab) is already visible: the setting switches it to its
          // options page in place, without closing and reopening its box.
          publishPlayerPoint(**ui, flow.playing()->scene());
          flow.playing()->preparePauseMenu();
          (*ui)->setSetting(MenuStartPageSetting, std::string{"options"});
          if (!(*ui)->isVisible(MenuDocument)) {
            (*ui)->show(MenuDocument, true);
          }
          (*ui)->setSetting(MenuStartPageSetting, std::string{});
          optionsDirect = true;
        }
        menuSounds.play(open ? ui::UiCue::Cancel : ui::UiCue::Confirm);
      }
      // M: the island map (world_map.hpp), where the game menu could open; M or cancel close it.
      if (PlaySession* play = flow.playing(); play != nullptr && !menuHeld) {
        play->pressWorldMap(input, !menuShut && !fadeProtected && !(*ui)->hasModal());
      }
      (*ui)->tick(input, TickSeconds);
      menuSounds.play((*ui)->takeCues());
      if (movies && movies->playing()) {
        movies->tick(input);
        input.endTick();
        ++ticksRun;
        continue;
      }
      if (interview) {
        interview->tick(input);
        input.endTick();
        ++ticksRun;
        continue;
      }
      if (flow.playing() == nullptr && versus.active()) {
        if (input.wasPressed("extra")) {
          versus.pressFinisher();
        }
        versus.tick(platform->nowSeconds());
        if (versus.takeClosed()) {
          title.show();
        }
      }
      if (flow.playing() != nullptr || !title.landing()) {
        titleIdleTicks = 0;
      } else if (movies && replayBootMovie && ++titleIdleTicks >= movieRules->landingIdleFrames) {
        core::logDebug("[port:movie] runLandingScreen (main.c:3604): {} ticks on the \"Press "
                       "Start\" screen without Start, the boot movie again",
                       movieRules->landingIdleFrames);
        titleIdleTicks = 0;
        replayBootMovie = playMovie(MovieMoment::Boot);
        titleAfterMovie = replayBootMovie;
      }
      PlaySession* play = flow.playing();
      // clock.c: tickPlaytime runs every frame the game loop runs, under the menus and in battles
      // (measured: 140 frames in 420 vblanks with the bag's options open, 100 in 307 in a battle)
      // and not under the pause (none in 476 vblanks) or a movie, which return above.
      if (play != nullptr) {
        play->countPlayTime(TickSeconds / ticksPerTick);
      }
      game::field::FieldInput intent;
      FishingButtons fishing;
      // A click walks the player only while the mouse is the walk control and nothing holds him (a
      // talk, a battle, a gym course, a screen). Elsewhere the click belongs to whatever is in
      // front (a page, a menu, an option) and must not start a walk once it closes.
      const bool pointerWalks = play != nullptr && walkControl.control() == WalkControl::Mouse &&
                                !(*ui)->capturesInput() && play->walksByPointer();
      if (!pointerWalks) {
        mouse.release();
      }
      if (play != nullptr && !(*ui)->capturesInput()) {
        const input::Vector2 move = input.vector("move_left", "move_right", "move_up", "move_down");
        // Up gives a negative y; the field's y points away from the camera.
        const WalkOrder walk = walkOrderOf(
            walkControl.control(),
            {.keys = {move.x, -move.y},
             .clicked = pointerWalks ? mouse.take() : std::nullopt,
             .steered = pointerWalks
                            ? mouse.steer(
                                  play->scene(),
                                  glm::vec2{static_cast<float>((*renderer)->targetExtent().width),
                                            static_cast<float>((*renderer)->targetExtent().height)})
                            : std::nullopt});
        intent.stick = walk.stick;
        intent.run = !input.isDown("walk");
        intent.moveTarget = walk.moveTarget;
        // A route's click on the ground walks there whatever walks the player (Autotest).
        if (autotest && !intent.moveTarget) {
          intent.moveTarget = autotest->takeWalkOrder();
        }
        // Escape, the right button or East drops a click-to-move target.
        intent.cancelTarget = input.wasPressed("cancel");
        const bool clickWalksAway =
            intent.moveTarget && !MouseSteering::pressesInPlace(
                                     *intent.moveTarget, play->scene().field().player().position);
        intent.confirm =
            clickWalksAway ? input.wasPressedByKeyOrPad("confirm") : input.wasPressed("confirm");
        play->pressStory(input);
        fishing = {.confirmDown = input.isDown("confirm"),
                   .cancelPressed = intent.cancelTarget,
                   .cameraDown = input.isDown("alt"),
                   .left = move.x < 0.0F,
                   .right = move.x > 0.0F,
                   .up = move.y < 0.0F,
                   .down = move.y > 0.0F};
      } else {
        mouse.release();
      }
      if (play != nullptr) {
        // The battle's document holds the input, but not the finisher's button nor the quick
        // battle's switch.
        if (play->battleSpeedMoment().fighting && input.wasPressed(BattleSpeedAction)) {
          battleSpeed.press();
        }
        play->pressBattle(input);
        play->pressItemKeeper(input);
        play->pressPartnerMenu(input);
        play->pressDestinationChoice(input);
        play->pressCurling(input);
        play->tick(intent, fishing);
        if (trace) {
          trace->write(play->traceFrame(ticksRun, input));
        }
      }
      input.endTick();
      ++ticksRun;
    }
    phases.mark(FramePhases::Ticks, platform->nowSeconds());
    if (control) {
      control->afterFrame(ticksRun);
      for (const ControlReply& reply : control->takeReplies()) {
        controlSocket->send(reply.client, reply.line);
      }
    }

    for (const ui::UiCommand& command : (*ui)->takeCommands()) {
      // The options the documents apply by themselves: shop extras, art fixes, widescreen pictures
      // (the movies read it as each starts), developer tools.
      if (changeUiToggle(command, **ui, userSettings, settingsFile)) {
        continue;
      }
      if (command.name == DevMenuRunCommand) {
        // The developer menu's console line, only while the developer tools are on and only the
        // commands the menu offers.
        const std::string* line = std::get_if<std::string>(&command.value);
        if (line == nullptr || !userSettings.devTools.value_or(DevToolsDefault) ||
            !devMenuLineAllowed(*line)) {
          core::logWarning("game: developer menu: refused {}",
                           line != nullptr ? *line : std::string{"?"});
        } else if (auto ran = (*scripts)->runString(*line, "dev_menu"); !ran) {
          core::logWarning("game: developer menu: {}", ran.error().message);
        } else {
          core::logInfo("game: developer menu: {}", *line);
        }
        continue;
      }
      if (flow.playing() != nullptr && flow.playing()->handle(command)) {
        continue;
      }
      if (command.name == ChibiStyleSetting) {
        // The chibi style, from the title's options or the game's (chibi_style.hpp).
        const ChibiMode mode = chibiMode.change(command.value);
        if (PlaySession* play = flow.playing()) {
          play->scene().setChibiMode(mode);
        }
        (*ui)->setSetting(ChibiStyleSetting, std::string{chibiModeName(mode)});
        continue;
      }
      if (graphics.handle(command, **ui)) {
        continue;
      }
      if (volumes.handle(command, **ui)) {
        continue;
      }
      if (command.name == HdScenerySetting) {
        const bool on = hdScenery.change(command.value);
        if (PlaySession* play = flow.playing()) {
          play->scene().setHdScenery(on);
        }
        (*ui)->setSetting(HdScenerySetting, on);
        continue;
      }
      if (command.name == AutosaveSetting) {
        // The autosave option from the title's options; the game's own are PlaySession's.
        changeAutosave(command.value, **ui, userSettings, settingsFile);
        continue;
      }
      if (command.name == BattleSpeedSetting) {
        battleSpeed.change(command.value);
        (*ui)->setSetting(BattleSpeedSetting, std::string{battleSpeedName(battleSpeed.speed())});
        continue;
      }
      if (command.name == GymSessionsSetting) {
        // Batch training, from the title's options or the game's.
        if (const double* number = std::get_if<double>(&command.value)) {
          userSettings.gymSessions =
              static_cast<int>(std::clamp(std::round(*number), static_cast<double>(GymSessionsMin),
                                          static_cast<double>(GymSessionsMax)));
          if (settingsFile) {
            saveUserSettings(*settingsFile, userSettings);
          }
          if (PlaySession* play = flow.playing()) {
            play->setGymSessions(gymSessions());
          }
        }
        (*ui)->setSetting(GymSessionsSetting, static_cast<double>(gymSessions()));
        continue;
      }
      if (command.name == DifficultySetting) {
        // The difficulty (rule_options.hpp), from the title's options or the game's: normal or
        // easy; anything else changes nothing.
        if (const std::string* level = std::get_if<std::string>(&command.value);
            level != nullptr && (*level == DifficultyNormal || *level == DifficultyEasy)) {
          userSettings.difficulty = *level;
          if (settingsFile) {
            saveUserSettings(*settingsFile, userSettings);
          }
          if (PlaySession* play = flow.playing()) {
            play->applyPlayOptions();
          }
        }
        (*ui)->setSetting(DifficultySetting,
                          userSettings.difficulty.value_or(std::string{DifficultyNormal}));
        continue;
      }
      if (command.name == WalkControlSetting) {
        PlaySession* play = flow.playing();
        walkControl.change(command.value, play != nullptr ? &play->scene().field() : nullptr);
        (*ui)->setSetting(WalkControlSetting, std::string{walkControlName(walkControl.control())});
        mouse.release();
        continue;
      }
      if (command.name == WeatherSetting) {
        // The weather (an engine improvement): shown or hidden at once, on the title screen too.
        if (const bool* shown = std::get_if<bool>(&command.value)) {
          userSettings.weather = *shown;
          if (settingsFile) {
            saveUserSettings(*settingsFile, userSettings);
          }
          if (PlaySession* play = flow.playing(); play != nullptr) {
            play->scene().setWeatherEnabled(*shown);
          }
        }
        (*ui)->setSetting(WeatherSetting, userSettings.weather.value_or(true));
        continue;
      }
      if (command.name == SeasonsSetting) {
        // The seasons (an engine improvement): shown or hidden at once, on the title screen too.
        if (const bool* shown = std::get_if<bool>(&command.value)) {
          userSettings.seasons = *shown;
          if (settingsFile) {
            saveUserSettings(*settingsFile, userSettings);
          }
          if (PlaySession* play = flow.playing(); play != nullptr) {
            play->scene().setSeasonsEnabled(*shown);
          }
        }
        (*ui)->setSetting(SeasonsSetting, userSettings.seasons.value_or(true));
        continue;
      }
      // With no game the options page's rows that only a game applies are kept for the next one.
      if (flow.playing() == nullptr &&
          changeGameToggle(command, **ui, userSettings, settingsFile)) {
        continue;
      }
      if (flow.playing() == nullptr && command.name == VersusCommand) {
        // The arena draws with the smooth motion setting, as a game does (PlaySession).
        versus.setSmoothMotion(userSettings.smoothMotion.value_or(true));
        if (versus.open()) {
          title.hide();
        }
        continue;
      }
      if (flow.playing() == nullptr && versus.handle(command)) {
        continue;
      }
      if (flow.playing() == nullptr && title.handle(command)) {
        if (const auto start = title.takeStart()) {
          if (start->load) {
            // Give the shared save indicator time to render and animate before the synchronous
            // save read starts blocking this loop.
            loadingIndicator = true;
            loadAfterIndicator = *start;
            loadingIndicatorSeconds = 0.0;
            (*ui)->setSetting(SaveIndicatorTextSetting, std::string{LoadingText});
            (*ui)->setSetting(SaveIndicatorOpacitySetting, 1.0);
            (*ui)->setSetting(SaveIndicatorLiftSetting, 0.0);
            (*ui)->setSetting(SaveIndicatorFigureSetting, saveFigure && (*saveFigure)->loaded());
            (*ui)->setSetting(SaveIndicatorShownSetting, true);
            (*ui)->show(SaveIndicatorDocument);
          } else {
            askNewGame(*start);
          }
        }
        continue;
      }
      running = apply(command, **ui, *window, **device, settings, displayChanger, languageChanger,
                      textFontChanger, uiSkinChanger, minimapChanger) &&
                running;
    }
    if (flow.playing() != nullptr && flow.playing()->takeToTitle()) {
      // As the original after its ending (main.c: main): the game is torn down whole and the title
      // screen comes back.
      core::logDebug("[port:save] main (main.c:3522-3527): back to the title screen");
      flow.toTitle();
      screenFade->setDarkness(0.0F);
      screenFade->setWhiteness(0.0F);
      title.showBoot();
    }
    if (flow.playing() != nullptr && flow.playing()->takeQuitGame()) {
      // The pause menu's quit, confirmed: the game is saved (quickSave, in its handler) and this
      // closes the window, as the title screen's quit does.
      core::logInfo("game: the player saved and quit from the pause menu");
      running = false;
    }
    if (interview) {
      interview->update(gameSeconds, (*renderer)->targetExtent());
      screenFade->setDarkness(interview->fadeLevel());
      if (interview->finished()) {
        PlayStart start = *std::exchange(startAfterInterview, std::nullopt);
        start.choice = interview->choice();
        // The scene's models leave the GPU before the game starts (main.c: unloadNewGameScene).
        (*device)->waitIdle();
        interview.reset();
        beginNewGame(start);
      }
    }
    // Where between the last two ticks the frame is drawn: the clock's, or the control channel's
    // while it holds the logic paused at one (captures of smooth motion).
    const auto drawAlpha =
        static_cast<float>(control && control->drawAlpha() ? *control->drawAlpha() : step.alpha());
    if (flow.playing() == nullptr) {
      versus.update(gameSeconds, drawAlpha, graphics.options());
    }
    if (PlaySession* play = flow.playing()) {
      play->update(sessionCommands, autotest.get(), frameStats, gameSeconds, drawAlpha);
      screenFade->setDarkness(play->fadeLevel());
      screenFade->setWhiteness(play->whiteLevel());
      mapChangeView.show(play->scene(), play->fadeLevel());
      if (const auto moment = play->takeMovieRequest()) {
        // The ending closes the game before its movie, then the title (main.c: main, MAIN_STATE 3).
        if (*moment == MovieMoment::Ending) {
          flow.toTitle();
          titleAfterMovie = true;
          nextMovie = movieAfter(*moment);
        }
        if (!playMovie(*moment)) {
          afterMovie();
        }
      }
    } else {
      sessionCommands.drop(autotest.get());
    }
    phases.mark(FramePhases::Update, platform->nowSeconds());
    if (consoleUiSkin) {
      uiSkinChanger.change(*std::exchange(consoleUiSkin, std::nullopt));
    }
    if (consoleTextFont) {
      textFontChanger.change(*std::exchange(consoleTextFont, std::nullopt));
    }
    for (const ui::UiCommand& command : std::exchange(consoleGraphics, {})) {
      graphics.handle(command, **ui);
    }
    if (consoleBattleSpeed) {
      battleSpeed.change(ui::UiValue{*std::exchange(consoleBattleSpeed, std::nullopt)});
      (*ui)->setSetting(BattleSpeedSetting, std::string{battleSpeedName(battleSpeed.speed())});
    }
    if (consoleHdScenery) {
      const bool on = hdScenery.change(ui::UiValue{*std::exchange(consoleHdScenery, std::nullopt)});
      if (PlaySession* play = flow.playing()) {
        play->scene().setHdScenery(on);
      }
      (*ui)->setSetting(HdScenerySetting, on);
    }
    if (movies) {
      movies->update(gameSeconds);
      if (moviePlayed && !movies->playing()) {
        moviePlayed = false;
        afterMovie();
      }
    }
    if (std::exchange(reportAsked, false)) {
      if (const PlaySession* play = flow.playing()) {
        bugReporter.report(*play, **renderer);
      } else {
        core::logInfo("bug report: only during a game");
      }
    }
    bugReporter.update(**ui, frameSeconds);
    // From the last frame's layout: the minimap hides under any window over it.
    minimapCover.update(**ui, frameSeconds);
    if (options.tuning) {
      (*ui)->reloadChanged();
    }
    (*ui)->update();
    // Keep the skin's mouse cursor on top: re-assert it after the UI updates (RmlUi processes hover
    // and cursor state during update), so a window event between the title and the field, or any
    // cursor change RmlUi asks for, can't leave the system arrow where the player expects the
    // skin's cursor.
    platform->reapplyColorCursor();
    phases.mark(FramePhases::Ui, platform->nowSeconds());

    overlayStats = {
        .framesPerSecond = fps.framesPerSecond,
        .frameMilliseconds = frameSeconds * 1000.0,
        .ticksThisFrame = ticks,
    };
    // The fade goes over the scene and under the UI: the HUD fades its panels itself and keeps the
    // map's name on top, as the original draws it in front of the fade (map.c: renderMapName, layer
    // 0).
    const std::array<render::Drawer*, 2> uiDrawers{&*screenFade, uiRenderer->get()};
    // A movie covers the game and the UI.
    const bool showingMovie = movies && movies->playing();
    // Antialiasing (graphics_options.hpp) for every scene: the field, the interview, the figures.
    (*renderer)->setMultisampling(graphics.options().antialiasing);
    // The models' texture filter for the menus' figures and the new game's host (the field's own
    // takes it in setGraphics); each uploads its textures again only when the filter changes.
    for (auto* figure : {portrait ? &(*portrait)->models() : nullptr,
                         playerPortrait ? &(*playerPortrait)->models() : nullptr,
                         saveFigure ? &(*saveFigure)->models() : nullptr,
                         medalFigure ? &(*medalFigure)->models() : nullptr,
                         interview ? &interview->models() : nullptr}) {
      if (figure != nullptr) {
        applyTextureFilter(*figure, graphics.options());
      }
    }
    std::span<render::Drawer* const> sceneDrawers;
    // The field's light and colour pass, over the field only (never a movie or the UI).
    render::Drawer* postDrawer = nullptr;
    if (showingMovie) {
      sceneDrawers = movieDrawers;
      // The texture filter «upscaled» upscales the movie too, with a sharpen pass after it.
      movies->setUpscaled(graphics.options().textureFilter == TextureFilter::Upscaled);
      postDrawer = movies->postDrawer();
    } else if (flow.playing() != nullptr) {
      FieldScene& field = flow.playing()->scene();
      field.setGraphics(graphics.options());
      sceneDrawers = field.drawers();
      postDrawer = field.postDrawer();
    } else if (interview) {
      sceneDrawers = interview->drawers();
    } else if (FieldScene* arena = versus.arenaScene()) {
      // The versus' battle on its arena, under the versus document.
      sceneDrawers = arena->drawers();
      postDrawer = arena->postDrawer();
    }
    std::vector<render::TextureScene> portraitScenes;
    // HD scenery's background pass: what the scroll uncovered, before the scene pass reads it.
    if (!showingMovie && flow.playing() != nullptr) {
      if (const auto hdPass = flow.playing()->scene().hdSceneryScene()) {
        portraitScenes.push_back(*hdPass);
      }
    }
    if (!showingMovie && portrait && flow.playing() != nullptr) {
      // The portrait follows the partner's form ahead of the status page: loaded on that page's
      // first frame, its model and textures stalled the menu (#221). A new form comes in as the
      // game starts (under its fade in) or evolves.
      const std::string species{flow.playing()->partnerSpecies()};
      if (species != portraitSpecies) {
        portraitSpecies = species;
        (*portrait)->clear();
        const auto* raise = portraitRaise ? portraitRaise->find(species) : nullptr;
        // A form the ruleset's species patches add (the hidden content) is only in its table.
        std::optional<assets::RaiseTable> patched;
        if (raise == nullptr) {
          if (auto table = flow.playing()->raiseTable(files); table) {
            patched = std::move(*table);
            raise = patched->find(species);
          }
        }
        if (raise) {
          if (auto loaded =
                  (*portrait)->load(files, species, *raise, flow.playing()->speciesScale(species));
              !loaded)
            core::logWarning("Status portrait {}: {}", species, loaded.error().message);
        } else {
          core::logWarning("Status portrait {}: no raise-table placement", species);
        }
      }
    }
    if (!showingMovie && portrait && flow.playing() != nullptr &&
        (*ui)->isVisible("partner_menu") &&
        !(*ui)->elementHasClass("partner_menu", "page-status", "hidden")) {
      // An addition: the model plays its idle, or the gesture of its first need, as on the field.
      const auto animation = game::partner::statusAnimation(flow.playing()->partnerConditions());
      const auto clip =
          portraitClips ? portraitClips->clipOf(game::partner::nameOf(animation)) : std::nullopt;
      (*portrait)->play(clip.value_or(0));
      portraitScenes.push_back((*portrait)->scene(frameSeconds));
    }
    if (!showingMovie && playerPortrait && (*ui)->isVisible("pause_menu") &&
        (*ui)->elementHasClass("pause_menu", "panel", "on-player") &&
        !(*ui)->elementHasClass("pause_menu", "panel", "player-tools-open") &&
        !(*ui)->elementHasClass("pause_menu", "panel", "player-chart"))
      portraitScenes.push_back((*playerPortrait)->scene());
    // game_menu.c: renderMedalView draws the medal (renderDigiviceMedals) while the selected medal
    // is held: the page marks that cell `showing`.
    if (!showingMovie && medalFigure && (*medalFigure)->loaded() &&
        (*ui)->isVisible("pause_menu") &&
        (*ui)->elementHasClass("pause_menu", "panel", "player-medals") &&
        !(*ui)->elementHasClass("pause_menu", "panel", "player-tools-open")) {
      for (std::int32_t medal = 0; medal < MedalCount; ++medal) {
        if ((*ui)->elementHasClass("pause_menu", std::format("medal-{}", medal), "showing")) {
          (*medalFigure)->show(medal);
          portraitScenes.push_back((*medalFigure)->scene(frameSeconds));
          break;
        }
      }
    }
    if (!showingMovie && saveFigure && (*saveFigure)->loaded() &&
        (loadingIndicator || (flow.playing() != nullptr && flow.playing()->saveIndicatorShown())))
      portraitScenes.push_back((*saveFigure)->scene(frameSeconds));
    mapOverlayShapes.clear();
    if (mapOverlay && !showingMovie) {
      if (const PlaySession* play = flow.playing(); play != nullptr && play->scene().camera()) {
        const FieldScene& scene = play->scene();
        buildMapOverlay(
            scene.map(), scene.field().collision(), scene.field().rules().triggers,
            [&scene](glm::vec3 world) { return scene.worldToWindowFraction(world); },
            mapOverlayShapes);
      }
    }
    (*debugUi)->setShapes(mapOverlayShapes);
    const bool drawn = (*renderer)->renderFrame({
        .clearColor = {0.0F, 0.0F, 0.0F, 1.0F},
        .scene = sceneDrawers,
        .ui = showingMovie ? std::span<render::Drawer* const>{} : uiDrawers,
        .overlay = std::ref(drawOverlay),
        .textureScenes = portraitScenes,
        .post = postDrawer,
    });
    phases.mark(FramePhases::Render, platform->nowSeconds());
    if (loadAfterIndicator) {
      loadingIndicatorSeconds += std::max(0.0, frameSeconds);
      if (loadingIndicatorSeconds >= 0.3) {
        PlayStart start = std::move(*loadAfterIndicator);
        loadAfterIndicator.reset();
        loadingIndicator = false;
        (*ui)->setSetting(SaveIndicatorShownSetting, false);
        (*ui)->hide(SaveIndicatorDocument);
        startPlaying(start);
      }
    }
    if (!drawn || (control && control->idle())) {
      platform->waitForEvents(IdleWait);
    }
  }
  (*device)->waitIdle();
  if (!autotest) {
    // The locals go in reverse order: the scene and the UI release their textures into the renderer
    // before it.
    exitCode = EXIT_SUCCESS;
    return exitCode;
  }
  autotest->summarize(host);
  // Shut down before judging the run, so that what teardown reports (leaks, objects destroyed in
  // use) counts: the scene and the UI release their textures into the renderer before it, and the
  // device goes last.
  const bool validationActive = (*device)->validationEnabled();
  sessionCommands.drop(autotest.get());
  // A route that ends during the opening's questions leaves their scene up: its models go before
  // the renderer like the session's.
  interview.reset();
  startAfterInterview.reset();
  flow.toTitle();
  scripts->reset();
  ui->reset();
  if (portrait)
    portrait->reset();
  if (playerPortrait)
    playerPortrait->reset();
  if (saveFigure)
    saveFigure->reset();
  if (medalFigure)
    medalFigure->reset();
  uiRenderer->reset();
  screenFade.reset();
  movies.reset();
  renderer->reset();
  debugUi->reset();
  device->reset();
  exitCode = autotest->verdict(validationActive);
  if (routeSaves) {
    std::error_code ignored;
    std::filesystem::remove_all(*routeSaves, ignored);
  }
  return exitCode;
}

} // namespace dwe::app
