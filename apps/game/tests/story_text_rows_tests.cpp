#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <variant>
#include <vector>

#include "story_text_rows.hpp"

using dwe::app::longestRow;
using dwe::app::TextboxLook;
using dwe::app::TextRow;

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

  // Not a cell of byte 0: drawn as written.
  rows.clear();
  dwe::app::appendPageRows(rows, "A{cell:889F}B");
  REQUIRE(rows.size() == 1);
  CHECK(rows[0][0].text == "A{cell:889F}B");
}
