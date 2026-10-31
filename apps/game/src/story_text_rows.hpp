#pragma once

#include <dwe/localization/catalog.hpp>
#include <dwe/screens/screen_settings.hpp>

#include <glm/vec2.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::app {

inline constexpr std::string_view StoryTextboxDocument = "story_textbox";

// A run of a textbox's row in one of the font's colours, and a row of runs.
struct TextRun {
  std::string text;
  std::int64_t color = 0;
};
using TextRow = std::vector<TextRun>;

// The text colour a page starts in: the font's normal colour, which the pages switch back to after
// a name ({color:1}).
inline constexpr std::int64_t PageColor = 1;

// The arguments that turn the {color:N} tags of a text into the markers appendPageRows splits at
// (localization::tr formats them before it drops an unknown tag).
[[nodiscard]] std::vector<localization::TextArgument> colorArguments();

// Appends a page's rows, split at its line breaks and colour markers, starting in PageColor; at
// most as many rows as the document has room for. From a {cell:00XX} tag on, the rows stay empty:
// the original stops drawing the page there.
void appendPageRows(std::vector<TextRow>& rows, std::string_view text);

// How the story_textbox document draws the box (script_textbox.c: showTextbox).
struct TextboxLook {
  bool open = false;
  // The speech box, or a box sized in columns and rows.
  bool speech = true;
  std::int64_t columns = 0;
  std::int64_t rows = 0;
  // How far the box has opened, from 0 to 1.
  double opening = 1.0;
  // Where the box opens from, as a fraction of the window.
  std::optional<glm::vec2> origin;
  bool prompt = false;
  // The row of a selection's cursor, counted from the box's first row.
  std::optional<std::size_t> cursorRow;
  // How many characters the selection's bar spans: the page's longest row, its speaker's name left
  // out (script_textbox.c: scriptShowSelection takes the longest line showTextbox wrote).
  std::optional<std::size_t> cursorColumns;
  // The speech box at the top of the screen instead of the bottom (script_menu.c:
  // setupNewGameDialogueBox).
  bool top = false;
};

// The characters of the longest of `rows`, from `first` on: a character is a code point.
[[nodiscard]] std::size_t longestRow(const std::vector<TextRow>& rows, std::size_t first);

// The story_textbox document's settings for a box and its rows; publish them with
// textboxRevisionSetting.
[[nodiscard]] screens::ScreenSettings textboxSettings(const TextboxLook& look,
                                                      const std::vector<TextRow>& rows);
inline constexpr std::string_view TextboxRevisionSetting = "story_textbox.revision";

} // namespace dwe::app
