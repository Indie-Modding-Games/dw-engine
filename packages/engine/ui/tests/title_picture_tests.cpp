// The title's pictures fill the window with the «widescreen pictures» option
// (content/packs/game/ui/common/title_picture.lua): scaled as one to cover the window's width, cut
// at most to a 16:9 window's rows, mirrored past their sides beyond that, and the boxes over them
// kept off the logo.

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

struct Window {
  int width = 0;
  int height = 0;
};

// 16:9 at two sizes, 21:9, 16:10 and 4:3.
constexpr std::array Windows{
    Window{.width = 1920, .height = 1080}, Window{.width = 2560, .height = 1440},
    Window{.width = 2560, .height = 1080}, Window{.width = 1680, .height = 1050},
    Window{.width = 1440, .height = 1080}};

// TITLE2's columns mirrored on its left (skin.yaml: ow-title-wide) and its logo's rectangles in its
// pixels (the badge on top, then the body and the wordmark).
constexpr float MirrorLeft = 100.0F;
constexpr std::array<Ui::Rect, 2> MenuLogo{
    Ui::Rect{.left = 211, .top = 165, .right = 263, .bottom = 178},
    Ui::Rect{.left = 174, .top = 178, .right = 293, .bottom = 228}};

constexpr float Tolerance = 0.5F;

Ui::Rect rectOf(const Screen& screen, std::string_view document, std::string_view id) {
  const std::optional<Ui::Rect> rect = screen.ui->elementRect(document, id);
  REQUIRE(rect.has_value());
  return *rect;
}

bool overlaps(const Ui::Rect& a, const Ui::Rect& b) {
  return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
}

// The logo's rectangles in window pixels, from where the picture is drawn: TITLE2 is 240 rows
// tall, its own columns after the mirrored ones.
std::array<Ui::Rect, 2> logoIn(const Ui::Rect& picture) {
  const float scale = (picture.bottom - picture.top) / 240.0F;
  const float left = picture.left + (MirrorLeft * scale);
  std::array<Ui::Rect, 2> logo{};
  for (std::size_t area = 0; area < logo.size(); ++area) {
    logo[area] = {.left = left + (MenuLogo[area].left * scale),
                  .top = picture.top + (MenuLogo[area].top * scale),
                  .right = left + (MenuLogo[area].right * scale),
                  .bottom = picture.top + (MenuLogo[area].bottom * scale)};
  }
  return logo;
}

void checkCovers(const Ui::Rect& picture, const Window& window) {
  CHECK(picture.left <= Tolerance);
  CHECK(picture.top <= Tolerance);
  CHECK(picture.right >= static_cast<float>(window.width) - Tolerance);
  CHECK(picture.bottom >= static_cast<float>(window.height) - Tolerance);
}

void checkInsideAndOffLogo(const Ui::Rect& box, const Window& window,
                           const std::array<Ui::Rect, 2>& logo) {
  CHECK(box.left >= -Tolerance);
  CHECK(box.top >= -Tolerance);
  CHECK(box.right <= static_cast<float>(window.width) + Tolerance);
  CHECK(box.bottom <= static_cast<float>(window.height) + Tolerance);
  for (const Ui::Rect& area : logo) {
    CHECK_FALSE(overlaps(box, area));
  }
}

} // namespace

TEST_CASE("the title's picture covers the window, its logo whole and the menu off it") {
  for (const Window& window : Windows) {
    CAPTURE(window.width);
    CAPTURE(window.height);
    Screen screen{"main_menu"};
    screen.skin("original");
    screen.ui->resize(window.width, window.height);
    screen.ui->show("main_menu", true);
    screen.frames();
    CHECK(screen.ui->elementHasClass("main_menu", "scrim", "wide"));
    const Ui::Rect picture = rectOf(screen, "main_menu", "picture");
    checkCovers(picture, window);
    const std::array<Ui::Rect, 2> logo = logoIn(picture);
    // The logo stays whole inside the window, its last row above the window's bottom.
    CHECK(logo[1].right <= static_cast<float>(window.width));
    CHECK(logo[1].bottom < static_cast<float>(window.height));
    CHECK(logo[0].top > 0.0F);
    checkInsideAndOffLogo(rectOf(screen, "main_menu", "panel"), window, logo);
    // Never scaled past a 16:9 window's cut: at least 180 of the 240 rows show.
    const float scale = (picture.bottom - picture.top) / 240.0F;
    CHECK(static_cast<float>(window.height) / scale >= 180.0F - Tolerance);
  }
}

