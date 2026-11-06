// The round minimap's ring (decorator minimap-disc): the bands each skin's ring is drawn with.

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <vector>

#include "minimap_disc.hpp"

using dwe::ui::MinimapRingBand;
using dwe::ui::minimapRingBands;
using dwe::ui::MinimapRingStyle;

TEST_CASE("the original ring is the original box's border drawn round, from the inside out") {
  const std::vector<MinimapRingBand> bands =
      minimapRingBands(MinimapRingStyle::Original, 0xFFFFFFFFU, 0x00000000U, 3.0F, 0.0F);
  REQUIRE(bands.size() == 4);
  // Black, light cyan, dark cyan and black, one original pixel (3 px here) each, solid.
  constexpr std::array<unsigned, 4> colours{0x020202FFU, 0x90D9FAFFU, 0x4A9FC5FFU, 0x020202FFU};
  for (std::size_t index = 0; index < bands.size(); ++index) {
    CAPTURE(index);
    CHECK(bands[index].inner == doctest::Approx(3.0F * static_cast<float>(index)));
    CHECK(bands[index].outer == doctest::Approx(3.0F * static_cast<float>(index + 1)));
    CHECK(bands[index].innerColour == colours.at(index));
    CHECK(bands[index].outerColour == colours.at(index));
  }
}

TEST_CASE("the plain ring has a shade inside, the ring, and a glow that fades out") {
  const std::vector<MinimapRingBand> bands =
      minimapRingBands(MinimapRingStyle::Plain, 0x8FD8FFFFU, 0x3AA8FF70U, 3.0F, 10.0F);
  REQUIRE(bands.size() == 3);
  // The shade: inside the rim, from clear to the ring's colour.
  CHECK(bands[0].inner < 0.0F);
  CHECK(bands[0].outer == doctest::Approx(0.0F));
  CHECK((bands[0].innerColour & 0xFFU) == 0U);
  CHECK((bands[0].outerColour & 0xFFFFFF00U) == 0x8FD8FF00U);
  // The ring, solid.
  CHECK(bands[1].inner == doctest::Approx(0.0F));
  CHECK(bands[1].outer == doctest::Approx(3.0F));
  CHECK(bands[1].innerColour == 0x8FD8FFFFU);
  // The glow, from its colour to clear.
  CHECK(bands[2].inner == doctest::Approx(3.0F));
  CHECK(bands[2].outer == doctest::Approx(13.0F));
  CHECK(bands[2].innerColour == 0x3AA8FF70U);
  CHECK(bands[2].outerColour == 0x3AA8FF00U);

  // No glow width, no glow.
  CHECK(minimapRingBands(MinimapRingStyle::Plain, 0x8FD8FFFFU, 0x3AA8FF70U, 3.0F, 0.0F).size() ==
        2);
}
