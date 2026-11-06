#include <dwe/game/field/white_fade.hpp>

#include <doctest/doctest.h>

using dwe::game::field::WhiteFade;

TEST_CASE("the fade to white climbs 3 a frame from the last fade's shade and holds 160") {
  // Measured in the original: fadeToWhite(40) on a map entered with fadeFromBlack(20), which left
  // FADE_PROGRESS at 20, reads 50 after ten frames and 160 from then on.
  WhiteFade fade;
  fade.fadeOut(40, 20);
  CHECK(fade.active());
  CHECK(fade.shade() == 23);
  for (int frame = 2; frame <= 10; ++frame) {
    fade.frame();
  }
  CHECK(fade.shade() == 50);
  for (int frame = 0; frame < 200; ++frame) {
    fade.frame();
  }
  CHECK(fade.shade() == 160);
  CHECK(fade.active());
  CHECK(fade.whiteness() == doctest::Approx(1.0F));
}

TEST_CASE("the fade from white draws its frames down to 40 and stops on the frame after") {
  // Measured: fadeFromWhite(40) draws 157 first and 40 on its 40th frame; on the 41st the object
  // goes, the screen has no white at all and FADE_PROGRESS keeps 40.
  WhiteFade fade;
  fade.fadeOut(40, 20);
  fade.fadeIn(40);
  CHECK(fade.shade() == 157);
  for (int frame = 2; frame <= 40; ++frame) {
    fade.frame();
  }
  CHECK(fade.active());
  CHECK(fade.shade() == 40);
  CHECK(fade.whiteness() == doctest::Approx(40.0F / 128.0F));
  fade.frame();
  CHECK_FALSE(fade.active());
  CHECK(fade.shade() == 40);
  CHECK(fade.whiteness() == 0.0F);
}

TEST_CASE("a map change takes the white away and keeps the shade for its fade out") {
  WhiteFade fade;
  fade.fadeOut(20, 20);
  for (int frame = 0; frame < 30; ++frame) {
    fade.frame();
  }
  fade.stop();
  CHECK_FALSE(fade.active());
  CHECK(fade.shade() == 160);
}

TEST_CASE("a fade out of no frames wraps the byte the original keeps its shade in") {
  // fadeToWhite(0): a step of 160; 160 + 160 wraps to 64 before the comparison.
  WhiteFade fade;
  fade.fadeOut(0, 0);
  CHECK(fade.shade() == 160);
  fade.frame();
  CHECK(fade.shade() == 64);
}
