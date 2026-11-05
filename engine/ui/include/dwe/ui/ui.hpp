#pragma once

#include <dwe/assets/image.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>
#include <dwe/input/input_state.hpp>
#include <dwe/localization/catalog.hpp>
#include <dwe/platform/events.hpp>
#include <dwe/script/script_vm.hpp>
#include <dwe/ui/minimap.hpp>
#include <dwe/ui/skin_catalog.hpp>
#include <dwe/ui/text_font.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace dwe::ui {

// A value passed between the menus and the app: nothing, a switch, a number or a text.
using UiValue = std::variant<std::monostate, bool, double, std::string>;

// Something a menu asks the app to do, such as {"quit"} or {"fullscreen", true}. The app decides
// what each means.
struct UiCommand {
  std::string name;
  UiValue value;
};

// What the player just did in a menu, for the app's feedback sounds: moved the focus (or a menu's
// own cursor), confirmed or backed out. A move that moves nothing and opening the menu give none,
// as in the original. A document's script adds the original boxes' own sounds with ui.cue: one
// opens (ui.c: createAnimatedUIBox) or starts closing (renderUIBoxAnim). The UI does not know what
// each sounds like.
enum class UiCue : std::uint8_t {
  Move,
  Confirm,
  Cancel,
  BoxOpen,
  BoxClose,
};

// The HUD contract: the ids of the elements the UI fills in every document that has them
// (docs/architecture/ui.md).
namespace hud {
// The round minimap's disc: the element a skin draws with `decorator: minimap-disc`, which fills
// its box with the map round the player, north-up (minimap.hpp: minimapDiscView). The markers are
// positioned in percent of it: the player always in the middle, the partner where it stands, or on
// the rim when it is out of view (the class "pinned").
inline constexpr std::string_view MinimapFrame = "minimap-frame";
// The current map's name, as text in the player's language.
inline constexpr std::string_view MinimapName = "minimap-name";
// The markers, placed with left, top and a rotate() transform, and hidden with display: none.
inline constexpr std::string_view MinimapPlayer = "minimap-player";
inline constexpr std::string_view MinimapPartner = "minimap-partner";
// The enemies on the map (an engine improvement; the original has no minimap): the UI fills this
// element of #minimap-frame with one <img class="marker enemy"> per enemy in view, with the ids
// MinimapEnemyPrefix 1, 2..., placed in percent of the frame as the partner, and hides the ones it
// does not need (display: none). An enemy out of the disc's view (MinimapEnemyRim) is hidden, not
// pinned. The element goes before the partner's and the player's markers so they draw over the
// enemies.
inline constexpr std::string_view MinimapEnemies = "minimap-enemies";
inline constexpr std::string_view MinimapEnemyPrefix = "minimap-enemy-";
} // namespace hud

// The attribute that gives an element its text by key, in the player's language: <span
// text-key="hud.menu"/>. The UI fills the element when the document loads and again when the
// language changes (Ui::refreshTexts).
inline constexpr std::string_view TextKeyAttribute = "text-key";
// The class every document's <body> has while the player's language is the one of the original's
// word pictures (its menu labels: ITEM, MENU, COUNT...), set with the text keys: a skin shows a
// picture under it and the text key's words without it (docs/architecture/localizacion.md).
inline constexpr std::string_view MenuArtClass = "use-menu-art";
inline constexpr std::string_view MenuArtLanguage = "en";
// A document whose <body> has this attribute plays its own sounds (its game sends them, as the
// original's egg box does): the keys pressed in it cue no menu sound.
inline constexpr std::string_view OwnSoundsAttribute = "own-sounds";

// Draws the UI's geometry. The headless UI (tests, tools) draws nothing; the game passes one built
// on render.
class UiRenderer;

