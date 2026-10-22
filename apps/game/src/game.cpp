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
#include <dwe/ruleset/ruleset.hpp>
#include <dwe/script/script_vm.hpp>
#include <dwe/session/evolution_adapter.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/ui/minimap.hpp>
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
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "autotest.hpp"
#include "battle_screen.hpp"
#include "battle_trigger.hpp"
#include "care_commands.hpp"
#include "common.hpp"
#include "content_mount.hpp"
#include "dev_assets_banner.hpp"
#include "field_hud.hpp"
#include "field_scene.hpp"
#include "frame_stats.hpp"
#include "language.hpp"
#include "messages.hpp"
#include "minimap_setting.hpp"
#include "partner_events.hpp"
#include "partner_menu.hpp"
#include "partner_tab.hpp"
#include "session_start.hpp"
#include "text_font_setting.hpp"
#include "ui_skin_setting.hpp"
#include "user_settings.hpp"

namespace dwe::app {
namespace {

constexpr std::string_view MenuDocument = "pause_menu";
// The menu the game starts at (R3.9): play, the language, quit.
constexpr std::string_view MainMenuDocument = "main_menu";
// Smaller changes of the map change's fade are not worth telling the UI.
constexpr double FadeEpsilon = 1e-4;
// Frames left out of the steady frame times after a map change: the new map's textures upload in the next frames.
constexpr int MapChangeSettleFrames = 5;

// Tells the HUD what the map change shows: how much the fade takes away and the key of the map name it shows, which
// the HUD writes in the player's language.
class MapChangeView {
public:
    explicit MapChangeView(ui::Ui& ui) : ui_(&ui) {
        ui_->setSetting(ScreenFadeSetting, fade_);
        ui_->setSetting(MapTitleKeySetting, titleKey_);
    }

