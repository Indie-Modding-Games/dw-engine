#include <dwe/input/action_map.hpp>
#include <dwe/input/input_state.hpp>
#include <dwe/input/keys.hpp>
#include <dwe/ui/navigation.hpp>

#include <doctest/doctest.h>

#include <vector>

namespace dwe::ui {
namespace {

using platform::GamepadAxis;
using platform::GamepadButton;

constexpr double Tick = 1.0 / 60.0;
constexpr platform::GamepadId Pad = 7;

// Runs one logic tick: navigation reads the input, then the tick closes.
std::vector<NavCommand> tick(Navigation& navigation, input::InputState& input) {
  std::vector<NavCommand> steps;
  navigation.tick(input, Tick, steps);
  input.endTick();
  return steps;
}

input::InputState withGamepad() {
  input::InputState input{input::defaultActionMap()};
  input.handle(platform::GamepadConnected{
      .id = Pad, .family = platform::GamepadFamily::Xbox, .name = "pad"});
  return input;
}

} // namespace

TEST_CASE("keys, the D-pad and the stick all step the same way") {
  Navigation navigation;
  input::InputState input = withGamepad();

  input.handle(platform::KeyChanged{.key = input::keys::Down, .down = true, .repeat = false});
  CHECK(tick(navigation, input) == std::vector{NavCommand::Down});
  input.handle(platform::KeyChanged{.key = input::keys::Down, .down = false, .repeat = false});
  CHECK(tick(navigation, input).empty());

  input.handle(
      platform::GamepadButtonChanged{.id = Pad, .button = GamepadButton::DpadUp, .down = true});
  CHECK(tick(navigation, input) == std::vector{NavCommand::Up});
  input.handle(
      platform::GamepadButtonChanged{.id = Pad, .button = GamepadButton::DpadUp, .down = false});
  CHECK(tick(navigation, input).empty());

  // A stick only counts once it is pushed past the threshold.
  input.handle(platform::GamepadAxisMoved{.id = Pad, .axis = GamepadAxis::LeftX, .value = 0.3F});
  CHECK(tick(navigation, input).empty());
  input.handle(platform::GamepadAxisMoved{.id = Pad, .axis = GamepadAxis::LeftX, .value = 0.9F});
  CHECK(tick(navigation, input) == std::vector{NavCommand::Right});
  input.handle(platform::GamepadAxisMoved{.id = Pad, .axis = GamepadAxis::LeftX, .value = 0.0F});
  CHECK(tick(navigation, input).empty());
}

TEST_CASE("confirm, cancel and menu come from every device") {
  Navigation navigation;
  input::InputState input = withGamepad();

  input.handle(platform::KeyChanged{.key = input::keys::Return, .down = true, .repeat = false});
  CHECK(tick(navigation, input) == std::vector{NavCommand::Confirm});
  input.handle(platform::KeyChanged{.key = input::keys::Return, .down = false, .repeat = false});
  input.handle(platform::KeyChanged{.key = input::keys::Escape, .down = true, .repeat = false});
  CHECK(tick(navigation, input) == std::vector{NavCommand::Cancel});
  input.handle(platform::KeyChanged{.key = input::keys::Tab, .down = true, .repeat = false});
  CHECK(tick(navigation, input) == std::vector{NavCommand::Menu});

  input.handle(
      platform::GamepadButtonChanged{.id = Pad, .button = GamepadButton::South, .down = true});
  CHECK(tick(navigation, input) == std::vector{NavCommand::Confirm});
  input.handle(
      platform::GamepadButtonChanged{.id = Pad, .button = GamepadButton::East, .down = true});
  CHECK(tick(navigation, input) == std::vector{NavCommand::Cancel});
  input.handle(
      platform::GamepadButtonChanged{.id = Pad, .button = GamepadButton::Start, .down = true});
  CHECK(tick(navigation, input) == std::vector{NavCommand::Menu});
  // Held buttons do not repeat.
  CHECK(tick(navigation, input).empty());
}

TEST_CASE("a held direction repeats after a delay, then at a steady rate") {
  Navigation navigation{NavigationTiming{.firstRepeatSeconds = 0.24, .repeatSeconds = 0.1}};
  input::InputState input{input::defaultActionMap()};
  input.handle(platform::KeyChanged{.key = input::keys::S, .down = true, .repeat = false});

  int steps = 0;
  std::vector<int> stepTicks;
  for (int index = 0; index < 60; ++index) {
    if (!tick(navigation, input).empty()) {
      ++steps;
      stepTicks.push_back(index);
    }
  }
  // One step at once, the first repeat after 0.24 s (tick 15), then one every 0.1 s (6 ticks): 9
  // steps in a second.
  REQUIRE(stepTicks.size() >= 3);
  CHECK(stepTicks[0] == 0);
  CHECK(stepTicks[1] == 15);
  CHECK(stepTicks[2] - stepTicks[1] == 6);
  CHECK(steps == 9);
}

TEST_CASE("a tap shorter than a tick still steps") {
  Navigation navigation;
  input::InputState input{input::defaultActionMap()};
  input.handle(platform::KeyChanged{.key = input::keys::Up, .down = true, .repeat = false});
  input.handle(platform::KeyChanged{.key = input::keys::Up, .down = false, .repeat = false});
  CHECK(tick(navigation, input) == std::vector{NavCommand::Up});
}

TEST_CASE("after a reset a direction held from before does nothing until released") {
  Navigation navigation;
  input::InputState input{input::defaultActionMap()};
  input.handle(platform::KeyChanged{.key = input::keys::W, .down = true, .repeat = false});
  CHECK(tick(navigation, input) == std::vector{NavCommand::Up});
  navigation.reset();
  for (int index = 0; index < 60; ++index) {
    CHECK(tick(navigation, input).empty());
  }
  input.handle(platform::KeyChanged{.key = input::keys::W, .down = false, .repeat = false});
  CHECK(tick(navigation, input).empty());
  input.handle(platform::KeyChanged{.key = input::keys::W, .down = true, .repeat = false});
  CHECK(tick(navigation, input) == std::vector{NavCommand::Up});
}

} // namespace dwe::ui