struct UiDesc {
  // Where documents, styles, scripts and fonts are read from, by asset id. Must outlive the UI.
  const assets::VirtualFileSystem* files = nullptr;
  // Draws the UI; nullptr for a UI that only lays out (tests). Must outlive the UI.
  UiRenderer* renderer = nullptr;
  // The player-facing texts in the player's language. Must outlive the UI. When null, the UI reads
  // the packs' English texts itself (tests, tools).
  const localization::Catalog* texts = nullptr;
  // The size of the area the UI covers, in pixels.
  int width = 1280;
  int height = 720;
  // The player's UI scale option, 1 = the default size; see Ui::setScale.
  float scale = 1.0F;
  // Re-reads documents, styles and scripts when their files change (development builds).
  bool hotReload = false;
  // Seconds from a monotonic clock, for animations and transitions; a steady clock when empty.
  std::function<double()> clock;
  // What the documents' scripts may use. A call from the UI and everything it causes (handlers of
  // the elements it clicks, documents it shows) share one instruction budget.
  script::ScriptLimits scripts;
};

// A marker on the minimap, in world space: a position on the ground and the direction it faces.
struct MinimapMarker {
  float x = 0.0F;
  float z = 0.0F;
  float facingX = 0.0F;
  float facingZ = -1.0F;
};

// The game's UI: RmlUi documents (RML and RCSS) with their logic in sandboxed Lua, a menu opened
// with the `menu` action, and the HUD with the minimap. There may be one Ui at a time, because
// RmlUi's state is global.
//
// Per frame the app: passes every platform event to handleEvent (and keeps from the game those it
// takes); calls tick once per logic tick, before the game's own tick, and skips the game's input
// while capturesInput(); applies takeCommands(); updates the minimap markers; and draws.
class Ui {
public:
  static core::Result<std::unique_ptr<Ui>> create(const UiDesc& desc);

  Ui(const Ui&) = delete;
  Ui& operator=(const Ui&) = delete;
  Ui(Ui&&) = delete;
  Ui& operator=(Ui&&) = delete;
  ~Ui();

  // Loads a document (an .rml asset) under a name, hidden. Its <script> elements run in its own Lua
  // environment.
  core::Result<void> loadDocument(std::string_view name, const assets::AssetId& document);

  // Shows a loaded document. A modal one takes the input: the game must not see it while it is
  // shown.
  void show(std::string_view name, bool modal = false);
  void hide(std::string_view name);
  [[nodiscard]] bool isVisible(std::string_view name) const;

  // The document the `menu` action opens, modal, when no other modal document is shown.
  void setMenuDocument(std::string_view name);
  // While shut, the `menu` action does not open the menu document: the game holds it (a map
  // change's fade, a script). Start still goes to the game.
  void setMenuShut(bool shut);

  // Passes a platform event to the UI. Returns true when the UI took it, and the game must not see
  // it: clicks and wheel turns over the UI or while a modal document is shown, and the first
  // pointer position after a resize or a focus loss (the player did not move the mouse). A focus
  // loss releases the buttons held over the UI.
  bool handleEvent(const platform::Event& event);

  // Navigates the shown modal document with the input's actions, once per logic tick, and runs the
  // documents' on_tick handlers. Call it before the game's tick, so a `menu` press that opens the
  // menu is not also the game's.
  void tick(const input::InputState& input, double seconds);

  // True while a modal document is shown: the game must ignore the input's actions.
  [[nodiscard]] bool capturesInput() const;

  // True while a modal document is shown, without the tick in which the UI took a navigation
  // command: the game's own shortcuts (the options' Select on a pad) still answer then.
  [[nodiscard]] bool hasModal() const;

  // The commands the menus sent since the last call, oldest first.
  [[nodiscard]] std::vector<UiCommand> takeCommands();

  // The cues of the menu actions since the last call, oldest first. Mouse clicks give none.
  [[nodiscard]] std::vector<UiCue> takeCues();

