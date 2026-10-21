#include <dwe/assets/image.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/localization/catalog.hpp>
#include <dwe/script/script_vm.hpp>
#include <dwe/ui/navigation.hpp>
#include <dwe/ui/prompts.hpp>
#include <dwe/ui/ui.hpp>

#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/StringUtilities.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "code_trust.hpp"
#include "font_engine.hpp"
#include "messages.hpp"
#include "render_bridge.hpp"
#include "rml_interfaces.hpp"
#include "scripting.hpp"

namespace dwe::ui {
namespace {

constexpr std::string_view ContextName = "dwe";

// The window height the UI is designed at: one dp is one pixel there. The width bound keeps narrow windows (4:3 and
// narrower) from cutting the menus.
constexpr float DesignHeight = 720.0F;
constexpr float DesignWidth = 960.0F;
constexpr float MinDpRatio = 0.5F;
constexpr float MaxDpRatio = 8.0F;

// The commands the menus may queue between two takeCommands(): more is a script gone wrong.
constexpr std::size_t MaxPendingCommands = 256;

// The units the markers are written in: thousandths of a percent of the minimap and tenths of a degree.
constexpr float MarkerPositionSteps = 100'000.0F;
constexpr float MarkerHeadingSteps = 10.0F;

// How often hot reload looks at the files, in seconds.
constexpr double ReloadInterval = 0.5;

// There may be one Ui at a time, because RmlUi's interfaces and factory are global.
bool uiAlive = false; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): guards RmlUi's global state.

std::string_view textOf(std::span<const std::byte> bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

float dpRatioFor(int width, int height, float scale) {
    const float fit = std::min(static_cast<float>(height) / DesignHeight, static_cast<float>(width) / DesignWidth);
    return std::clamp(fit, MinDpRatio, MaxDpRatio) * scale;
}

// The player's scale option within the range Ui::setScale documents, or nothing for a value that is not a number.
std::optional<float> validScale(float scale) {
    if (!std::isfinite(scale)) {
        return std::nullopt;
    }
    return std::clamp(scale, Ui::MinScale, Ui::MaxScale);
}

std::string_view actionName(NavCommand command) {
    switch (command) {
    case NavCommand::Up:
        return "up";
    case NavCommand::Down:
        return "down";
    case NavCommand::Left:
        return "left";
    case NavCommand::Right:
        return "right";
    case NavCommand::Confirm:
        return "confirm";
    case NavCommand::Cancel:
        return "cancel";
    case NavCommand::Menu:
        return "menu";
    }
    return "";
}

std::optional<Rml::Input::KeyIdentifier> keyOf(NavCommand command) {
    switch (command) {
    case NavCommand::Up:
        return Rml::Input::KI_UP;
    case NavCommand::Down:
        return Rml::Input::KI_DOWN;
    case NavCommand::Left:
        return Rml::Input::KI_LEFT;
    case NavCommand::Right:
        return Rml::Input::KI_RIGHT;
    case NavCommand::Confirm:
        return Rml::Input::KI_RETURN;
    case NavCommand::Cancel:
    case NavCommand::Menu:
        break;
    }
    return std::nullopt;
}

// RmlUi's mouse button numbers.
std::optional<int> mouseButtonIndex(platform::MouseButton button) {
    switch (button) {
    case platform::MouseButton::Left:
        return 0;
    case platform::MouseButton::Right:
        return 1;
    case platform::MouseButton::Middle:
        return 2;
    case platform::MouseButton::Back:
    case platform::MouseButton::Forward:
        break;
    }
    return std::nullopt;
}

std::string percent(float value) {
    return std::format("{:.3f}%", value * 100.0F);
}

// The handler a shown document gets when the language changes.
constexpr std::string_view LanguageHandler = "on_language";
constexpr std::string_view Blanks = " \t";

// A text without the blanks around it: the original pads map names to centre them in its box.
std::string_view trimmed(std::string_view text) {
    const std::size_t first = text.find_first_not_of(Blanks);
    if (first == std::string_view::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(Blanks) - first + 1);
}

// Gives every element under root with a TextKeyAttribute the text of its key.
void applyTextKeys(Rml::Element& root, const localization::Catalog& texts) {
    if (const Rml::Variant* key = root.GetAttribute(Rml::String{TextKeyAttribute})) {
        root.SetInnerRML(Rml::StringUtilities::EncodeRml(texts.text(key->Get<Rml::String>())));
    }
    for (int i = 0; i < root.GetNumChildren(); ++i) {
        applyTextKeys(*root.GetChild(i), texts);
    }
}

} // namespace

struct Ui::Impl {
    struct Document {
        ScriptedDocument* element = nullptr;
        assets::AssetId asset;
        std::string path;
        std::map<std::string, std::size_t> files;
    };

