#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/input/action_map.hpp>
#include <dwe/input/keys.hpp>
#include <dwe/ui/prompts.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace dwe::ui {

using input::DeviceKind;
using platform::GamepadFamily;

namespace {

// The game pack's texts in a language, the ones the player sees.
struct GameTexts {
    assets::MemoryFileSystem memory;
    std::unique_ptr<assets::VirtualFileSystem> files;
    std::unique_ptr<localization::Catalog> catalog;

    explicit GameTexts(std::string_view language) {
        const std::filesystem::path game = DWE_GAME_PACK;
        for (const auto& entry : std::filesystem::recursive_directory_iterator{game / "localization"}) {
            if (entry.is_regular_file()) {
                std::ifstream stream{entry.path(), std::ios::binary};
                const std::string text{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
                memory.addFile(std::filesystem::path{"game"} / entry.path().lexically_relative(game), text);
            }
        }
        memory.addFile("game/pack.yaml", "id: game\nversion: 1.0.0\nlayer: base\n");
        auto mounted = assets::VirtualFileSystem::mount(memory, std::vector<std::filesystem::path>{"game"});
        REQUIRE_MESSAGE(mounted.has_value(), mounted.error().message);
        files = std::make_unique<assets::VirtualFileSystem>(std::move(*mounted));
        auto loaded = localization::Catalog::load(*files, language);
        REQUIRE_MESSAGE(loaded.has_value(), loaded.error().message);
        catalog = std::make_unique<localization::Catalog>(std::move(*loaded));
    }
};

} // namespace

TEST_CASE("prompts name the control of the device in use, in the game pack's words") {
    const input::ActionMap map = input::defaultActionMap();
    const GameTexts texts{"es"};
    const localization::Catalog& labels = *texts.catalog;
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
    const GameTexts texts{"es"};
    const localization::Catalog& labels = *texts.catalog;
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

    const GameTexts english{"en"};
    const localization::Catalog& labels = *english.catalog;
    CHECK(controlLabel("key:space", GamepadFamily::Xbox, labels) == "Space");
    CHECK(controlLabel("key:enter", GamepadFamily::Xbox, labels) == "Enter");
    CHECK(controlLabel("key:q", GamepadFamily::Xbox, labels) == "Q");
    CHECK(controlLabel("key:f5", GamepadFamily::Xbox, labels) == "F5");
    CHECK(controlLabel("key:1000", GamepadFamily::Xbox, labels) == "key:1000");
    CHECK(controlLabel("gamepad:south", GamepadFamily::Xbox, labels) == "A");
    CHECK(controlLabel("gamepad:south", GamepadFamily::PlayStation, labels) == "Cross");
    CHECK(controlLabel("gamepad:dpad_up", GamepadFamily::PlayStation, labels) == "D-pad up");
}

} // namespace dwe::ui
