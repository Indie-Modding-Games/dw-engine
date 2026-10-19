#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// The characters of the original's text: two-byte Shift-JIS codes (code page 932) as the font draws them.
//
// Only the non-kanji rows of cp932 are mapped (lead bytes 0x81-0x84: punctuation, full-width Latin letters and
// digits, kana, Greek, Cyrillic and box drawing). That covers every glyph of the US font and every code in the US
// data; kanji, which only the Japanese releases need, decode as tags (game_text.hpp), so nothing is lost.
namespace dwe::formats::text {

// A two-byte Shift-JIS code as the game compares it: lead byte << 8 | trail byte.
using SjisCode = std::uint16_t;

// Returns the character of a code, or nothing if the code is not in the table. It is cp932 except where the US
// font draws something else: 0x8175 (cp932 U+300C) is an apostrophe, U+2019, and 0x8176 (cp932 U+300D) a double
// quote, U+201D. cp932's own codes for those two characters, 0x8166 and 0x8168, have no character, so the mapping
// stays one to one.
std::optional<char32_t> sjisToUnicode(SjisCode code);

// Returns the code of a character, the inverse of sjisToUnicode.
std::optional<SjisCode> unicodeToSjis(char32_t character);

// The full-width code that stands for a printable ASCII character (0x20-0x7E) in the original's two-byte text, as
// sjis.c: convertAsciiToJis converts it, with the US font's own glyphs for ' ' (0x8140), '.' (0x8142), '\''
// (0x8175) and '"' (0x8176). Other characters have none.
std::optional<SjisCode> fullWidthOf(char ascii);

// The inverse of fullWidthOf: the ASCII character a full-width code stands for.
std::optional<char> asciiOf(SjisCode code);

// Appends a character to UTF-8 text.
void appendUtf8(std::string& text, char32_t character);

// Reads the UTF-8 character at the start of text and removes it. Returns nothing for invalid UTF-8 (overlong forms,
// surrogates, values past U+10FFFF or a truncated sequence), and then leaves the text as it was.
std::optional<char32_t> takeUtf8(std::string_view& text);

} // namespace dwe::formats::text
