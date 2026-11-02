// The default bindings the engine adds to the original's pad (docs/modulos/engine/input.md).

#include <dwe/input/input_state.hpp>
#include <dwe/input/keys.hpp>

#include <doctest/doctest.h>

using namespace dwe::input;
using namespace dwe::platform;

TEST_CASE("F10 and the pad's Select open the game's options, and nothing else") {
  InputState input{defaultActionMap()};
  input.handle(KeyChanged{.key = keys::F10, .down = true, .repeat = false});
  CHECK(input.wasPressed("settings"));
  CHECK_FALSE(input.wasPressed("pause"));
  CHECK_FALSE(input.wasPressed("menu"));
  CHECK_FALSE(input.wasPressed("cancel"));

  InputState pad{defaultActionMap()};
  pad.handle(GamepadConnected{.id = 7, .family = GamepadFamily::Generic, .name = "Pad"});
  pad.handle(GamepadButtonChanged{.id = 7, .button = GamepadButton::Back, .down = true});
  CHECK(pad.wasPressed("settings"));
  CHECK_FALSE(pad.wasPressed("pause"));
}