  // Publishes a value the menus can read with ui.setting(name), such as the current window mode.
  // Shown documents are told through their on_setting handler when it changes.
  void setSetting(std::string_view name, UiValue value);
  [[nodiscard]] UiValue setting(std::string_view name) const;

  // Resizes the UI to the window, in pixels.
  void resize(int width, int height);

  // The range of the player's UI scale option.
  static constexpr float MinScale = 0.5F;
  static constexpr float MaxScale = 3.0F;

  // Changes the player's UI scale option (1 = default). Values outside [MinScale, MaxScale] are
  // clamped to it; a value that is not a number is ignored.
  void setScale(float scale);
  [[nodiscard]] float scale() const;

  // Pixels per dp: the window's height against the 720 pixels the UI is designed at (its width
  // against 960 when that is smaller), times the scale option.
  [[nodiscard]] float dpRatio() const;

  // Changes what "original look" text (font-family original-look, ui/fonts/original_look.yaml)
  // draws with, and lays out and draws the documents again with it. Without the original bitmap
  // font in the packs, the original choice draws with the modern font.
  void setTextFont(TextFont font);
  // The player's choice; the packs' default until one is made.
  [[nodiscard]] TextFont textFont() const;

  // The skins the packs offer (ui/skins/<id>/skin.yaml); a mod adds one by adding its folder.
  [[nodiscard]] const SkinCatalog& skins() const;

  // Gives every document the look of a skin at once: documents keep their state, focus and scripts,
  // and only a screen whose document the skin replaces loads again. Starts with the packs' default
  // (ui/ui_skin.yaml). Fails, keeping the look, for a skin the packs do not offer.
  core::Result<void> setSkin(std::string_view id);
  // The skin shown; empty when the packs offer none.
  [[nodiscard]] std::string_view skin() const;
  // The documents the skin shown puts classes on (document_classes) that are not loaded, each
  // logged as a warning. The game calls it once its documents are loaded; a switch checks again,
  // unless no document is loaded yet.
  [[nodiscard]] std::vector<std::string> checkSkinDocuments() const;

  // Replaces the minimap with a new map's: its walkable area and the key of its name
  // (localization::mapNameKey; empty for none), shown in the hud:: elements.
  void setMinimap(const MinimapGrid& grid, std::string_view mapNameKey);

  // Gives the documents an image the app made (the island map), under a name of letters, digits
  // and underscores, replacing the one it had: returns the source an <img src> or a decorator names
  // it by now (`generated:<name>/<n>`), which changes with each call so RmlUi loads it again.
  std::string setGeneratedImage(std::string_view name, assets::Image image);

  // Shows every text again after the catalog's language changed: the elements with a
  // TextKeyAttribute, the map's name, and whatever the shown documents' on_language handlers write.
  void refreshTexts();

  // Moves the minimap markers (hud::MinimapPlayer and hud::MinimapPartner) and the enemies' dots
  // (hud::MinimapEnemies; their facing is not drawn), every frame. Markers that did not move cost
  // nothing.
  void setMinimapMarkers(const MinimapMarker& player, const std::optional<MinimapMarker>& partner,
                         std::span<const MinimapMarker> enemies = {});

  // Lays out the documents and records their geometry for the renderer. Once per frame, before
  // drawing. Until the skin's sprite sheet images are all loaded it also loads one ahead of its
  // first use.
  void update();

  // Re-reads the documents whose files changed, when hot reload is on. Cheap enough to call every
  // frame; it checks twice a second.
  void reloadChanged();

  // Calls a global function of a document's Lua environment with string arguments, for tests and
  // tools.
  core::Result<void> callScript(std::string_view document, std::string_view function,
                                std::span<const std::string> arguments = {});

  // The id of the element that has the focus in a document, or an empty string.
  [[nodiscard]] std::string focusedElementId(std::string_view document) const;

  // The value of a style property of an element of a document (its body for an empty id) as RCSS
  // writes it, or an empty string when it has none, for tests.
  [[nodiscard]] std::string styleProperty(std::string_view document, std::string_view elementId,
                                          std::string_view property) const;

