#pragma once

#include <dwe/assets/image.hpp>
#include <dwe/core/error.hpp>

#include <string_view>

namespace dwe::ui {

// The drawings of the button prompts' glyphs (prompts.hpp), all drawn here from code: no maker's
// artwork. A UI path under GlyphPrefix names one:
//
//   glyph/classic/button/<symbol>/<frame>  a round button in the original's pixel style, 16 x 12,
//                                          whose top sinks a pixel a frame (0 to 2), as the
//                                          original's confirm button does
//   glyph/classic/cap/<symbol>/<frame>     a key cap in the same style, 12 tall and as wide as its
//                                          symbol needs (classicCapWidth)
//   glyph/modern/<symbol>                  a symbol alone, white on clear, 64 x 64 and smooth,
//                                          for the modern skin to put on its own badge
//
// A symbol is a short text in capitals (A-Z, 0-9, + - = , . ?) or a named one: @cross, @circle,
// @square, @triangle (the shapes of the original's pad), @south, @east, @west, @north (the
// position of a button among the four), @space, @enter, @backspace, @tab, @shift, @caps_lock,
// @slash, @backslash, @left_bracket, @right_bracket, @semicolon, @apostrophe, @grave, @up, @down,
// @left, @right, @dpad_up, @dpad_down, @dpad_left, @dpad_right, @menu, @view, @home (the pad's
// own marks) and @mouse_left, @mouse_right, @mouse_middle.
inline constexpr std::string_view GlyphPrefix = "glyph/";
inline constexpr std::string_view ClassicGlyphPrefix = "glyph/classic/";

// Whether a classic glyph draws this symbol whole: a named one, or a text all in its capitals.
[[nodiscard]] bool classicDraws(std::string_view symbol);

// How wide a classic cap with this symbol is, in the original's pixels.
[[nodiscard]] int classicCapWidth(std::string_view symbol);

// The pixels (RGBA, not premultiplied) of a glyph by its path, GlyphPrefix left out.
[[nodiscard]] core::Result<assets::Image> drawGlyph(std::string_view path);

} // namespace dwe::ui
