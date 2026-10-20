#include <dwe/input/action_map.hpp>
#include <dwe/input/keys.hpp>
#include <dwe/ui/prompts.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace dwe::ui {

using input::DeviceKind;
using platform::GamepadFamily;

namespace {

// The game pack's labels, the ones the player sees.
PromptLabels gameLabels() {
    std::ifstream stream{std::filesystem::path{DWE_GAME_PACK} / "ui/common/prompts.yaml", std::ios::binary};
    const std::string text{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
    auto labels = PromptLabels::parse(text);
    REQUIRE_MESSAGE(labels.has_value(), labels.error().message);
    return std::move(*labels);
}

} // namespace

TEST_CASE("prompts name the control of the device in use, in the game pack's words") {
    const input::ActionMap map = input::defaultActionMap();
    const PromptLabels labels = gameLabels();
    CHECK(promptLabel(map, DeviceKind::KeyboardMouse, GamepadFamily::Xbox, "confirm", labels) == "Intro");
    CHECK(promptLabel(map, DeviceKind::KeyboardMouse, GamepadFamily::Xbox, "cancel", labels) == "Esc");
    CHECK(promptLabel(map, DeviceKind::KeyboardMouse, GamepadFamily::Xbox, "menu", labels) == "Tab");
    CHECK(promptLabel(map, DeviceKind::Gamepad, GamepadFamily::Xbox, "confirm", labels) == "A");
    CHECK(promptLabel(map, DeviceKind::Gamepad, GamepadFamily::PlayStation, "confirm", labels) == "Cruz");
    CHECK(promptLabel(map, DeviceKind::Gamepad, GamepadFamily::PlayStation, "cancel", labels) == "Círculo");
    // The same position is labelled B on Nintendo gamepads.
    CHECK(promptLabel(map, DeviceKind::Gamepad, GamepadFamily::Nintendo, "confirm", labels) == "B");
    CHECK(promptLabel(map, DeviceKind::Gamepad, GamepadFamily::Steam, "menu", labels) == "Menú");
    CHECK(promptLabel(map, DeviceKind::Gamepad, GamepadFamily::Generic, "menu", labels) == "Start");
    CHECK(promptLabel(map, DeviceKind::Gamepad, GamepadFamily::Xbox, "debug_console", labels).empty());
    CHECK(promptLabel(map, DeviceKind::Gamepad, GamepadFamily::Xbox, "unknown", labels).empty());
}

TEST_CASE("prompts follow rebinding") {
    input::ActionMap map;
    const PromptLabels labels = gameLabels();
    map.bind("confirm", input::MouseBinding{platform::MouseButton::Left});
    map.bind("confirm", input::GamepadAxisBinding{.axis = platform::GamepadAxis::RightTrigger, .direction = 1.0F});
    CHECK(promptLabel(map, DeviceKind::KeyboardMouse, GamepadFamily::Xbox, "confirm", labels) == "Clic izquierdo");
    CHECK(promptLabel(map, DeviceKind::Gamepad, GamepadFamily::Xbox, "confirm", labels) == "Gatillo derecho");
    map.bind("confirm", input::KeyBinding{input::keys::E});
    CHECK(promptLabel(map, DeviceKind::KeyboardMouse, GamepadFamily::Xbox, "confirm", labels) == "E");
}

TEST_CASE("controls have stable names, and labels fall back to them") {
    CHECK(keyControl(input::keys::A) == "key:a");
    CHECK(keyControl(input::keys::Space) == "key:space");
    CHECK(keyControl(30) == "key:1");
    CHECK(keyControl(39) == "key:0");
    CHECK(keyControl(input::keys::F1) == "key:f1");
    CHECK(keyControl(69) == "key:f12");
    CHECK(keyControl(1000) == "key:1000");
    CHECK(gamepadButtonControl(platform::GamepadButton::DpadUp) == "gamepad:dpad_up");

    auto labels = PromptLabels::parse("keys:\n  space: Espacio\nfamilies:\n  xbox:\n    south: A\n");
    REQUIRE_MESSAGE(labels.has_value(), labels.error().message);
    const PromptLabels& parsed = labels.value_or(PromptLabels{});
    CHECK(parsed.label("key:space", GamepadFamily::Xbox) == "Espacio");
    CHECK(parsed.label("key:q", GamepadFamily::Xbox) == "Q");
    CHECK(parsed.label("key:f5", GamepadFamily::Xbox) == "F5");
    CHECK(parsed.label("key:enter", GamepadFamily::Xbox) == "key:enter");
    CHECK(parsed.label("gamepad:south", GamepadFamily::Xbox) == "A");
    CHECK(parsed.label("gamepad:south", GamepadFamily::PlayStation) == "gamepad:south");

    CHECK_FALSE(PromptLabels::parse("buttons: {}\n").has_value());
    CHECK_FALSE(PromptLabels::parse("keys: [a, b]\n").has_value());
}

} // namespace dwe::ui
