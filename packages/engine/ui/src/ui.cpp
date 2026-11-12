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
#include <RmlUi/Core/ElementUtilities.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/RenderManager.h>
#include <RmlUi/Core/StringUtilities.h>
#include <RmlUi/Core/StyleSheetContainer.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <iterator>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "code_trust.hpp"
#include "font_engine.hpp"
#include "messages.hpp"
#include "minimap_disc.hpp"
#include "original_box.hpp"
#include "render_bridge.hpp"
#include "rml_interfaces.hpp"
#include "scripting.hpp"
#include "skin_styler.hpp"
#include "text_fit.hpp"

namespace dwe::ui {
namespace {

constexpr std::string_view ContextName = "dwe";

// The window height the UI is designed at: one dp is one pixel there. The width bound keeps narrow
// windows (4:3 and narrower) from cutting the menus.
constexpr float DesignHeight = 720.0F;
constexpr float DesignWidth = 960.0F;
constexpr float MinDpRatio = 0.5F;
constexpr float MaxDpRatio = 8.0F;

// The commands the menus may queue between two takeCommands(): more is a script gone wrong.
constexpr std::size_t MaxPendingCommands = 256;

// The units the markers are written in: thousandths of a percent of the minimap and tenths of a
// degree.
constexpr float MarkerPositionSteps = 100'000.0F;
constexpr float MarkerHeadingSteps = 10.0F;
// How far out a partner out of the minimap's view sits, in parts of the disc's radius: on the rim,
// with its marker still whole inside it.
constexpr float PartnerRim = 0.9F;

// How often hot reload looks at the files, in seconds.
constexpr double ReloadInterval = 0.5;

// There may be one Ui at a time, because RmlUi's interfaces and factory are global.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables): guards RmlUi's global state.
bool uiAlive = false;

std::string_view textOf(std::span<const std::byte> bytes) {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

float dpRatioFor(int width, int height, float scale) {
  const float fit =
      std::min(static_cast<float>(height) / DesignHeight, static_cast<float>(width) / DesignWidth);
  return std::clamp(fit, MinDpRatio, MaxDpRatio) * scale;
}

// The player's scale option within the range Ui::setScale documents, or nothing for a value that is
// not a number.
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
  case NavCommand::TabPrevious:
    return "tab_previous";
  case NavCommand::TabNext:
    return "tab_next";
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
  case NavCommand::TabPrevious:
  case NavCommand::TabNext:
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

// The document's words in the player's language: its text keys, and MenuArtClass while the
// language is the one the original's word pictures are in.
void applyLanguage(Rml::Element& document, const localization::Catalog& texts) {
  applyTextKeys(document, texts);
  document.SetClass(Rml::String{MenuArtClass}, texts.language() == MenuArtLanguage);
}

} // namespace

struct Ui::Impl {
  struct Document {
    ScriptedDocument* element = nullptr;
    // The document shown, and the screen's own one, which a skin may replace.
    assets::AssetId asset;
    assets::AssetId screenAsset;
    // The document's own style sheet, under the skin's.
    Rml::SharedPtr<Rml::StyleSheetContainer> ownSheet;
    std::string path;
    // What the document and its skin's look were read from, and what they held, for hot reload.
    std::map<std::string, std::size_t> documentFiles;
    std::map<std::string, std::size_t> files;
  };

  // A marker's elements and the values they show, so markers that did not move cost nothing.
  struct MarkerState {
    bool visible = false;
    long u = 0;
    long v = 0;
    long heading = 0;
    bool pinned = false;
    bool operator==(const MarkerState&) const = default;
  };

  struct MarkerSlot {
    std::string_view id;
    std::vector<Rml::ObserverPtr<Rml::Element>> elements;
    std::optional<MarkerState> shown;
  };

  // The kinds of marks the UI makes as many of as the game asks: the enemies' dots and the ground
  // items' marks, each in its own layer of the disc.
  struct DotKind {
    std::string_view container;
    std::string_view prefix;
    std::string_view className;
    std::string_view source;
  };
  static constexpr std::array<DotKind, 2> DotKinds{DotKind{.container = hud::MinimapEnemies,
                                                           .prefix = hud::MinimapEnemyPrefix,
                                                           .className = "enemy",
                                                           .source = "generated:minimap_enemy"},
                                                   DotKind{.container = hud::MinimapItems,
                                                           .prefix = hud::MinimapItemPrefix,
                                                           .className = "item",
                                                           .source = "generated:minimap_item"}};
  static constexpr std::size_t EnemyDots = 0;
  static constexpr std::size_t ItemDots = 1;

  // One kind's marks in one document's layer element (DotKind::container): the marks made in it,
  // in order, and what each one shows.
  struct DotLayer {
    std::size_t kind = EnemyDots;
    Rml::ObserverPtr<Rml::Element> container;
    std::vector<Rml::ObserverPtr<Rml::Element>> dots;
    std::vector<std::optional<MarkerState>> shown;
  };

  struct Minimap {
    MinimapLayout layout;
    std::string source; // the generated image's name, with the prefix
    std::string mapNameKey;
    float viewRadius = MinimapMinViewRadius; // world units from the disc's centre to its rim
  };

  UiDesc desc;
  std::unique_ptr<script::ScriptVm> vm;
  SystemInterface system;
  CodeTrust trust;
  FileInterface files;
  TextureSources sources{*desc.files};
  RenderBridge render{sources, desc.renderer};
  std::unique_ptr<Scripting> scripting;
  // Installed over RmlUi's own after it starts; RmlUi shuts it down, so it outlives Rml::Shutdown.
  std::unique_ptr<FontEngine> fontEngine;
  // The original look's box border (`decorator: original-box`), made and registered once RmlUi
  // starts (its property parsers are RmlUi's); it outlives Rml::Shutdown as the font engine does.
  std::unique_ptr<OriginalBoxInstancer> originalBox;
  // The round minimap's decorator (`decorator: minimap-disc`) and what it shows, the same way.
  MinimapDiscState disc;
  std::unique_ptr<MinimapDiscInstancer> minimapDisc;
  Rml::Context* context = nullptr;
  SkinCatalog skins;
  SkinStyler styler{*desc.files, skins};
  // What the skin catalog was read from, for hot reload of skin.yaml.
  std::size_t skinSources = 0;
  // The skin's sprite sheet images not yet loaded ahead (warmOne).
  std::vector<std::string> warming;

