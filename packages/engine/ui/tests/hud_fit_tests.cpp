// The game pack's HUD in both looks: what it writes stays inside the boxes drawn round it
// (Ui::overflowingTexts, docs/modulos/engine/ui.md).

#include <dwe/ui/minimap.hpp>
#include <dwe/ui/ui.hpp>

#include <doctest/doctest.h>

#include <array>
#include <format>
#include <string>
#include <string_view>
#include <variant>

#include "game_pack_fixture.hpp"

namespace dwe::ui {
namespace {

using fixture::Screen;

struct Window {
  int width = 0;
  int height = 0;
  float scale = 1.0F;
};

// 4:3, 16:10, 16:9 and wider at the default UI scale, 130 % and larger, and a small window at the
// largest.
constexpr std::array Windows{
    Window{.width = 1024, .height = 768, .scale = 1.0F},
    Window{.width = 1280, .height = 720, .scale = 1.0F},
    Window{.width = 1280, .height = 720, .scale = 1.3F},
    Window{.width = 1680, .height = 1050, .scale = 1.3F},
    Window{.width = 2560, .height = 1080, .scale = 1.3F},
    Window{.width = 1024, .height = 768, .scale = 1.5F},
    Window{.width = 1280, .height = 720, .scale = 2.0F},
    Window{.width = 800, .height = 600, .scale = 2.0F},
};

void bump(Ui& ui, std::string_view revision) {
  const UiValue current = ui.setting(revision);
  const double* count = std::get_if<double>(&current);
  ui.setSetting(revision, (count == nullptr ? 0.0 : *count) + 1.0);
}

// The partner on the screen at x percent of the window's width, with `needs` of its six needs
// (hunger, sleep, toilet, tiredness, injury, sickness), ranked in that order.
void publishPartner(Ui& ui, double x, int needs) {
  ui.setSetting("hud.partner.visible", true);
  ui.setSetting("hud.partner.x", x);
  ui.setSetting("hud.partner.y", 45.0);
  bump(ui, "hud.partner.revision");
  for (int index = 0; index < 6; ++index) {
    ui.setSetting(std::format("hud.need.{}.active", index), index < needs);
    ui.setSetting(std::format("hud.need.{}.urgency", index),
                  static_cast<double>(index < needs ? index + 1 : 0));
  }
  bump(ui, "hud.revision");
  ui.update();
}

void checkInside(const Ui::Rect& inner, const Ui::Rect& outer) {
  constexpr float Tolerance = 0.5F;
  CHECK(inner.left >= outer.left - Tolerance);
  CHECK(inner.top >= outer.top - Tolerance);
  CHECK(inner.right <= outer.right + Tolerance);
  CHECK(inner.bottom <= outer.bottom + Tolerance);
}

} // namespace

TEST_CASE(
    "the stacked need bubble keeps five needs in one row inside the window at every UI scale") {
  // An engine improvement (need_bubble_stack): one balloon with up to five needs side by side,
  // moved in from a side of the window it would cross. The original look draws it; the modern one
  // shows the needs on its panel instead.
  for (const Window& window : Windows) {
    for (const double x : {1.0, 50.0, 99.0}) {
      CAPTURE(window.width);
      CAPTURE(window.height);
      CAPTURE(window.scale);
      CAPTURE(x);
      Screen screen{"hud", "en"};
      screen.skin("original");
      screen.ui->resize(window.width, window.height);
      screen.ui->setScale(window.scale);
      screen.ui->show("hud");
      publishPartner(*screen.ui, x, 5);
      // Past the balloon's growth (its first 10 frames at 20 a second).
      screen.frames(40);
      REQUIRE(screen.ui->elementHasClass("hud", "field-partner-bubble", "stack-5"));
      const auto bubble = screen.ui->elementDrawnRect("hud", "field-partner-bubble");
      REQUIRE(bubble.has_value());
      checkInside(*bubble, {.left = 0.0F,
                            .top = 0.0F,
                            .right = static_cast<float>(window.width),
                            .bottom = static_cast<float>(window.height)});
      // The five icons each in their own place, left to right, inside the balloon.
      float right = bubble->left;
      for (int icon = 0; icon < 5; ++icon) {
        CAPTURE(icon);
        const auto drawn =
            screen.ui->elementDrawnRect("hud", std::format("field-partner-bubble-icon-{}", icon));
        REQUIRE(drawn.has_value());
        CHECK(drawn->right - drawn->left > 0.0F);
        checkInside(*drawn, *bubble);
        CHECK(drawn->left >= right - 0.5F);
        right = drawn->right;
      }
      // Five icons' room and the ends: a third of the original's 320-pixel screen.
      const float dp = static_cast<float>(window.height) / 720.0F * window.scale;
      CHECK(bubble->right - bubble->left == doctest::Approx(312.0F * dp).epsilon(0.01));
      // In the middle of the window it hangs over the partner's head, as the original's.
      if (x == 50.0) {
        CHECK((bubble->left + bubble->right) / 2.0F ==
              doctest::Approx(static_cast<float>(window.width) / 2.0F).epsilon(0.002));
      }
    }
  }
}

TEST_CASE(
    "the original's need bubble stays over the partner's head, even at a side of the window") {
  for (const bool stacked : {true, false}) {
    CAPTURE(stacked);
    Screen screen{"hud", "en"};
    screen.skin("original");
    screen.ui->setSetting("need_bubble_stack", stacked);
    screen.ui->show("hud");
    // One need is the original's bubble either way; off, several are too.
    publishPartner(*screen.ui, 1.0, stacked ? 1 : 5);
    screen.frames(40);
    const auto bubble = screen.ui->elementDrawnRect("hud", "field-partner-bubble");
    REQUIRE(bubble.has_value());
    CHECK((bubble->left + bubble->right) / 2.0F == doctest::Approx(1280.0F * 0.01F).epsilon(0.01));
  }
}

TEST_CASE("the minimap's north mark sits on its own badge over the ring, in both looks") {
  for (const std::string_view skin : {"original", "modern"}) {
    CAPTURE(skin);
    Screen screen{"hud", "en"};
    screen.skin(skin);
    screen.ui->setSetting("minimap", std::string{"auto"});
    screen.ui->setSetting("minimap_size", std::string{"medium"});
    screen.ui->show("hud");
    MinimapGrid grid{
        .columns = 4, .rows = 2, .cells = {}, .cellSize = 100.0F, .originX = 0.0F, .originZ = 0.0F};
    grid.cells.assign(8, MinimapCell::Walkable);
    screen.ui->setMinimap(grid, "Bosque");
    screen.frames();
    const auto north = screen.ui->elementRect("hud", "minimap-north");
    const auto frame = screen.ui->elementRect("hud", "minimap-frame");
    REQUIRE(north.has_value());
    REQUIRE(frame.has_value());
    // On the top of the ring, crossing its edge: the badge is drawn over it, so the letter reads.
    CHECK(north->top < frame->top);
    CHECK(north->bottom > frame->top);
    for (const Ui::TextOverflow& overflow : screen.ui->overflowingTexts()) {
      MESSAGE(overflow.element << " '" << overflow.text << "' " << overflow.box << " by "
                               << overflow.overflow << " dp");
      CHECK_FALSE(overflow.element.contains("minimap"));
    }
  }
}

} // namespace dwe::ui
