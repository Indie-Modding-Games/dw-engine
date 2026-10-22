#include <dwe/assets/file_system.hpp>
#include <dwe/assets/image.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/log.hpp>
#include <dwe/input/action_map.hpp>
#include <dwe/input/input_state.hpp>
#include <dwe/input/keys.hpp>
#include <dwe/localization/catalog.hpp>
#include <dwe/ui/ui.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "bitmap_font_fixture.hpp"

namespace dwe::ui {
namespace {

using assets::MemoryFileSystem;
using assets::VirtualFileSystem;
using platform::GamepadButton;

constexpr double Tick = 1.0 / 60.0;
constexpr platform::GamepadId Pad = 3;

assets::AssetId idOf(std::string_view text) {
  auto id = assets::AssetId::parse(text);
  REQUIRE(id.has_value());
  return *id;
}

// The team pack from the source tree, a stand-in for the base pack it depends on, a pack for the
// tests' own documents in the base layer, all three the engine's own (only the engine's own packs
// of its layers run their documents' Lua), a mod from a mods folder in the layer it declares, and,
// once a test adds to it, a dev pack: all in memory so a test can edit a file.
struct Content {
  MemoryFileSystem memory;
  std::unique_ptr<VirtualFileSystem> files;
  bool hasDevPack = false;

  explicit Content(std::string_view modLayer = "mods") {
    const std::filesystem::path game = DWE_GAME_PACK;
    for (const auto& entry : std::filesystem::recursive_directory_iterator{game}) {
      if (!entry.is_regular_file()) {
        continue;
      }
      std::ifstream stream{entry.path(), std::ios::binary};
      const std::string bytes{std::istreambuf_iterator<char>{stream},
                              std::istreambuf_iterator<char>{}};
      memory.addFile(std::filesystem::path{"packs/game"} / entry.path().lexically_relative(game),
                     bytes);
    }
    memory.addFile("packs/base/pack.yaml", "id: base\nversion: 0.1.0\nlayer: base\npriority: 0\n");
    // Above the team pack, so the tests' files override its own.
    memory.addFile("packs/tests/pack.yaml",
                   "id: tests\nversion: 0.1.0\nlayer: base\npriority: 2\n");
    memory.addFile("packs/mod/pack.yaml",
                   std::format("id: mod\nversion: 0.1.0\nlayer: {}\npriority: 0\n", modLayer));
    memory.addFile("packs/dev/pack.yaml", "id: dev\nversion: 0.1.0\nlayer: base\npriority: 0\n");
  }

  void add(std::string_view path, std::string_view text) {
    memory.addFile(std::filesystem::path{"packs/tests"} / path, text);
  }

  void addToMod(std::string_view path, std::string_view text) {
    memory.addFile(std::filesystem::path{"packs/mod"} / path, text);
  }

  void addToDev(std::string_view path, std::string_view text) {
    memory.addFile(std::filesystem::path{"packs/dev"} / path, text);
    hasDevPack = true;
  }

  // Mounts the packs; call after adding the tests' files, because mounting lists them.
  VirtualFileSystem& mount() {
    const auto location = [this](std::string_view root, assets::PackOrigin origin,
                                 bool fillsGaps = false) {
      return assets::PackLocation{.fileSystem = &memory,
                                  .root = std::string{root},
                                  .fillsGaps = fillsGaps,
                                  .origin = origin};
    };
    std::vector<assets::PackLocation> locations{location("packs/base", assets::PackOrigin::Engine),
                                                location("packs/game", assets::PackOrigin::Engine),
                                                location("packs/tests", assets::PackOrigin::Engine),
                                                location("packs/mod", assets::PackOrigin::Mod)};
    if (hasDevPack) {
      locations.push_back(location("packs/dev", assets::PackOrigin::Engine, true));
    }
    auto mounted = VirtualFileSystem::mount(locations);
    REQUIRE_MESSAGE(mounted.has_value(), mounted.error().message);
    files = std::make_unique<VirtualFileSystem>(std::move(*mounted));
    return *files;
  }
};

// A clock the tests move by hand.
struct FakeClock {
  double now = 0.0;
};

std::unique_ptr<Ui> createUi(const VirtualFileSystem& files, FakeClock* clock = nullptr,
                             bool hotReload = false, const script::ScriptLimits& limits = {}) {
  UiDesc desc{.files = &files,
              .width = 1280,
              .height = 720,
              .hotReload = hotReload,
              .clock = {},
              .scripts = limits};
  if (clock != nullptr) {
    desc.clock = [clock] { return clock->now; };
  }
  auto ui = Ui::create(desc);
  REQUIRE_MESSAGE(ui.has_value(), ui.error().message);
  return std::move(*ui);
}

// A frame: a logic tick with whatever the input holds, then layout.
void frame(Ui& ui, input::InputState& input) {
  ui.tick(input, Tick);
  input.endTick();
  ui.update();
}

void press(Ui& ui, input::InputState& input, platform::KeyCode key) {
  input.handle(platform::KeyChanged{.key = key, .down = true, .repeat = false});
  frame(ui, input);
  input.handle(platform::KeyChanged{.key = key, .down = false, .repeat = false});
  frame(ui, input);
}

void press(Ui& ui, input::InputState& input, GamepadButton button) {
  input.handle(platform::GamepadButtonChanged{.id = Pad, .button = button, .down = true});
  frame(ui, input);
  input.handle(platform::GamepadButtonChanged{.id = Pad, .button = button, .down = false});
  frame(ui, input);
}

// Moves the pointer to an element and clicks it, as the platform reports it. Returns whether the UI
// took the click.
bool click(Ui& ui, input::InputState& input, std::string_view document, std::string_view element) {
  const auto found = ui.elementCenter(document, element);
  REQUIRE_MESSAGE(found.has_value(), element);
  const Ui::Point center = found.value_or(Ui::Point{});
  const platform::Event move = platform::MouseMoved{.x = center.x, .y = center.y};
  const platform::Event down =
      platform::MouseButtonChanged{.button = platform::MouseButton::Left, .down = true};
  const platform::Event up =
      platform::MouseButtonChanged{.button = platform::MouseButton::Left, .down = false};
  bool taken = true;
  for (const platform::Event& event : {move, down, up}) {
    const bool took = ui.handleEvent(event);
    if (!std::holds_alternative<platform::MouseMoved>(event)) {
      taken = taken && took;
    }
    if (!took) {
      input.handle(event);
    }
  }
  frame(ui, input);
  return taken;
}

std::vector<std::string> commandNames(Ui& ui) {
  std::vector<std::string> names;
  for (const UiCommand& command : ui.takeCommands()) {
    names.push_back(command.name);
  }
  return names;
}

// A minimal document: a button that sends a command and a label its script fills.
constexpr std::string_view TestDocument = R"rml(<rml>
<head>
	<link type="text/rcss" href="/ui/common/base.rcss"/>
	<script src="logic.lua"></script>
	<script>shown = 0</script>
</head>
<body>
	<p id="label">before</p>
	<button id="first" autofocus="autofocus" onclick="ui.send('pressed', element.id)">One</button>
	<button id="second" onclick="ui.send('pressed', element.id)">Two</button>
</body>
</rml>)rml";

constexpr std::string_view TestLogic = R"lua(
function on_show(document)
	shown = shown + 1
	document:get("label"):set_text("shown " .. shown .. " <&>")
end
function greet(name)
	ui.send("greeted", name)
end
)lua";

// The lines written to the log since it was made.
class LogLines {
public:
  LogLines() {
    static const bool added = [] {
      core::addLogSink([](core::LogLevel /*level*/, std::string_view message) {
        lines().emplace_back(message);
      });
      return true;
    }();
    static_cast<void>(added);
    lines().clear();
  }

  [[nodiscard]] static std::size_t count(std::string_view text) {
    return static_cast<std::size_t>(std::ranges::count_if(
        lines(), [text](const std::string& line) { return line.contains(text); }));
  }

private:
  static std::vector<std::string>& lines() {
    static std::vector<std::string> captured;
    return captured;
  }
};

// A document a mod brings: a script from the game's layers, one of its own, inline Lua and an event
// attribute.
constexpr std::string_view ModDocument = R"rml(<rml>
<head>
	<link type="text/rcss" href="/ui/common/base.rcss"/>
	<script src="/ui/test/logic.lua"></script>
	<script src="own.lua"></script>
	<script>ui.send('inline')</script>
</head>
<body>
	<p id="label">before</p>
	<button id="first" autofocus="autofocus" onclick="ui.send('pressed', element.id)">One</button>
</body>
</rml>)rml";

// A template of the game, its override by a mod (with Lua in an event attribute), and a game
// document that uses it.
constexpr std::string_view FrameTemplate = R"rml(<template name="frame" content="content">
<head><link type="text/rcss" href="/ui/common/base.rcss"/></head>
<body><button id="framed" onclick="ui.send('game')">Framed</button><div id="content"/></body>
</template>)rml";

constexpr std::string_view ModFrameTemplate = R"rml(<template name="frame" content="content">
<head><link type="text/rcss" href="/ui/common/base.rcss"/></head>
<body><button id="framed" onclick="ui.send('hijacked')">Framed</button><div id="content"/></body>
</template>)rml";