  // The text inside an element of a document, for tests.
  [[nodiscard]] std::string elementText(std::string_view document,
                                        std::string_view elementId) const;

  // Whether an element of a document has a class, as its scripts set it, for tests.
  [[nodiscard]] bool elementHasClass(std::string_view document, std::string_view elementId,
                                     std::string_view className) const;

  // The centre of an element of a document in pixels, as laid out by the last update(), for tests
  // and scripted runs that click on it; nullopt if there is no such element. It leaves out RCSS
  // transforms, which also move where the pointer hits an element: menus that tests click should
  // not move their buttons with transforms.
  struct Point {
    float x;
    float y;
  };
  [[nodiscard]] std::optional<Point> elementCenter(std::string_view document,
                                                   std::string_view elementId) const;
  // Whether an element of a document is shown: it and what holds it are not hidden. A hidden
  // element keeps the box it had, so its rectangle does not say.
  [[nodiscard]] bool elementShown(std::string_view document, std::string_view elementId) const;

  // A text the player sees that does not fit: it sticks out of the box drawn round it (the nearest
  // element with a background, a border or a decorator), is cut by an element that clips its
  // content, leaves the window, crosses the edge of a box beside it or lies over another text, by
  // more than one of the original's pixels (3dp). Texts under a later opaque box are hidden and
  // left out. For tests and routes (expect_text_fits).
  struct TextOverflow {
    std::string document;
    // The text's element, with its parents up to one with an id: "button#sleep > span.menu-label".
    std::string element;
    // The text as laid out, its lines joined with " / ".
    std::string text;
    // Where it goes, with the element as `element` names it: "out of div#panel", "across the edge
    // of div#value", "over the text 'Level' (span#level)" or "off the window".
    std::string box;
    // How far, in dp, on its worst side.
    float overflow = 0.0F;
  };
  // The texts of the shown documents that do not fit, as laid out by the last update().
  [[nodiscard]] std::vector<TextOverflow> overflowingTexts() const;

  // A rectangle in pixels, as laid out by the last update().
  struct Rect {
    float left = 0.0F;
    float top = 0.0F;
    float right = 0.0F;
    float bottom = 0.0F;

    friend bool operator==(const Rect&, const Rect&) = default;
  };
  // The border box of an element of a document; nullopt if there is no such element.
  [[nodiscard]] std::optional<Rect> elementRect(std::string_view document,
                                                std::string_view elementId) const;
  // What the player sees of the shown documents but one, within the window: each text's lines and
  // each box (an element with a background, a border or a decorator) and image, left out when it
  // or what holds it is hidden, transparent or under a transform (a box opening or closing). A
  // layer over most of the window (a scrim that dims the game) is not a box and is left out too.
  // For a notice that must stay clear of every window (the save indicator).
  [[nodiscard]] std::vector<Rect> drawnRects(std::string_view exceptDocument) const;
  // What the player sees of one shown document, the same way (nothing when it is hidden).
  [[nodiscard]] std::vector<Rect> drawnRectsIn(std::string_view document) const;
  // What the player sees of one element of a shown document and what it holds, the same way
  // (nothing when it, or what holds it, is hidden or there is no such element).
  [[nodiscard]] std::vector<Rect> drawnRectsOf(std::string_view document,
                                               std::string_view elementId) const;
  // What the player sees of every shown document but that element, what it holds and the boxes
  // round it (an element whose box holds it whole: its panel). For a part of a document that must
  // stay clear of the rest of it and of every window (the HUD's time in digits).
  [[nodiscard]] std::vector<Rect> drawnRectsAround(std::string_view document,
                                                   std::string_view elementId) const;

private:
  struct Impl;

  explicit Ui(std::unique_ptr<Impl> impl);

  std::unique_ptr<Impl> impl_;
};

} // namespace dwe::ui