  std::map<std::string, Document, std::less<>> documents;
  std::vector<std::string> modalStack;
  std::string menuDocument;
  bool menuShut = false;
  Navigation navigation;
  std::vector<NavCommand> pendingSteps;
  bool capturedThisTick = false;
  // Whether the pointer position is the player's: false after a resize or a focus loss, until it
  // moves again.
  bool pointerKnown = true;
  // The entry under the pointer at its last move: the focus follows the pointer only onto another
  // entry.
  Rml::ObserverPtr<Rml::Element> pointerEntry;
  std::array<bool, platform::MouseButtonCount> capturedButtons{};
  // RmlUi's buttons (left, right, middle) held down, released on a focus loss.
  std::array<bool, 3> rmlButtonsDown{};

  std::vector<UiCommand> commands;
  std::vector<UiCue> cues;
  std::map<std::string, UiValue, std::less<>> settings;
  const input::InputState* input = nullptr;
  input::DeviceKind device = input::DeviceKind::KeyboardMouse;
  platform::GamepadFamily family = platform::GamepadFamily::Generic;

  std::optional<Minimap> minimap;
  int minimapGeneration = 0;
  // The images the app made (Ui::setGeneratedImage): the source each name shows now.
  std::map<std::string, std::string, std::less<>> appImages;
  int appImageGeneration = 0;
  std::array<MarkerSlot, 2> markers{
      MarkerSlot{.id = hud::MinimapPlayer, .elements = {}, .shown = {}},
      MarkerSlot{.id = hud::MinimapPartner, .elements = {}, .shown = {}}};
  std::vector<DotLayer> dotLayers;
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
    // Documents hold Lua references, and Lua holds pointers into RmlUi: the documents go first,
    // then Lua, then RmlUi.
    if (context != nullptr) {
      Rml::RemoveContext(std::string{ContextName});
    }
    // Style sheets hold RmlUi's textures and decorators: they go before RmlUi too.
    documents.clear();
    styler.use(nullptr);
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
          return promptLabel(input != nullptr ? input->actionMap() : defaults, device, family,
                             action, *texts);
        },
        .glyph = [this](std::string_view action) -> PromptGlyph {
          static const input::ActionMap defaults = input::defaultActionMap();
          return promptGlyph(input != nullptr ? input->actionMap() : defaults, device, family,
                             action, *texts);
        },
        .text =
            [this](std::string_view key, std::span<const localization::TextArgument> arguments) {
              return texts->text(key, arguments);
            },
        .languages =
            [this]() {
              return std::vector<std::string>{texts->languages().begin(), texts->languages().end()};
            },
        .skins =
            [this]() {
              std::vector<std::string> ids;
              for (const Skin& skin : skins.skins()) {
                ids.push_back(skin.id);
              }
              return ids;
            },
        .skinNameKey = [this](std::string_view id) -> std::string {
          const Skin* skin = skins.find(id);
          return skin != nullptr ? skin->nameKey : std::string{};
        },
        .layout = [this](std::string_view screen,
                         std::string_view option) { return styler.layoutOption(screen, option); },
        .device = [this]() -> std::string {
          return device == input::DeviceKind::Gamepad ? "gamepad" : "keyboard_mouse";
        },
        .gamepadFamily = [this]() { return std::string{gamepadFamilyName(family)}; },
        .show = [this](std::string_view name, bool modal,
                       const UiArguments& arguments) { show(name, modal, arguments); },
        .cue =
            [this](std::string_view name) {
              if (name == "box_open") {
                cue(UiCue::BoxOpen);
              } else if (name == "box_close") {
                cue(UiCue::BoxClose);
              } else if (name == "confirm") {
                cue(UiCue::Confirm);
              } else {
                return false;
              }
              return true;
            },
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