    // A marker's elements and the values they show, so markers that did not move cost nothing.
    struct MarkerState {
        bool visible = false;
        long u = 0;
        long v = 0;
        long heading = 0;
        bool operator==(const MarkerState&) const = default;
    };

    struct MarkerSlot {
        std::string_view id;
        std::vector<Rml::ObserverPtr<Rml::Element>> elements;
        std::optional<MarkerState> shown;
    };

    struct Minimap {
        MinimapLayout layout;
        std::string source; // the generated image's name, with the prefix
        std::string mapNameKey;
        float aspect = 1.0F;
    };

    UiDesc desc;
    std::unique_ptr<script::ScriptVm> vm;
    SystemInterface system;
    CodeTrust trust;
    FileInterface files;
    TextureSources sources{files};
    RenderBridge render{sources, desc.renderer};
    std::unique_ptr<Scripting> scripting;
    // Installed over RmlUi's own after it starts; RmlUi shuts it down, so it outlives Rml::Shutdown.
    std::unique_ptr<FontEngine> fontEngine;
    Rml::Context* context = nullptr;

    std::map<std::string, Document, std::less<>> documents;
    std::vector<std::string> modalStack;
    std::string menuDocument;
    Navigation navigation;
    std::vector<NavCommand> pendingSteps;
    bool capturedThisTick = false;
    // Whether the pointer position is the player's: false after a resize or a focus loss, until it moves again.
    bool pointerKnown = true;
    // The entry under the pointer at its last move: the focus follows the pointer only onto another entry.
    Rml::ObserverPtr<Rml::Element> pointerEntry;
    std::array<bool, platform::MouseButtonCount> capturedButtons{};
    // RmlUi's buttons (left, right, middle) held down, released on a focus loss.
    std::array<bool, 3> rmlButtonsDown{};

    std::vector<UiCommand> commands;
    std::map<std::string, UiValue, std::less<>> settings;
    const input::InputState* input = nullptr;
    input::DeviceKind device = input::DeviceKind::KeyboardMouse;
    platform::GamepadFamily family = platform::GamepadFamily::Generic;

    std::optional<Minimap> minimap;
    int minimapGeneration = 0;
    std::array<MarkerSlot, 2> markers{MarkerSlot{.id = hud::MinimapPlayer, .elements = {}, .shown = {}},
                                      MarkerSlot{.id = hud::MinimapPartner, .elements = {}, .shown = {}}};
    bool markersStale = true;
    // The texts: the app's catalog, or ownTexts read from the packs when it gives none.
    const localization::Catalog* texts = nullptr;
    std::optional<localization::Catalog> ownTexts;
    double lastReloadCheck = 0.0;

    explicit Impl(const UiDesc& uiDesc, std::unique_ptr<script::ScriptVm> scripts)
        : desc{uiDesc}, vm{std::move(scripts)}, system{uiDesc.clock}, trust{*uiDesc.files},
          files{*uiDesc.files, trust} {}

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;

    ~Impl() {
        // Documents hold Lua references, and Lua holds pointers into RmlUi: the documents go first, then Lua, then
        // RmlUi.
        if (context != nullptr) {
            Rml::RemoveContext(std::string{ContextName});
        }
        scripting.reset();
        vm.reset();
        Rml::Shutdown();
        uiAlive = false;
    }

    ScriptHost host() {
        return ScriptHost{
            .send =
                [this](UiCommand command) {
                    if (commands.size() >= MaxPendingCommands) {
                        return false;
                    }
                    commands.push_back(std::move(command));
                    return true;
                },
            .setting = [this](std::string_view name) -> UiValue {
                auto found = settings.find(name);
                return found == settings.end() ? UiValue{} : found->second;
            },
            .prompt = [this](std::string_view action) -> std::string {
                static const input::ActionMap defaults = input::defaultActionMap();
                return promptLabel(input != nullptr ? input->actionMap() : defaults, device, family, action, *texts);
            },
            .text =
                [this](std::string_view key, std::span<const localization::TextArgument> arguments) {
                    return texts->text(key, arguments);
                },
            .languages =
                [this]() { return std::vector<std::string>{texts->languages().begin(), texts->languages().end()}; },
            .device = [this]() -> std::string {
                return device == input::DeviceKind::Gamepad ? "gamepad" : "keyboard_mouse";
            },
            .gamepadFamily = [this]() { return std::string{gamepadFamilyName(family)}; },
            .show = [this](std::string_view name) { show(name, false); },
            .hide = [this](std::string_view name) { hide(name); },
            .isVisible = [this](std::string_view name) { return isVisible(name); },
        };
    }

