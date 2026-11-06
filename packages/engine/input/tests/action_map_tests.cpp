// The default bindings the engine adds to the original's pad (docs/modulos/engine/input.md).

#include <dwe/input/input_state.hpp>
#include <dwe/input/keys.hpp>

#include <doctest/doctest.h>

using namespace dwe::input;
using namespace dwe::platform;

TEST_CASE("Escape and F10 open the game's options; cancel lives on the Ctrl pair") {
  // `settings` takes the keyboard's `Escape` and `F10` (docs/architecture/controles.md): the quick
  // options page, the PC's classic pause key.
  InputState input{defaultActionMap()};
  input.handle(KeyChanged{.key = keys::Escape, .down = true, .repeat = false});
  CHECK(input.wasPressed("settings"));
  CHECK_FALSE(input.wasPressed("cancel"));

  InputState ten{defaultActionMap()};
  ten.handle(KeyChanged{.key = keys::F10, .down = true, .repeat = false});
  CHECK(ten.wasPressed("settings"));

  InputState ctrl{defaultActionMap()};
  ctrl.handle(KeyChanged{.key = keys::LeftCtrl, .down = true, .repeat = false});
  CHECK(ctrl.wasPressed("cancel"));

  InputState pad{defaultActionMap()};
  pad.handle(GamepadConnected{.id = 7, .family = GamepadFamily::Generic, .name = "Pad"});
  pad.handle(GamepadButtonChanged{.id = 7, .button = GamepadButton::Back, .down = true});
  CHECK(pad.wasPressed("settings"));
  CHECK_FALSE(pad.wasPressed("pause"));
}

TEST_CASE("the space bar and the F key both confirm") {
  InputState input{defaultActionMap()};
  input.handle(KeyChanged{.key = keys::F, .down = true, .repeat = false});
  CHECK(input.wasPressed("confirm"));
  CHECK(input.isDown("confirm"));
}
