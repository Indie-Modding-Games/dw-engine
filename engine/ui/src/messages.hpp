#pragma once

#include <string_view>

// The log and error messages of ui. Templates take their arguments in the order of their placeholders.
namespace dwe::ui::messages {

inline constexpr std::string_view NoTexts = "cannot read the texts of the packs: {}";
// Bitmap font meta (font/main_meta.yaml).
inline constexpr std::string_view BitmapFontContext = "bitmap font";
inline constexpr std::string_view CellSizeTooSmall = "the cells must be at least one pixel wide and high";
inline constexpr std::string_view BaselineOutsideCell = "the baseline is outside the cells";
inline constexpr std::string_view AtlasPixelsMismatch = "the atlas has not as many pixels as its size says";
inline constexpr std::string_view GlyphOutsideAtlas = "the glyph's cell is outside the atlas";
inline constexpr std::string_view NotOneCharacter = "must be one character";

// ui/fonts/original_look.yaml.
inline constexpr std::string_view OriginalLookContext = "ui/fonts/original_look";
inline constexpr std::string_view UnknownTextFont = "the text font must be modern or original";
inline constexpr std::string_view FallbackSizeNotPositive = "fallback_size must be above 0";

// Loading the "original look" font: the asset id and the problem.
inline constexpr std::string_view OriginalLookMissing = "ui: no {} in the packs; there is no original look font";
inline constexpr std::string_view BitmapFontUnavailable =
    "ui: the original bitmap font ({}) cannot be read: {}; original look text uses the modern font";
inline constexpr std::string_view BitmapFontLoaded = "ui: original bitmap font {}: {} characters, {}x{} cells";
inline constexpr std::string_view TextFontChanged = "ui: original look text font: {}";

} // namespace dwe::ui::messages