    [[nodiscard]] core::Result<void> loadFonts() const {
        int loaded = 0;
        for (const assets::AssetId& id : desc.files->ids()) {
            if (!id.str().starts_with("ui/fonts/")) {
                continue;
            }
            const assets::AssetSource* source = desc.files->resolve(id);
            const std::string extension = source->file.extension().string();
            if (extension != ".ttf" && extension != ".otf") {
                continue;
            }
            if (!Rml::LoadFontFace(id.str() + extension)) {
                return core::fail(std::format("could not load the font {}", id.str()));
            }
            ++loaded;
        }
        if (loaded == 0) {
            return core::fail("no fonts under ui/fonts/ in the packs");
        }
        return {};
    }

    // Puts the "original look" family over RmlUi's font engine. Without its description in the packs there is no
    // such family; without the extracted bitmap font, it draws with the modern font.
    [[nodiscard]] core::Result<void> installFontEngine() {
        auto id = assets::AssetId::parse(OriginalLookAsset);
        if (!id || desc.files->resolve(*id) == nullptr) {
            core::logWarning("{}", core::formatMessage(messages::OriginalLookMissing, OriginalLookAsset));
            return {};
        }
        auto bytes = desc.files->read(*id);
        if (!bytes) {
            return std::unexpected{bytes.error()};
        }
        auto look = parseOriginalLook(textOf(*bytes));
        if (!look) {
            return std::unexpected{look.error()};
        }
        auto bitmap = loadBitmapFont(*look);
        if (bitmap) {
            core::logInfo("{}", core::formatMessage(messages::BitmapFontLoaded, look->atlas, bitmap->characterCount(),
                                                    bitmap->cellWidth(), bitmap->cellHeight()));
        } else {
            core::logWarning("{}",
                             core::formatMessage(messages::BitmapFontUnavailable, look->atlas, bitmap.error().message));
        }
        fontEngine = std::make_unique<FontEngine>(*Rml::GetFontEngineInterface(), std::move(*look),
                                                  bitmap ? std::optional{std::move(*bitmap)} : std::nullopt);
        Rml::SetFontEngineInterface(fontEngine.get());
        return {};
    }

    [[nodiscard]] core::Result<BitmapFont> loadBitmapFont(const OriginalLook& look) const {
        auto atlasId = assets::AssetId::parse(look.atlas);
        auto metaId = assets::AssetId::parse(look.meta);
        if (!atlasId) {
            return std::unexpected{atlasId.error()};
        }
        if (!metaId) {
            return std::unexpected{metaId.error()};
        }
        auto png = desc.files->read(*atlasId);
        if (!png) {
            return std::unexpected{png.error()};
        }
        auto atlas = assets::decodePng(*png);
        if (!atlas) {
            return std::unexpected{atlas.error()};
        }
        auto meta = desc.files->read(*metaId);
        if (!meta) {
            return std::unexpected{meta.error()};
        }
        return BitmapFont::parse(textOf(*meta), std::move(*atlas), look.baseline);
    }

    void setTextFont(TextFont font) const {
        if (fontEngine == nullptr || fontEngine->textFont() == font) {
            return;
        }
        fontEngine->setTextFont(font);
        core::logInfo("{}", core::formatMessage(messages::TextFontChanged, textFontName(font)));
        // RmlUi asks for the faces again, so every element with the family lays out and draws with the new font.
        Rml::ReleaseFontResources();
    }

    [[nodiscard]] core::Result<void> loadTexts() {
        if (desc.texts != nullptr) {
            texts = desc.texts;
            return {};
        }
        auto loaded = localization::Catalog::load(*desc.files, localization::FallbackLanguage);
        if (!loaded) {
            return core::fail(std::format(messages::NoTexts, loaded.error().message));
        }
        ownTexts = std::move(*loaded);
        texts = &*ownTexts;
        return {};
    }

    Document* find(std::string_view name) {
        auto found = documents.find(name);
        return found == documents.end() ? nullptr : &found->second;
    }

