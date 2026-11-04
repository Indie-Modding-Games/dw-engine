#include <dwe/ui/world_map.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace {

dwe::ui::WorldMapPicture twoAreas() {
  dwe::ui::WorldMapPicture picture{.width = 400.0F, .height = 200.0F, .areas = {}, .roads = {}};
  picture.areas.push_back({.x = 140.0F, .y = 100.0F, .radius = 40.0F, .colour = 0xC03020FF});
  picture.areas.push_back({.x = 260.0F, .y = 100.0F, .radius = 40.0F, .colour = 0x2040C0FF});
  picture.roads.push_back({.from = 0, .to = 1});
  return picture;
}

std::array<std::uint8_t, 4> pixelAt(const dwe::assets::Image& image, std::uint32_t x,
                                    std::uint32_t y) {
  const std::size_t at = (static_cast<std::size_t>(y) * image.width + x) * 4;
  return {static_cast<std::uint8_t>(image.pixels[at]),
          static_cast<std::uint8_t>(image.pixels[at + 1]),
          static_cast<std::uint8_t>(image.pixels[at + 2]),
          static_cast<std::uint8_t>(image.pixels[at + 3])};
}

} // namespace

TEST_CASE("the island map's land belongs to the nearest area and the far corners are sea") {
  const dwe::ui::WorldMapPicture picture = twoAreas();
  CHECK(dwe::ui::worldMapAreaAt(picture, 140.0F, 100.0F) == 0);
  CHECK(dwe::ui::worldMapAreaAt(picture, 265.0F, 95.0F) == 1);
  CHECK_FALSE(dwe::ui::worldMapAreaAt(picture, 2.0F, 2.0F).has_value());
  CHECK_FALSE(dwe::ui::worldMapAreaAt(picture, 398.0F, 198.0F).has_value());
}

TEST_CASE("the painted island keeps the canvas's proportions and is opaque") {
  const dwe::assets::Image image = dwe::ui::paintWorldMap(twoAreas(), {.pixelsWide = 200});
  CHECK(image.width == 200);
  CHECK(image.height == 100);
  REQUIRE(image.pixels.size() == std::size_t{200} * 100 * 4);
  for (std::size_t i = 3; i < image.pixels.size(); i += 4) {
    if (image.pixels[i] != std::byte{0xFF}) {
      FAIL("pixel " << i / 4 << " is not opaque");
    }
  }
}

TEST_CASE("each area is painted in its colour, faded when the player does not know it") {
  dwe::ui::WorldMapPicture picture = twoAreas();
  const dwe::assets::Image known = dwe::ui::paintWorldMap(picture, {.pixelsWide = 200});
  // Off the road, inside each area: red land on the left, blue on the right, and sea in a corner.
  const auto red = pixelAt(known, 70, 70);
  const auto blue = pixelAt(known, 130, 70);
  const auto sea = pixelAt(known, 2, 2);
  CHECK(red[0] > red[2]);
  CHECK(blue[2] > blue[0]);
  CHECK(sea != red);

  picture.areas[1].known = false;
  const dwe::assets::Image faded = dwe::ui::paintWorldMap(picture, {.pixelsWide = 200});
  CHECK(pixelAt(faded, 70, 70) == red);
  const auto pale = pixelAt(faded, 130, 70);
  CHECK(pale != blue);
  // Faded towards the parchment: more red and green than the known blue.
  CHECK(pale[0] > blue[0]);
  CHECK(pale[1] > blue[1]);
}

TEST_CASE("a picture without areas paints nothing") {
  CHECK(dwe::ui::paintWorldMap({}).pixels.empty());
  dwe::ui::WorldMapPicture flat = twoAreas();
  flat.height = 0.0F;
  CHECK(dwe::ui::paintWorldMap(flat).pixels.empty());
}

TEST_CASE("a canvas point lands on the image from 0 to 1, clamped") {
  const dwe::ui::WorldMapPicture picture = twoAreas();
  const dwe::ui::WorldMapPoint middle = dwe::ui::locateOnWorldMap(picture, 200.0F, 50.0F);
  CHECK(middle.u == doctest::Approx(0.5));
  CHECK(middle.v == doctest::Approx(0.25));
  const dwe::ui::WorldMapPoint out = dwe::ui::locateOnWorldMap(picture, -10.0F, 500.0F);
  CHECK(out.u == 0.0F);
  CHECK(out.v == 1.0F);
}
