#include <doctest/doctest.h>

#include "medal_sheet.hpp"

using dwe::app::medalFace;
using dwe::app::MedalSlot;

TEST_CASE("map_object.c activateMedalTexture: each medal's face beside the model's slot") {
  // The slot: VRAM (512, 432), the image's (0, 176).
  CHECK(MedalSlot.x == 0);
  CHECK(MedalSlot.y == 176);
  CHECK(MedalSlot.width == 24U);
  CHECK(MedalSlot.height == 32U);
  // Medal 0: word 518, row 432; medal 8: word 566; medal 9: word 518, row 464; medal 14: 548.
  CHECK(medalFace(0).x == 24);
  CHECK(medalFace(0).y == 176);
  CHECK(medalFace(8).x == 216);
  CHECK(medalFace(8).y == 176);
  CHECK(medalFace(9).x == 24);
  CHECK(medalFace(9).y == 208);
  CHECK(medalFace(14).x == 144);
  CHECK(medalFace(14).y == 208);
}