    [[nodiscard]] const Document* find(std::string_view name) const {
        auto found = documents.find(name);
        return found == documents.end() ? nullptr : &found->second;
    }

    // Loads a document without making it the one known by its name.
    core::Result<Document> loadDetached(std::string_view name, const assets::AssetId& asset) {
        const assets::AssetSource* source = desc.files->resolve(asset);
        if (source == nullptr) {
            return core::fail(std::format("no document {} in the packs", asset.str()));
        }
        const std::string path = asset.str() + source->file.extension().string();
        files.startRecording();
        // RmlUi caches templates by path and by name, and a cached one is not read again: a mod's template that an
        // earlier load read would reach this document unseen by the trust, and its event attributes would run as the
        // engine's. Emptying the cache makes every load read, and so judge, the templates it uses.
        Rml::Factory::ClearTemplateCache();
        trust.beginLoad(path);
        Rml::ElementDocument* loaded = context->LoadDocument(path);
        core::Result<void> ownCode = trust.endLoad();
        auto read = files.stopRecording();
        auto* document = dynamic_cast<ScriptedDocument*>(loaded);
        if (document == nullptr) {
            if (loaded != nullptr) {
                context->UnloadDocument(loaded);
            }
            return core::fail(std::format("could not load the document {}", path));
        }
        // A document that read a mod's template while loading loses its own code, also the handlers made before.
        if (document->ownCode()) {
            document->setOwnCode(std::move(ownCode));
        }
        document->setName(name);
        applyTextKeys(*document, *texts);
        applyMinimap(*document);
        return Document{.element = document, .asset = asset, .path = path, .files = std::move(read)};
    }

    core::Result<void> load(std::string_view name, const assets::AssetId& asset) {
        auto loaded = loadDetached(name, asset);
        if (!loaded) {
            return std::unexpected{loaded.error()};
        }
        if (Document* old = find(name)) {
            std::erase(modalStack, name);
            context->UnloadDocument(old->element);
        }
        documents.insert_or_assign(std::string{name}, std::move(*loaded));
        markersStale = true;
        return {};
    }

    void show(std::string_view name, bool modal) {
        Document* document = find(name);
        if (document == nullptr) {
            core::logWarning("ui: no document named {}", name);
            return;
        }
        if (document->element->IsVisible()) {
            return;
        }
        document->element->Show(modal ? Rml::ModalFlag::Modal : Rml::ModalFlag::None, Rml::FocusFlag::Auto);
        if (modal) {
            modalStack.emplace_back(name);
            navigation.reset();
        }
        document->element->callHandler("on_show");
    }

    void hide(std::string_view name) {
        Document* document = find(name);
        if (document == nullptr || !document->element->IsVisible()) {
            return;
        }
        document->element->Hide();
        std::erase(modalStack, name);
        document->element->callHandler("on_hide");
    }

    [[nodiscard]] bool isVisible(std::string_view name) const {
        const Document* document = find(name);
        return document != nullptr && document->element->IsVisible();
    }

    Document* topModal() { return modalStack.empty() ? nullptr : find(modalStack.back()); }

    template <typename Function>
    void forEachVisible(Function function) {
        for (auto& [name, document] : documents) {
            if (document.element->IsVisible()) {
                function(document);
            }
        }
    }

    // Whether an element is one the player picks: keyboard and gamepad navigation stops on it.
    static bool selectable(const Rml::Element* element) {
        return element != nullptr && element->GetComputedValues().tab_index() == Rml::Style::TabIndex::Auto;
    }

    // The entry the pointer is over, if any.
    [[nodiscard]] Rml::Element* entryUnderPointer() const {
        for (Rml::Element* element = context->GetHoverElement(); element != nullptr;
             element = element->GetParentNode()) {
            if (selectable(element)) {
                return element;
            }
        }
        return nullptr;
    }

    // While a menu is open the focus follows the pointer onto each entry it enters, so the highlighted entry is the
    // one a click or confirm acts on. A pointer resting on an entry never takes the focus back from the keyboard or
    // the gamepad, and neither does the position a window reports after a resize, which the player did not move.
    bool pointerMoved(const platform::MouseMoved& moved) {
        context->ProcessMouseMove(static_cast<int>(moved.x), static_cast<int>(moved.y), 0);
        Rml::Element* entry = entryUnderPointer();
        const bool entered = entry != pointerEntry.get();
        pointerEntry = entry != nullptr ? entry->GetObserverPtr() : Rml::ObserverPtr<Rml::Element>{};
        if (!std::exchange(pointerKnown, true)) {
            return true; // nor switch the prompts to the mouse
        }
        if (entered && entry != nullptr && !modalStack.empty() && context->GetFocusElement() != entry) {
            entry->Focus(true);
        }
        return false;
    }