TEST_CASE("a window wider than 16:9 sees the picture's mirrored sides, darker towards its edges") {
  Screen screen{"main_menu"};
  screen.skin("original");
  screen.ui->resize(2560, 1080);
  screen.ui->show("main_menu", true);
  screen.frames();
  const Ui::Rect picture = rectOf(screen, "main_menu", "picture");
  // 1080 lines show 180 rows: 6 pixels a row, and the picture's own 320 columns 1920 pixels wide,
  // its mirrored sides filling the 640 left over.
  CHECK((picture.bottom - picture.top) / 240.0F == doctest::Approx(6.0F));
  const float left = picture.left + (MirrorLeft * 6.0F);
  CHECK(left > 0.0F);
  CHECK(left + 1920.0F < 2560.0F);
  const Ui::Rect fadeLeft = rectOf(screen, "main_menu", "picture-fade-left");
  const Ui::Rect fadeRight = rectOf(screen, "main_menu", "picture-fade-right");
  CHECK(fadeLeft.right == doctest::Approx(left).epsilon(0.01));
  CHECK(fadeRight.left == doctest::Approx(left + 1920.0F).epsilon(0.01));
  CHECK(fadeRight.right == doctest::Approx(2560.0F).epsilon(0.01));
}

TEST_CASE("a 4:3 window draws the title as the original, and the option off keeps the original "
          "frame") {
  Screen screen{"main_menu"};
  screen.skin("original");
  screen.ui->resize(1440, 1080);
  screen.ui->show("main_menu", true);
  screen.frames();
  const Ui::Rect picture = rectOf(screen, "main_menu", "picture");
  CHECK(picture.left + (MirrorLeft * 4.5F) == doctest::Approx(0.0F));
  CHECK(picture.top == doctest::Approx(0.0F));
  CHECK(picture.bottom == doctest::Approx(1080.0F));
  const Ui::Rect wide = rectOf(screen, "main_menu", "panel");
  screen.ui->setSetting("widescreen_pictures", false);
  screen.frames();
  CHECK_FALSE(screen.ui->elementHasClass("main_menu", "scrim", "wide"));
  const Ui::Rect original = rectOf(screen, "main_menu", "panel");
  // The original's place, 72 pixels across in the 4:3 picture, with the option and without.
  CHECK(original.left == doctest::Approx(72.0F * 4.5F));
  CHECK(wide.left == doctest::Approx(original.left));
  CHECK(wide.top == doctest::Approx(original.top));

  // In a 16:9 window the option off puts the menu back in the 4:3 frame in the middle.
  screen.ui->resize(1920, 1080);
  screen.frames();
  CHECK(rectOf(screen, "main_menu", "panel").left == doctest::Approx(240.0F + (72.0F * 4.5F)));
  screen.ui->setSetting("widescreen_pictures", true);
  screen.frames();
  CHECK(screen.ui->elementHasClass("main_menu", "scrim", "wide"));
  CHECK(rectOf(screen, "main_menu", "panel").left == doctest::Approx(72.0F * 6.0F));
}

TEST_CASE("the slot list and its summary move off the logo of the larger picture") {
  for (const Window& window : Windows) {
    CAPTURE(window.width);
    CAPTURE(window.height);
    Screen screen{"save_slots"};
    screen.skin("original");
    screen.ui->resize(window.width, window.height);
    // A new game's list: eight slots and playing without saving, the tallest it gets.
    screen.ui->setSetting("save.slots.mode", std::string{"new"});
    screen.ui->setSetting("save.slots.manual", 8.0);
    screen.ui->show("save_slots", true);
    screen.frames();
    const Ui::Rect picture = rectOf(screen, "save_slots", "picture");
    checkCovers(picture, window);
    const std::array<Ui::Rect, 2> logo = logoIn(picture);
    checkInsideAndOffLogo(rectOf(screen, "save_slots", "panel"), window, logo);
    const Ui::Rect detail = rectOf(screen, "save_slots", "detail");
    if (detail.bottom > detail.top) {
      checkInsideAndOffLogo(detail, window, logo);
    }
  }
}

TEST_CASE("the modern look's title draws no picture and keeps its boxes") {
  Screen screen{"main_menu"};
  screen.skin("modern");
  screen.ui->resize(1920, 1080);
  screen.ui->show("main_menu", true);
  screen.frames();
  CHECK_FALSE(screen.ui->elementHasClass("main_menu", "scrim", "wide"));
  CHECK(screen.ui->styleProperty("main_menu", "panel", "margin-left") != "0dp");
}

} // namespace dwe::ui