  // Puts the "original look" family over RmlUi's font engine. Without its description in the packs
  // there is no such family; without the extracted bitmap font, it draws with the modern font.
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
      core::logInfo("{}", core::formatMessage(messages::BitmapFontLoaded, look->atlas,
                                              bitmap->characterCount(), bitmap->cellWidth(),
                                              bitmap->cellHeight()));
    } else {
      core::logWarning("{}", core::formatMessage(messages::BitmapFontUnavailable, look->atlas,
                                                 bitmap.error().message));
    }
    fontEngine =
        std::make_unique<FontEngine>(*Rml::GetFontEngineInterface(), std::move(*look),
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
    // RmlUi asks for the faces again, so every element with the family lays out and draws with the
    // new font.
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

  // Loads a document without making it the one known by its name: the skin's replacement for the
  // screen, if it has one, styled with the skin's look.
  core::Result<Document> loadDetached(std::string_view name, const assets::AssetId& screenAsset) {
    const assets::AssetId asset = styler.documentOf(name, screenAsset);
    const assets::AssetSource* source = desc.files->resolve(asset);
    if (source == nullptr) {
      return core::fail(std::format("no document {} in the packs", asset.str()));
    }
    const std::string path = asset.str() + source->file.extension().string();
    files.startRecording();
    // RmlUi caches templates by path and by name, and a cached one is not read again: a mod's
    // template that an earlier load read would reach this document unseen by the trust, and its
    // event attributes would run as the engine's. Emptying the cache makes every load read, and so
    // judge, the templates it uses.
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
    // A document that read a mod's template while loading loses its own code, also the handlers
    // made before.
    if (document->ownCode()) {
      document->setOwnCode(std::move(ownCode));
    }
    document->setName(name);
    applyLanguage(*document, *texts);
    applyMinimap(*document);
    Document loadedDocument{.element = document,
                            .asset = asset,
                            .screenAsset = screenAsset,
                            .ownSheet = ownSheetOf(*document),
                            .path = path,
                            .documentFiles = std::move(read),
                            .files = {}};
    applySkin(loadedDocument);
    return loadedDocument;
  }

  // A copy of the style sheet a document's own links and <style> give it.
  static Rml::SharedPtr<Rml::StyleSheetContainer> ownSheetOf(const Rml::ElementDocument& document) {
    const Rml::StyleSheetContainer* own = document.GetStyleSheetContainer();
    const Rml::StyleSheetContainer none;
    return own != nullptr ? own->CombineStyleSheetContainer(none)
                          : Rml::MakeShared<Rml::StyleSheetContainer>();
  }

  // Gives a document its screen's look in the current skin, and watches the files the look comes
  // from.
  void applySkin(Document& document) {
    const std::vector<std::string> lookFiles =
        styler.style(*document.element, document.element->name(), *document.ownSheet);
    document.files = document.documentFiles;
    for (const std::string& path : lookFiles) {
      document.files.insert_or_assign(path, files.fingerprint(path).value_or(0));
    }
  }

  // Installs the current skin's mouse cursor through the app's hook: reads and decodes the skin's
  // cursor image, or reports none so the system arrow shows. No-op when the app gave no hook.
  void applyCursor() {
    if (!desc.cursor) {
      return;
    }
    const Skin* skin = styler.skin();
    const std::optional<SkinCursor> cursor = skin != nullptr ? skins.cursorOf(*skin) : std::nullopt;
    if (!cursor) {
      desc.cursor(std::nullopt);
      return;
    }
    auto bytes = desc.files->read(cursor->image);
    if (!bytes) {
      core::logWarning("skin cursor {}: {}", cursor->image.str(), bytes.error().message);
      desc.cursor(std::nullopt);
      return;
    }
    auto image = assets::decodePng(std::span<const std::byte>{*bytes});
    if (!image) {
      core::logWarning("skin cursor {}: {}", cursor->image.str(), image.error().message);
      desc.cursor(std::nullopt);
      return;
    }
    desc.cursor(UiCursor{.image = std::move(*image), .hotX = cursor->hotX, .hotY = cursor->hotY});
  }

  // Switches to a skin: every document takes its look. The documents the skin lays out with another
  // document are loaded first, hidden; if one cannot load, none of them is kept, the UI goes back
  // to the skin it had and the switch fails. Until then no document has changed, so going back
  // cannot fail and nothing is loaded or shown twice.
  core::Result<void> setSkin(std::string_view id) {
    const Skin* skin = skins.find(id);
    if (skin == nullptr) {
      return core::fail(core::formatMessage(messages::UnknownSkin, id));
    }
    const Skin* previous = styler.skin();
    if (skin == previous) {
      return {};
    }
    styler.use(skin);
    clearRmlCaches();
    auto fresh = loadReplacements();
    if (!fresh) {
      styler.use(previous);
      clearRmlCaches();
      return core::fail(core::formatMessage(messages::SkinSwitchFailed, id, fresh.error().message,
                                            skinIdOf(previous)));
    }
    for (auto& [name, document] : documents) {
      if (!fresh->contains(name)) {
        applySkin(document);
      }
    }
    for (auto& [name, document] : *fresh) {
      swapIn(name, std::move(document));
    }
    warming = styler.spriteImages();
    core::logInfo("{}", core::formatMessage(messages::SkinChanged, id));
    // Before the game loads its documents (the skin set at start) there is nothing to check yet.
    if (!documents.empty()) {
      static_cast<void>(checkSkinDocuments());
    }
    applyCursor();
    return {};
  }

  // Smoothing on or off for the sheets that let it: their images change name (the reference's
  // upscale option), so every document takes the new sprite sheets and RmlUi loads the images
  // again.
  void setSmoothPictures(bool smooth) {
    if (!styler.setSmoothPictures(smooth)) {
      return;
    }
    clearRmlCaches();
    for (auto& [name, document] : documents) {
      applySkin(document);
    }
    warming = styler.spriteImages();
  }

  [[nodiscard]] std::vector<std::string> checkSkinDocuments() const {
    std::vector<std::string> loaded;
    loaded.reserve(documents.size());
    for (const auto& [name, document] : documents) {
      loaded.push_back(name);
    }
    return styler.unknownClassedDocuments(loaded);
  }

  static std::string_view skinIdOf(const Skin* skin) {
    return skin != nullptr ? skin->id : std::string_view{};
  }

  // Loads, hidden, every document the current skin lays out with another document than the one it
  // shows. Every document is tried; if any cannot load, the others are unloaded and the errors are
  // joined.
  core::Result<std::map<std::string, Document, std::less<>>> loadReplacements() {
    std::map<std::string, Document, std::less<>> fresh;
    std::string errors;
    for (const auto& [name, document] : documents) {
      if (styler.documentOf(name, document.screenAsset) == document.asset) {
        continue;
      }
      auto loaded = loadDetached(name, document.screenAsset);
      if (!loaded) {
        errors += (errors.empty() ? "" : "; ") +
                  core::formatMessage(messages::DocumentReloadFailed, name, loaded.error().message);
        continue;
      }
      fresh.emplace(name, std::move(*loaded));
    }
    if (!errors.empty()) {
      for (const auto& [name, document] : fresh) {
        context->UnloadDocument(document.element);
      }
      return core::fail(std::move(errors));
    }
    return fresh;
  }

  // Makes RmlUi read style sheets and templates from their files again.
  // Loads one of the skin's sprite sheet images ahead of its first use, a frame each: one loaded
  // where a screen first draws it (the battle's opening banner) stalls that frame.
  void warmOne() {
    if (warming.empty()) {
      return;
    }
    const std::string source = std::move(warming.back());
    warming.pop_back();
    // The game hands over a runtime image (a portrait) itself: there is nothing to load ahead.
    if (source.starts_with("runtime/")) {
      return;
    }
    static_cast<void>(context->GetRenderManager().LoadTexture(source).GetDimensions());
  }

  static void clearRmlCaches() {
    Rml::Factory::ClearStyleSheetCache();
    Rml::Factory::ClearTemplateCache();
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

  void show(std::string_view name, bool modal, const UiArguments& arguments) {
    Document* document = find(name);
    if (document == nullptr) {
      core::logWarning("ui: no document named {}", name);
      return;
    }
    if (document->element->IsVisible()) {
      return;
    }
    document->element->Show(modal ? Rml::ModalFlag::Modal : Rml::ModalFlag::None,
                            Rml::FocusFlag::Auto);
    if (modal) {
      modalStack.emplace_back(name);
      navigation.reset();
    }
    document->element->callHandler("on_show", arguments);
  }

  void hide(std::string_view name) {
    Document* document = find(name);
    // Always drop the name from the modal stack, even if the document was
    // already hidden (e.g. closed by another path), so capturesInput() does
    // not stay stuck true forever.
    std::erase(modalStack, name);
    if (document == nullptr || !document->element->IsVisible()) {
      return;
    }
    document->element->Hide();
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
    return element != nullptr &&
           element->GetComputedValues().tab_index() == Rml::Style::TabIndex::Auto;
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

  // While a menu is open the focus follows the pointer onto each entry it enters, so the
  // highlighted entry is the one a click or confirm acts on. A pointer resting on an entry never
  // takes the focus back from the keyboard or the gamepad, and neither does the position a window
  // reports after a resize, which the player did not move.
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

  // The window lost the focus: buttons held now are released where the UI cannot see it. RmlUi
  // forgets them without a click, because the pointer leaves first.
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
        // RmlUi focuses whatever is clicked, such as a menu's background; keep the focus on an
        // entry, so the keyboard and the gamepad go on from it.
        Rml::Element* clicked = context->GetFocusElement();
        if (focused != nullptr && clicked != focused && !selectable(clicked)) {
          focused->Focus();
        }
      }
      capturedButtons.at(slot) = !modalStack.empty() || context->IsMouseInteracting();
      return capturedButtons.at(slot);
    }
    if (const auto* wheel = std::get_if<platform::MouseWheelMoved>(&event)) {
      // RmlUi scrolls down for positive values; the platform reports turning the wheel away as
      // positive.
      context->ProcessMouseWheel(Rml::Vector2f{wheel->x, -wheel->y}, 0);
      return !modalStack.empty() || context->IsMouseInteracting();
    }
    if (const auto* resized = std::get_if<platform::WindowResized>(&event)) {
      resize(resized->width, resized->height);
      pointerKnown = false;
    }
    return false;
  }