constexpr std::string_view FramedDocument = R"rml(<rml>
<head>
	<link type="text/template" href="/ui/tpl/frame.rml"/>
	<script>ui.send('inline')</script>
</head>
<body template="frame"><p>inside</p></body>
</rml>)rml";

} // namespace

TEST_CASE("documents load through the virtual file system and run their scripts") {
  Content content;
  content.add("ui/test/document.rml", TestDocument);
  content.add("ui/test/logic.lua", TestLogic);
  auto ui = createUi(content.mount());

  REQUIRE(ui->loadDocument("test", idOf("ui/test/document")));
  CHECK_FALSE(ui->isVisible("test"));
  CHECK(ui->elementText("test", "label") == "before");
  ui->show("test");
  CHECK(ui->isVisible("test"));
  // Text set from Lua is text, never markup.
  CHECK(ui->elementText("test", "label") == "shown 1 &lt;&amp;&gt;");
  CHECK_FALSE(ui->capturesInput());

  REQUIRE(ui->callScript("test", "greet", std::array<std::string, 1>{"lua"}));
  const auto commands = ui->takeCommands();
  REQUIRE(commands.size() == 1);
  CHECK(commands[0].name == "greeted");
  CHECK(commands[0].value == UiValue{std::string{"lua"}});
  CHECK_FALSE(ui->callScript("test", "missing"));

  CHECK_FALSE(ui->loadDocument("nothing", idOf("ui/test/missing")));
}

TEST_CASE("a mod's document loads and runs the game's scripts, but none of its own Lua") {
  const LogLines log;
  Content content;
  content.add("ui/test/logic.lua", TestLogic);
  content.addToMod("ui/modded/document.rml", ModDocument);
  content.addToMod("ui/modded/own.lua", "function greet() ui.send('hijacked') end");
  auto ui = createUi(content.mount());
  REQUIRE(ui->loadDocument("modded", idOf("ui/modded/document")));
  ui->show("modded");
  // The game's logic.lua ran; the mod's own.lua, its inline script and its onclick did not.
  CHECK(commandNames(*ui).empty());
  REQUIRE(ui->callScript("modded", "greet", std::array<std::string, 1>{"lua"}));
  CHECK(commandNames(*ui) == std::vector<std::string>{"greeted"});
  input::InputState input{input::defaultActionMap()};
  ui->show("modded", true);
  frame(*ui, input);
  CHECK(click(*ui, input, "modded", "first"));
  CHECK(commandNames(*ui).empty());
  CHECK(log.count(
            "the engine script ui/modded/own comes from the pack 'mod' (a mod, mods layer)") == 1);
  // One line for the document, however many inline scripts and attributes it has.
  CHECK(
      log.count(
          "the document ui/modded/document comes from the pack 'mod' (a mod, mods layer); its inline scripts "
          "and event attributes do not run") == 1);
}

TEST_CASE(
    "a mod that declares the base layer is still a mod: none of its own Lua runs as the engine") {
  const LogLines log;
  Content content{"base"};
  content.add("ui/test/logic.lua", TestLogic);
  content.addToMod("ui/modded/document.rml", ModDocument);
  content.addToMod("ui/modded/own.lua", "function greet() ui.send('hijacked') end");
  auto ui = createUi(content.mount());
  REQUIRE(ui->loadDocument("modded", idOf("ui/modded/document")));
  ui->show("modded");
  // The game's logic.lua ran; the mod's own.lua, its inline script and its onclick did not.
  CHECK(commandNames(*ui).empty());
  input::InputState input{input::defaultActionMap()};
  ui->show("modded", true);
  frame(*ui, input);
  CHECK(click(*ui, input, "modded", "first"));
  CHECK(commandNames(*ui).empty());
  CHECK(log.count(
            "the engine script ui/modded/own comes from the pack 'mod' (a mod, base layer)") == 1);
  CHECK(
      log.count(
          "the document ui/modded/document comes from the pack 'mod' (a mod, base layer); its inline "
          "scripts and event attributes do not run") == 1);
}

TEST_CASE("a dev pack is one of the engine's own: its documents run their Lua") {
  Content content;
  content.addToDev("ui/test/document.rml", TestDocument);
  content.addToDev("ui/test/logic.lua", TestLogic);
  auto ui = createUi(content.mount());
  REQUIRE(ui->loadDocument("test", idOf("ui/test/document")));
  REQUIRE(ui->callScript("test", "greet", std::array<std::string, 1>{"lua"}));
  CHECK(commandNames(*ui) == std::vector<std::string>{"greeted"});
  input::InputState input{input::defaultActionMap()};
  ui->show("test", true);
  frame(*ui, input);
  CHECK(click(*ui, input, "test", "second"));
  CHECK(commandNames(*ui) == std::vector<std::string>{"pressed"});
}

TEST_CASE("a game document does not run a script that a mod overrides, and runs the rest") {
  const LogLines log;
  Content content;
  content.add("ui/test/document.rml", TestDocument);
  content.add("ui/test/logic.lua", TestLogic);
  content.addToMod("ui/test/logic.lua", "function greet() ui.send('hijacked') end");
  auto ui = createUi(content.mount());
  REQUIRE(ui->loadDocument("test", idOf("ui/test/document")));
  CHECK_FALSE(ui->callScript("test", "greet", std::array<std::string, 1>{"lua"}));
  CHECK(commandNames(*ui).empty());
  CHECK(log.count(
            "the engine script ui/test/logic comes from the pack 'mod' (a mod, mods layer)") == 1);
  // Its own inline Lua and its event attributes still run.
  input::InputState input{input::defaultActionMap()};
  ui->show("test", true);
  frame(*ui, input);
  CHECK(click(*ui, input, "test", "second"));
  CHECK(commandNames(*ui) == std::vector<std::string>{"pressed"});
}

TEST_CASE("a mod's template taints every game document that uses it, also after the first") {
  const LogLines log;
  Content content;
  content.add("ui/tpl/frame.rml", FrameTemplate);
  content.add("ui/first/document.rml", FramedDocument);
  content.add("ui/second/document.rml", FramedDocument);
  content.addToMod("ui/tpl/frame.rml", ModFrameTemplate);
  auto ui = createUi(content.mount());
  REQUIRE(ui->loadDocument("first", idOf("ui/first/document")));
  REQUIRE(ui->loadDocument("second", idOf("ui/second/document")));
  input::InputState input{input::defaultActionMap()};
  for (const std::string_view name : {"first", "second"}) {
    CAPTURE(name);
    ui->show(name, true);
    frame(*ui, input);
    CHECK(click(*ui, input, name, "framed"));
    ui->hide(name);
  }
  // Neither the inline script nor the template's onclick ran, in either document.
  CHECK(commandNames(*ui).empty());
  CHECK(log.count("the document ui/tpl/frame comes from the pack 'mod' (a mod, mods layer)") == 2);
}

TEST_CASE("a mod's template named like the game's does not replace it in a game document") {
  Content content;
  content.add("ui/tpl/frame.rml", FrameTemplate);
  content.add("ui/first/document.rml", FramedDocument);
  content.add("ui/second/document.rml", FramedDocument);
  content.addToMod("ui/modtpl/frame.rml", ModFrameTemplate);
  content.addToMod("ui/modded/document.rml", R"rml(<rml>
<head><link type="text/template" href="/ui/modtpl/frame.rml"/></head>
<body template="frame"><p>mod</p></body>
</rml>)rml");
  auto ui = createUi(content.mount());
  // The game's template is read by the first document, then the mod's takes its name, then the
  // second document links the game's again.
  REQUIRE(ui->loadDocument("first", idOf("ui/first/document")));
  REQUIRE(ui->loadDocument("modded", idOf("ui/modded/document")));
  REQUIRE(ui->loadDocument("second", idOf("ui/second/document")));
  CHECK(commandNames(*ui) == std::vector<std::string>{"inline", "inline"});
  input::InputState input{input::defaultActionMap()};
  ui->show("second", true);
  frame(*ui, input);
  CHECK(click(*ui, input, "second", "framed"));
  CHECK(commandNames(*ui) == std::vector<std::string>{"game"});
}

TEST_CASE("a script cannot set an event attribute, so it cannot make Lua from text") {
  Content content;
  content.add("ui/test/document.rml", R"rml(<rml>
<head><script>
function on_show(document) shown = document end
function arm(name) shown:get("target"):set_attribute(name, "ui.send('armed')") end
function title() shown:get("target"):set_attribute("title", "fine") ui.send("titled") end
</script></head>
<body><button id="target">Target</button></body>
</rml>)rml");
  auto ui = createUi(content.mount());
  REQUIRE(ui->loadDocument("test", idOf("ui/test/document")));
  input::InputState input{input::defaultActionMap()};
  ui->show("test", true);
  frame(*ui, input);
  for (const std::string_view name : {"onclick", "OnClick", "onmousedown"}) {
    CAPTURE(name);
    CHECK_FALSE(ui->callScript("test", "arm", std::array<std::string, 1>{std::string{name}}));
  }
  REQUIRE(ui->callScript("test", "title"));
  CHECK(click(*ui, input, "test", "target"));
  CHECK(commandNames(*ui) == std::vector<std::string>{"titled"});
}

