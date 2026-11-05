#include <dwe/ui/glyph_art.hpp>

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace dwe::ui {
namespace {

std::uint8_t alphaAt(const assets::Image& image, std::uint32_t x, std::uint32_t y) {
  return static_cast<std::uint8_t>(image.pixels.at((((y * image.width) + x) * 4) + 3));
}

std::size_t opaquePixels(const assets::Image& image) {
  std::size_t count = 0;
  for (std::size_t at = 3; at < image.pixels.size(); at += 4) {
    count += image.pixels[at] != std::byte{0} ? 1 : 0;
  }
  return count;
}

} // namespace

TEST_CASE(
    "the classic glyphs are the original's 16 x 12 button and caps that grow with their text") {
  const auto button = drawGlyph("classic/button/A/0");
  REQUIRE(button.has_value());
  CHECK(button->width == 16);
  CHECK(button->height == 12);
  CHECK(button->pixels.size() == std::size_t{16} * 12 * 4);
  // The corners are clear, the middle is the button.
  CHECK(alphaAt(*button, 0, 0) == 0);
  CHECK(alphaAt(*button, 8, 6) == 255);

  CHECK(classicCapWidth("A") == 16);
  CHECK(classicCapWidth("@space") == 17);
  CHECK(classicCapWidth("OPTIONS") == 33);
  const auto cap = drawGlyph("classic/cap/ESC/1");
  REQUIRE(cap.has_value());
  CHECK(cap->width == 17);
  CHECK(cap->height == 12);

  // The three drawings of the animation differ: the top sinks a pixel a frame.
  const auto first = drawGlyph("classic/button/@cross/0");
  const auto last = drawGlyph("classic/button/@cross/2");
  REQUIRE(first.has_value());
  REQUIRE(last.has_value());
  CHECK(first->pixels != last->pixels);
  CHECK(alphaAt(*first, 8, 1) == 255);
  CHECK(alphaAt(*last, 8, 1) == 0);
}

TEST_CASE("the modern glyphs are smooth white symbols") {
  for (const std::string_view symbol :
       {"@cross", "@circle", "@square", "@triangle", "@south", "@east", "@west", "@north", "@left",
        "@right", "@up", "@down"}) {
    CAPTURE(symbol);
    const auto image = drawGlyph(std::string{"modern/"} + std::string{symbol});
    REQUIRE(image.has_value());
    CHECK(image->width == 64);
    CHECK(image->height == 64);
    CHECK(opaquePixels(*image) > 100);
  }
  const auto cross = drawGlyph("modern/@cross");
  REQUIRE(cross.has_value());
  CHECK(alphaAt(*cross, 32, 32) == 255);
  CHECK(alphaAt(*cross, 0, 0) == 0);
  // Smooth: some pixels are only partly covered.
  bool partial = false;
  for (std::size_t at = 3; at < cross->pixels.size(); at += 4) {
    const auto alpha = static_cast<std::uint8_t>(cross->pixels[at]);
    partial = partial || (alpha > 0 && alpha < 255);
  }
  CHECK(partial);
  // An arrow points its way: the tip's side is covered at the shaft's height, the tail's corners
  // are clear.
  const auto left = drawGlyph("modern/@left");
  REQUIRE(left.has_value());
  CHECK(alphaAt(*left, 18, 32) == 255);
  CHECK(alphaAt(*left, 46, 22) == 0);
  CHECK(alphaAt(*left, 22, 24) > 0);
}

TEST_CASE("an unknown glyph is an error") {
  CHECK_FALSE(drawGlyph("modern/@star").has_value());
  CHECK_FALSE(drawGlyph("classic/button/A/3").has_value());
  CHECK_FALSE(drawGlyph("classic/wheel/A/0").has_value());
  CHECK_FALSE(drawGlyph("classic/button/A").has_value());
  CHECK_FALSE(drawGlyph("other").has_value());
}

} // namespace dwe::ui