  void cue(UiCue what) {
    if (cues.size() < MaxPendingCommands) {
      cues.push_back(what);
    }
  }

  // As the original's menus (main_menu.c: tickMenuInput): confirm and cancel always sound, a move
  // only when the cursor moved, and opening the menu is silent (main_menu.c: tickMainMenu).
  static std::optional<UiCue> pressCue(NavCommand command) {
    switch (command) {
    case NavCommand::Confirm:
      return UiCue::Confirm;
    case NavCommand::Cancel:
    case NavCommand::Menu:
      return UiCue::Cancel;
    case NavCommand::Up:
    case NavCommand::Down:
    case NavCommand::Left:
    case NavCommand::Right:
    case NavCommand::TabPrevious:
    case NavCommand::TabNext:
      break;
    }
    return std::nullopt;
  }

  void step(NavCommand command) {
    Document* modal = topModal();
    if (modal == nullptr) {
      if (command == NavCommand::Menu && !menuShut && find(menuDocument) != nullptr) {
        show(menuDocument, true, {});
        capturedThisTick = true;
      }
      return;
    }
    capturedThisTick = true;
    const bool sounds = !modal->element->HasAttribute(Rml::String{OwnSoundsAttribute});
    const std::optional<UiCue> pressed = pressCue(command);
    if (pressed && sounds) {
      cue(*pressed);
    }
    if (modal->element->callHandler("on_action", std::string{actionName(command)})) {
      // The document moves its own cursor, so a move sounds whenever it takes one.
      if (!pressed && sounds) {
        cue(UiCue::Move);
      }
      return;
    }
    if (auto key = keyOf(command)) {
      const Rml::Element* before = context->GetFocusElement();
      context->ProcessKeyDown(*key, 0);
      context->ProcessKeyUp(*key, 0);
      if (!pressed && sounds && context->GetFocusElement() != before) {
        cue(UiCue::Move);
      }
    } else if (command == NavCommand::Cancel || command == NavCommand::Menu) {
      hide(modal->element->name());
    }
    // A tab step that the document does not take does nothing: a page without tabs ignores it.
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
    // Without a game menu to open (the title, a movie, a naming box), Start does what the
    // original's Start does there, the same as `menu`; in a game the app takes it to pause.
    if (menuDocument.empty() && state.wasPressed("pause") && !state.wasPressed("menu")) {
      steps.push_back(NavCommand::Menu);
    }
    for (const NavCommand command : steps) {
      step(command);
    }
    forEachVisible(
        [seconds](Document& document) { document.element->callHandler("on_tick", seconds); });
  }

