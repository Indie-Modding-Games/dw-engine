#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "story_text_rows.hpp"

using dwe::app::longestRow;
using dwe::app::TextboxLook;
using dwe::app::TextRow;
using dwe::app::TextRun;

namespace {

double settingNumber(const dwe::screens::ScreenSettings& settings, const std::string& name) {
  const auto found = std::ranges::find(settings, name, &dwe::screens::ScreenSetting::name);
  REQUIRE(found != settings.end());
  REQUIRE(std::holds_alternative<double>(found->value));
  return std::get<double>(found->value);
}

} // namespace

TEST_CASE("textbox: a selection's bar spans the page's longest row, the speaker's name left out") {
  // The gym sign's choice: "Sign" names the speaker; scriptShowSelection's cursorWidth is 86, the 7
  // characters of "Explain" at 12 pixels each and 2 more.
  const std::vector<TextRow> rows{
      {{.text = "Sign board", .color = 5}},
      {{.text = "Begin", .color = 1}},
      {{.text = "Ex", .color = 1}, {.text = "plain", .color = 2}},
      {{.text = "Quit", .color = 1}},
  };
  CHECK(longestRow(rows, 1) == 7);
  CHECK(longestRow(rows, 0) == 10);
  // A character is a code point, not a byte.
  CHECK(longestRow({{{.text = "Señal", .color = 1}}}, 0) == 5);
  CHECK(longestRow({}, 0) == 0);

  TextboxLook look{};
  look.cursorRow = 1;
  look.cursorColumns = longestRow(rows, 1);
  CHECK(settingNumber(dwe::app::textboxSettings(look, rows), "story_textbox.cursor_columns") ==
        7.0);
  look.cursorColumns.reset();
  CHECK(settingNumber(dwe::app::textboxSettings(look, rows), "story_textbox.cursor_columns") ==
        -1.0);
}

TEST_CASE("textbox: a page stops drawing at a cell of byte 0, its later rows kept empty") {
  // story_054.text.0c02: drawString2 stops at the byte 0 and draws no more rows; the original
  // shows "fades away, he'll " and an empty third row.
  std::vector<TextRow> rows;
  dwe::app::appendPageRows(
      rows, "Sometimes, if MetalGreymon\nfades away, he'll {cell:0000}?reymon\ndigivolve into "
            "SkullGreymon.");
  REQUIRE(rows.size() == 3);
  REQUIRE(rows[1].size() == 1);
  CHECK(rows[1][0].text == "fades away, he'll ");
  CHECK(rows[2].empty());

  // dwx writes the zero cell as {stop}; the content of older versions has {cell:0000}.
  for (const std::string_view stop : {"{stop}", "{stop:raw=05}"}) {
    rows.clear();
    dwe::app::appendPageRows(rows, std::format("Hey guys, if you want\nto get thru here,{}?nt\n"
                                               "leave all your stuff!",
                                               stop));
    REQUIRE(rows.size() == 3);
    CHECK(rows[1][0].text == "to get thru here,");
    CHECK(rows[2].empty());
  }

  // Not a cell of byte 0: drawn as written.
  rows.clear();
  dwe::app::appendPageRows(rows, "A{cell:889F}B");
  REQUIRE(rows.size() == 1);
  CHECK(rows[0][0].text == "A{cell:889F}B");
}

TEST_CASE("textbox: a name the text runs into the next word gets a space with name spacing") {
  // script_textbox.c: showTextbox copies an inserted name as it is, and a few texts of the original
  // have no space after one (story_000.text.5cbe, a move's name and then a word): measured in the
  // original, the name and the word are drawn stuck together.
  const std::string name = dwe::app::endedName("Blaze Ring");
  const auto firstRow = [](std::string_view text, bool spaced) {
    std::vector<TextRow> rows;
    dwe::app::appendPageRows(rows, text, spaced);
    std::string out;
    for (const TextRun& run : rows.at(0)) {
      out += run.text;
    }
    return out;
  };
  // Without the improvement, as the original: the marker goes and the name stays stuck.
  CHECK(firstRow(name + "goes on.", false) == "Blaze Ringgoes on.");
  CHECK(firstRow(name + "goes on.", true) == "Blaze Ring goes on.");
  // A digit, and a Latin letter beyond ASCII, are words too.
  CHECK(firstRow(name + "2", true) == "Blaze Ring 2");
  CHECK(firstRow(name + "ésta", true) == "Blaze Ring ésta");
  // Punctuation, a space already there, a line break and the end of the page take none.
  CHECK(firstRow(name + ".", true) == "Blaze Ring.");
  CHECK(firstRow(name + "!", true) == "Blaze Ring!");
  CHECK(firstRow(name + "- ?", true) == "Blaze Ring- ?");
  CHECK(firstRow(name + " card.", true) == "Blaze Ring card.");
  CHECK(firstRow(name + "\nwas", true) == "Blaze Ring");
  CHECK(firstRow(name, true) == "Blaze Ring");
  CHECK(firstRow(name + "¿", true) == "Blaze Ring¿");

  // Past the colour tags a name sits between ("{color:10}{partner}{color:1}is going to"): the
  // space goes into the name's run, the next word keeps its colour.
  std::string coloured = "A{color:10}" + name + "{color:1}is going to";
  for (const auto& argument : dwe::app::colorArguments()) {
    const std::string tag = std::format("{{{}}}", argument.name);
    for (std::size_t at = coloured.find(tag); at != std::string::npos; at = coloured.find(tag)) {
      coloured.replace(at, tag.size(), argument.value);
    }
  }
  std::vector<TextRow> rows;
  dwe::app::appendPageRows(rows, coloured, true);
  REQUIRE(rows.size() == 1);
  REQUIRE(rows[0].size() == 3);
  CHECK(rows[0][1].text == "Blaze Ring ");
  CHECK(rows[0][1].color == 10);
  CHECK(rows[0][2].text == "is going to");
  CHECK(rows[0][2].color == 1);
}
