#include "game.hpp"

#include <dwe/assets/asset_id.hpp>
#include <dwe/content/game_content.hpp>
#include <dwe/core/fixed_step.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/debug/debug_ui.hpp>
#include <dwe/input/action_map.hpp>
#include <dwe/input/input_state.hpp>
#include <dwe/localization/catalog.hpp>
#include <dwe/platform/locale.hpp>
#include <dwe/platform/platform.hpp>
#include <dwe/platform/window.hpp>
#include <dwe/render/renderer.hpp>
#include <dwe/render/screen_fade.hpp>
#include <dwe/rhi/device.hpp>
#include <dwe/screens/screen_settings.hpp>
#include <dwe/script/script_vm.hpp>
#include <dwe/ui/ui.hpp>
#include <dwe/ui/ui_renderer.hpp>

#include <SDL3/SDL_events.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "app_flow.hpp"
#include "autotest.hpp"
#include "common.hpp"
#include "content_mount.hpp"
#include "dev_assets_banner.hpp"
#include "field_scene.hpp"
#include "frame_stats.hpp"
#include "game_audio.hpp"
#include "language.hpp"
#include "messages.hpp"
#include "minimap_setting.hpp"
#include "movie_screen.hpp"
#include "play_session.hpp"
#include "text_font_setting.hpp"
#include "title_screen.hpp"
#include "ui_skin_setting.hpp"
#include "user_settings.hpp"

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
bool apply(const ui::UiCommand& command, ui::Ui& ui, platform::Window& window, Settings& settings,
           LanguageChanger& language, TextFontChanger& textFont, UiSkinChanger& skin,
           MinimapChanger& minimap) {
  core::logInfo("game: command {} = {}", command.name, describe(command.value));
  if (command.name == "quit") {
    return false;
  }
  if (command.name == "open_menu") {
    ui.show(MenuDocument, true);
  } else if (command.name == LanguageCommand &&
             std::holds_alternative<std::string>(command.value)) {
    language.change(std::get<std::string>(command.value));
  } else if (command.name == MinimapSetting || command.name == LegacyMinimapVisibleSetting) {
    ui.setSetting(MinimapSetting, std::string{minimap.change(command.value)});
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
  } else if (command.name == ui::TextFontSetting &&
             std::holds_alternative<std::string>(command.value)) {
    textFont.change(std::get<std::string>(command.value));
  } else if (command.name == ui::UiSkinSetting &&
             std::holds_alternative<std::string>(command.value)) {
    skin.change(std::get<std::string>(command.value));
  } else if (command.name == "vsync") {
    // The device picks its present mode when it is created (--no-vsync); the menu keeps showing the
    // real one.
    core::logInfo("game: vsync cannot change while running yet");
    ui.setSetting("vsync", settings.vsync);
  }
  return true;
}