    // Publishes the field's fade and map name key when they changed.
    void show(const FieldScene& scene) {
        showFade(scene.field().fadeLevel());
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

// Applies a command of the UI's documents and publishes the resulting state. Returns false for quit.
bool apply(const ui::UiCommand& command, ui::Ui& ui, platform::Window& window, Settings& settings,
           LanguageChanger& language, TextFontChanger& textFont, UiSkinChanger& skin, MinimapChanger& minimap) {
    core::logInfo("game: command {} = {}", command.name, describe(command.value));
    if (command.name == "quit") {
        return false;
    }
    if (command.name == "open_menu") {
        ui.show(MenuDocument, true);
    } else if (command.name == LanguageCommand && std::holds_alternative<std::string>(command.value)) {
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
    } else if (command.name == ui::TextFontSetting && std::holds_alternative<std::string>(command.value)) {
        textFont.change(std::get<std::string>(command.value));
    } else if (command.name == ui::UiSkinSetting && std::holds_alternative<std::string>(command.value)) {
        skin.change(std::get<std::string>(command.value));
    } else if (command.name == "vsync") {
        // The device picks its present mode when it is created (--no-vsync); the menu keeps showing the real one.
        core::logInfo("game: vsync cannot change while running yet");
        ui.setSetting("vsync", settings.vsync);
    }
    return true;
}

// Where the original's need bubble hangs over the partner, in world units: it starts 20 below the top of its
// species' height (bubble.c: addConditionBubble: baseHeight = -height + 20) and rises 65 over its first 10 frames,
// where it settles (bubble.c: tickConditionBubble: activeHeight = baseHeight - 65). Its bob of 12 up and down around
// that point is the skin's animation and is not published.
constexpr float BubbleDrop = 20.0F;
constexpr float BubbleRise = 65.0F;

// Where the partner's need bubble settles in the window; nothing off the map's camera.
std::optional<screens::WindowFraction> partnerBubbleOf(const FieldScene& scene, const assets::DigimonTable* species,
                                                       std::string_view partnerSpecies) {
    const assets::DigimonRow* row = species != nullptr ? species->find(partnerSpecies) : nullptr;
    const float height = row != nullptr ? static_cast<float>(row->height) - BubbleDrop : 0.0F;
    glm::vec3 bubble = scene.field().partner().position;
    bubble.y += height + BubbleRise;
    const std::optional<glm::vec2> fraction = scene.worldToWindowFraction(bubble);
    if (!fraction) {
        return std::nullopt;
    }
    return screens::WindowFraction{.x = fraction->x, .y = fraction->y};
}

ui::MinimapMarker markerOf(const game::field::CharacterState& character) {
    return {.x = character.position.x,
            .z = character.position.z,
            .facingX = std::sin(character.yaw),
            .facingZ = std::cos(character.yaw)};
}

// Starts the battle a battle("npcs") command asks for against the NPCs of the field's map.
void startBattle(BattleScreen& screen, const assets::DigimonTable* species, const FieldScene& scene,
                 std::string_view npcs) {
    auto started = [&]() -> core::Result<void> {
        if (species == nullptr) {
            return core::fail(std::string{messages::NoSpeciesTable});
        }
        auto chosen = npcsOf(npcs);
        if (!chosen) {
            return std::unexpected{chosen.error()};
        }
        auto request = mapBattleRequest(scene.field(), scene.map(), *species, *chosen);
        if (!request) {
            return std::unexpected{request.error()};
        }
        return screen.start(std::move(*request));
    }();
    if (!started) {
        core::logWarning("{}", core::formatMessage(messages::BattleStartFailed, started.error().message));
    }
}

core::Result<void> loadDocuments(ui::Ui& ui) {
    for (const auto& [name, id] :
         {std::pair{"hud", "ui/hud/document"}, std::pair{"pause_menu", "ui/pause_menu/document"},
          std::pair{"main_menu", "ui/main_menu/document"}, std::pair{"battle", "ui/battle/document"},
          std::pair{"partner_menu", "ui/partner_menu/document"}}) {
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

// The mouse on the ground (R2.2): a click walks to the point around walls (click-to-move), and holding the button
// walks towards the pointer for as long as it is held, like a stick, which also reaches exits at the edge of the
// screen that no click can point at.
class MouseSteering {
public:
    // Every move of the pointer, also those the UI takes: the UI keeps the first move after a resize for itself, and a
    // click there must still land where the pointer is, not where it was before.
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

// Makes a catalog the one localization::tr reads while it lives, and clears it before the catalog goes.
class CatalogInUse {
public:
    explicit CatalogInUse(const localization::Catalog& texts) { localization::useCatalog(&texts); }
    CatalogInUse(const CatalogInUse&) = delete;
    CatalogInUse& operator=(const CatalogInUse&) = delete;
    CatalogInUse(CatalogInUse&&) = delete;
    CatalogInUse& operator=(CatalogInUse&&) = delete;
    ~CatalogInUse() { localization::useCatalog(nullptr); }
};

// The desktop's own pointer and keyboard; the gamepad is not among them, as an autotest's is a virtual one.
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

bool isPointerOrKey(const platform::Event& event) {
    return std::holds_alternative<platform::KeyChanged>(event) || std::holds_alternative<platform::MouseMoved>(event) ||
           std::holds_alternative<platform::MouseButtonChanged>(event) ||
           std::holds_alternative<platform::MouseWheelMoved>(event);
}

// Logs which ruleset the game plays with, and where each of its sections comes from.
void logRuleset(const ruleset::Ruleset& rules) {
    std::string sections;
    for (const auto& [name, section] : rules.sections()) {
        if (!sections.empty()) {
            sections += messages::ListSeparator;
        }
        sections += core::formatMessage(messages::RulesetSection, name, section.owner);
    }
    const ruleset::RulesetInfo& info = rules.info();
    core::logInfo("{}", core::formatMessage(messages::RulesetActive, info.id, info.version, info.name, sections));
}

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
    auto content = mountGameContent(options);
    if (!content) {
        core::logError("game: {}", content.error().message);
        return EXIT_FAILURE;
    }
    const assets::VirtualFileSystem& files = (*content)->files();
    // The game session (apps/session): the ruleset, the game clock and the game systems, saved and loaded as one.
    // The tuning panel tunes its ruleset, sets the speed of its clock, pauses it and jumps it.
    auto game = startGame(files, options, !autotest);
    if (!game) {
        core::logError("{}", game.error().message);
        return EXIT_FAILURE;
    }
    session::GameSession& session = *game->session;
    logRuleset(session.rules().current());
    (*debugUi)->attachTuning({.files = &files, .store = &session.rules(), .clock = &session.clock()});
    (*debugUi)->addTuningTab(partnerTab(session));
    if (options.tuning) {
        (*debugUi)->openTuning();
    }
    // An autotest neither reads nor writes the player's settings: its language is --lang or the system's.
    const std::optional<std::filesystem::path> settingsFile = autotest ? std::nullopt : userSettingsFile();
    UserSettings userSettings = settingsFile ? loadUserSettings(*settingsFile) : UserSettings{};
    const std::vector<std::string> languages = localization::availableLanguages(files);
    auto language = startLanguage(languages, options.language, userSettings.language, platform::systemLanguage());
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
    (*ui)->show("hud");
    if (options.devAssets) {
        if (auto shown = showDevAssetsBanner(**ui); !shown) {
            core::logError("game: {}", shown.error().message);
            return EXIT_FAILURE;
        }
    }
    // The game starts at the main menu; an autotest only when its route asks for it, so the others start in the field.
    if (!autotest || autotest->startsAtMainMenu()) {
        (*ui)->show(MainMenuDocument, true);
    }

    // A loaded game starts where it was saved, unless --start says otherwise.
    const std::optional<session::FieldPlace> savedPlace = options.startMap ? std::nullopt : session.savedPlace();
    auto scene = FieldScene::create(**renderer, files,
                                    {.startMap = savedPlace ? std::optional{savedPlace->map} : options.startMap,
                                     .startExit = options.startExit,
                                     .startAt = savedPlace ? std::optional{savedPlace->spawn} : std::nullopt,
                                     .clock = &session.clock(),
                                     .time = options.time});
    if (!scene) {
        core::logError("game: {}", scene.error().message);
        return EXIT_FAILURE;
    }
    session.attachField((*scene)->field());
    // A loaded game's partner may be another species than the game pack's partner model.
    if (auto shown = (*scene)->showPartnerSpecies(session.partnerView().species); !shown) {
        core::logWarning("{}", core::formatMessage(messages::PartnerModelNotShown, session.partnerView().species,
                                                   shown.error().message));
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
    // The console's text_font("original"|"modern"), the main menu's setting; applied between frames, like a menu's
    // command, so the UI never changes fonts while a frame draws it.
    std::optional<std::string> consoleTextFont;
    (*scripts)->bindCommand(ui::TextFontSetting,
                            [&consoleTextFont](std::string_view name) { consoleTextFont = std::string{name}; });
    // The console's ui_skin("<id>"), the menus' skin setting, applied between frames in the same way.
    std::optional<std::string> consoleUiSkin;
    (*scripts)->bindCommand(ui::UiSkinSetting,
                            [&consoleUiSkin](std::string_view id) { consoleUiSkin = std::string{id}; });

    // The battle screen, and the console's and the scripts' battle("npcs") that starts one against the map's NPCs
    // (battle_trigger.hpp), applied between frames like the other commands.
    auto species = speciesTable(files, session.rules().current());
    if (!species) {
        core::logWarning("{}", core::formatMessage(messages::BattleStartFailed, species.error().message));
    }
    BattleScreen battleScreen{{.session = &session,
                               .battle = game->systems.battle,
                               .inventory = game->systems.inventory,
                               .species = species ? &*species : nullptr,
                               .ui = ui->get()}};
    PartnerMenu partnerMenu{{.session = &session,
                             .inventory = game->systems.inventory,
                             .species = species ? &*species : nullptr,
                             .ui = ui->get()}};
    FieldHud fieldHud{**ui, session};
    std::optional<std::string> pendingBattle;
    std::optional<std::string> pendingGift;
    (*scripts)->bindCommand(GiveItemCommand,
                            [&pendingGift](std::string_view item) { pendingGift = std::string{item}; });
    std::optional<std::string> pendingUse;
    (*scripts)->bindCommand(UseItemCommand, [&pendingUse](std::string_view item) { pendingUse = std::string{item}; });
    (*scripts)->bindCommand(BattleCommand,
                            [&pendingBattle](std::string_view npcs) { pendingBattle = std::string{npcs}; });
    // The care debug commands (toilet, sleep, scold, train, clock_speed), applied between frames like the others.
    CareCommands careCommands;
    careCommands.bind(**scripts);
    // The partner's status for the autotest's checks, refreshed every frame of a run.
    screens::ScreenSettings partnerStatus;
    std::optional<std::string> pendingEvolution;
    (*scripts)->bindCommand(EvolveCommand,
                            [&pendingEvolution](std::string_view form) { pendingEvolution = std::string{form}; });

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
        .scripts = scripts->get(),
        .partner = &partnerStatus,
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
            partnerStatus = partnerMenu.statusSettings();
            autotest->frame(host, ticksRun, platform->nowSeconds(), events);
            if (autotest->finished()) {
                break;
            }
        }
        // During an autotest the route is the only player: the desktop's own mouse and keyboard (someone using the
        // computer, the pointer resting over the window) would move the pointer under a click or a hold.
        const std::size_t injected = events.size();
        platform->pollEvents(events, [&](const SDL_Event& raw) {
            if (!autotest || !isPointerOrKeyEvent(raw)) {
                (*debugUi)->handleRawEvent(raw);
            }
        });
        if (autotest) {
            const auto real = std::ranges::remove_if(events.begin() + static_cast<std::ptrdiff_t>(injected),
                                                     events.end(), isPointerOrKey);
            events.erase(real.begin(), real.end());
        }
        for (const platform::Event& event : events) {
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
            // The menus pause the field and the game time; a battle runs instead of the field.
            if (battleScreen.active()) {
                if (!(*ui)->isVisible(MenuDocument)) {
                    battleScreen.tick(TickSeconds);
                }
            } else if (!(*ui)->isVisible(MenuDocument) && !(*ui)->isVisible(MainMenuDocument) &&
                       !(*ui)->isVisible(PartnerMenuDocument)) {
                (*scene)->tick(intent, static_cast<float>(TickSeconds));
                session.tick(TickSeconds);
                (*scene)->finishTick();
            }
            input.endTick();
            ++ticksRun;
        }

        for (const ui::UiCommand& command : (*ui)->takeCommands()) {
            if (battleScreen.handle(command) || partnerMenu.handle(command)) {
                continue;
            }
            running = apply(command, **ui, *window, settings, languageChanger, textFontChanger, uiSkinChanger,
                            minimapChanger) &&
                      running;
        }
        if (pendingGift) {
            if (auto given = partnerMenu.giveItem(*std::exchange(pendingGift, std::nullopt)); !given) {
                core::logWarning("{}", core::formatMessage(messages::GiveItemFailed, given.error().message));
            }
        }
        if (pendingUse) {
            if (auto used = partnerMenu.useItem(*std::exchange(pendingUse, std::nullopt)); !used) {
                core::logWarning("{}", core::formatMessage(messages::UseItemFailed, used.error().message));
            }
        }
        if (pendingBattle) {
            startBattle(battleScreen, species ? &*species : nullptr, **scene,
                        *std::exchange(pendingBattle, std::nullopt));
        }
        careCommands.apply(session);
        if (pendingEvolution) {
            const std::string form = *std::exchange(pendingEvolution, std::nullopt);
            if (auto evolved = game->systems.evolution->evolveByScript(session, form); !evolved) {
                core::logWarning("{}", core::formatMessage(messages::EvolveFailed, evolved.error().message));
            } else if (!*evolved) {
                core::logWarning("{}", core::formatMessage(messages::EvolveRefused, form));
            }
        }
        if (consoleUiSkin) {
            uiSkinChanger.change(*std::exchange(consoleUiSkin, std::nullopt));
        }
        if (consoleTextFont) {
            textFontChanger.change(*std::exchange(consoleTextFont, std::nullopt));
        }
        if ((*scene)->mapSerial() != mapSerial) {
            mapSerial = (*scene)->mapSerial();
            (*ui)->setMinimap(ui::minimapGridOf((*scene)->map()), (*scene)->mapNameKey());
            if (const auto& change = (*scene)->lastMapChange(); change && autotest) {
                autotest->mapChanged(*change);
            }
            frameStats.skipNext(MapChangeSettleFrames);
        }
        const game::field::Field& field = (*scene)->field();
        (*ui)->setMinimapMarkers(markerOf(field.player()), markerOf(field.partner()));
        screenFade->setDarkness(field.fadeLevel());
        mapChangeView.show(**scene);

        partnerMenu.update();
        showPartnerEvents(session.takePartnerEvents(), session, **scene);

        const bool paused = (*ui)->isVisible(MenuDocument) || (*ui)->isVisible(MainMenuDocument) ||
                            (*ui)->isVisible(PartnerMenuDocument) || battleScreen.active();
        fieldHud.update(!paused,
                        partnerBubbleOf(**scene, species ? &*species : nullptr, session.partnerView().species));
        (*scene)->update(frameSeconds, static_cast<float>(step.alpha()), paused);
        (*ui)->update();

        overlayStats = {
            .framesPerSecond = fps.framesPerSecond,
            .frameMilliseconds = frameSeconds * 1000.0,
            .ticksThisFrame = ticks,
        };
        // The fade goes over the scene and under the UI: the HUD fades its panels itself and keeps the map's name
        // on top, as the original draws it in front of the fade (map.c: renderMapName, layer 0).
        const std::array<render::Drawer*, 2> uiDrawers{&*screenFade, uiRenderer->get()};
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
    session.detachField();
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
    screenFade.reset();
    renderer->reset();
    debugUi->reset();
    device->reset();
    return autotest->verdict(validationActive);
}

} // namespace dwe::app