TEST_CASE("there is one UI at a time, and it needs files") {
  Content content;
  auto& files = content.mount();
  CHECK_FALSE(Ui::create(UiDesc{}).has_value());
  auto ui = createUi(files);
  CHECK_FALSE(Ui::create(UiDesc{.files = &files, .clock = {}, .scripts = {}}).has_value());
  ui.reset();
  auto again = Ui::create(UiDesc{.files = &files, .clock = {}, .scripts = {}});
  CHECK(again.has_value());
}

TEST_CASE("each document has its own Lua environment") {
  Content content;
  content.add("ui/one/document.rml",
              "<rml><head><script>value = 'one'\nfunction tell() ui.send(value) end"
              "</script></head><body/></rml>");
  content.add("ui/two/document.rml",
              "<rml><head><script>value = 'two'\nfunction tell() ui.send(value) end"
              "</script></head><body/></rml>");
  auto ui = createUi(content.mount());
  REQUIRE(ui->loadDocument("one", idOf("ui/one/document")));
  REQUIRE(ui->loadDocument("two", idOf("ui/two/document")));
  REQUIRE(ui->callScript("two", "tell"));
  REQUIRE(ui->callScript("one", "tell"));
  CHECK(commandNames(*ui) == std::vector<std::string>{"two", "one"});
}

TEST_CASE("a broken script is reported and the document still loads") {
  Content content;
  content.add("ui/broken/document.rml",
              "<rml><head><script>this is not lua</script><script>while true do end</script></head>"
              "<body><p id='text' onclick='error(1)'>still here</p></body></rml>");
  auto ui = createUi(content.mount());
  REQUIRE(ui->loadDocument("broken", idOf("ui/broken/document")));
  CHECK(ui->elementText("broken", "text") == "still here");
}

TEST_CASE("a modal document takes the navigation and confirm clicks the focused entry") {
  Content content;
  content.add("ui/test/document.rml", TestDocument);
  content.add("ui/test/logic.lua", TestLogic);
  auto ui = createUi(content.mount());
  REQUIRE(ui->loadDocument("test", idOf("ui/test/document")));
  input::InputState input{input::defaultActionMap()};

  ui->setMenuDocument("test");
  press(*ui, input, input::keys::Tab);
  CHECK(ui->isVisible("test"));
  CHECK(ui->capturesInput());
  CHECK(ui->focusedElementId("test") == "first");

  press(*ui, input, input::keys::Down);
  CHECK(ui->focusedElementId("test") == "second");
  press(*ui, input, input::keys::Return);
  auto commands = ui->takeCommands();
  REQUIRE(commands.size() == 1);
  CHECK(commands[0].value == UiValue{std::string{"second"}});

  // Without an on_action handler, cancel closes the document.
  press(*ui, input, input::keys::Escape);
  CHECK_FALSE(ui->isVisible("test"));
  CHECK_FALSE(ui->capturesInput());
}

TEST_CASE("the pause menu works with the keyboard alone") {
  Content content;
  auto ui = createUi(content.mount());
  REQUIRE(ui->loadDocument("pause_menu", idOf("ui/pause_menu/document")));
  ui->setMenuDocument("pause_menu");
  ui->setSetting("minimap", std::string{"auto"});
  ui->setSetting("minimap_size", std::string{"medium"});
  ui->setSetting("ui_scale", 1.0);
  input::InputState input{input::defaultActionMap()};

  press(*ui, input, input::keys::Tab);
  REQUIRE(ui->isVisible("pause_menu"));
  CHECK(ui->focusedElementId("pause_menu") == "continue");
  CHECK(ui->elementText("pause_menu", "prompt-confirm") == "Enter");
  CHECK(ui->elementText("pause_menu", "prompt-cancel") == "Esc");

  press(*ui, input, input::keys::Down);
  CHECK(ui->focusedElementId("pause_menu") == "open-minimap");
  press(*ui, input, input::keys::Return);
  CHECK(ui->elementText("pause_menu", "title") == "Minimap");
  CHECK(ui->focusedElementId("pause_menu") == "minimap-mode");
  CHECK(ui->elementText("pause_menu", "minimap-mode-value") == "As the style");

  press(*ui, input, input::keys::Return);
  auto commands = ui->takeCommands();
  REQUIRE(commands.size() == 1);
  CHECK(commands[0].name == "minimap");
  CHECK(commands[0].value == UiValue{std::string{"on"}});
  // The label follows what the game reports, not what was asked.
  ui->setSetting("minimap", std::string{"off"});
  CHECK(ui->elementText("pause_menu", "minimap-mode-value") == "Never");

  // Right on an option changes it, as confirm does.
  press(*ui, input, input::keys::Down);
  CHECK(ui->focusedElementId("pause_menu") == "minimap-size");
  press(*ui, input, input::keys::Right);
  commands = ui->takeCommands();
  REQUIRE(commands.size() == 1);
  CHECK(commands[0].value == UiValue{std::string{"large"}});

  // Cancel goes back to the entry that opened the page, then closes the menu.
  press(*ui, input, input::keys::Escape);
  CHECK(ui->elementText("pause_menu", "title") == "Pause");
  CHECK(ui->focusedElementId("pause_menu") == "open-minimap");
  press(*ui, input, input::keys::Escape);
  CHECK_FALSE(ui->isVisible("pause_menu"));
  CHECK_FALSE(ui->capturesInput());

  // Tab opens and closes it; Quit sends quit.
  press(*ui, input, input::keys::Tab);
  press(*ui, input, input::keys::Up);
  CHECK(ui->focusedElementId("pause_menu") == "quit");
  press(*ui, input, input::keys::Space);
  CHECK(commandNames(*ui) == std::vector<std::string>{"quit"});
  press(*ui, input, input::keys::Tab);
  CHECK_FALSE(ui->isVisible("pause_menu"));
}

TEST_CASE("the pause menu works with a gamepad alone, and the prompts follow it") {
  Content content;
  auto ui = createUi(content.mount());
  REQUIRE(ui->loadDocument("pause_menu", idOf("ui/pause_menu/document")));
  ui->setMenuDocument("pause_menu");
  ui->setSetting("ui_scale", 1.0);
  input::InputState input{input::defaultActionMap()};
  input.handle(platform::GamepadConnected{
      .id = Pad, .family = platform::GamepadFamily::PlayStation, .name = "pad"});

  press(*ui, input, GamepadButton::Start);
  REQUIRE(ui->isVisible("pause_menu"));
  CHECK(ui->elementText("pause_menu", "prompt-confirm") == "Cross");
  CHECK(ui->elementText("pause_menu", "prompt-cancel") == "Circle");

  press(*ui, input, GamepadButton::DpadDown);
  press(*ui, input, GamepadButton::DpadDown);
  CHECK(ui->focusedElementId("pause_menu") == "open-options");
  press(*ui, input, GamepadButton::South);
  CHECK(ui->focusedElementId("pause_menu") == "display-mode");

  // The stick navigates too.
  input.handle(
      platform::GamepadAxisMoved{.id = Pad, .axis = platform::GamepadAxis::LeftY, .value = 1.0F});
  frame(*ui, input);
  input.handle(
      platform::GamepadAxisMoved{.id = Pad, .axis = platform::GamepadAxis::LeftY, .value = 0.0F});
  frame(*ui, input);
  CHECK(ui->focusedElementId("pause_menu") == "ui-scale");
  press(*ui, input, GamepadButton::DpadRight);
  auto commands = ui->takeCommands();
  REQUIRE(commands.size() == 1);
  CHECK(commands[0].name == "ui_scale");
  CHECK(commands[0].value == UiValue{1.15});

  // Touching the keyboard switches the prompts back.
  input.handle(platform::KeyChanged{.key = input::keys::Q, .down = true, .repeat = false});
  frame(*ui, input);
  CHECK(ui->elementText("pause_menu", "prompt-confirm") == "Enter");

  press(*ui, input, GamepadButton::East);
  press(*ui, input, GamepadButton::East);
  CHECK_FALSE(ui->isVisible("pause_menu"));
}