core::Result<void> loadDocuments(ui::Ui& ui) {
  for (const auto& [name, id] :
       {std::pair{"hud", "ui/hud/document"}, std::pair{"pause_menu", "ui/pause_menu/document"},
        std::pair{"main_menu", "ui/main_menu/document"}, std::pair{"battle", "ui/battle/document"},
        std::pair{"partner_menu", "ui/partner_menu/document"},
        std::pair{"destination_choice", "ui/destination_choice/document"},
        std::pair{"save_slots", "ui/save_slots/document"},
        std::pair{"dialog", "ui/dialog/document"},
        std::pair{"sleep_menu", "ui/sleep_menu/document"}}) {
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

  // Takes the button events the UI did not.
  void handle(const platform::Event& event, const FieldScene& scene) {
    if (const auto* button = std::get_if<platform::MouseButtonChanged>(&event);
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

} // namespace

int runGame(const Options& options) {
  auto platform = platform::Platform::create();
  if (!platform) {
    core::logError("{}", platform.error().message);
    return EXIT_FAILURE;
  }
  audio::Audio sound = openGameAudio();
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
  auto window =
      platform::Window::create({.title = "dwe", .width = options.width, .height = options.height});
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
  core::logInfo("game: validation layer {}, vsync {}",
                validationState((*device)->validationEnabled()), options.vsync ? "on" : "off");
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
  // An autotest neither reads nor writes the player's settings: its language is --lang or the
  // system's.
  const std::optional<std::filesystem::path> settingsFile =
      autotest ? std::nullopt : userSettingsFile();
  UserSettings userSettings = settingsFile ? loadUserSettings(*settingsFile) : UserSettings{};
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
  // The map change's fade, subtracted from the scene before the UI is drawn (fade.c: renderFade).
  std::optional<render::ScreenFade> screenFade;
  if (auto fade = render::ScreenFade::create(**renderer)) {
    screenFade.emplace(std::move(*fade));
  } else {
    core::logError("{}", fade.error().message);
    return EXIT_FAILURE;
  }
  // The original's movies (docs/architecture/peliculas.md). None in an autotest: its routes start
  // where they say.
  std::optional<MovieRules> movieRules;
  std::unique_ptr<MovieScreen> movies;
  if (!autotest) {
    movieRules = loadMovieRules(files, options.ruleset);
    if (auto screen = MovieScreen::create(**renderer, sound); screen && movieRules) {
      movies = std::move(*screen);
    } else if (!screen) {
      core::logWarning("movies: {}", screen.error().message);
    }
  }
  const platform::PixelSize size = window->pixelSize();
  auto ui = ui::Ui::create({
      .files = &files,
      .renderer = uiRenderer->get(),
      .texts = &*texts,
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
  // The skin goes on before the documents load, so they are styled once.
  applyStartUiSkin(**ui, startUiSkin(options.uiSkin, userSettings.uiSkin));
  UiSkinChanger uiSkinChanger{**ui, userSettings, settingsFile};
  if (auto loaded = loadDocuments(**ui); !loaded) {
    core::logError("game: {}", loaded.error().message);
    return EXIT_FAILURE;
  }
  LanguageChanger languageChanger{*texts, **ui, userSettings, settingsFile};
  (*ui)->setSetting(LanguageSetting, texts->language());
  MinimapChanger minimapChanger{userSettings, settingsFile};
  (*ui)->setSetting(MinimapSetting, std::string{startMinimapMode(userSettings.minimap)});
  (*ui)->setSetting("minimap_size", std::string{"medium"});
  (*ui)->setSetting("fullscreen", settings.fullscreen);
  (*ui)->setSetting("ui_scale", settings.uiScale);
  (*ui)->setSetting("vsync", settings.vsync);
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
  // The console's ui_skin("<id>"), the menus' skin setting, applied between frames in the same way.
  std::optional<std::string> consoleUiSkin;
  (*scripts)->bindCommand(ui::UiSkinSetting, [&consoleUiSkin](std::string_view id) {
    consoleUiSkin = std::string{id};
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
                                .settingsFile = settingsFile},
                               start);
  }};
  TitleScreen title{files, options, !autotest, **ui};
  // What waits for the movie that plays: the title, or a game to start (main.c: main).
  std::optional<PlayStart> startAfterMovie;
  bool titleAfterMovie = false;
  bool moviePlayed = false;
  // The title's ticks without input, and whether the boot movie plays again after them
  // (runLandingScreen); not when it could not play.
  std::uint32_t titleIdleTicks = 0;
  bool replayBootMovie = true;
  const auto playMovie = [&](MovieMoment moment) {
    if (!movies) {
      return false;
    }
    // The music stops before a movie and starts again after it (main.c:3441 finalizeMusic,
    // playMovie, initializeMusic); the game plays no music yet (VAN-01, slice 3).
    core::logDebug("[port:movie] playMovie (main.c:{}): movie {} '{}'; finalizeMusic: no music to "
                   "stop yet",
                   movieCallSite(moment), static_cast<int>(moment), movieRules->of(moment).file);
    if (!movies->play(files, movieRules->of(moment))) {
      return false;
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
    if (auto started = flow.play({.load = options.load, .backup = false, .saveTo = std::nullopt});
        !started) {
      core::logError("game: {}", started.error().message);
      return EXIT_FAILURE;
    }
  } else if (playMovie(MovieMoment::Boot)) {
    titleAfterMovie = true;
  } else {
    title.show();
  }

  FrameStats frameStats;
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
  };

  core::FixedStep step{TickSeconds, MaxFrameSeconds};
  FpsCounter fps;
  MouseSteering mouse;
  std::vector<platform::Event> events;
  std::uint64_t ticksRun = 0;
  double previous = platform->nowSeconds();
  bool running = true;
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
    }
  };
  // After a movie: the game that waited for it, or the title.
  const auto afterMovie = [&] {
    core::logDebug("[port:movie] playMovie (main.c:3443): the movie is over; initializeMusic: no "
                   "music to start yet");
    if (startAfterMovie) {
      startPlaying(*std::exchange(startAfterMovie, std::nullopt));
    } else if (std::exchange(titleAfterMovie, false)) {
      title.show();
    }
    titleIdleTicks = 0;
  };
  std::array<render::Drawer*, 1> movieDrawers{movies ? &movies->drawer() : nullptr};

  while (running) {
    events.clear();
    if (autotest) {
      const PlaySession* play = flow.playing();
      partnerStatus = play != nullptr ? play->partnerStatus() : screens::ScreenSettings{};
      host.scene = play != nullptr ? &play->scene() : nullptr;
      autotest->frame(host, ticksRun, platform->nowSeconds(), events);
      if (autotest->finished()) {
        break;
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
    for (const platform::Event& event : events) {
      if (isPointerOrKey(event) || std::holds_alternative<platform::GamepadButtonChanged>(event)) {
        titleIdleTicks = 0;
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
        continue;
      }
      input.handle(event);
      if (!(*ui)->capturesInput() && flow.playing() != nullptr) {
        mouse.handle(event, flow.playing()->scene());
      }
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
      core::logInfo("game: slow frame, {:.1f} ms, at tick {}{}", frameSeconds * 1000.0, ticksRun,
                    steady ? "" : " (expected: load, resize or capture)");
    }

    const int ticks = step.advance(frameSeconds);
    sound.update(ticks * TickSeconds);
    for (int tick = 0; tick < ticks; ++tick) {
      if (input.wasPressed("debug_console")) {
        (*debugUi)->toggle();
      }
      (*ui)->tick(input, TickSeconds);
      if (movies && movies->playing()) {
        movies->tick(input);
        input.endTick();
        ++ticksRun;
        continue;
      }
      if (flow.playing() == nullptr && movies && replayBootMovie &&
          ++titleIdleTicks >= movieRules->landingIdleFrames) {
        core::logDebug("[port:movie] runLandingScreen (main.c:3604): {} ticks without input on the "
                       "title, the boot movie again",
                       movieRules->landingIdleFrames);
        titleIdleTicks = 0;
        replayBootMovie = playMovie(MovieMoment::Boot);
        titleAfterMovie = replayBootMovie;
      }
      PlaySession* play = flow.playing();
      game::field::FieldInput intent;
      if (play != nullptr && !(*ui)->capturesInput()) {
        const input::Vector2 move = input.vector("move_left", "move_right", "move_up", "move_down");
        // Up gives a negative y; the field's y points away from the camera.
        intent.stick = {move.x, -move.y};
        intent.run = !input.isDown("walk");
        intent.moveTarget = mouse.take();
        if (const auto held = mouse.steer(
                play->scene(), glm::vec2{static_cast<float>((*renderer)->targetExtent().width),
                                         static_cast<float>((*renderer)->targetExtent().height)});
            held && move.x == 0.0F && move.y == 0.0F) {
          intent.stick = *held;
        }
        // Escape, the right button or East drops a click-to-move target.
        intent.cancelTarget = input.wasPressed("cancel");
        intent.confirm = input.wasPressed("confirm");
      } else {
        mouse.release();
      }
      if (play != nullptr) {
        play->tick(intent);
      }
      input.endTick();
      ++ticksRun;
    }

    for (const ui::UiCommand& command : (*ui)->takeCommands()) {
      if (flow.playing() != nullptr && flow.playing()->handle(command)) {
        continue;
      }
      if (flow.playing() == nullptr && title.handle(command)) {
        if (const auto start = title.takeStart()) {
          // A new game's movie plays before its first map (main.c: main, MAIN_STATE 0).
          if (!start->load && playMovie(MovieMoment::NewGame)) {
            startAfterMovie = *start;
          } else {
            startPlaying(*start);
          }
        }
        continue;
      }
      running = apply(command, **ui, *window, settings, languageChanger, textFontChanger,
                      uiSkinChanger, minimapChanger) &&
                running;
    }
    if (flow.playing() != nullptr && flow.playing()->takeToTitle()) {
      // As the original after its ending (main.c: main): the game is torn down whole and the title
      // screen comes back.
      core::logDebug("[port:save] main (main.c:3522-3527): back to the title screen");
      flow.toTitle();
      screenFade->setDarkness(0.0F);
      title.show();
    }
    if (PlaySession* play = flow.playing()) {
      play->update(sessionCommands, autotest.get(), frameStats, frameSeconds,
                   static_cast<float>(step.alpha()));
      screenFade->setDarkness(play->fadeLevel());
      mapChangeView.show(play->scene(), play->fadeLevel());
      if (const auto moment = play->takeMovieRequest()) {
        // The ending closes the game before its movie, then the title (main.c: main, MAIN_STATE 3).
        if (*moment == MovieMoment::Ending) {
          flow.toTitle();
          titleAfterMovie = true;
        }
        if (!playMovie(*moment)) {
          afterMovie();
        }
      }
    } else {
      sessionCommands.drop(autotest.get());
    }
    if (consoleUiSkin) {
      uiSkinChanger.change(*std::exchange(consoleUiSkin, std::nullopt));
    }
    if (consoleTextFont) {
      textFontChanger.change(*std::exchange(consoleTextFont, std::nullopt));
    }
    if (movies) {
      movies->update(frameSeconds);
      if (moviePlayed && !movies->playing()) {
        moviePlayed = false;
        afterMovie();
      }
    }
    (*ui)->update();

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
    std::span<render::Drawer* const> sceneDrawers;
    if (showingMovie) {
      sceneDrawers = movieDrawers;
    } else if (flow.playing() != nullptr) {
      sceneDrawers = flow.playing()->scene().drawers();
    }
    const bool drawn = (*renderer)->renderFrame({
        .clearColor = {0.0F, 0.0F, 0.0F, 1.0F},
        .scene = sceneDrawers,
        .ui = showingMovie ? std::span<render::Drawer* const>{} : uiDrawers,
        .overlay = std::ref(drawOverlay),
    });
    if (!drawn) {
      platform->waitForEvents(IdleWait);
    }
  }
  (*device)->waitIdle();
  if (!autotest) {
    // The locals go in reverse order: the scene and the UI release their textures into the renderer
    // before it.
    return EXIT_SUCCESS;
  }
  autotest->summarize(host);
  // Shut down before judging the run, so that what teardown reports (leaks, objects destroyed in
  // use) counts: the scene and the UI release their textures into the renderer before it, and the
  // device goes last.
  const bool validationActive = (*device)->validationEnabled();
  sessionCommands.drop(autotest.get());
  flow.toTitle();
  scripts->reset();
  ui->reset();
  uiRenderer->reset();
  screenFade.reset();
  renderer->reset();
  debugUi->reset();
  device->reset();
  return autotest->verdict(validationActive);
}

} // namespace dwe::app
