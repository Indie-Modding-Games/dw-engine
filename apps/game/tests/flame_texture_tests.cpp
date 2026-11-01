#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "flame_texture.hpp"

using dwe::app::flameStripFrame;
using dwe::app::flameStripPixels;
using dwe::app::flameStripWidth;

TEST_CASE("anim.c animateEntityTexture: the flame species and their strips' widths") {
  CHECK(flameStripWidth("species/mera/model") == 120U);
  CHECK(flameStripWidth("species/blmr/model") == 120U);
  CHECK(flameStripWidth("species/bird/model") == 160U);
  CHECK(flameStripWidth("species/sabd/model") == 160U);
  CHECK(flameStripWidth("species/flar/model") == 64U);
  CHECK(flameStripWidth("species/puti/model") == 176U);
  CHECK_FALSE(flameStripWidth("species/agum/model").has_value());
  CHECK_FALSE(flameStripWidth("mera").has_value());
  CHECK_FALSE(flameStripWidth("species/mera/other").has_value());
}

TEST_CASE("anim.c animateEntityTexture: the strip of each original frame") {
  // Even frames only: 2 on multiples of 6, else 1 on multiples of 4, else 0.
  const std::vector<std::uint32_t> twelve{2, 0, 1, 2, 1, 0};
  for (std::uint64_t frame = 0; frame < 24; ++frame) {
    if (frame % 2 != 0) {
      CHECK_FALSE(flameStripFrame(frame).has_value());
    } else {
      CHECK(flameStripFrame(frame) == twelve[(frame % 12) / 2]);
    }
  }
}

TEST_CASE("anim.c animateEntityTexture: a strip is 32 rows of its width, below the top one") {
  // A 8 x 128 image whose texels hold their row number.
  dwe::assets::Image image{.width = 8, .height = 128, .pixels = {}};
  for (std::uint32_t row = 0; row < image.height; ++row) {
    for (std::uint32_t column = 0; column < image.width * 4; ++column) {
      image.pixels.push_back(static_cast<std::byte>(row));
    }
  }
  const std::vector<std::byte> strip = flameStripPixels(image, 4, 1);
  REQUIRE(strip.size() == std::size_t{4} * 32 * 4);
  // Strip 1 starts at row 64.
  CHECK(strip.front() == std::byte{64});
  CHECK(strip.back() == std::byte{95});
  CHECK(flameStripPixels(image, 4, 3).empty());
  CHECK(flameStripPixels(image, 9, 0).empty());
}
