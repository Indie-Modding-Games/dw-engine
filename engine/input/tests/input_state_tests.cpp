#include <dwe/input/input_state.hpp>
#include <dwe/input/keys.hpp>

#include <doctest/doctest.h>

using namespace dwe::input;
using namespace dwe::platform;

namespace {

constexpr GamepadId Pad = 7;

InputState withGamepad() {
  InputState input{defaultActionMap()};
  input.handle(
      GamepadConnected{.id = Pad, .family = GamepadFamily::PlayStation, .name = "DualSense"});
  return input;
}

} // namespace

TEST_CASE("a key drives every action bound to it") {
  InputState input{defaultActionMap()};
  input.handle(KeyChanged{.key = keys::Return, .down = true, .repeat = false});
  CHECK(input.isDown("confirm"));
  CHECK(input.wasPressed("confirm"));
  CHECK_FALSE(input.isDown("cancel"));
  CHECK(input.lastDevice() == DeviceKind::KeyboardMouse);
}

TEST_CASE("a press is reported for one tick, even if released before the tick") {
  InputState input{defaultActionMap()};
  input.handle(KeyChanged{.key = keys::Space, .down = true, .repeat = false});
  input.handle(KeyChanged{.key = keys::Space, .down = false, .repeat = false});
  CHECK_FALSE(input.isDown("confirm"));
  CHECK(input.wasPressed("confirm"));
  input.endTick();
  CHECK_FALSE(input.wasPressed("confirm"));
}

TEST_CASE("key repeats are not new presses") {
  InputState input{defaultActionMap()};
  input.handle(KeyChanged{.key = keys::Return, .down = true, .repeat = false});
  input.endTick();
  input.handle(KeyChanged{.key = keys::Return, .down = true, .repeat = true});
  CHECK_FALSE(input.wasPressed("confirm"));
  CHECK(input.isDown("confirm"));
}

TEST_CASE("gamepad buttons drive actions and switch the prompts to that gamepad") {
  InputState input = withGamepad();
  input.handle(GamepadButtonChanged{.id = Pad, .button = GamepadButton::East, .down = true});
  CHECK(input.isDown("cancel"));
  CHECK(input.lastDevice() == DeviceKind::Gamepad);
  CHECK(input.lastGamepadFamily() == GamepadFamily::PlayStation);
}

TEST_CASE("only a real pointer move switches back to the keyboard and mouse") {
  InputState input = withGamepad();
  input.handle(MouseMoved{.x = 100.0F, .y = 50.0F});
  input.handle(GamepadButtonChanged{.id = Pad, .button = GamepadButton::South, .down = true});
  // The window reports the pointer where it was, as when it gains the focus.
  input.handle(MouseMoved{.x = 100.0F, .y = 50.0F});
  CHECK(input.lastDevice() == DeviceKind::Gamepad);
  input.handle(MouseMoved{.x = 140.0F, .y = 50.0F});
  CHECK(input.lastDevice() == DeviceKind::KeyboardMouse);
}

TEST_CASE("events of an unknown gamepad are ignored") {
  InputState input{defaultActionMap()};
  input.handle(GamepadButtonChanged{.id = Pad, .button = GamepadButton::South, .down = true});
  CHECK_FALSE(input.isDown("confirm"));
}

TEST_CASE("the stick deadzone is ignored and the rest is rescaled") {
  InputState input = withGamepad();
  input.handle(GamepadAxisMoved{.id = Pad, .axis = GamepadAxis::LeftX, .value = 0.1F});
  CHECK(input.value("move_right") == doctest::Approx(0.0F));
  input.handle(GamepadAxisMoved{.id = Pad, .axis = GamepadAxis::LeftX, .value = 0.6F});
  CHECK(input.value("move_right") == doctest::Approx(0.5F));
  CHECK(input.value("move_left") == doctest::Approx(0.0F));
}

TEST_CASE("pushing a stick past the middle is a press") {
  InputState input = withGamepad();
  input.handle(GamepadAxisMoved{.id = Pad, .axis = GamepadAxis::LeftY, .value = -0.9F});
  CHECK(input.wasPressed("move_up"));
}

TEST_CASE("a diagonal direction never exceeds length 1") {
  InputState input{defaultActionMap()};
  input.handle(KeyChanged{.key = keys::D, .down = true, .repeat = false});
  input.handle(KeyChanged{.key = keys::S, .down = true, .repeat = false});
  const Vector2 move = input.vector("move_left", "move_right", "move_up", "move_down");
  CHECK(move.x == doctest::Approx(0.7071F).epsilon(0.001));
  CHECK(move.y == doctest::Approx(0.7071F).epsilon(0.001));
}