  void setSetting(std::string_view name, UiValue value) {
    auto found = settings.find(name);
    if (found != settings.end() && found->second == value) {
      return;
    }
    settings.insert_or_assign(std::string{name}, std::move(value));
    forEachVisible([name](Document& document) {
      document.element->callHandler("on_setting", std::string{name});
    });
  }

  void resize(int width, int height) {
    desc.width = width;
    desc.height = height;
    context->SetDimensions({width, height});
    const float ratio = dpRatioFor(width, height, desc.scale);
    if (fontEngine != nullptr) {
      fontEngine->setDpRatio(ratio);
    }
    context->SetDensityIndependentPixelRatio(ratio);
  }

  void applyMinimap(Rml::ElementDocument& document) {
    if (Rml::Element* name = document.GetElementById(Rml::String{hud::MinimapName})) {
      const std::string text = !minimap || minimap->mapNameKey.empty()
                                   ? std::string{}
                                   : texts->text(minimap->mapNameKey);
      name->SetInnerRML(Rml::StringUtilities::EncodeRml(Rml::String{trimmed(text)}));
    }
  }

  void refreshTexts() {
    for (auto& [name, document] : documents) {
      applyLanguage(*document.element, *texts);
      applyMinimap(*document.element);
    }
    forEachVisible([](Document& document) { document.element->callHandler(LanguageHandler); });
  }