    // The window lost the focus: buttons held now are released where the UI cannot see it. RmlUi forgets them
    // without a click, because the pointer leaves first.
    void releaseButtons() {
        if (std::ranges::any_of(rmlButtonsDown, [](bool down) { return down; })) {
            context->ProcessMouseLeave();
            for (std::size_t index = 0; index < rmlButtonsDown.size(); ++index) {
                if (std::exchange(rmlButtonsDown.at(index), false)) {
                    context->ProcessMouseButtonUp(static_cast<int>(index), 0);
                }
            }
        }
        capturedButtons.fill(false);
        pointerKnown = false;
    }

    bool handleEvent(const platform::Event& event) {
        if (const auto* moved = std::get_if<platform::MouseMoved>(&event)) {
            return pointerMoved(*moved);
        }
        if (std::holds_alternative<platform::FocusLost>(event)) {
            releaseButtons();
            return false;
        }
        if (const auto* button = std::get_if<platform::MouseButtonChanged>(&event)) {
            const auto slot = static_cast<std::size_t>(button->button);
            if (!button->down) {
                if (auto index = mouseButtonIndex(button->button)) {
                    rmlButtonsDown.at(static_cast<std::size_t>(*index)) = false;
                    context->ProcessMouseButtonUp(*index, 0);
                }
                return std::exchange(capturedButtons.at(slot), false);
            }
            // A right click backs out of a menu, as cancel does.
            if (!modalStack.empty() && button->button == platform::MouseButton::Right) {
                pendingSteps.push_back(NavCommand::Cancel);
            }
            if (auto index = mouseButtonIndex(button->button)) {
                Rml::Element* focused = context->GetFocusElement();
                rmlButtonsDown.at(static_cast<std::size_t>(*index)) = true;
                context->ProcessMouseButtonDown(*index, 0);
                // RmlUi focuses whatever is clicked, such as a menu's background; keep the focus on an entry, so
                // the keyboard and the gamepad go on from it.
                Rml::Element* clicked = context->GetFocusElement();
                if (focused != nullptr && clicked != focused && !selectable(clicked)) {
                    focused->Focus();
                }
            }
            capturedButtons.at(slot) = !modalStack.empty() || context->IsMouseInteracting();
            return capturedButtons.at(slot);
        }
        if (const auto* wheel = std::get_if<platform::MouseWheelMoved>(&event)) {
            // RmlUi scrolls down for positive values; the platform reports turning the wheel away as positive.
            context->ProcessMouseWheel(Rml::Vector2f{wheel->x, -wheel->y}, 0);
            return !modalStack.empty() || context->IsMouseInteracting();
        }
        if (const auto* resized = std::get_if<platform::WindowResized>(&event)) {
            resize(resized->width, resized->height);
            pointerKnown = false;
        }
        return false;
    }

    void step(NavCommand command) {
        Document* modal = topModal();
        if (modal == nullptr) {
            if (command == NavCommand::Menu && find(menuDocument) != nullptr) {
                show(menuDocument, true);
                capturedThisTick = true;
            }
            return;
        }
        capturedThisTick = true;
        if (modal->element->callHandler("on_action", std::string{actionName(command)})) {
            return;
        }
        if (auto key = keyOf(command)) {
            context->ProcessKeyDown(*key, 0);
            context->ProcessKeyUp(*key, 0);
        } else {
            hide(modal->element->name());
        }
    }

    void tick(const input::InputState& state, double seconds) {
        input = &state;
        capturedThisTick = false;
        if (state.lastDevice() != device || state.lastGamepadFamily() != family) {
            device = state.lastDevice();
            family = state.lastGamepadFamily();
            forEachVisible([](Document& document) { document.element->callHandler("on_device"); });
        }
        std::vector<NavCommand> steps = std::exchange(pendingSteps, {});
        navigation.tick(state, seconds, steps);
        for (const NavCommand command : steps) {
            step(command);
        }
        forEachVisible([seconds](Document& document) { document.element->callHandler("on_tick", seconds); });
    }

    void setSetting(std::string_view name, UiValue value) {
        auto found = settings.find(name);
        if (found != settings.end() && found->second == value) {
            return;
        }
        settings.insert_or_assign(std::string{name}, std::move(value));
        forEachVisible([name](Document& document) { document.element->callHandler("on_setting", std::string{name}); });
    }

