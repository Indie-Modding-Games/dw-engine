#pragma once

#include <dwe/input/input_state.hpp>
#include <dwe/localization/catalog.hpp>
#include <dwe/screens/screen_settings.hpp>
#include <dwe/ui/ui.hpp>

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

// A name a page's tag inserted (the player's or the partner's name, a species, a move, an item, a
// song or a tournament) ends with this marker, so appendPageRows can tell where it stops.
inline constexpr char NameEnd = '\x1E';

// The name with its NameEnd marker.
[[nodiscard]] std::string endedName(std::string_view name);

// Space after names, an engine improvement (a UI setting and the pause menu's command with the
// same name, true or false; on by default): script_textbox.c: showTextbox copies an inserted name
// as it is, and ten texts of the original have no space after one (MAPHEAD 0x5cbe: a move's name
// and then a word), so the original draws the name stuck to the next word. With it, a name
// followed by a letter or a digit (past any colour tags) gets a space.
inline constexpr std::string_view NameSpacingSetting = "name_spacing";
// Whether the UI's NameSpacingSetting is on (on when nobody published it).
[[nodiscard]] bool spacesAfterNames(const ui::Ui& ui);

// Appends a page's rows, split at its line breaks and colour markers, starting in PageColor; at
// most as many rows as the document has room for. From a {stop} tag on ({cell:00XX} in older
// extractions), the rows stay empty: the original stops drawing the page there. A NameEnd marker
// is dropped, or becomes a space before a letter or a digit with spaceAfterNames.
void appendPageRows(std::vector<TextRow>& rows, std::string_view text,
                    bool spaceAfterNames = false);

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

// Whether a window point lies in a rectangle the UI laid out; an empty one (a hidden element)
// holds none.
[[nodiscard]] bool rectHolds(const ui::Ui::Rect& rect, glm::vec2 point);

// What the pointer does to a textbox selection in a tick (the engine's, the original has no
// pointer): the option it moved onto and the option clicked, each from 0. The options are the rows
// of the story_textbox document from row `first` on (after the speaker's name), as last laid out;
// the caller checks how many there are. Nothing while the box is hidden or the pointer is on no
// row.
struct OptionPointer {
  std::optional<std::size_t> pointed;
  std::optional<std::size_t> clicked;
};
[[nodiscard]] OptionPointer optionPointer(const ui::Ui& ui, const input::InputState& input,
                                          std::size_t first);

} // namespace dwe::app
