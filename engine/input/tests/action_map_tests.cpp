// The default bindings the engine adds to the original's pad (docs/modulos/engine/input.md).

#include <dwe/input/input_state.hpp>
#include <dwe/input/keys.hpp>

#include <doctest/doctest.h>

using namespace dwe::input;
using namespace dwe::platform;

TEST_CASE("the pad's Select opens the game's options; the keyboard has no options key") {
  // `settings` has no keyboard binding (docs/architecture/controles.md): options open from the menu.
  InputState input{defaultActionMap()};
  input.handle(KeyChanged{.key = keys::F10, .down = true, .repeat = false});
  CHECK_FALSE(input.wasPressed("settings"));

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