    void resize(int width, int height) {
        desc.width = width;
        desc.height = height;
        context->SetDimensions({width, height});
        context->SetDensityIndependentPixelRatio(dpRatioFor(width, height, desc.scale));
    }

    void applyMinimap(Rml::ElementDocument& document) {
        Rml::Element* image = document.GetElementById(Rml::String{hud::MinimapImage});
        if (image == nullptr) {
            return;
        }
        Rml::Element* frame = document.GetElementById(Rml::String{hud::MinimapFrame});
        if (!minimap) {
            image->SetAttribute("src", std::string{});
            return;
        }
        image->SetAttribute("src", minimap->source);
        if (frame != nullptr) {
            // The frame keeps the image's proportions inside its square panel, centred.
            const float width = minimap->aspect >= 1.0F ? 1.0F : minimap->aspect;
            const float height = minimap->aspect >= 1.0F ? 1.0F / minimap->aspect : 1.0F;
            frame->SetProperty("width", percent(width));
            frame->SetProperty("height", percent(height));
            frame->SetProperty("left", percent((1.0F - width) / 2.0F));
            frame->SetProperty("top", percent((1.0F - height) / 2.0F));
        }
        if (Rml::Element* name = document.GetElementById(Rml::String{hud::MinimapName})) {
            const std::string text = minimap->mapNameKey.empty() ? std::string{} : texts->text(minimap->mapNameKey);
            name->SetInnerRML(Rml::StringUtilities::EncodeRml(Rml::String{trimmed(text)}));
        }
    }

    void refreshTexts() {
        for (auto& [name, document] : documents) {
            applyTextKeys(*document.element, *texts);
            applyMinimap(*document.element);
        }
        forEachVisible([](Document& document) { document.element->callHandler(LanguageHandler); });
    }

    void setMinimap(const MinimapGrid& grid, std::string_view mapNameKey) {
        if (minimap) {
            Rml::ReleaseTexture(minimap->source);
            sources.dropGenerated(minimap->source.substr(GeneratedPrefix.size()));
        }
        minimap.reset();
        if (auto built = buildMinimap(grid)) {
            const std::string name = std::format("minimap/{}", ++minimapGeneration);
            minimap = Minimap{
                .layout = built->layout,
                .source = std::string{GeneratedPrefix} + name,
                .mapNameKey = std::string{mapNameKey},
                .aspect = static_cast<float>(built->image.width) / static_cast<float>(built->image.height),
            };
            sources.setGenerated(name, std::move(built->image));
        } else {
            core::logWarning("ui: the map {} has no walkable cell for the minimap", mapNameKey);
        }
        for (auto& [name, document] : documents) {
            applyMinimap(*document.element);
        }
        for (MarkerSlot& slot : markers) {
            slot.shown.reset();
        }
    }

    // Finds the markers' elements again, after documents were loaded or reloaded.
    void findMarkers() {
        for (MarkerSlot& slot : markers) {
            slot.elements.clear();
            slot.shown.reset();
            for (auto& [name, document] : documents) {
                if (Rml::Element* element = document.element->GetElementById(Rml::String{slot.id})) {
                    slot.elements.push_back(element->GetObserverPtr());
                }
            }
        }
        markersStale = false;
    }

    void placeMarker(MarkerSlot& slot, const std::optional<MinimapMarker>& marker) {
        MarkerState state;
        if (marker && minimap) {
            const MinimapPoint point = locateOnMinimap(minimap->layout, marker->x, marker->z);
            state = MarkerState{
                .visible = true,
                .u = std::lround(std::clamp(point.u, 0.0F, 1.0F) * MarkerPositionSteps),
                .v = std::lround(std::clamp(point.v, 0.0F, 1.0F) * MarkerPositionSteps),
                .heading = std::lround(minimapHeading(marker->facingX, marker->facingZ) * MarkerHeadingSteps),
            };
        }
        if (slot.shown == state) {
            return;
        }
        slot.shown = state;
        for (const Rml::ObserverPtr<Rml::Element>& element : slot.elements) {
            if (!element) {
                continue;
            }
            if (!state.visible) {
                element->SetProperty("display", "none");
                continue;
            }
            element->SetProperty("display", "block");
            element->SetProperty("left", percent(static_cast<float>(state.u) / MarkerPositionSteps));
            element->SetProperty("top", percent(static_cast<float>(state.v) / MarkerPositionSteps));
            element->SetProperty(
                "transform", std::format("rotate({:.1f}deg)", static_cast<float>(state.heading) / MarkerHeadingSteps));
        }
    }