  std::string setGeneratedImage(std::string_view name, assets::Image image) {
    if (const auto shown = appImages.find(name); shown != appImages.end()) {
      Rml::ReleaseTexture(shown->second);
      sources.dropGenerated(shown->second.substr(GeneratedPrefix.size()));
      appImages.erase(shown);
    }
    const std::string generated = std::format("{}/{}", name, ++appImageGeneration);
    std::string source = std::string{GeneratedPrefix} + generated;
    sources.setGenerated(generated, std::move(image));
    appImages.emplace(std::string{name}, source);
    return source;
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
          .viewRadius = minimapViewRadius(built->layout),
      };
      sources.setGenerated(name, std::move(built->image));
    } else {
      core::logWarning("ui: the map {} has no walkable cell for the minimap", mapNameKey);
    }
    disc.source = minimap ? minimap->source : std::string{};
    for (auto& [name, document] : documents) {
      applyMinimap(*document.element);
    }
    for (MarkerSlot& slot : markers) {
      slot.shown.reset();
    }
    for (DotLayer& layer : dotLayers) {
      std::ranges::fill(layer.shown, std::nullopt);
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
    // A document that stayed keeps the marks made in it: they are found again, in their order.
    // Only a layer inside a minimap's disc is one: the options page names its rows after their
    // options, and the rows of "minimap-items" and "minimap-enemies" are no layers.
    const auto inDisc = [](const Rml::Element& element) {
      for (const Rml::Element* parent = element.GetParentNode(); parent != nullptr;
           parent = parent->GetParentNode()) {
        if (parent->GetId() == hud::MinimapFrame) {
          return true;
        }
      }
      return false;
    };
    dotLayers.clear();
    for (auto& [name, document] : documents) {
      for (std::size_t kind = 0; kind < DotKinds.size(); ++kind) {
        Rml::Element* container =
            document.element->GetElementById(Rml::String{DotKinds.at(kind).container});
        if (container == nullptr || !inDisc(*container)) {
          continue;
        }
        DotLayer layer{
            .kind = kind, .container = container->GetObserverPtr(), .dots = {}, .shown = {}};
        for (int child = 0; child < container->GetNumChildren(); ++child) {
          layer.dots.push_back(container->GetChild(child)->GetObserverPtr());
        }
        layer.shown.resize(layer.dots.size());
        dotLayers.push_back(std::move(layer));
      }
    }
    markersStale = false;
  }

  // Shows a marker's state on its element: placed with left and top in percent of the disc, turned
  // with a rotate() transform when `turned`, or hidden.
  static void showMarker(Rml::Element& element, const MarkerState& state, bool turned) {
    if (!state.visible) {
      element.SetProperty("display", "none");
      return;
    }
    element.SetProperty("display", "block");
    element.SetProperty("left", percent(static_cast<float>(state.u) / MarkerPositionSteps));
    element.SetProperty("top", percent(static_cast<float>(state.v) / MarkerPositionSteps));
    if (turned) {
      element.SetProperty(
          "transform",
          std::format("rotate({:.1f}deg)", static_cast<float>(state.heading) / MarkerHeadingSteps));
    }
    element.SetClass("pinned", state.pinned);
  }

  // Places a marker at (u, v), in parts of the disc from its top left corner, turned to face
  // along its facing; nothing hides it.
  void placeMarker(MarkerSlot& slot, const std::optional<MinimapMarker>& marker, float u, float v,
                   bool pinned) {
    MarkerState state;
    if (marker && minimap) {
      state = MarkerState{
          .visible = true,
          .u = std::lround(std::clamp(u, 0.0F, 1.0F) * MarkerPositionSteps),
          .v = std::lround(std::clamp(v, 0.0F, 1.0F) * MarkerPositionSteps),
          .heading =
              std::lround(minimapHeading(marker->facingX, marker->facingZ) * MarkerHeadingSteps),
          .pinned = pinned,
      };
    }
    if (slot.shown == state) {
      return;
    }
    slot.shown = state;
    for (const Rml::ObserverPtr<Rml::Element>& element : slot.elements) {
      if (element) {
        showMarker(*element.get(), state, true);
      }
    }
  }

  // One kind's marks (the enemies' dots, the ground items' marks), where they stand from the
  // player on the north-up disc; one out of view (farther than MinimapEnemyRim) is hidden. Each
  // layer of the kind gets as many marks as points, made the first time they are needed, and hides
  // the rest.
  void placeDots(std::size_t kind, const MinimapMarker& player,
                 std::span<const MinimapMarker> points) {
    std::vector<MarkerState> states;
    if (minimap) {
      states.reserve(points.size());
      for (const MinimapMarker& point : points) {
        const MinimapDiscPoint at = locateOnDisc(player.x, player.z, minimap->viewRadius, point.x,
                                                 point.z, MinimapEnemyRim);
        states.push_back(
            at.pinned ? MarkerState{}
                      : MarkerState{.visible = true,
                                    .u = std::lround(std::clamp(0.5F + (at.x / 2.0F), 0.0F, 1.0F) *
                                                     MarkerPositionSteps),
                                    .v = std::lround(std::clamp(0.5F + (at.y / 2.0F), 0.0F, 1.0F) *
                                                     MarkerPositionSteps),
                                    .heading = 0,
                                    .pinned = false});
      }
    }
    const DotKind& made = DotKinds.at(kind);
    for (DotLayer& layer : dotLayers) {
      if (layer.kind != kind || !layer.container) {
        continue;
      }
      while (layer.dots.size() < states.size()) {
        Rml::ElementDocument* owner = layer.container->GetOwnerDocument();
        if (owner == nullptr) {
          break;
        }
        Rml::ElementPtr dot = owner->CreateElement("img");
        dot->SetId(std::format("{}{}", made.prefix, layer.dots.size() + 1));
        dot->SetClass("marker", true);
        dot->SetClass(Rml::String{made.className}, true);
        dot->SetProperty("display", "none");
        dot->SetAttribute("src", Rml::String{made.source});
        layer.dots.push_back(layer.container->AppendChild(std::move(dot))->GetObserverPtr());
        layer.shown.emplace_back();
      }
      for (std::size_t index = 0; index < layer.dots.size(); ++index) {
        const MarkerState state = index < states.size() ? states[index] : MarkerState{};
        if (layer.shown[index] == state || !layer.dots[index]) {
          continue;
        }
        layer.shown[index] = state;
        showMarker(*layer.dots[index].get(), state, false);
      }
    }
  }

  // The disc follows the player, north-up: the player stays in its middle and the partner goes
  // where it stands from the player, pinned to the rim when it is out of view.
  void placeMarkers(const MinimapMarker& player, const std::optional<MinimapMarker>& partner,
                    std::span<const MinimapMarker> enemies, std::span<const MinimapMarker> items) {
    if (markersStale) {
      findMarkers();
    }
    if (minimap) {
      disc.view = minimapDiscView(minimap->layout, player.x, player.z, minimap->viewRadius);
    }
    placeMarker(markers[0], player, 0.5F, 0.5F, false);
    MinimapDiscPoint at;
    if (partner && minimap) {
      at =
          locateOnDisc(player.x, player.z, minimap->viewRadius, partner->x, partner->z, PartnerRim);
    }
    placeMarker(markers[1], partner, 0.5F + (at.x / 2.0F), 0.5F + (at.y / 2.0F), at.pinned);
    placeDots(EnemyDots, player, enemies);
    placeDots(ItemDots, player, items);
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
    if (reloadSkins()) {
      return;
    }
    std::vector<std::string> stale;
    for (const auto& [name, document] : documents) {
      const bool changed = std::ranges::any_of(document.files, [&](const auto& file) {
        return files.fingerprint(file.first) != std::optional<std::size_t>{file.second};
      });
      if (changed) {
        stale.push_back(name);
      }
    }
    if (stale.empty()) {
      return;
    }
    clearRmlCaches();
    styler.forget();
    for (const std::string& name : stale) {
      if (auto swapped = replace(name); !swapped) {
        core::logError("ui: {}", swapped.error().message);
        // Wait for the next edit instead of failing again every check.
        for (auto& [path, fingerprint] : find(name)->files) {
          fingerprint = files.fingerprint(path).value_or(0);
        }
      }
    }
  }

  // Reads the skin catalog again when a skin.yaml or ui/ui_skin.yaml changed, and loads every
  // document again in the current skin as the new catalog has it. A catalog that no longer offers
  // the current skin (a broken edit) is not taken: the old one stays until the next edit. True when
  // the catalog was read again.
  bool reloadSkins() {
    const std::size_t fingerprint = SkinCatalog::sourceFingerprint(*desc.files);
    if (fingerprint == skinSources) {
      return false;
    }
    skinSources = fingerprint;
    const std::string current{skinIdOf(styler.skin())};
    SkinCatalog fresh = SkinCatalog::load(*desc.files);
    if (!current.empty() && fresh.find(current) == nullptr) {
      core::logError("{}", core::formatMessage(messages::SkinReloadLostSkin, current));
      return true;
    }
    skins = std::move(fresh);
    styler.use(current.empty() ? nullptr : skins.find(current));
    clearRmlCaches();
    styler.forget();
    std::vector<std::string> names;
    names.reserve(documents.size());
    for (const auto& [name, document] : documents) {
      names.push_back(name);
    }
    for (const std::string& name : names) {
      if (auto swapped = replace(name); !swapped) {
        core::logError("ui: {}", swapped.error().message);
      }
    }
    warming = styler.spriteImages();
    core::logInfo("{}", core::formatMessage(messages::SkinsReloaded, skins.skins().size()));
    return true;
  }

  // Loads a document again, in the current skin, and swaps it in only when that worked: on an error
  // the old one keeps running. The caller clears the style caches first.
  core::Result<void> replace(const std::string& name) {
    auto fresh = loadDetached(name, find(name)->screenAsset);
    if (!fresh) {
      return core::fail(
          core::formatMessage(messages::DocumentReloadFailed, name, fresh.error().message));
    }
    swapIn(name, std::move(*fresh));
    return {};
  }

  // Puts a document loaded hidden in the place of the one with its name: shown, and in the same
  // place of the modal stack, if the old one was, and told so through on_show.
  void swapIn(const std::string& name, Document fresh) {
    Document& old = *find(name);
    const bool visible = old.element->IsVisible();
    const auto place = std::ranges::find(modalStack, name);
    const bool modal = place != modalStack.end();
    context->UnloadDocument(old.element);
    old = std::move(fresh);
    markersStale = true;
    core::logInfo("{}", core::formatMessage(messages::DocumentReloaded, name));
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
    old.element->callHandler("on_show", UiArguments{});
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
  impl->originalBox = std::make_unique<OriginalBoxInstancer>();
  Rml::Factory::RegisterDecoratorInstancer(OriginalBoxDecorator, impl->originalBox.get());
  impl->minimapDisc = std::make_unique<MinimapDiscInstancer>(impl->disc);
  Rml::Factory::RegisterDecoratorInstancer(MinimapDiscDecorator, impl->minimapDisc.get());
  if (auto fonts = impl->installFontEngine(); !fonts) {
    return std::unexpected{fonts.error()};
  }
  impl->scripting->install();
  if (auto texts = impl->loadTexts(); !texts) {
    return std::unexpected{texts.error()};
  }
  constexpr std::uint32_t MarkerSize = 64;
  impl->sources.setGenerated("minimap_marker", buildMinimapMarker(MarkerSize));
  impl->sources.setGenerated("minimap_enemy", buildMinimapDot(MarkerSize));
  impl->sources.setGenerated("minimap_item", buildMinimapDiamond(MarkerSize));
  if (auto fonts = impl->loadFonts(); !fonts) {
    return std::unexpected{fonts.error()};
  }
  impl->context = Rml::CreateContext(std::string{ContextName}, {desc.width, desc.height});
  if (impl->context == nullptr) {
    return core::fail("could not create the UI context");
  }
  impl->resize(desc.width, desc.height);
  impl->skins = SkinCatalog::load(*desc.files);
  impl->skinSources = SkinCatalog::sourceFingerprint(*desc.files);
  impl->styler.use(impl->skins.find(impl->skins.defaultSkin()));
  impl->warming = impl->styler.spriteImages();
  impl->applyCursor();
  core::logInfo("RmlUi {} started", Rml::GetVersion());
  return std::unique_ptr<Ui>{new Ui{std::move(impl)}};
}