TEST_CASE("the pause menu works with the mouse alone") {
  Content content;
  auto ui = createUi(content.mount());
  REQUIRE(ui->loadDocument("pause_menu", idOf("ui/pause_menu/document")));
  ui->setSetting("fullscreen", false);
  input::InputState input{input::defaultActionMap()};

  // The mouse opens the menu from a button the game shows; here the app opens it.
  ui->show("pause_menu", true);
  ui->update();
  CHECK(click(*ui, input, "pause_menu", "open-options"));
  CHECK(ui->elementText("pause_menu", "title") == "Options");
  CHECK(click(*ui, input, "pause_menu", "display-mode"));
  auto commands = ui->takeCommands();
  REQUIRE(commands.size() == 1);
  CHECK(commands[0].name == "fullscreen");
  CHECK(commands[0].value == UiValue{true});
  // The clicks were the UI's: the game never saw a confirm.
  CHECK_FALSE(input.wasPressed("confirm"));

  // Hovering moves the focus, so there is only one highlighted entry.
  const auto vsync = ui->elementCenter("pause_menu", "vsync");
  REQUIRE(vsync.has_value());
  const Ui::Point vsyncAt = vsync.value_or(Ui::Point{});
  ui->handleEvent(platform::MouseMoved{.x = vsyncAt.x, .y = vsyncAt.y});
  CHECK(ui->focusedElementId("pause_menu") == "vsync");

  CHECK(click(*ui, input, "pause_menu", "options-back"));
  CHECK(ui->elementText("pause_menu", "title") == "Pause");
  // A click on the background keeps the focus on an entry, so the keyboard goes on from it.
  const std::string before = ui->focusedElementId("pause_menu");
  ui->handleEvent(platform::MouseMoved{.x = 4.0F, .y = 4.0F});
  CHECK(ui->handleEvent(
      platform::MouseButtonChanged{.button = platform::MouseButton::Left, .down = true}));
  CHECK(ui->handleEvent(
      platform::MouseButtonChanged{.button = platform::MouseButton::Left, .down = false}));
  frame(*ui, input);
  CHECK(ui->focusedElementId("pause_menu") == before);
  CHECK(ui->elementText("pause_menu", "title") == "Pause");
  // Nor does the pointer position the window reports after a resize, which the player did not move.
  const auto quit = ui->elementCenter("pause_menu", "quit");
  REQUIRE(quit.has_value());
  const Ui::Point quitAt = quit.value_or(Ui::Point{});
  ui->handleEvent(platform::WindowResized{.width = 1280, .height = 720});
  CHECK(ui->handleEvent(platform::MouseMoved{.x = quitAt.x, .y = quitAt.y}));
  frame(*ui, input);
  CHECK(ui->focusedElementId("pause_menu") == before);
  // A right click backs out, as cancel does.
  CHECK(ui->handleEvent(
      platform::MouseButtonChanged{.button = platform::MouseButton::Right, .down = true}));
  CHECK(ui->handleEvent(
      platform::MouseButtonChanged{.button = platform::MouseButton::Right, .down = false}));
  frame(*ui, input);
  CHECK_FALSE(ui->isVisible("pause_menu"));

  // With no menu open, clicks over the HUD go to the game.
  REQUIRE(ui->loadDocument("hud", idOf("ui/hud/document")));
  ui->show("hud");
  ui->update();
  const auto minimap = ui->elementCenter("hud", "minimap");
  REQUIRE(minimap.has_value());
  const Ui::Point minimapAt = minimap.value_or(Ui::Point{});
  ui->handleEvent(platform::MouseMoved{.x = minimapAt.x, .y = minimapAt.y});
  CHECK_FALSE(ui->handleEvent(
      platform::MouseButtonChanged{.button = platform::MouseButton::Left, .down = true}));
  CHECK_FALSE(ui->handleEvent(
      platform::MouseButtonChanged{.button = platform::MouseButton::Left, .down = false}));
}

TEST_CASE("the HUD's menu button asks the game to open the menu and names the key that does it") {
  Content content;
  auto ui = createUi(content.mount());
  input::InputState input{input::defaultActionMap()};
  REQUIRE(ui->loadDocument("hud", idOf("ui/hud/document")));
  ui->show("hud");
  ui->update();
  CHECK(ui->elementText("hud", "prompt-menu") == "Tab");
  CHECK(click(*ui, input, "hud", "menu-button"));
  CHECK(commandNames(*ui) == std::vector<std::string>{"open_menu"});
  // A gamepad names its own button.
  input.handle(platform::GamepadConnected{
      .id = Pad, .family = platform::GamepadFamily::Xbox, .name = "pad"});
  press(*ui, input, GamepadButton::South);
  CHECK(ui->elementText("hud", "prompt-menu") != "Tab");
}

namespace {

// One field bar of the hud.* contract (docs/architecture/ui.md#estado-del-campo).
void setFieldMeter(Ui& ui, int index, double value, double minimum, double maximum) {
  const std::string base = "hud.meter." + std::to_string(index) + ".";
  ui.setSetting(base + "value", value);
  ui.setSetting(base + "min", minimum);
  ui.setSetting(base + "max", maximum);
  ui.setSetting(base + "two_tone", true);
}

void bumpFieldRevision(Ui& ui) {
  const UiValue current = ui.setting("hud.revision");
  const double* count = std::get_if<double>(&current);
  ui.setSetting("hud.revision", (count == nullptr ? 0.0 : *count) + 1.0);
  ui.update();
}

} // namespace

TEST_CASE(
    "the HUD draws the field status on each revision, with the bars' low half marked as the original's icons") {
  Content content;
  auto ui = createUi(content.mount());
  REQUIRE(ui->loadDocument("hud", idOf("ui/hud/document")));
  ui->show("hud");
  ui->update();
  CHECK_FALSE(ui->elementHasClass("hud", "field-status", "shown"));

  ui->setSetting("hud.shown", true);
  ui->setSetting("hud.clock.hour", 7.0);
  ui->setSetting("hud.clock.minute", 5.0);
  // Happiness from -100 to 100 and discipline from 0 to 100: the middle is 0 and 50
  // (partner_impl.c: renderStatusBars swaps the happiness face below 0 and the discipline icon
  // below 50).
  setFieldMeter(*ui, 0, -1.0, -100.0, 100.0);
  setFieldMeter(*ui, 1, 50.0, 0.0, 100.0);
  bumpFieldRevision(*ui);
  CHECK(ui->elementHasClass("hud", "field-status", "shown"));
  CHECK(ui->elementText("hud", "field-time") == "07:05");
  CHECK(ui->elementHasClass("hud", "field-meter-0", "low"));
  CHECK_FALSE(ui->elementHasClass("hud", "field-meter-1", "low"));
  // Two tones: discipline 50 fills the first colour and none of the second.
  CHECK(ui->styleProperty("hud", "field-meter-1-fill", "width") == "100%");
  CHECK(ui->styleProperty("hud", "field-meter-1-high", "width") == "0%");

  setFieldMeter(*ui, 0, 0.0, -100.0, 100.0);
  setFieldMeter(*ui, 1, 49.0, 0.0, 100.0);
  bumpFieldRevision(*ui);
  CHECK_FALSE(ui->elementHasClass("hud", "field-meter-0", "low"));
  CHECK(ui->elementHasClass("hud", "field-meter-1", "low"));

  // Only the revision redraws: a value alone changes nothing until the game bumps it.
  ui->setSetting("hud.shown", false);
  ui->update();
  CHECK(ui->elementHasClass("hud", "field-status", "shown"));
  bumpFieldRevision(*ui);
  CHECK_FALSE(ui->elementHasClass("hud", "field-status", "shown"));

  // A skin switch redraws too, without a new revision: the new skin may change how the bars fill
  // (pips).
  ui->setSetting("hud.shown", true);
  ui->setSetting("hud.clock.minute", 6.0);
  ui->setSetting("ui_skin", std::string{"another"});
  ui->update();
  CHECK(ui->elementHasClass("hud", "field-status", "shown"));
  CHECK(ui->elementText("hud", "field-time") == "07:06");
}

TEST_CASE(
    "the field bars fill both tones by the value's place in each half, and only whole pips with pips") {
  Content content;
  auto ui = createUi(content.mount());
  REQUIRE(ui->loadDocument("hud", idOf("ui/hud/document")));
  ui->show("hud");
  // Without pips the second tone fills its part of the upper half: happiness 30 of 0 to 100 is 30
  // %, discipline 70 of 50 to 100 is 40 %.
  setFieldMeter(*ui, 0, 30.0, -100.0, 100.0);
  setFieldMeter(*ui, 1, 70.0, 0.0, 100.0);
  bumpFieldRevision(*ui);
  CHECK(ui->styleProperty("hud", "field-meter-0-fill", "width") == "100%");
  CHECK(ui->styleProperty("hud", "field-meter-0-high", "width") == "30%");
  CHECK(ui->styleProperty("hud", "field-meter-1-fill", "width") == "100%");
  CHECK(ui->styleProperty("hud", "field-meter-1-high", "width") == "40%");
  // In the lower half the first tone fills part of its half and the second none.
  setFieldMeter(*ui, 1, 10.0, 0.0, 100.0);
  bumpFieldRevision(*ui);
  CHECK(ui->styleProperty("hud", "field-meter-1-fill", "width") == "20%");
  CHECK(ui->styleProperty("hud", "field-meter-1-high", "width") == "0%");
}