    void placeMarkers(const MinimapMarker& player, const std::optional<MinimapMarker>& partner) {
        if (markersStale) {
            findMarkers();
        }
        placeMarker(markers[0], player);
        placeMarker(markers[1], partner);
    }

    void reloadChanged() {
        if (!desc.hotReload) {
            return;
        }
        const double now = system.GetElapsedTime();
        if (now - lastReloadCheck < ReloadInterval) {
            return;
        }
        lastReloadCheck = now;
        std::vector<std::string> stale;
        for (const auto& [name, document] : documents) {
            const bool changed = std::ranges::any_of(document.files, [&](const auto& file) {
                return files.fingerprint(file.first) != std::optional<std::size_t>{file.second};
            });
            if (changed) {
                stale.push_back(name);
            }
        }
        for (const std::string& name : stale) {
            reload(name);
        }
    }

    // Loads the document again and swaps it in only when that worked: a broken edit leaves the old one running.
    // A shown document is shown again in its place among the modal ones.
    void reload(const std::string& name) {
        Document& old = *find(name);
        Rml::Factory::ClearStyleSheetCache();
        Rml::Factory::ClearTemplateCache();
        auto fresh = loadDetached(name, old.asset);
        if (!fresh) {
            core::logError("ui: could not reload {}: {}", name, fresh.error().message);
            // Wait for the next edit instead of failing again every check.
            for (auto& [path, fingerprint] : old.files) {
                fingerprint = files.fingerprint(path).value_or(0);
            }
            return;
        }
        const bool visible = old.element->IsVisible();
        const auto place = std::ranges::find(modalStack, name);
        const bool modal = place != modalStack.end();
        context->UnloadDocument(old.element);
        old = std::move(*fresh);
        markersStale = true;
        core::logInfo("ui: reloaded {}", name);
        if (!visible) {
            return;
        }
        if (!modal) {
            old.element->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
        } else {
            // The documents above it in the stack are shown again after it, so they stay in front.
            for (auto above = place; above != modalStack.end(); ++above) {
                find(*above)->element->Show(Rml::ModalFlag::Modal, Rml::FocusFlag::Auto);
            }
        }
        old.element->callHandler("on_show");
    }
};

core::Result<std::unique_ptr<Ui>> Ui::create(const UiDesc& desc) {
    if (desc.files == nullptr) {
        return core::fail("the UI needs a file system");
    }
    if (uiAlive) {
        return core::fail("there may be only one UI at a time");
    }
    auto vm = script::ScriptVm::create(desc.scripts);
    if (!vm) {
        return std::unexpected{vm.error()};
    }
    uiAlive = true;
    auto impl = std::make_unique<Impl>(desc, std::move(*vm));
    impl->desc.scale = validScale(desc.scale).value_or(1.0F);
    impl->scripting = std::make_unique<Scripting>(*impl->vm, impl->host(), impl->trust);

    Rml::SetSystemInterface(&impl->system);
    Rml::SetFileInterface(&impl->files);
    Rml::SetRenderInterface(&impl->render);
    if (!Rml::Initialise()) {
        return core::fail("could not start RmlUi");
    }
    if (auto fonts = impl->installFontEngine(); !fonts) {
        return std::unexpected{fonts.error()};
    }
    impl->scripting->install();
    if (auto texts = impl->loadTexts(); !texts) {
        return std::unexpected{texts.error()};
    }
    constexpr std::uint32_t MarkerSize = 64;
    impl->sources.setGenerated("minimap_marker", buildMinimapMarker(MarkerSize));
    if (auto fonts = impl->loadFonts(); !fonts) {
        return std::unexpected{fonts.error()};
    }
    impl->context = Rml::CreateContext(std::string{ContextName}, {desc.width, desc.height});
    if (impl->context == nullptr) {
        return core::fail("could not create the UI context");
    }
    impl->resize(desc.width, desc.height);
    core::logInfo("RmlUi {} started", Rml::GetVersion());
    return std::unique_ptr<Ui>{new Ui{std::move(impl)}};
}

Ui::Ui(std::unique_ptr<Impl> impl) : impl_{std::move(impl)} {}

Ui::~Ui() = default;

core::Result<void> Ui::loadDocument(std::string_view name, const assets::AssetId& document) {
    return impl_->load(name, document);
}

void Ui::show(std::string_view name, bool modal) {
    impl_->show(name, modal);
}

void Ui::hide(std::string_view name) {
    impl_->hide(name);
}

bool Ui::isVisible(std::string_view name) const {
    return impl_->isVisible(name);
}

void Ui::setTextFont(TextFont font) {
    impl_->setTextFont(font);
}

TextFont Ui::textFont() const {
    return impl_->fontEngine != nullptr ? impl_->fontEngine->textFont() : TextFont::Modern;
}

void Ui::setMenuDocument(std::string_view name) {
    impl_->menuDocument = name;
}

bool Ui::handleEvent(const platform::Event& event) {
    return impl_->handleEvent(event);
}

void Ui::tick(const input::InputState& input, double seconds) {
    impl_->tick(input, seconds);
}

bool Ui::capturesInput() const {
    return !impl_->modalStack.empty() || impl_->capturedThisTick;
}

std::vector<UiCommand> Ui::takeCommands() {
    return std::exchange(impl_->commands, {});
}

void Ui::setSetting(std::string_view name, UiValue value) {
    impl_->setSetting(name, std::move(value));
}

UiValue Ui::setting(std::string_view name) const {
    auto found = impl_->settings.find(name);
    return found == impl_->settings.end() ? UiValue{} : found->second;
}

void Ui::resize(int width, int height) {
    impl_->resize(width, height);
}

void Ui::setScale(float scale) {
    const std::optional<float> valid = validScale(scale);
    if (!valid) {
        core::logWarning("ui: ignored a UI scale that is not a number");
        return;
    }
    impl_->desc.scale = *valid;
    impl_->resize(impl_->desc.width, impl_->desc.height);
}

float Ui::scale() const {
    return impl_->desc.scale;
}

float Ui::dpRatio() const {
    return impl_->context->GetDensityIndependentPixelRatio();
}

void Ui::setMinimap(const MinimapGrid& grid, std::string_view mapNameKey) {
    impl_->setMinimap(grid, mapNameKey);
}

void Ui::refreshTexts() {
    impl_->refreshTexts();
}

void Ui::setMinimapMarkers(const MinimapMarker& player, const std::optional<MinimapMarker>& partner) {
    impl_->placeMarkers(player, partner);
}

void Ui::update() {
    impl_->context->Update();
    impl_->render.beginFrame();
    impl_->context->Render();
}

void Ui::reloadChanged() {
    impl_->reloadChanged();
}

core::Result<void> Ui::callScript(std::string_view document, std::string_view function,
                                  std::span<const std::string> arguments) {
    Impl::Document* found = impl_->find(document);
    if (found == nullptr) {
        return core::fail(std::format("no document named {}", document));
    }
    const LuaCallee callee{.function = nullptr, .environment = &found->element->environment(), .name = function};
    auto result = impl_->scripting->call(callee, arguments);
    if (!result) {
        return std::unexpected{result.error()};
    }
    if (!result->called) {
        return core::fail(std::format("{} has no function {}", document, function));
    }
    return {};
}

std::string Ui::focusedElementId(std::string_view document) const {
    const Impl::Document* found = impl_->find(document);
    Rml::Element* focus = impl_->context->GetFocusElement();
    if (found == nullptr || focus == nullptr || focus->GetOwnerDocument() != found->element) {
        return {};
    }
    return focus->GetId();
}

std::string Ui::elementText(std::string_view document, std::string_view elementId) const {
    const Impl::Document* found = impl_->find(document);
    Rml::Element* element = found != nullptr ? found->element->GetElementById(std::string{elementId}) : nullptr;
    return element != nullptr ? element->GetInnerRML() : std::string{};
}

} // namespace dwe::ui

namespace dwe::ui {

std::optional<Ui::Point> Ui::elementCenter(std::string_view document, std::string_view elementId) const {
    const Impl::Document* found = impl_->find(document);
    Rml::Element* element = found != nullptr ? found->element->GetElementById(std::string{elementId}) : nullptr;
    if (element == nullptr) {
        return std::nullopt;
    }
    const Rml::Vector2f offset = element->GetAbsoluteOffset(Rml::BoxArea::Border);
    const Rml::Vector2f size = element->GetBox().GetSize(Rml::BoxArea::Border);
    return Point{.x = offset.x + (size.x / 2.0F), .y = offset.y + (size.y / 2.0F)};
}

} // namespace dwe::ui
