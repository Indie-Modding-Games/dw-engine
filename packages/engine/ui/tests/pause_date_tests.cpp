#include <dwe/ui/ui.hpp>

#include <doctest/doctest.h>

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "game_pack_fixture.hpp"

namespace dwe::ui {
namespace {

using fixture::Screen;

constexpr std::array<std::string_view, 4> Skins{"original", "modern", "pixel", "devvago"};

struct DateWords {
  std::string_view language;
  std::string_view yearLabel;
  std::string_view yearUnit;
  std::string_view dayLabel;
  std::string_view dayUnit;
};

constexpr std::array<DateWords, 4> Dates{
    DateWords{
        .language = "en", .yearLabel = "Year", .yearUnit = "", .dayLabel = "Day", .dayUnit = ""},
    DateWords{
        .language = "ja", .yearLabel = "", .yearUnit = "年目", .dayLabel = "", .dayUnit = "日目"},
    DateWords{
        .language = "zh", .yearLabel = "第", .yearUnit = "年", .dayLabel = "第", .dayUnit = "天"},
    DateWords{.language = "ko", .yearLabel = "", .yearUnit = "년", .dayLabel = "", .dayUnit = "일"},
};

std::optional<Ui::Rect> shownRect(Ui& ui, std::string_view element) {
  if (ui.elementHasClass("pause_menu", element, "hidden")) {
    return std::nullopt;
  }
  return ui.elementRect("pause_menu", element);
}

void checkGroup(Ui& ui, std::string_view part, std::string_view label, std::string_view unit) {
  const std::string prefix = std::string{"field-"} + std::string{part};
  CHECK(ui.elementHasClass("pause_menu", prefix + "-label", "hidden") == label.empty());
  CHECK(ui.elementHasClass("pause_menu", prefix + "-unit", "hidden") == unit.empty());
  const auto digits = shownRect(ui, prefix + "-digits");
  REQUIRE(digits.has_value());
  if (!label.empty()) {
    CHECK(ui.elementText("pause_menu", prefix + "-label") == label);
    const auto before = shownRect(ui, prefix + "-label");
    REQUIRE(before.has_value());
    CHECK(before->left < digits->left);
  }
  if (!unit.empty()) {
    CHECK(ui.elementText("pause_menu", prefix + "-unit") == unit);
    const auto after = shownRect(ui, prefix + "-unit");
    REQUIRE(after.has_value());
    CHECK(after->left >= digits->left);
    CHECK(after->top < digits->bottom);
    CHECK(after->bottom > digits->top);
  }
}

} // namespace

TEST_CASE("the pause header puts each number before or after its words as the language writes "
          "dates, in every skin") {
  for (const DateWords& date : Dates) {
    for (const std::string_view skin : Skins) {
      CAPTURE(date.language);
      CAPTURE(skin);
      Screen screen{"pause_menu", date.language};
      screen.skin(skin);
      screen.ui->setSetting("hud.clock.year", 1.0);
      screen.ui->setSetting("hud.clock.day", 3.0);
      screen.ui->show("pause_menu", true);
      screen.frames();
      REQUIRE(screen.ui->elementHasClass("pause_menu", "panel", "on-main"));
      CHECK(screen.ui->elementHasClass("pause_menu", "title-date", "date-units") ==
            !date.yearUnit.empty());
      checkGroup(*screen.ui, "year", date.yearLabel, date.yearUnit);
      checkGroup(*screen.ui, "day", date.dayLabel, date.dayUnit);
      const auto year = screen.ui->elementRect("pause_menu", "field-year-digits");
      const auto day = screen.ui->elementRect("pause_menu", "field-day-digits");
      REQUIRE(year.has_value());
      REQUIRE(day.has_value());
      CHECK(year->right < day->left);
      if (!date.yearUnit.empty()) {
        const auto unit = screen.ui->elementRect("pause_menu", "field-year-unit");
        REQUIRE(unit.has_value());
        CHECK(unit->right <= day->left);
      }
    }
  }
}

} // namespace dwe::ui
