#pragma once

#include <dwe/core/error.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The original's texts as UTF-8 with readable tags, and back to the same bytes.
//
// There are two encodings. The executable's and the overlays' strings are byte strings: printable
// ASCII is one byte, and a byte from 0x80 starts a two-byte Shift-JIS code (font.c: drawString,
// sjis.c: convertAsciiToJis). Event script text is two-byte cells only (script_textbox.c:
// showTextbox): the US letters are full-width codes, which the decoder writes as plain ASCII.
//
// In both, '{' and '}' are written "{{" and "}}", and anything without a character becomes a tag in
// braces:
// - byte strings: {byte:XX} for a control byte, 0x7F or a lead byte with no trail byte, {sjis:XXXX}
// for a code
//   without a character;
// - script text: one tag per control cell (see scriptControlTags), {cell:XXXX} for a cell without a
// character or
//   meaning, "\n" for a new line, and {end:XXYY} when the final cells carry bytes other than 0.
namespace dwe::formats::text {

// How a script text control code uses its parameter byte.
enum class ControlParameter : std::uint8_t {
  Value,   // a number: {color:2}
  Pstat,   // the index of a script variable whose value is shown: {item:pstat=5}
  Ignored, // read and skipped: {tamer}; a byte other than 0 is kept as {tamer:raw=XX}
  Raw,     // no known consumer: {raw02:XX}
};

struct ControlTag {
  std::uint8_t code = 0;
  std::string_view name;
  ControlParameter parameter = ControlParameter::Ignored;
};

// The control codes of event script text and their tags, from showTextbox (codes it expands: names,
// numbers) and script_textbox.c: drawString2 (codes it draws: colour, tabs, the US-only columns).
// 0x03, which takes three parameter bytes ({raw03:XXXXXX}), and 0x0D, the new line, are handled
// apart.
std::span<const ControlTag> scriptControlTags();

// Decodes a byte string of the executable or an overlay (without its NUL). It never fails: every
// byte has a form. The codes the game reads inside some strings become tags too: #C<digit> is
// {color:<digit>}, #R is {br}, #W is {wait} (the end-of-battle messages), and each %d is {0},
// {1}... in order (the numbers sprintf prints).
std::string decodeByteString(std::span<const std::byte> raw);

// Encodes text written by decodeByteString back to its bytes.
core::Result<std::vector<std::byte>> encodeByteString(std::string_view text);

// A run of event script text.
struct ScriptText {
  std::string text;
  std::size_t size = 0; // the bytes of the run, its final 0D xx 00 yy included
};

// Decodes the run of script text at the start of bytes: two-byte cells (four for code 0x03) up to a
// 0x0D cell that is followed by a 0x00 byte, which ends the run with the cell it starts. Fails if
// the run does not end inside the bytes.
core::Result<ScriptText> decodeScriptText(std::span<const std::byte> bytes);

// Encodes text written by decodeScriptText back to its bytes, final cells included.
core::Result<std::vector<std::byte>> encodeScriptText(std::string_view text);

} // namespace dwe::formats::text
