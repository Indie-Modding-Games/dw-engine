#pragma once

#include <dwe/assets/asset_id.hpp>
#include <dwe/core/error.hpp>
#include <dwe/ui/image_reference.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::ui {

// The player's skin setting: its name in the UI commands, in ui.setting() and in the user settings.
inline constexpr std::string_view UiSkinSetting = "ui_skin";

// Where the packs keep their skins: ui/skins/<id>/skin.yaml, one folder per skin
// (docs/architecture/ui.md#aspectos).
inline constexpr std::string_view SkinsFolder = "ui/skins/";
inline constexpr std::string_view SkinDocumentName = "skin";

// Where the packs name the skin the game starts with before the player picks one.
inline constexpr std::string_view SkinChoiceAsset = "ui/ui_skin";
// The dp an original pixel of a sprite sheet covers when skin.yaml does not say (dp_per_pixel).
inline constexpr float DefaultDpPerPixel = 1.0F;
// The range of dp_per_pixel: below it a sprite shrinks to nothing, above it one pixel covers more
// than a screen tile.
inline constexpr float MinDpPerPixel = 1.0F / 16.0F;
inline constexpr float MaxDpPerPixel = 16.0F;

// A sprite of a sprite sheet: its name in the style sheets and its rectangle in the image, in
// original pixels.
struct SkinSprite {
  std::string name;
  std::uint32_t x = 0;
  std::uint32_t y = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
};

// A sprite sheet a skin declares for its style sheets (RCSS `@spritesheet`), measured in the pixels
// of the original image. When the packs provide a larger image, an upscaled copy (a remaster, or a
// dwx recipe), the rectangles grow with it, so the sprites keep their size on screen and only get
// sharper.
struct SkinSpriteSheet {
  std::string name;
  // The image, with the palette of an indexed image to draw it with, if not the one its file holds,
  // and its mirrored sides when the sheet asks for them (skin.yaml `mirror_sides: [left, right]`:
  // the image is that much wider, and original_size counts them).
  ImageReference image;
  // The image's size in original pixels; when not given, the image found is the original.
  std::optional<std::uint32_t> originalWidth;
  std::optional<std::uint32_t> originalHeight;
  // How many dp an original pixel covers: 3 draws the original's 240 lines over the UI's 720 dp.
  float dpPerPixel = DefaultDpPerPixel;
  std::vector<SkinSprite> sprites;
  // Whether the image may be drawn smoothed by the texture upscaler (skin.yaml `upscale: smooth`),
  // when the player's texture filter is the upscaler (Ui::setSmoothPictures): a picture, such as
  // the title's, whose curves and cut-out edges show stairs as pixel art.
  bool smooth = false;
};

// What a skin does to one screen, a document the game loads by name (hud, main_menu,
// pause_menu...).
struct SkinScreen {
  // Style sheets for the screen's document, after the skin's theme, in order (.rcss assets).
  std::vector<assets::AssetId> styles;
  // A document (.rml asset) that replaces the screen's own: the same element ids and scripts, laid
  // out another way.
  std::optional<assets::AssetId> document;
};

// The classes a skin puts on elements of one document: element id → classes, so a skin marks
// elements of a document every skin shares (such as the bars that fill in steps) without its own
// copy of the document.
using SkinElementClasses = std::map<std::string, std::vector<std::string>, std::less<>>;

// A skin: the look of every screen of the UI, as ui/skins/<id>/skin.yaml describes it. A skin only
// changes the look: the documents keep their element ids, their scripts and what they send and read
// (docs/architecture/ui.md#aspectos).
struct Skin {
  std::string id;
  // The text key of the name the menus show (ui_skin.<id>).
  std::string nameKey;
  // The skin this one adds to: its sheets come first, so this one's rules win.
  std::optional<std::string> extends;
  // Style sheets for every document, before the screen's own styles.
  std::vector<assets::AssetId> theme;
  std::map<std::string, SkinScreen, std::less<>> screens;
  std::vector<SkinSpriteSheet> spriteSheets;
  // The classes it puts on elements, by the name of the document (document_classes). The documents'
  // scripts must not toggle these classes themselves: a switch takes them off every element a skin
  // of the catalog names.
  std::map<std::string, SkinElementClasses, std::less<>> documentClasses;
};

// Reads a skin.yaml. Checks its shape, not that the assets it names exist (SkinCatalog does).
core::Result<Skin> parseSkin(std::string_view yaml);

// What ui/ui_skin.yaml names: the skin the game starts with, and the one it starts with instead
// when that one is not offered (the original look's skin needs images extracted from the disc).
struct SkinChoice {
  std::string preferred;
  std::optional<std::string> fallback;
};

// Reads ui/ui_skin.yaml.
core::Result<SkinChoice> parseSkinChoice(std::string_view yaml);

} // namespace dwe::ui