TEST_CASE("a field bar with pips fills in whole pips of the original's 25, rounding down") {
  // The shared document with the bars marked as a skin marks them
  // (docs/architecture/ui.md#estado-del-campo).
  std::ifstream stream{std::filesystem::path{DWE_GAME_PACK} / "ui/hud/document.rml",
                       std::ios::binary};
  std::string document{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
  for (const std::string_view bar : {"field-meter-0-bar", "field-meter-1-bar"}) {
    const std::string plain = std::format(R"(class="bar" id="{}")", bar);
    const std::size_t at = document.find(plain);
    REQUIRE(at != std::string::npos);
    document.replace(at, plain.size(), std::format(R"(class="bar pips" id="{}")", bar));
  }
  Content content;
  content.add("ui/hud/document.rml", document);
  auto ui = createUi(content.mount());
  REQUIRE(ui->loadDocument("hud", idOf("ui/hud/document")));
  ui->show("hud");
  // Happiness 30: the upper half's 25 pips are 4 points each, so 7 of them (28 %); -97 is no pip of
  // the lower half (partner_impl.c: renderStatusBars).
  setFieldMeter(*ui, 0, 30.0, -100.0, 100.0);
  // Discipline 31: 2 points a pip, 15 pips of the lower half (60 %), none of the upper.
  setFieldMeter(*ui, 1, 31.0, 0.0, 100.0);
  bumpFieldRevision(*ui);
  CHECK(ui->styleProperty("hud", "field-meter-0-fill", "width") == "100%");
  CHECK(ui->styleProperty("hud", "field-meter-0-high", "width") == "28%");
  CHECK(ui->styleProperty("hud", "field-meter-1-fill", "width") == "60%");
  CHECK(ui->styleProperty("hud", "field-meter-1-high", "width") == "0%");
  setFieldMeter(*ui, 0, -97.0, -100.0, 100.0);
  bumpFieldRevision(*ui);
  CHECK(ui->styleProperty("hud", "field-meter-0-fill", "width") == "0%");
  CHECK(ui->styleProperty("hud", "field-meter-0-high", "width") == "0%");
}

TEST_CASE("the field clock is at night before 6:00 and from 17:00") {
  Content content;
  auto ui = createUi(content.mount());
  REQUIRE(ui->loadDocument("hud", idOf("ui/hud/document")));
  ui->show("hud");
  // clock.c: renderGameClock draws the moon with HOUR < 6 or HOUR >= 17.
  for (const auto& [hour, night] :
       {std::pair{0.0, true}, std::pair{5.0, true}, std::pair{6.0, false}, std::pair{16.0, false},
        std::pair{17.0, true}, std::pair{23.0, true}}) {
    CAPTURE(hour);
    ui->setSetting("hud.clock.hour", hour);
    bumpFieldRevision(*ui);
    CHECK(ui->elementHasClass("hud", "field-clock", "night") == night);
  }
}

namespace {

void setFieldNeeds(Ui& ui, bool hunger, bool sleep, bool toilet, bool tired = false) {
  ui.setSetting("hud.need.0.active", hunger);
  ui.setSetting("hud.need.1.active", sleep);
  ui.setSetting("hud.need.2.active", toilet);
  ui.setSetting("hud.need.3.active", tired);
  bumpFieldRevision(ui);
}

void placePartner(Ui& ui, bool visible) {
  ui.setSetting("hud.partner.visible", visible);
  ui.setSetting("hud.partner.x", visible ? 40.0 : 0.0);
  ui.setSetting("hud.partner.y", visible ? 30.0 : 0.0);
  const UiValue current = ui.setting("hud.partner.revision");
  const double* count = std::get_if<double>(&current);
  ui.setSetting("hud.partner.revision", (count == nullptr ? 0.0 : *count) + 1.0);
  ui.update();
}

// The need class the bubble has, or an empty string.
std::string bubbleNeed(const Ui& ui) {
  for (const std::string_view need : {"hunger", "sleep", "toilet", "tired"}) {
    if (ui.elementHasClass("hud", "field-partner-bubble", need)) {
      return std::string{need};
    }
  }
  return {};
}

} // namespace

TEST_CASE("the need bubble shows the need the original's bubble would, over the partner's head") {
  Content content;
  auto ui = createUi(content.mount());
  input::InputState input{input::defaultActionMap()};
  REQUIRE(ui->loadDocument("hud", idOf("ui/hud/document")));
  ui->show("hud");
  placePartner(*ui, true);
  setFieldNeeds(*ui, false, false, false);
  CHECK_FALSE(ui->elementHasClass("hud", "field-partner-bubble", "shown"));
  CHECK(ui->styleProperty("hud", "field-partner-bubble", "left") == "40%");
  CHECK(ui->styleProperty("hud", "field-partner-bubble", "top") == "30%");

  // Each need alone.
  setFieldNeeds(*ui, true, false, false);
  CHECK(ui->elementHasClass("hud", "field-partner-bubble", "shown"));
  CHECK(bubbleNeed(*ui) == "hunger");
  setFieldNeeds(*ui, false, true, false);
  CHECK(bubbleNeed(*ui) == "sleep");
  setFieldNeeds(*ui, false, false, true);
  CHECK(bubbleNeed(*ui) == "toilet");

  // Several: the highest first (partner_impl.c: handleConditionBubble: sleep, then hunger, then the
  // toilet)...
  setFieldNeeds(*ui, false, false, false);
  setFieldNeeds(*ui, true, true, true);
  CHECK(bubbleNeed(*ui) == "sleep");
  // ...and after 50 frames at 20 a second the highest other one, so the two highest take turns.
  ui->tick(input, 2.0);
  CHECK(bubbleNeed(*ui) == "sleep");
  ui->tick(input, 0.6);
  CHECK(bubbleNeed(*ui) == "hunger");
  ui->tick(input, 2.6);
  CHECK(bubbleNeed(*ui) == "sleep");
  setFieldNeeds(*ui, true, false, true);
  CHECK(bubbleNeed(*ui) == "hunger");
  ui->tick(input, 2.6);
  CHECK(bubbleNeed(*ui) == "toilet");

  // Tiredness goes between sleep and hunger (partner_impl.c: handleConditionBubble: condition bit
  // 0x02, bubble 4).
  setFieldNeeds(*ui, false, false, false);
  setFieldNeeds(*ui, false, false, false, true);
  CHECK(bubbleNeed(*ui) == "tired");
  setFieldNeeds(*ui, false, false, false);
  setFieldNeeds(*ui, true, false, true, true);
  CHECK(bubbleNeed(*ui) == "tired");
  ui->tick(input, 2.6);
  CHECK(bubbleNeed(*ui) == "hunger");
  setFieldNeeds(*ui, false, false, false);
  setFieldNeeds(*ui, true, true, false, true);
  CHECK(bubbleNeed(*ui) == "sleep");
  ui->tick(input, 2.6);
  CHECK(bubbleNeed(*ui) == "tired");

  // Hidden while the partner is not on the screen.
  placePartner(*ui, false);
  CHECK_FALSE(ui->elementHasClass("hud", "field-partner-bubble", "shown"));
}

TEST_CASE("the HUD leaves the minimap to the skin on auto, and shows or hides it on on and off") {
  Content content;
  auto ui = createUi(content.mount());
  REQUIRE(ui->loadDocument("hud", idOf("ui/hud/document")));
  ui->show("hud");
  ui->update();
  // Unset is auto.
  CHECK(ui->elementHasClass("hud", "minimap", "auto"));
  CHECK_FALSE(ui->elementHasClass("hud", "minimap", "hidden"));
  ui->setSetting("minimap", std::string{"off"});
  CHECK_FALSE(ui->elementHasClass("hud", "minimap", "auto"));
  CHECK(ui->elementHasClass("hud", "minimap", "hidden"));
  ui->setSetting("minimap", std::string{"on"});
  CHECK_FALSE(ui->elementHasClass("hud", "minimap", "auto"));
  CHECK_FALSE(ui->elementHasClass("hud", "minimap", "hidden"));
  ui->setSetting("minimap", std::string{"auto"});
  CHECK(ui->elementHasClass("hud", "minimap", "auto"));
}

TEST_CASE("the HUD shows the minimap of the current map and its markers") {
  Content content;
  auto ui = createUi(content.mount());
  REQUIRE(ui->loadDocument("hud", idOf("ui/hud/document")));
  ui->show("hud");

  MinimapGrid grid{
      .columns = 4, .rows = 2, .cells = {}, .cellSize = 100.0F, .originX = 0.0F, .originZ = 0.0F};
  grid.cells.assign(8, MinimapCell::Walkable);
  ui->setMinimap(grid, "Ciudad <Archivo>");
  ui->setMinimapMarkers(MinimapMarker{.x = 200.0F, .z = 100.0F, .facingX = 1.0F, .facingZ = 0.0F},
                        std::nullopt);
  ui->update();
  CHECK(ui->elementText("hud", "minimap-name") == "Ciudad &lt;Archivo&gt;");
  const auto frame = ui->elementCenter("hud", "minimap-frame");
  const auto player = ui->elementCenter("hud", "minimap-player");
  REQUIRE(frame.has_value());
  const Ui::Point frameAt = frame.value_or(Ui::Point{});
  REQUIRE(player.has_value());
  const Ui::Point playerAt = player.value_or(Ui::Point{});
  // The player stands in the middle of the map, so the marker's centre is the frame's.
  CHECK(playerAt.x == doctest::Approx(frameAt.x).epsilon(0.02));
  CHECK(playerAt.y == doctest::Approx(frameAt.y).epsilon(0.02));

  // A quarter of the way along the map and three quarters down it, the marker is there in the frame
  // too.
  ui->setMinimapMarkers(MinimapMarker{.x = 100.0F, .z = 150.0F, .facingX = 0.0F, .facingZ = 1.0F},
                        MinimapMarker{.x = 350.0F, .z = 50.0F, .facingX = 0.0F, .facingZ = -1.0F});
  ui->update();
  const auto frameBox = ui->elementCenter("hud", "minimap-frame");
  const auto movedPlayer = ui->elementCenter("hud", "minimap-player");
  const auto partner = ui->elementCenter("hud", "minimap-partner");
  REQUIRE(frameBox.has_value());
  REQUIRE(movedPlayer.has_value());
  REQUIRE(partner.has_value());
  const Ui::Point frameCentre = frameBox.value_or(Ui::Point{});
  const Ui::Point playerNow = movedPlayer.value_or(Ui::Point{});
  const Ui::Point partnerNow = partner.value_or(Ui::Point{});
  // The player is 100 units left of and 50 below the centre, the partner 150 right of and 50 above
  // it.
  CHECK(playerNow.x < frameCentre.x - 1.0F);
  CHECK(playerNow.y > frameCentre.y + 1.0F);
  CHECK(partnerNow.x > frameCentre.x + 1.0F);
  CHECK(partnerNow.y < frameCentre.y - 1.0F);
  CHECK((frameCentre.x - playerNow.x) ==
        doctest::Approx((partnerNow.x - frameCentre.x) * 2.0F / 3.0F).epsilon(0.05));
  CHECK((playerNow.y - frameCentre.y) ==
        doctest::Approx(frameCentre.y - partnerNow.y).epsilon(0.05));

  // A new map replaces the image.
  grid.columns = 2;
  grid.rows = 4;
  ui->setMinimap(grid, "Casa");
  ui->update();
  CHECK(ui->elementText("hud", "minimap-name") == "Casa");
}

// A UI that shows the texts of a catalog, as the game creates it.
std::unique_ptr<Ui> createUi(const VirtualFileSystem& files, const localization::Catalog& texts) {
  auto ui = Ui::create(UiDesc{
      .files = &files, .texts = &texts, .width = 1280, .height = 720, .clock = {}, .scripts = {}});
  REQUIRE_MESSAGE(ui.has_value(), ui.error().message);
  return std::move(*ui);
}

localization::Catalog catalogIn(const VirtualFileSystem& files, std::string_view language) {
  auto catalog = localization::Catalog::load(files, language);
  REQUIRE_MESSAGE(catalog.has_value(), catalog.error().message);
  return std::move(*catalog);
}

TEST_CASE("documents show the texts of the player's language and follow a change at run time") {
  Content content;
  content.add("text/en/main.yaml", "map_names.0001: \"File City <A>\"\n");
  content.add("text/es/main.yaml", "map_names.0001: Ciudad Archivo\n");
  content.add("ui/said/document.rml", R"rml(<rml><head><script>
function on_show(document) document:get("said"):set_text(t("pause_menu.ui_scale.value", {percent = 150})) end
function on_language(document)
	on_show(document)
	ui.send("languages", table.concat(ui.languages(), ","))
end
function unknown() ui.send(t("nobody.has.this")) end
</script></head><body><p id="label" text-key="common.yes"></p><p id="said"></p></body></rml>)rml");
  VirtualFileSystem& files = content.mount();
  localization::Catalog texts = catalogIn(files, "es");
  auto ui = createUi(files, texts);
  REQUIRE(ui->loadDocument("said", idOf("ui/said/document")));
  REQUIRE(ui->loadDocument("hud", idOf("ui/hud/document")));
  REQUIRE(ui->loadDocument("pause_menu", idOf("ui/pause_menu/document")));
  REQUIRE(ui->loadDocument("main_menu", idOf("ui/main_menu/document")));
  ui->setSetting("ui_scale", 1.0);
  ui->show("main_menu");
  ui->show("said");
  ui->show("hud");
  ui->show("pause_menu", true);
  MinimapGrid grid{
      .columns = 2, .rows = 2, .cells = {}, .cellSize = 100.0F, .originX = 0.0F, .originZ = 0.0F};
  grid.cells.assign(4, MinimapCell::Walkable);
  ui->setMinimap(grid, "main.map_names.0001");
  ui->update();
  CHECK(ui->elementText("said", "label") == "Sí");
  CHECK(ui->elementText("said", "said") == "150 %");
  CHECK(ui->elementText("hud", "minimap-name") == "Ciudad Archivo");
  CHECK(ui->elementText("pause_menu", "title") == "Pausa");
  CHECK(ui->elementText("pause_menu", "prompt-confirm") == "Intro");

  REQUIRE(texts.setLanguage("en"));
  ui->refreshTexts();
  ui->update();
  CHECK(ui->elementText("said", "label") == "Yes");
  CHECK(ui->elementText("said", "said") == "150%");
  CHECK(ui->elementText("hud", "minimap-name") == "File City &lt;A&gt;");
  CHECK(ui->elementText("pause_menu", "title") == "Pause");
  CHECK(ui->elementText("pause_menu", "prompt-confirm") == "Enter");
  CHECK(ui->elementText("main_menu", "prompt-confirm") == "Enter");
  CHECK(ui->elementText("main_menu", "language-value") == "English");
  const auto commands = ui->takeCommands();
  REQUIRE(commands.size() == 1);
  CHECK(commands[0].name == "languages");
  CHECK(commands[0].value == UiValue{std::string{"en,es"}});

  // A key no table has shows as itself, so the missing text is easy to spot.
  REQUIRE(ui->callScript("said", "unknown"));
  CHECK(commandNames(*ui) == std::vector<std::string>{"nobody.has.this"});
  // A map without a name shows none.
  ui->setMinimap(grid, "");
  ui->update();
  CHECK(ui->elementText("hud", "minimap-name").empty());
}

TEST_CASE("the field status writes its labels, time and date again in the new language") {
  Content content;
  VirtualFileSystem& files = content.mount();
  localization::Catalog texts = catalogIn(files, "es");
  auto ui = createUi(files, texts);
  REQUIRE(ui->loadDocument("hud", idOf("ui/hud/document")));
  ui->show("hud");
  ui->setSetting("hud.meter.0.label", std::string{"partner_menu.happiness"});
  ui->setSetting("hud.need.0.label", std::string{"hud.need.hunger"});
  ui->setSetting("hud.clock.day", 3.0);
  ui->setSetting("hud.clock.year", 1.0);
  bumpFieldRevision(*ui);
  CHECK(ui->elementText("hud", "field-meter-0-label") == "Felicidad");
  CHECK(ui->elementText("hud", "field-need-0") == "Hambre");
  CHECK(ui->elementText("hud", "field-date") == "Día 3, año 1");

  // No new revision: the language change alone writes them again.
  REQUIRE(texts.setLanguage("en"));
  ui->refreshTexts();
  ui->update();
  CHECK(ui->elementText("hud", "field-meter-0-label") == "Happiness");
  CHECK(ui->elementText("hud", "field-need-0") == "Hunger");
  CHECK(ui->elementText("hud", "field-date") == "Day 3, Year 1");
}

TEST_CASE(
    "the HUD shows the map change's map name in the middle of the screen, in the player's language") {
  Content content;
  content.add("text/en/main.yaml", "map_names.0002: Native Forest\n");
  content.add("text/es/main.yaml", "map_names.0002: Bosque Nativo\n");
  VirtualFileSystem& files = content.mount();
  localization::Catalog texts = catalogIn(files, "en");
  auto ui = createUi(files, texts);
  REQUIRE(ui->loadDocument("hud", idOf("ui/hud/document")));
  ui->setSetting("map_title_key", std::string{});
  ui->show("hud");
  ui->update();
  CHECK(ui->elementText("hud", "map-title").empty());

  ui->setSetting("map_title_key", std::string{"main.map_names.0002"});
  ui->setSetting("screen_fade", 0.75);
  ui->update();
  CHECK(ui->elementText("hud", "map-title") == "Native Forest");
  const auto title = ui->elementCenter("hud", "map-title");
  REQUIRE(title.has_value());
  const Ui::Point titleAt = title.value_or(Ui::Point{});
  CHECK(titleAt.x == doctest::Approx(640.0).epsilon(0.01));
  CHECK(titleAt.y == doctest::Approx(360.0).epsilon(0.01));

  REQUIRE(texts.setLanguage("es"));
  ui->refreshTexts();
  ui->update();
  CHECK(ui->elementText("hud", "map-title") == "Bosque Nativo");

  ui->setSetting("map_title_key", std::string{});
  ui->update();
  CHECK(ui->elementText("hud", "map-title").empty());
}

TEST_CASE("the main menu changes the language with the keyboard and starts the game") {
  Content content;
  VirtualFileSystem& files = content.mount();
  const localization::Catalog texts = catalogIn(files, "es");
  auto ui = createUi(files, texts);
  REQUIRE(ui->loadDocument("main_menu", idOf("ui/main_menu/document")));
  ui->setSetting("language", std::string{"es"});
  input::InputState input{input::defaultActionMap()};
  ui->show("main_menu", true);
  frame(*ui, input);
  CHECK(ui->focusedElementId("main_menu") == "play");
  CHECK(ui->elementText("main_menu", "play") == "Jugar");
  CHECK(ui->elementText("main_menu", "language-value") == "Español");

  press(*ui, input, input::keys::Down);
  REQUIRE(ui->focusedElementId("main_menu") == "language");
  press(*ui, input, input::keys::Right);
  auto commands = ui->takeCommands();
  REQUIRE(commands.size() == 1);
  CHECK(commands[0].name == "language");
  CHECK(commands[0].value == UiValue{std::string{"en"}});
  // Cancel does not leave the main menu.
  press(*ui, input, input::keys::Escape);
  CHECK(ui->isVisible("main_menu"));

  press(*ui, input, input::keys::Up);
  press(*ui, input, input::keys::Return);
  CHECK_FALSE(ui->isVisible("main_menu"));
}

TEST_CASE(
    "the main menu switches the text font and shows the one the game reports, in the player's language") {
  Content content;
  VirtualFileSystem& files = content.mount();
  localization::Catalog texts = catalogIn(files, "es");
  auto ui = createUi(files, texts);
  REQUIRE(ui->loadDocument("main_menu", idOf("ui/main_menu/document")));
  ui->setSetting("language", std::string{"es"});
  ui->setSetting("text_font", std::string{"modern"});
  input::InputState input{input::defaultActionMap()};
  ui->show("main_menu", true);
  frame(*ui, input);
  CHECK(ui->elementText("main_menu", "text-font-value") == "No");

  press(*ui, input, input::keys::Down);
  press(*ui, input, input::keys::Down);
  REQUIRE(ui->focusedElementId("main_menu") == "text-font");
  // With two fonts, left and right both switch to the other one.
  press(*ui, input, input::keys::Left);
  auto commands = ui->takeCommands();
  REQUIRE(commands.size() == 1);
  CHECK(commands[0].name == "text_font");
  CHECK(commands[0].value == UiValue{std::string{"original"}});

  // The menu shows what the game reports back, not what it asked for.
  ui->setSetting("text_font", std::string{"original"});
  frame(*ui, input);
  CHECK(ui->elementText("main_menu", "text-font-value") == "Sí");
  press(*ui, input, input::keys::Return);
  commands = ui->takeCommands();
  REQUIRE(commands.size() == 1);
  CHECK(commands[0].value == UiValue{std::string{"modern"}});

  REQUIRE(texts.setLanguage("en"));
  ui->setSetting("text_font", std::string{"modern"});
  ui->refreshTexts();
  frame(*ui, input);
  CHECK(ui->elementText("main_menu", "text-font-value") == "No");
}

TEST_CASE("the main menu works with the mouse alone") {
  Content content;
  VirtualFileSystem& files = content.mount();
  const localization::Catalog texts = catalogIn(files, "en");
  auto ui = createUi(files, texts);
  REQUIRE(ui->loadDocument("main_menu", idOf("ui/main_menu/document")));
  ui->setSetting("language", std::string{"en"});
  input::InputState input{input::defaultActionMap()};
  ui->show("main_menu", true);
  frame(*ui, input);
  CHECK(ui->elementText("main_menu", "language-value") == "English");
  CHECK(click(*ui, input, "main_menu", "language"));
  auto commands = ui->takeCommands();
  REQUIRE(commands.size() == 1);
  CHECK(commands[0].value == UiValue{std::string{"es"}});
  CHECK(click(*ui, input, "main_menu", "play"));
  CHECK_FALSE(ui->isVisible("main_menu"));
}

TEST_CASE("hot reload picks up an edited document") {
  Content content;
  content.add("ui/test/document.rml", TestDocument);
  content.add("ui/test/logic.lua", TestLogic);
  FakeClock clock;
  auto ui = createUi(content.mount(), &clock, true);
  REQUIRE(ui->loadDocument("test", idOf("ui/test/document")));
  ui->show("test");
  CHECK(ui->elementText("test", "label") == "shown 1 &lt;&amp;&gt;");

  content.add("ui/test/logic.lua",
              R"(function on_show(document) document:get("label"):set_text("edited") end)");
  ui->reloadChanged();
  CHECK(ui->elementText("test", "label") ==
        "shown 1 &lt;&amp;&gt;"); // checked at most twice a second
  clock.now = 1.0;
  ui->reloadChanged();
  CHECK(ui->isVisible("test"));
  CHECK(ui->elementText("test", "label") == "edited");
}

TEST_CASE("handlers a script causes share its instruction budget") {
  Content content;
  content.add("ui/loop/document.rml", R"rml(<rml><head><script>
local root
function on_show(document) root = document end
function click_forever() while true do root:get("button"):click() end end
function show_forever() while true do ui.show("other") ui.hide("other") end end
function fine() ui.send("fine") end
</script></head><body><button id="button" onclick="clicks = (clicks or 0) + 1">Go</button></body></rml>)rml");
  content.add(
      "ui/other/document.rml",
      "<rml><head><script>function on_show() shown = (shown or 0) + 1 end</script></head><body/></rml>");
  auto ui = createUi(content.mount(), nullptr, false,
                     script::ScriptLimits{.instructionsPerCall = 200'000});
  REQUIRE(ui->loadDocument("loop", idOf("ui/loop/document")));
  REQUIRE(ui->loadDocument("other", idOf("ui/other/document")));
  ui->show("loop");
  // Each click and each show runs a handler; were their budgets fresh, these loops would never end.
  CHECK_FALSE(ui->callScript("loop", "click_forever"));
  CHECK_FALSE(ui->callScript("loop", "show_forever"));
  // The next call has its own budget again.
  CHECK(ui->takeCommands().empty());
  REQUIRE(ui->callScript("loop", "fine"));
  CHECK(commandNames(*ui) == std::vector<std::string>{"fine"});
}

TEST_CASE("documents cannot change what other documents see") {
  Content content;
  content.add("ui/one/document.rml", R"rml(<rml><head><script>
string.upper = function() return "changed" end
math.pi = 3
ui.send = nil
_G.shared = true
local ok = pcall(function() getmetatable("").__index.lower = function() return "changed" end end)
</script></head><body/></rml>)rml");
  content.add("ui/two/document.rml", R"rml(<rml><head><script>
function tell()
	ui.send(string.upper("a") .. ("B"):lower() .. tostring(math.pi > 3.14) .. tostring(shared))
end
</script></head><body/></rml>)rml");
  auto ui = createUi(content.mount());
  REQUIRE(ui->loadDocument("one", idOf("ui/one/document")));
  REQUIRE(ui->loadDocument("two", idOf("ui/two/document")));
  REQUIRE(ui->callScript("two", "tell"));
  CHECK(commandNames(*ui) == std::vector<std::string>{"Abtruenil"});
}

TEST_CASE("scripts cannot flood the app with commands or text") {
  Content content;
  content.add("ui/flood/document.rml", R"rml(<rml><head><script>
function flood() for i = 1, 100000 do ui.send("again") end end
function long_name() ui.send(string.rep("n", 1000)) end
function long_text() ui.send("text", string.rep("t", 1000000)) end
</script></head><body><p id="label">label</p></body></rml>)rml");
  auto ui = createUi(content.mount());
  REQUIRE(ui->loadDocument("flood", idOf("ui/flood/document")));
  CHECK_FALSE(ui->callScript("flood", "flood"));
  const auto queued = ui->takeCommands();
  CHECK(queued.size() == 256);
  CHECK_FALSE(ui->callScript("flood", "long_name"));
  CHECK_FALSE(ui->callScript("flood", "long_text"));
  CHECK(ui->takeCommands().empty());
}

TEST_CASE("running out of script memory inside the API leaks nothing") {
  // Every API function that returns text or an element is called until Lua has no memory left for
  // the result. Lua unwinds with longjmp, so a C++ string alive at that moment would leak, and
  // LeakSanitizer would say so.
  Content content;
  content.add("ui/memory/document.rml", R"rml(<rml><head><script>
local root
local id = "label-with-a-long-id-that-does-not-fit-in-a-short-string"
function on_show(document) root = document end
local function exhaust(make)
	local kept = {}
	local ok = pcall(function() while true do kept[#kept + 1] = make() end end)
	kept = nil
	return ok
end
function fill()
	exhaust(function() return root:get(id):text() end)
	exhaust(function() return root:get(id):attribute("title") end)
	exhaust(function() return root:get(id).id end)
	exhaust(function() return root:get(id) end)
	exhaust(function() return ui.prompt("confirm") end)
	exhaust(function() return ui.setting("long") end)
	exhaust(function() return t(id) end)
	exhaust(function() return t("pause_menu.ui_scale.value", {percent = id}) end)
	exhaust(function() return ui.languages() end)
end
</script></head><body><p id="label-with-a-long-id-that-does-not-fit-in-a-short-string" title="a title long enough to live on the heap, not in the string">a text long enough to live on the heap, not inside the string</p></body></rml>)rml");
  auto ui = createUi(content.mount(), nullptr, false,
                     script::ScriptLimits{.memoryBytes = std::size_t{4} * 1024 * 1024,
                                          .instructionsPerCall = 1'000'000'000,
                                          .callDeadline = std::chrono::seconds{60}});
  REQUIRE(ui->loadDocument("memory", idOf("ui/memory/document")));
  ui->setSetting("long", std::string(200, 's'));
  ui->show("memory");
  CHECK(ui->callScript("memory", "fill"));
}

TEST_CASE("the UI scale is clamped, and the UI follows high-density displays") {
  Content content;
  content.add("ui/test/document.rml", TestDocument);
  content.add("ui/test/logic.lua", TestLogic);
  auto ui = createUi(content.mount());
  REQUIRE(ui->loadDocument("test", idOf("ui/test/document")));
  ui->show("test");
  ui->update();
  CHECK(ui->dpRatio() == doctest::Approx(1.0F));
  const auto small = ui->elementCenter("test", "second");
  REQUIRE(small.has_value());

  // The same window on a display with twice the pixels: everything is twice as far from the corner.
  ui->resize(2560, 1440);
  ui->update();
  CHECK(ui->dpRatio() == doctest::Approx(2.0F));
  const auto large = ui->elementCenter("test", "second");
  REQUIRE(large.has_value());
  CHECK(large.value_or(Ui::Point{}).x ==
        doctest::Approx(small.value_or(Ui::Point{}).x * 2.0F).epsilon(0.01));
  CHECK(large.value_or(Ui::Point{}).y ==
        doctest::Approx(small.value_or(Ui::Point{}).y * 2.0F).epsilon(0.01));

  ui->setScale(1.5F);
  CHECK(ui->dpRatio() == doctest::Approx(3.0F));
  ui->setScale(100.0F);
  CHECK(ui->scale() == doctest::Approx(Ui::MaxScale));
  ui->setScale(0.01F);
  CHECK(ui->scale() == doctest::Approx(Ui::MinScale));
  ui->setScale(std::numeric_limits<float>::quiet_NaN());
  CHECK(ui->scale() == doctest::Approx(Ui::MinScale));
  ui->setScale(std::numeric_limits<float>::infinity());
  CHECK(ui->scale() == doctest::Approx(Ui::MinScale));
  CHECK(std::isfinite(ui->dpRatio()));
}

TEST_CASE("a resting pointer does not take the focus back from the keyboard") {
  Content content;
  content.add("ui/test/document.rml", TestDocument);
  content.add("ui/test/logic.lua", TestLogic);
  auto ui = createUi(content.mount());
  REQUIRE(ui->loadDocument("test", idOf("ui/test/document")));
  input::InputState input{input::defaultActionMap()};
  ui->show("test", true);
  frame(*ui, input);
  const auto first = ui->elementCenter("test", "first");
  REQUIRE(first.has_value());
  const Ui::Point firstAt = first.value_or(Ui::Point{});
  ui->handleEvent(platform::MouseMoved{.x = firstAt.x, .y = firstAt.y});
  CHECK(ui->focusedElementId("test") == "first");
  press(*ui, input, input::keys::Down);
  CHECK(ui->focusedElementId("test") == "second");
  // The hand on the mouse trembles: the pointer stays on the same entry, so the focus stays where
  // it is.
  ui->handleEvent(platform::MouseMoved{.x = firstAt.x + 1.0F, .y = firstAt.y});
  CHECK(ui->focusedElementId("test") == "second");
  // Onto another entry and back: the focus follows.
  const auto second = ui->elementCenter("test", "second");
  REQUIRE(second.has_value());
  ui->handleEvent(platform::MouseMoved{.x = second.value_or(Ui::Point{}).x,
                                       .y = second.value_or(Ui::Point{}).y});
  ui->handleEvent(platform::MouseMoved{.x = firstAt.x, .y = firstAt.y});
  CHECK(ui->focusedElementId("test") == "first");
}

TEST_CASE("losing the window focus releases the buttons held over the UI, without a click") {
  Content content;
  content.add("ui/test/document.rml", TestDocument);
  content.add("ui/test/logic.lua", TestLogic);
  auto ui = createUi(content.mount());
  REQUIRE(ui->loadDocument("test", idOf("ui/test/document")));
  ui->show("test", true);
  ui->update();
  const auto first = ui->elementCenter("test", "first");
  REQUIRE(first.has_value());
  ui->handleEvent(
      platform::MouseMoved{.x = first.value_or(Ui::Point{}).x, .y = first.value_or(Ui::Point{}).y});
  CHECK(ui->handleEvent(
      platform::MouseButtonChanged{.button = platform::MouseButton::Left, .down = true}));
  CHECK_FALSE(ui->handleEvent(platform::FocusLost{}));
  // The release never comes; the next press and release are a whole new click, and the lost one
  // sent nothing.
  CHECK(ui->takeCommands().empty());
  ui->handleEvent(
      platform::MouseMoved{.x = first.value_or(Ui::Point{}).x, .y = first.value_or(Ui::Point{}).y});
  CHECK(ui->handleEvent(
      platform::MouseButtonChanged{.button = platform::MouseButton::Left, .down = true}));
  CHECK(ui->handleEvent(
      platform::MouseButtonChanged{.button = platform::MouseButton::Left, .down = false}));
  const auto commands = ui->takeCommands();
  REQUIRE(commands.size() == 1);
  CHECK(commands[0].value == UiValue{std::string{"first"}});
}

TEST_CASE("hot reload keeps the old document when the edit is broken, and keeps its place") {
  Content content;
  content.add("ui/test/document.rml", TestDocument);
  content.add("ui/test/logic.lua", TestLogic);
  content.add("ui/top/document.rml",
              "<rml><head><link type='text/rcss' href='/ui/common/base.rcss'/></head>"
              "<body><button id='top' autofocus='autofocus'>Top</button></body></rml>");
  FakeClock clock;
  auto ui = createUi(content.mount(), &clock, true);
  REQUIRE(ui->loadDocument("test", idOf("ui/test/document")));
  REQUIRE(ui->loadDocument("top", idOf("ui/top/document")));
  ui->show("test", true);
  ui->show("top", true);
  CHECK(ui->focusedElementId("top") == "top");

  content.add("ui/test/document.rml", "<rml><body><p id='label'>edited</p></body></rml>");
  clock.now = 1.0;
  ui->reloadChanged();
  CHECK(ui->elementText("test", "label") == "edited");
  // The reloaded document stayed under the one shown after it.
  CHECK(ui->focusedElementId("top") == "top");
  CHECK(ui->isVisible("test"));

  // An editor that saves by replacing the file leaves it missing for a moment.
  content.memory.removeFile("packs/tests/ui/test/document.rml");
  clock.now = 2.0;
  ui->reloadChanged();
  CHECK(ui->isVisible("test"));
  CHECK(ui->elementText("test", "label") == "edited");
}

TEST_CASE("a path names an asset only with the extension of the file that provides it") {
  Content content;
  content.add("ui/test/document.rml", "<rml><head><script src='logic.lua'/></head><body/></rml>");
  // The script is not there: a text file with its name is, and is not run as one.
  content.add("ui/test/logic.txt", "ui.send('ran')");
  auto ui = createUi(content.mount());
  REQUIRE(ui->loadDocument("test", idOf("ui/test/document")));
  CHECK(ui->takeCommands().empty());
}

namespace {

// Three samples with the original look, at a size the fixture font draws three times larger (33 px
// over 11-pixel cells): all in the bitmap font, with a character it lacks, and with its glyph that
// has no ASCII equivalent.
constexpr std::string_view OriginalLookDocument = R"rml(<rml>
<head>
	<link type="text/rcss" href="/ui/common/base.rcss"/>
	<style>
		div { position: absolute; left: 0; font-size: 33px; }
	</style>
</head>
<body>
	<div id="bitmap" class="original-look" style="top: 0;">AB</div>
	<div id="missing" class="original-look" style="top: 50px;">AñB</div>
	<div id="symbol" class="original-look" style="top: 100px;">A○B</div>
</body>
</rml>)rml";

// The width of a sample: it sits at the left edge, so its centre is half of it.
float widthOf(const Ui& ui, std::string_view sample) {
  const auto center = ui.elementCenter("look", sample);
  REQUIRE(center.has_value());
  return 2.0F * center.value_or(Ui::Point{}).x;
}

void addFixtureFont(Content& content) {
  auto png = assets::encodePng(fixture::atlas());
  REQUIRE(png.has_value());
  content.add("font/main.png",
              std::string_view{reinterpret_cast<const char*>(png->data()), png->size()});
  content.add("font/main_meta.yaml", fixture::Meta);
  content.add("ui/look/document.rml", OriginalLookDocument);
}

} // namespace

TEST_CASE("original look text draws with the font the player picks, at once") {
  Content content;
  addFixtureFont(content);
  auto ui = createUi(content.mount());
  REQUIRE(ui->loadDocument("look", idOf("ui/look/document")));
  ui->show("look");
  ui->update();
  // The game pack's default is the modern font.
  CHECK(ui->textFont() == TextFont::Modern);
  const float modern = widthOf(*ui, "bitmap");
  CHECK(modern > 0.0F);

  ui->setTextFont(TextFont::Original);
  ui->update();
  CHECK(ui->textFont() == TextFont::Original);
  // The fixture's A and B advance 3 and 4 pixels, three times larger.
  CHECK(widthOf(*ui, "bitmap") == doctest::Approx(21.0F));
  CHECK(widthOf(*ui, "symbol") == doctest::Approx(39.0F));
  // ñ is not in the bitmap font: the modern font draws it between the bitmap's A and B.
  CHECK(widthOf(*ui, "missing") > 21.0F);

  ui->setTextFont(TextFont::Modern);
  ui->update();
  CHECK(widthOf(*ui, "bitmap") == doctest::Approx(modern));
}

TEST_CASE("without the extracted bitmap font, original look text keeps the modern font") {
  Content content;
  content.add("ui/look/document.rml", OriginalLookDocument);
  auto ui = createUi(content.mount());
  REQUIRE(ui->loadDocument("look", idOf("ui/look/document")));
  ui->show("look");
  ui->update();
  const float modern = widthOf(*ui, "bitmap");
  ui->setTextFont(TextFont::Original);
  ui->update();
  CHECK(widthOf(*ui, "bitmap") == doctest::Approx(modern));
}

} // namespace dwe::ui