TEST_CASE("disconnecting a gamepad is reported for one tick and releases its controls") {
  InputState input = withGamepad();
  input.handle(GamepadButtonChanged{.id = Pad, .button = GamepadButton::South, .down = true});
  input.handle(GamepadDisconnected{Pad});
  CHECK(input.gamepadLost());
  CHECK_FALSE(input.isDown("confirm"));
  CHECK(input.gamepads().empty());
  input.endTick();
  CHECK_FALSE(input.gamepadLost());
}

TEST_CASE("an action can be rebound") {
  InputState input{defaultActionMap()};
  input.actionMap().clear("confirm");
  input.actionMap().bind("confirm", KeyBinding{keys::E});
  input.handle(KeyChanged{.key = keys::Return, .down = true, .repeat = false});
  CHECK_FALSE(input.isDown("confirm"));
  input.handle(KeyChanged{.key = keys::E, .down = true, .repeat = false});
  CHECK(input.isDown("confirm"));
}

TEST_CASE("a stick is pressed exactly when it starts to count as down") {
  InputState input = withGamepad();
  // 0.55 is past the middle of the raw range but, after the deadzone, still short of it.
  input.handle(GamepadAxisMoved{.id = Pad, .axis = GamepadAxis::LeftX, .value = 0.55F});
  CHECK_FALSE(input.isDown("move_right"));
  CHECK_FALSE(input.wasPressed("move_right"));
  input.handle(GamepadAxisMoved{.id = Pad, .axis = GamepadAxis::LeftX, .value = 0.7F});
  CHECK(input.isDown("move_right"));
  CHECK(input.wasPressed("move_right"));
}

TEST_CASE("losing focus releases everything that was held") {
  InputState input = withGamepad();
  input.handle(KeyChanged{.key = keys::Return, .down = true, .repeat = false});
  input.handle(GamepadAxisMoved{.id = Pad, .axis = GamepadAxis::LeftX, .value = 1.0F});
  CHECK(input.isDown("confirm"));
  CHECK(input.isDown("move_right"));
  input.handle(FocusLost{});
  CHECK_FALSE(input.isDown("confirm"));
  CHECK_FALSE(input.isDown("move_right"));
  CHECK(input.value("move_right") == doctest::Approx(0.0F));
}

TEST_CASE("walk is held with either shift key or the gamepad's right shoulder") {
  InputState input = withGamepad();
  CHECK_FALSE(input.isDown("walk"));
  input.handle(KeyChanged{.key = keys::LeftShift, .down = true, .repeat = false});
  CHECK(input.isDown("walk"));
  input.handle(KeyChanged{.key = keys::LeftShift, .down = false, .repeat = false});
  input.handle(KeyChanged{.key = keys::RightShift, .down = true, .repeat = false});
  CHECK(input.isDown("walk"));
  input.handle(KeyChanged{.key = keys::RightShift, .down = false, .repeat = false});
  input.handle(GamepadButtonChanged{.id = Pad, .button = GamepadButton::RightShoulder, .down = true});
  CHECK(input.isDown("walk"));
  CHECK(input.lastDevice() == DeviceKind::Gamepad);
}

TEST_CASE("a click tells itself apart from a key's or a gamepad's press of the same action") {
  InputState input = withGamepad();
  input.handle(MouseButtonChanged{.button = MouseButton::Left, .down = true});
  CHECK(input.wasPressed("confirm"));
  CHECK(input.wasClicked("confirm"));
  CHECK_FALSE(input.wasPressedByKeyOrPad("confirm"));
  input.endTick();
  CHECK_FALSE(input.wasClicked("confirm"));
  input.handle(KeyChanged{.key = keys::Return, .down = true, .repeat = false});
  CHECK(input.wasPressedByKeyOrPad("confirm"));
  CHECK_FALSE(input.wasClicked("confirm"));
  input.endTick();
  input.handle(GamepadButtonChanged{.id = Pad, .button = GamepadButton::South, .down = true});
  CHECK(input.wasPressedByKeyOrPad("confirm"));
  CHECK_FALSE(input.wasClicked("confirm"));
}

TEST_CASE("the pointer's place is kept, and only a real move counts as moved for the tick") {
  InputState input{defaultActionMap()};
  CHECK_FALSE(input.pointer().has_value());
  input.handle(MouseMoved{.x = 100.0F, .y = 50.0F});
  REQUIRE(input.pointer().has_value());
  CHECK(input.pointer()->x == 100.0F);
  CHECK_FALSE(input.pointerMoved()); // the first report is where it was
  input.endTick();
  input.handle(MouseMoved{.x = 140.0F, .y = 50.0F});
  CHECK(input.pointerMoved());
  input.endTick();
  CHECK_FALSE(input.pointerMoved());
  // A move the UI kept for itself still places the pointer, without counting as the player's.
  input.notePointer(MouseMoved{.x = 300.0F, .y = 200.0F});
  CHECK(input.pointer()->x == 300.0F);
  CHECK(input.pointer()->y == 200.0F);
  CHECK_FALSE(input.pointerMoved());
}
