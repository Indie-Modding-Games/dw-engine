// The boxes placed as on the original's screen stay whole inside the window at every UI scale
// (content/packs/game/ui/common/screen_fit.lua): a scale larger than the window allows drew the
// speech box past the window's bottom edge and cut its last rows.

#include <doctest/doctest.h>

#include <array>
#include <format>
#include <string>
#include <string_view>

#include "game_pack_fixture.hpp"

namespace dwe::ui {
namespace {

using fixture::Screen;

struct Window {
  int width = 0;
  int height = 0;
  float scale = 1.0F;
};

// 16:10 at the scale step the speech box first ran out at, 16:9 and 4:3 at larger steps, a small
// window at the largest.
constexpr std::array Windows{
    Window{.width = 1680, .height = 1050, .scale = 1.15F},
    Window{.width = 1280, .height = 720, .scale = 1.3F},
    Window{.width = 1920, .height = 1080, .scale = 1.5F},
    Window{.width = 1024, .height = 768, .scale = 1.5F},
    Window{.width = 1280, .height = 720, .scale = 2.0F},
    Window{.width = 800, .height = 600, .scale = 2.0F},
};

// The speech box with the speaker's row and three rows of text, the most it holds.
void publishSpeech(Ui& ui) {
  ui.setSetting("story_textbox.open", true);
  ui.setSetting("story_textbox.kind", std::string{"speech"});
  ui.setSetting("story_textbox.opening", 1.0);
  ui.setSetting("story_textbox.row_count", 4.0);
  ui.setSetting("story_textbox.row.0.runs", 1.0);
  ui.setSetting("story_textbox.row.0.run.0.text", std::string{"Speaker"});
  ui.setSetting("story_textbox.row.0.run.0.color", 7.0);
  for (int row = 1; row < 4; ++row) {
    ui.setSetting(std::format("story_textbox.row.{}.runs", row), 1.0);
    ui.setSetting(std::format("story_textbox.row.{}.run.0.text", row),
                  std::string{"A row of the page's text"});
    ui.setSetting(std::format("story_textbox.row.{}.run.0.color", row), 1.0);
  }
  ui.setSetting("story_textbox.revision", 1.0);
}

void checkInside(const Ui::Rect& inner, const Ui::Rect& outer) {
  constexpr float Tolerance = 0.5F;
  CHECK(inner.left >= outer.left - Tolerance);
  CHECK(inner.top >= outer.top - Tolerance);
  CHECK(inner.right <= outer.right + Tolerance);
  CHECK(inner.bottom <= outer.bottom + Tolerance);
}

Ui::Rect windowRect(const Window& window) {
  return {.left = 0.0F,
          .top = 0.0F,
          .right = static_cast<float>(window.width),
          .bottom = static_cast<float>(window.height)};
}

} // namespace

TEST_CASE("the speech box and its rows stay inside the window at every UI scale") {
  for (const std::string_view skin : {"original", "modern"}) {
    for (const Window& window : Windows) {
      CAPTURE(skin);
      CAPTURE(window.width);
      CAPTURE(window.height);
      CAPTURE(window.scale);
      Screen screen{"story_textbox"};
      screen.skin(skin);
      screen.ui->resize(window.width, window.height);
      screen.ui->setScale(window.scale);
      screen.ui->show("story_textbox");
      publishSpeech(*screen.ui);
      screen.frames();
      const auto box = screen.ui->elementDrawnRect("story_textbox", "box");
      REQUIRE(box.has_value());
      checkInside(*box, windowRect(window));
      for (int row = 0; row < 4; ++row) {
        CAPTURE(row);
        const auto drawn = screen.ui->elementDrawnRect("story_textbox", std::format("row-{}", row));
        REQUIRE(drawn.has_value());
        checkInside(*drawn, *box);
      }
    }
  }
}

TEST_CASE("a speech box that fits the window is drawn where it is laid out") {
  Screen screen{"story_textbox"};
  screen.skin("original");
  screen.ui->show("story_textbox");
  publishSpeech(*screen.ui);
  screen.frames();
  const auto laid = screen.ui->elementRect("story_textbox", "box");
  const auto drawn = screen.ui->elementDrawnRect("story_textbox", "box");
  REQUIRE(laid.has_value());
  REQUIRE(drawn.has_value());
  CHECK(drawn->left == doctest::Approx(laid->left));
  CHECK(drawn->top == doctest::Approx(laid->top));
  CHECK(drawn->right == doctest::Approx(laid->right));
  CHECK(drawn->bottom == doctest::Approx(laid->bottom));
  CHECK(screen.ui->styleProperty("story_textbox", "box", "transform").find("translate") ==
        std::string::npos);
}

TEST_CASE("the notices in the speech box's place stay inside the window at every UI scale") {
  for (const std::string_view document : {"field_item_notice", "award_notice"}) {
    for (const Window& window : Windows) {
      CAPTURE(document);
      CAPTURE(window.width);
      CAPTURE(window.scale);
      Screen screen{document};
      screen.skin("original");
      screen.ui->resize(window.width, window.height);
      screen.ui->setScale(window.scale);
      screen.ui->show(document);
      screen.frames();
      const auto box = screen.ui->elementDrawnRect(document, "box");
      REQUIRE(box.has_value());
      checkInside(*box, windowRect(window));
    }
  }
}

} // namespace dwe::ui
