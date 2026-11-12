#pragma once

#include <string_view>

// The log and error messages of ui. Templates take their arguments in the order of their
// placeholders.
namespace dwe::ui::messages {

// Lua that documents bring (code_trust.cpp, scripting.cpp).
inline constexpr std::string_view CodeOutsideLoad =
    "an event attribute or inline script outside a document's load does not run: only a document of the base and "
    "remaster layers runs its own code";
// A C string: Lua takes it without a size.
inline constexpr const char* EventAttributeFromScript =
    "set_attribute cannot set an event attribute (on...): its value would run as Lua";

inline constexpr std::string_view NoTexts = "cannot read the texts of the packs: {}";
// Bitmap font meta (font/main_meta.yaml).
inline constexpr std::string_view BitmapFontContext = "bitmap font";
inline constexpr std::string_view CellSizeTooSmall =
    "the cells must be at least one pixel wide and high";
inline constexpr std::string_view BaselineOutsideCell = "the baseline is outside the cells";
inline constexpr std::string_view AtlasPixelsMismatch =
    "the atlas has not as many pixels as its size says";
inline constexpr std::string_view GlyphOutsideAtlas = "the glyph's cell is outside the atlas";
inline constexpr std::string_view NotOneCharacter = "must be one character";

// ui/fonts/original_look.yaml.
inline constexpr std::string_view OriginalLookContext = "ui/fonts/original_look";
inline constexpr std::string_view UnknownTextFont = "the text font must be modern or original";
inline constexpr std::string_view FallbackSizeNotPositive = "fallback_size must be above 0";
inline constexpr std::string_view FontAtlasOutsideFonts =
    "the game reads the font's exact pixels, so it must live under font/ (an id such as font/main)";

// Loading the "original look" font: the asset id and the problem.
inline constexpr std::string_view OriginalLookMissing =
    "ui: no {} in the packs; there is no original look font";
inline constexpr std::string_view BitmapFontUnavailable =
    "ui: the original bitmap font ({}) cannot be read: {}; original look text uses the modern font";
inline constexpr std::string_view BitmapFontLoaded =
    "ui: original bitmap font {}: {} characters, {}x{} cells";
inline constexpr std::string_view TextFontChanged = "ui: original look text font: {}";

// Images named by documents and style sheets (image_reference.cpp): the asset id, then the
// problem's values.
inline constexpr std::string_view BadPaletteNumber = "'{}' is not a palette number";
inline constexpr std::string_view BadImageOption =
    "'{}' is not an image option: palette=<number> or tone=grey";
inline constexpr std::string_view BadMirroredColumns =
    "'{}' is not a count of mirrored columns: a whole number from 0 to {}";
inline constexpr std::string_view NoImage = "no image {} in the packs";
// A player's image of one (assets/image_replacement.hpp) that cannot be drawn: the game's is.
inline constexpr std::string_view ReplacementUnusable =
    "ui: the player's image {} of pack '{}' is not used, the game's {} is drawn: {}";
inline constexpr std::string_view ReplacementWrongSize =
    "ui: the player's image {} of pack '{}' is {}x{}, not {}x{} or a whole number of times larger "
    "with the same aspect ratio (up to {}x): the game's {} is drawn";
inline constexpr std::string_view ImageTooLarge =
    "the image {} is {}x{}, larger than {} on a side for the UI";
inline constexpr std::string_view UiImageOverBudget =
    "it takes {} bytes decoded over its {} levels, more than the {} the UI allows for one image";
inline constexpr std::string_view ImageFileTooLarge =
    "the file of the image {} has {} bytes, more than the UI reads ({})";
inline constexpr std::string_view ImageUnusable = "the image {} cannot be used: {}";
inline constexpr std::string_view NotAnImage = "{} is a {} file, not a PNG or a KTX2 image";
inline constexpr std::string_view PaletteNeedsPng =
    "a palette of {} is asked for, but it is a {} file, not an indexed PNG";
inline constexpr std::string_view NoImageMeta =
    "a palette of {} is asked for, but it has no metadata with palettes";
inline constexpr std::string_view PaletteOutOfRange = "{} has no palette {}: it has {}";

// A UI addon's addon.yaml (ui_addon.cpp).
inline constexpr std::string_view UiAddonContext = "addon.yaml";
inline constexpr std::string_view UiAddonWhat = "a UI addon";
inline constexpr std::string_view UiAddonDocumentsWhat = "documents";
inline constexpr std::string_view BadUiAddonId =
    "must be 1 to 64 lowercase letters, digits and underscores";
inline constexpr std::string_view BadUiAddonText = "must be 1 to {} characters, on one line";
inline constexpr std::string_view BadUiAddonType = "must be ui or plugin";
inline constexpr std::string_view UiAddonScreensNotMapping =
    "must map each screen to its document (screens: <screen>: <document>)";
inline constexpr std::string_view UiAddonNeedsScreens = "a ui addon must name its screens";
inline constexpr std::string_view UiAddonNeedsDocuments = "a plugin must name its documents";
inline constexpr std::string_view UiAddonWrongPart = "a {} addon has no {}";
inline constexpr std::string_view UiAddonDocumentOutside =
    "must stay inside the addon's folder: no leading / and no ..";
inline constexpr std::string_view BadUiApiRange =
    "ui_api {} must be a version (0.8) or bounds (>=0.8 <0.12)";
inline constexpr std::string_view EmptyUiApiRange = "ui_api {} holds no version";

// skin.yaml and ui/ui_skin.yaml (skin.cpp).
inline constexpr std::string_view SkinContext = "skin";
inline constexpr std::string_view SkinChoiceContext = "ui/ui_skin";
inline constexpr std::string_view SkinWhat = "a skin";
inline constexpr std::string_view SkinThemeWhat = "theme";
inline constexpr std::string_view SkinScreenWhat = "a screen";
inline constexpr std::string_view SkinStylesWhat = "styles";
inline constexpr std::string_view SpriteSheetsWhat = "sprite_sheets";
inline constexpr std::string_view SpriteSheetWhat = "a sprite sheet";
inline constexpr std::string_view SpriteRectangleWhat = "a sprite: [x, y, width, height]";
inline constexpr std::string_view OriginalSizeWhat = "original_size: [width, height]";
inline constexpr std::string_view HotspotWhat = "cursor_hotspot: [x, y]";
inline constexpr std::string_view MirrorSidesWhat = "mirror_sides: [left, right]";
inline constexpr std::string_view TooManyMirroredColumns =
    "mirror_sides counts at most {} columns a side";
inline constexpr std::string_view DocumentClassesNotMapping =
    "must map each name to its entries (document_classes: <document>: <element id>: [classes])";
inline constexpr std::string_view BadElementId =
    "must be 1 to 64 letters, digits, underscores and hyphens";
inline constexpr std::string_view ElementClassesWhat = "the classes of an element";
inline constexpr std::string_view NoElementClasses = "must name at least one class";
inline constexpr std::string_view SkinClassesUnknownElement =
    "ui: the skin {} puts classes on #{} of the document {}, which has no such element";
inline constexpr std::string_view SkinClassesUnknownDocument =
    "ui: the skin {} puts classes on elements of the document {}, which the game does not load";
inline constexpr std::string_view BadSkinName =
    "must be 1 to 64 lowercase letters, digits and underscores";
inline constexpr std::string_view BadStyleName =
    "must be 1 to 64 lowercase letters, digits, underscores and hyphens, starting with a letter";
inline constexpr std::string_view LayoutNotMapping =
    "must map each name to its options (layout: <document>: <option>: <value>)";
inline constexpr std::string_view ScreensNotMapping =
    "screens must map screen names to their styles";
inline constexpr std::string_view EmptySprite = "a sprite must be at least one pixel wide and high";
inline constexpr std::string_view EmptyOriginalSize =
    "the original size must be at least one pixel wide and high";
inline constexpr std::string_view DpPerPixelOutOfRange =
    "dp_per_pixel must be a number from {} to {}";
inline constexpr std::string_view NoSprites = "sprites must map sprite names to rectangles";
inline constexpr std::string_view UnknownSheetUpscale = "upscale must be smooth, not '{}'";

// The skins of the packs (skin_catalog.cpp): the skin's folder or id, then the problem.
inline constexpr std::string_view SkinLeftOut = "ui: the skin {} is left out: {}";
inline constexpr std::string_view SkinIdNotFolder = "its id is {}, not {}, the name of its folder";
inline constexpr std::string_view SkinAssetMissing = "the packs have no {} of the type it needs";
inline constexpr std::string_view SkinBaseMissing = "it extends '{}', which is not offered";
inline constexpr std::string_view SkinExtendsLoop =
    "it extends '{}', which comes back to it through extends";
inline constexpr std::string_view NoSkinChoice =
    "ui: no {} in the packs; the UI starts without a skin";
inline constexpr std::string_view BadSkinChoice =
    "ui: {} cannot be read: {}; the UI starts without a skin";
inline constexpr std::string_view DefaultSkinFallback =
    "ui: the packs start with the skin {}, which is not offered; the UI starts with its fallback, {}";
inline constexpr std::string_view DefaultSkinNotOffered =
    "ui: the packs start with the skin {}, which is not offered";

// Sprite sheets of a skin (sprite_sheet_rcss.cpp, skin_styler.cpp).
inline constexpr std::string_view SpriteSheetSizeMismatch =
    "the sprite sheet {} is {}x{} pixels: not its original size times a whole number";
inline constexpr std::string_view SpriteOutsideImage =
    "the sprite {} is outside the image of the sprite sheet {}";
inline constexpr std::string_view SpriteSheetUnusable =
    "ui: the sprite sheet {} of the skin {} is left out: {}";
inline constexpr std::string_view DuplicateSprite =
    "ui: the skin {} names the sprite {} in the sprite sheets {} and {}; the one in {} is used";
inline constexpr std::string_view SkinStyleUnreadable =
    "ui: the style sheet {} of the skin {} cannot be read";

// Switching skins (ui.cpp).
inline constexpr std::string_view UnknownSkin = "no skin '{}' in the packs";
inline constexpr std::string_view SkinChanged = "ui: skin {}";
inline constexpr std::string_view SkinsReloaded = "ui: skins read again ({} offered)";
inline constexpr std::string_view SkinReloadLostSkin =
    "ui: the edited skins no longer offer '{}', the skin in use; the old ones stay until the next edit";
inline constexpr std::string_view SkinSwitchFailed =
    "the skin '{}' cannot be used: {}; the UI keeps '{}'";
inline constexpr std::string_view DocumentReloadFailed = "could not reload {}: {}";
inline constexpr std::string_view DocumentReloaded = "ui: reloaded {}";

} // namespace dwe::ui::messages