Ui::Ui(std::unique_ptr<Impl> impl) : impl_{std::move(impl)} {}

Ui::~Ui() = default;

core::Result<void> Ui::loadDocument(std::string_view name, const assets::AssetId& document) {
  return impl_->load(name, document);
}

void Ui::show(std::string_view name, bool modal, const UiArguments& arguments) {
  impl_->show(name, modal, arguments);
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

const SkinCatalog& Ui::skins() const {
  return impl_->skins;
}

core::Result<void> Ui::setSkin(std::string_view id) {
  return impl_->setSkin(id);
}

void Ui::setSmoothPictures(bool smooth) {
  impl_->setSmoothPictures(smooth);
}

void Ui::setUpscaleCache(std::shared_ptr<const render::UpscaleCache> cache) {
  impl_->render.setUpscaleCache(std::move(cache));
}

std::vector<std::string> Ui::checkSkinDocuments() const {
  return impl_->checkSkinDocuments();
}

std::string_view Ui::skin() const {
  const Skin* skin = impl_->styler.skin();
  return skin != nullptr ? std::string_view{skin->id} : std::string_view{};
}

void Ui::setMenuDocument(std::string_view name) {
  impl_->menuDocument = name;
}

void Ui::setMenuShut(bool shut) {
  impl_->menuShut = shut;
}

bool Ui::handleEvent(const platform::Event& event) {
  return impl_->handleEvent(event);
}

void Ui::tick(const input::InputState& input, double seconds) {
  // Defensive cleanup: drop any modal-stack entries whose documents are no
  // longer visible, so a stale modal never keeps capturesInput() true.
  auto& stack = impl_->modalStack;
  std::erase_if(stack, [this](const std::string& name) {
    const auto* d = impl_->find(name);
    return d == nullptr || !d->element->IsVisible();
  });
  impl_->tick(input, seconds);
}

bool Ui::capturesInput() const {
  return !impl_->modalStack.empty() || impl_->capturedThisTick;
}

bool Ui::hasModal() const {
  return !impl_->modalStack.empty();
}

std::vector<UiCommand> Ui::takeCommands() {
  return std::exchange(impl_->commands, {});
}

std::vector<UiCue> Ui::takeCues() {
  return std::exchange(impl_->cues, {});
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

std::string Ui::setGeneratedImage(std::string_view name, assets::Image image) {
  return impl_->setGeneratedImage(name, std::move(image));
}

void Ui::refreshTexts() {
  impl_->refreshTexts();
}

void Ui::setMinimapMarkers(const MinimapMarker& player, const std::optional<MinimapMarker>& partner,
                           std::span<const MinimapMarker> enemies,
                           std::span<const MinimapMarker> items) {
  impl_->placeMarkers(player, partner, enemies, items);
}

void Ui::update() {
  impl_->warmOne();
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
  const LuaCallee callee{
      .function = nullptr, .environment = &found->element->environment(), .name = function};
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

std::string Ui::styleProperty(std::string_view document, std::string_view elementId,
                              std::string_view property) const {
  const Impl::Document* found = impl_->find(document);
  if (found == nullptr) {
    return {};
  }
  Rml::Element* element =
      elementId.empty() ? found->element : found->element->GetElementById(std::string{elementId});
  const Rml::Property* value =
      element != nullptr ? element->GetProperty(std::string{property}) : nullptr;
  return value != nullptr ? value->ToString() : std::string{};
}

std::string Ui::elementText(std::string_view document, std::string_view elementId) const {
  const Impl::Document* found = impl_->find(document);
  Rml::Element* element =
      found != nullptr ? found->element->GetElementById(std::string{elementId}) : nullptr;
  return element != nullptr ? element->GetInnerRML() : std::string{};
}

bool Ui::elementHasClass(std::string_view document, std::string_view elementId,
                         std::string_view className) const {
  const Impl::Document* found = impl_->find(document);
  Rml::Element* element = nullptr;
  if (found != nullptr) {
    element =
        elementId.empty() ? found->element : found->element->GetElementById(std::string{elementId});
  }
  return element != nullptr && element->IsClassSet(std::string{className});
}

} // namespace dwe::ui

namespace dwe::ui {

std::optional<Ui::Point> Ui::elementCenter(std::string_view document,
                                           std::string_view elementId) const {
  const Impl::Document* found = impl_->find(document);
  Rml::Element* element =
      found != nullptr ? found->element->GetElementById(std::string{elementId}) : nullptr;
  if (element == nullptr) {
    return std::nullopt;
  }
  const Rml::Vector2f offset = element->GetAbsoluteOffset(Rml::BoxArea::Border);
  const Rml::Vector2f size = element->GetBox().GetSize(Rml::BoxArea::Border);
  return Point{.x = offset.x + (size.x / 2.0F), .y = offset.y + (size.y / 2.0F)};
}

bool Ui::elementShown(std::string_view document, std::string_view elementId) const {
  const Impl::Document* found = impl_->find(document);
  const Rml::Element* element =
      found != nullptr ? found->element->GetElementById(std::string{elementId}) : nullptr;
  return element != nullptr && element->IsVisible(true);
}

} // namespace dwe::ui

namespace dwe::ui {

std::vector<Ui::TextOverflow> Ui::overflowingTexts() const {
  std::vector<TextOverflow> found;
  const Rml::Vector2i size = impl_->context->GetDimensions();
  for (const auto& [name, document] : impl_->documents) {
    auto overflows = findTextOverflows(*document.element, name, dpRatio(),
                                       static_cast<float>(size.x), static_cast<float>(size.y));
    found.insert(found.end(), std::make_move_iterator(overflows.begin()),
                 std::make_move_iterator(overflows.end()));
  }
  return found;
}

std::optional<Ui::Rect> Ui::elementRect(std::string_view document,
                                        std::string_view elementId) const {
  const Impl::Document* found = impl_->find(document);
  Rml::Element* element =
      found != nullptr ? found->element->GetElementById(std::string{elementId}) : nullptr;
  if (element == nullptr) {
    return std::nullopt;
  }
  const Rml::Vector2f offset = element->GetAbsoluteOffset(Rml::BoxArea::Border);
  const Rml::Vector2f size = element->GetBox().GetSize(Rml::BoxArea::Border);
  return Rect{
      .left = offset.x, .top = offset.y, .right = offset.x + size.x, .bottom = offset.y + size.y};
}

std::optional<Ui::Rect> Ui::elementDrawnRect(std::string_view document,
                                             std::string_view elementId) const {
  const Impl::Document* found = impl_->find(document);
  Rml::Element* element =
      found != nullptr ? found->element->GetElementById(std::string{elementId}) : nullptr;
  if (element == nullptr) {
    return std::nullopt;
  }
  Rml::Rectanglef drawn;
  if (!Rml::ElementUtilities::GetBoundingBox(drawn, element, Rml::BoxArea::Border)) {
    return elementRect(document, elementId);
  }
  return Rect{
      .left = drawn.Left(), .top = drawn.Top(), .right = drawn.Right(), .bottom = drawn.Bottom()};
}

std::vector<Ui::Rect> Ui::drawnRects(std::string_view exceptDocument) const {
  std::vector<Rect> found;
  const Rml::Vector2i size = impl_->context->GetDimensions();
  for (const auto& [name, document] : impl_->documents) {
    if (name == exceptDocument) {
      continue;
    }
    auto rects =
        findDrawnRects(*document.element, static_cast<float>(size.x), static_cast<float>(size.y));
    found.insert(found.end(), rects.begin(), rects.end());
  }
  return found;
}

std::vector<Ui::Rect> Ui::drawnRectsIn(std::string_view document) const {
  const Impl::Document* found = impl_->find(document);
  if (found == nullptr) {
    return {};
  }
  const Rml::Vector2i size = impl_->context->GetDimensions();
  return findDrawnRects(*found->element, static_cast<float>(size.x), static_cast<float>(size.y));
}

std::vector<Ui::Rect> Ui::drawnRectsOf(std::string_view document,
                                       std::string_view elementId) const {
  const Impl::Document* found = impl_->find(document);
  Rml::Element* element =
      found != nullptr ? found->element->GetElementById(std::string{elementId}) : nullptr;
  if (element == nullptr) {
    return {};
  }
  const Rml::Vector2i size = impl_->context->GetDimensions();
  return findDrawnRects(*element, static_cast<float>(size.x), static_cast<float>(size.y));
}

std::vector<Ui::Rect> Ui::drawnRectsAround(std::string_view document,
                                           std::string_view elementId) const {
  const Impl::Document* holder = impl_->find(document);
  Rml::Element* element =
      holder != nullptr ? holder->element->GetElementById(std::string{elementId}) : nullptr;
  std::vector<Rect> found;
  const Rml::Vector2i size = impl_->context->GetDimensions();
  for (const auto& [name, shown] : impl_->documents) {
    auto rects = findDrawnRects(*shown.element, static_cast<float>(size.x),
                                static_cast<float>(size.y), name == document ? element : nullptr);
    found.insert(found.end(), rects.begin(), rects.end());
  }
  return found;
}

} // namespace dwe::ui
