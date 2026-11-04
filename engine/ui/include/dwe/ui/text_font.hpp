#pragma once

#include <dwe/core/error.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace dwe::ui {

// How "original look" text (the map name, and other text later) is drawn: the player's text_font
// setting.
enum class TextFont : std::uint8_t {
  // A modern font (Noto Sans).
  Modern,
  // The original game's bitmap font, extracted by dwx to the base pack.
  Original,
};

// The setting's name in the UI commands, in ui.setting() and in the user settings.
inline constexpr std::string_view TextFontSetting = "text_font";

// The name of a choice in commands, settings and on the command line: "modern" or "original".
[[nodiscard]] std::string_view textFontName(TextFont font);

// The choice with that name, or nothing for an unknown name.
[[nodiscard]] std::optional<TextFont> textFontNamed(std::string_view name);

// Where the packs describe the "original look" font.
inline constexpr std::string_view OriginalLookAsset = "ui/fonts/original_look";

// The "original look" font as ui/fonts/original_look.yaml describes it: an RCSS font family that
// the player's choice resolves to the original bitmap font or to a modern family.
struct OriginalLook {
  // The family the documents name, as in `font-family: original-look`.
  std::string family;
  // The choice before the player makes one.
  TextFont defaultFont = TextFont::Modern;
  // The family the modern choice draws with; it also draws what the bitmap font lacks.
  std::string modernFamily;
  // The bitmap font's atlas and meta, by asset id (docs/formats/textos.md).
  std::string atlas;
  std::string meta;
  // The row under the capitals, from the top of a cell: where the line's baseline sits.
  int baseline = 0;
  // The size of the modern family that draws missing characters, as a fraction of the bitmap font's
  // line height.
  float fallbackSize = 1.0F;
};

// Reads ui/fonts/original_look.yaml.
core::Result<OriginalLook> parseOriginalLook(std::string_view yaml);

// A text RmlUi wrote in capitals (text-transform: uppercase) with its accented letters in capitals
// too. RmlUi only changes the letters a to z, so "Regañar" came out "REGAñAR": a text with capital
// letters a to z, none small, and a small accented Latin-1 letter is such a text, and those letters
// go to capitals ("REGAÑAR"). Any other text comes back as it is ("Año", "día", "HP").
[[nodiscard]] std::string finishUppercase(std::string_view text);

} // namespace dwe::ui
