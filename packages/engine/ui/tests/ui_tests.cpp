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
#include <span>
#include <string>
#include <string_view>
#include <tuple>
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

  // White stand-ins, of the disc's sizes, for the images dwx extracts that the original skin draws
  // its sprites from: the skin is offered only when the packs have them.
  void addOriginalArt() {
    for (const auto& [path, width, height] :
         {std::tuple{"texture/etcdat/system_w.png", 256U, 128U},
          std::tuple{"texture/etcdat/fi_info.png", 256U, 64U},
          std::tuple{"texture/etcdat/sboy.png", 256U, 256U},
          std::tuple{"texture/etcdat/bi_in.png", 256U, 64U},
          std::tuple{"texture/etcdat/feel_ef.png", 256U, 64U},
          std::tuple{"texture/etcdat/btlcomnd.png", 256U, 64U},
          std::tuple{"texture/etcna/title2.png", 320U, 240U},
          std::tuple{"texture/etcna/title256.png", 320U, 256U},
          std::tuple{"texture/stddat/time.png", 66U, 29U},
          std::tuple{"texture/stddat/stdtim/002.png", 256U, 120U},
          std::tuple{"texture/stddat/stdtim/003.png", 256U, 256U},
          std::tuple{"texture/stddat/taisen1.png", 256U, 256U},
          std::tuple{"texture/stddat/taisen2.png", 128U, 144U},
          std::tuple{"texture/stddat/16taisen.png", 256U, 256U},
          std::tuple{"map/trai00/texture4/001.png", 256U, 256U}}) {
      assets::Image white{.width = width, .height = height, .pixels = {}};
      white.pixels.assign(std::size_t{width} * height * 4, std::byte{0xFF});
      auto png = assets::encodePng(white);
      REQUIRE(png.has_value());
      memory.addFile(std::filesystem::path{"packs/base"} / path, *png);
    }
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
              .scripts = limits,
              .cursor = {}};
  if (clock != nullptr) {
    desc.clock = [clock] { return clock->now; };
  }
  auto ui = Ui::create(desc);
  REQUIRE_MESSAGE(ui.has_value(), ui.error().message);
  return std::move(*ui);
}

// Puts on a skin the way the game does: the switch, then its mirror for the scripts
// (apps/game/src/ui_skin_setting.cpp). The layout options come from the skin itself.
void useSkin(Ui& ui, std::string_view skin) {
  const auto switched = ui.setSkin(skin);
  REQUIRE_MESSAGE(switched.has_value(), switched.error().message);
  ui.setSetting("ui_skin", std::string{skin});
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

TEST_CASE("a runtime image the game has not bound loads as an empty one, without a warning") {
  Content content;
  content.add("ui/test/document.rml", R"rml(<rml>
<head><link type="text/rcss" href="/ui/common/base.rcss"/></head>
<body><img id="portrait" src="/runtime/portrait"/></body>
</rml>)rml");
  auto ui = createUi(content.mount());
  const LogLines log;
  REQUIRE(ui->loadDocument("test", idOf("ui/test/document")));
  ui->show("test");
  ui->update();
  CHECK(log.count("runtime/portrait") == 0);
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
  CHECK_FALSE(
      Ui::create(UiDesc{.files = &files, .clock = {}, .scripts = {}, .cursor = {}}).has_value());
  ui.reset();
  auto again = Ui::create(UiDesc{.files = &files, .clock = {}, .scripts = {}, .cursor = {}});
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
  press(*ui, input, input::keys::LeftCtrl);
  CHECK_FALSE(ui->isVisible("test"));
  CHECK_FALSE(ui->capturesInput());
}

TEST_CASE("a shut menu does not open on the menu action, and opens again once let go") {
  // The walking state's tick (tamer.c:520) opens the game menu only while nothing holds the player.
  Content content;
  content.add("ui/test/document.rml", TestDocument);
  content.add("ui/test/logic.lua", TestLogic);
  auto ui = createUi(content.mount());
  REQUIRE(ui->loadDocument("test", idOf("ui/test/document")));
  input::InputState input{input::defaultActionMap()};

  ui->setMenuDocument("test");
  ui->setMenuShut(true);
  press(*ui, input, input::keys::Tab);
  CHECK_FALSE(ui->isVisible("test"));
  CHECK_FALSE(ui->capturesInput());

  ui->setMenuShut(false);
  press(*ui, input, input::keys::Tab);
  CHECK(ui->isVisible("test"));
  // Shut while open, the menu still closes as it did.
  ui->setMenuShut(true);
  press(*ui, input, input::keys::LeftCtrl);
  CHECK_FALSE(ui->isVisible("test"));
}

TEST_CASE("a menu a script shows modal again takes the navigation back") {
  // game_menu.c tickTriangleMenu: cancelling out of the bag or the partner's page returns to a menu
  // that still answers the pad. A plain ui.show would leave it on screen without any input.
  Content content;
  content.add("ui/menu/document.rml", R"rml(<rml><head><script>
function to_page() ui.hide("menu") ui.show("page", true) end
</script></head><body><button id="entry">Entry</button></body></rml>)rml");
  content.add("ui/page/document.rml", R"rml(<rml><head><script>
function on_action(document, action)
  if action == "cancel" then ui.hide("page") ui.show("menu", true) return true end
  return false
end
</script></head><body><button id="slot">Slot</button></body></rml>)rml");
  auto ui = createUi(content.mount());
  REQUIRE(ui->loadDocument("menu", idOf("ui/menu/document")));
  REQUIRE(ui->loadDocument("page", idOf("ui/page/document")));
  input::InputState input{input::defaultActionMap()};

  ui->setMenuDocument("menu");
  press(*ui, input, input::keys::Tab);
  REQUIRE(ui->isVisible("menu"));
  REQUIRE(ui->callScript("menu", "to_page"));
  CHECK(ui->isVisible("page"));
  CHECK(ui->capturesInput());

  press(*ui, input, input::keys::LeftCtrl);
  CHECK_FALSE(ui->isVisible("page"));
  CHECK(ui->isVisible("menu"));
  CHECK(ui->capturesInput());
  // The menu answers again: cancel closes it.
  press(*ui, input, input::keys::LeftCtrl);
  CHECK_FALSE(ui->isVisible("menu"));
  CHECK_FALSE(ui->capturesInput());
}

TEST_CASE("a document shown with arguments reads them in on_show, and an empty table without") {
  // ui.show(name, modal, args): one menu tells the next where to start without a setting of the
  // game in between (docs/modding/ui-api.md).
  Content content;
  content.add("ui/menu/document.rml", R"rml(<rml><head><script>
function open_page() ui.show("page", true, {entry = "items", step = 2.5, back = true}) end
function open_with_function() ui.show("page", true, {entry = function() end}) end
function open_with_list() ui.show("page", true, {"items"}) end
</script></head><body><button id="entry">Entry</button></body></rml>)rml");
  content.add("ui/page/document.rml", R"rml(<rml><head><script>
function on_show(document, args)
  local seen = {}
  for name, value in pairs(args) do
    seen[#seen + 1] = name .. "=" .. tostring(value)
  end
  table.sort(seen)
  ui.send("shown", table.concat(seen, " "))
end
</script></head><body><button id="slot">Slot</button></body></rml>)rml");
  auto ui = createUi(content.mount());
  REQUIRE(ui->loadDocument("menu", idOf("ui/menu/document")));
  REQUIRE(ui->loadDocument("page", idOf("ui/page/document")));
  const auto shownWith = [&] {
    const auto commands = ui->takeCommands();
    REQUIRE(commands.size() == 1);
    CHECK(commands[0].name == "shown");
    const auto* text = std::get_if<std::string>(&commands[0].value);
    REQUIRE(text != nullptr);
    return *text;
  };

  REQUIRE(ui->callScript("menu", "open_page"));
  CHECK(ui->isVisible("page"));
  CHECK(shownWith() == "back=true entry=items step=2.5");

  ui->hide("page");
  ui->show("page", true);
  CHECK(shownWith() == "");

  // Only named switches, numbers and texts: anything else is the script's error, and nothing shows.
  ui->hide("page");
  CHECK_FALSE(ui->callScript("menu", "open_with_function"));
  CHECK_FALSE(ui->callScript("menu", "open_with_list"));
  CHECK_FALSE(ui->isVisible("page"));
  CHECK(ui->takeCommands().empty());
}

TEST_CASE("a script sounds the original boxes and reads where an element is") {
  // ui.c: createAnimatedUIBox plays playSound(0, 0), renderUIBoxAnim playSound(0, 1); the tech
  // page's help opens with playSound(0, 3), the confirm sound (game_menu.c: tickDigimonMenu).
  Content content;
  content.add("ui/box/document.rml", R"rml(<rml><head><style>
body { width: 100%; height: 100%; }
#box { position: absolute; left: 30dp; top: 60dp; width: 90dp; height: 45dp; }
</style><script>
function sound() ui.cue("box_open") ui.cue("box_close") ui.cue("confirm") end
function wrong() ui.cue("boom") end
local root
function on_show(document) root = document end
function measure()
  local left, top, width, height = root:get("box"):rect()
  ui.send("rect", string.format("%d %d %d %d", left, top, width, height))
end
</script></head><body><div id="box"/></body></rml>)rml");
  auto ui = createUi(content.mount());
  REQUIRE(ui->loadDocument("box", idOf("ui/box/document")));
  ui->show("box");
  REQUIRE(ui->callScript("box", "sound"));
  CHECK(ui->takeCues() == std::vector<UiCue>{UiCue::BoxOpen, UiCue::BoxClose, UiCue::Confirm});
  CHECK_FALSE(ui->callScript("box", "wrong"));
  CHECK(ui->takeCues().empty());
  ui->update();
  REQUIRE(ui->callScript("box", "measure"));
  const auto commands = ui->takeCommands();
  REQUIRE(commands.size() == 1);
  CHECK(commands[0].value == UiValue{std::string{"30 60 90 45"}});
}

namespace {

// The original skin's bag, two items in a bag of 10 slots, after its boxes have grown in.
void openOriginalBag(Ui& ui, input::InputState& input) {
  REQUIRE(ui.loadDocument("partner_menu", idOf("ui/partner_menu/document")));
  useSkin(ui, "original");
  ui.setSetting("partner_menu.start_page", std::string{"items"});
  ui.setSetting("bag.slot.count", 10.0);
  for (int slot = 0; slot < 10; ++slot) {
    ui.setSetting(std::format("bag.slot.{}.empty", slot), slot >= 2);
  }
  for (const std::string_view item : {"0", "1"}) {
    ui.setSetting(std::format("bag.slot.{}.name", item), std::format("item.{}", item));
    ui.setSetting(std::format("bag.slot.{}.description", item), std::format("about.{}", item));
    ui.setSetting(std::format("bag.slot.{}.count", item), 1.0);
  }
  ui.show("partner_menu", true);
  for (int tick = 0; tick < 60; ++tick) {
    frame(ui, input);
  }
}

// Lets an original box finish growing or shrinking (ui/common/original_box.lua: 5 frames at 20 a
// second).
void settle(Ui& ui, input::InputState& input) {
  for (int tick = 0; tick < 30; ++tick) {
    frame(ui, input);
  }
}

} // namespace

TEST_CASE("the original bag describes the slot under the cursor, also while an item moves") {
  Content content;
  content.addOriginalArt();
  auto ui = createUi(content.mount());
  input::InputState input{input::defaultActionMap()};
  openOriginalBag(*ui, input);
  REQUIRE(ui->focusedElementId("partner_menu") == "slot-0");
  CHECK(ui->elementText("partner_menu", "bag-description") == "about.0");

  // The options keep the item's description and its slot keeps the cursor (inventory.c:
  // renderInventoryTop draws it in every state).
  press(*ui, input, input::keys::Return);
  settle(*ui, input);
  REQUIRE(ui->focusedElementId("partner_menu") == "opt-use");
  CHECK(ui->elementHasClass("partner_menu", "slot-0", "picked"));
  CHECK(ui->elementText("partner_menu", "bag-description") == "about.0");

  // Move (inventory.c: renderInventoryBottom in state 4): the slot under the cursor's description,
  // none for an empty slot, never a question.
  press(*ui, input, input::keys::Down);
  press(*ui, input, input::keys::Return);
  settle(*ui, input);
  REQUIRE(ui->focusedElementId("partner_menu") == "slot-0");
  CHECK(ui->elementHasClass("partner_menu", "slot-0", "moving"));
  CHECK_FALSE(ui->elementHasClass("partner_menu", "slot-0", "picked"));
  CHECK(ui->elementText("partner_menu", "bag-description") == "about.0");
  press(*ui, input, input::keys::Right);
  frame(*ui, input);
  CHECK(ui->elementText("partner_menu", "bag-description") == "about.1");
  press(*ui, input, input::keys::Down);
  frame(*ui, input);
  REQUIRE(ui->focusedElementId("partner_menu") == "slot-3");
  CHECK(ui->elementText("partner_menu", "bag-description").empty());
}

TEST_CASE("the original bag's boxes stop at their ends, ask No first and sort under the cursor") {
  // Measured in the original (2026-11-04, 15_item_option_box): the options start on Use and stop at
  // Use and Drop (tickInventoryOptionSelector); the question starts on No and stops at Yes
  // (createDropItemConfirmBox, tickDropItemConfirmBox); after a sort the cursor stayed on slot 3
  // (selectSorting leaves INVENTORY_POINTER alone).
  Content content;
  content.addOriginalArt();
  auto ui = createUi(content.mount());
  input::InputState input{input::defaultActionMap()};
  openOriginalBag(*ui, input);
  ui->setSetting("bag.slot.1.discardable", true);
  press(*ui, input, input::keys::Right);
  REQUIRE(ui->focusedElementId("partner_menu") == "slot-1");
  press(*ui, input, input::keys::Return);
  settle(*ui, input);
  REQUIRE(ui->focusedElementId("partner_menu") == "opt-use");
  press(*ui, input, input::keys::Up);
  CHECK(ui->focusedElementId("partner_menu") == "opt-use");
  for (int step = 0; step < 5; ++step) {
    press(*ui, input, input::keys::Down);
  }
  CHECK(ui->focusedElementId("partner_menu") == "opt-drop");

  press(*ui, input, input::keys::Return);
  settle(*ui, input);
  CHECK(ui->focusedElementId("partner_menu") == "confirm-no");
  press(*ui, input, input::keys::Left);
  press(*ui, input, input::keys::Left);
  CHECK(ui->focusedElementId("partner_menu") == "confirm-yes");
  press(*ui, input, input::keys::Right);
  press(*ui, input, input::keys::Right);
  CHECK(ui->focusedElementId("partner_menu") == "confirm-no");
  // No: back to the options, on Drop.
  press(*ui, input, input::keys::Return);
  settle(*ui, input);
  CHECK(ui->focusedElementId("partner_menu") == "opt-drop");
  CHECK(ui->takeCommands().empty());

  press(*ui, input, input::keys::Up);
  press(*ui, input, input::keys::Return);
  settle(*ui, input);
  REQUIRE(ui->focusedElementId("partner_menu") == "sort-0");
  press(*ui, input, input::keys::Down);
  press(*ui, input, input::keys::Down);
  press(*ui, input, input::keys::Down);
  CHECK(ui->focusedElementId("partner_menu") == "sort-2");
  press(*ui, input, input::keys::Return);
  settle(*ui, input);
  CHECK(ui->focusedElementId("partner_menu") == "slot-1");
  const std::vector<UiCommand> commands = ui->takeCommands();
  REQUIRE(commands.size() == 1);
  CHECK(commands[0].name == "bag_sort");
  CHECK(commands[0].value == UiValue{2.0});
}

TEST_CASE("the bag marks the partner's favourite food while the favorite_food option is on") {
  Content content;
  content.addOriginalArt();
  auto ui = createUi(content.mount());
  input::InputState input{input::defaultActionMap()};
  ui->setSetting("bag.slot.1.favorite", true);
  openOriginalBag(*ui, input);
  // On by default: the game has not published the option yet.
  CHECK_FALSE(ui->elementHasClass("partner_menu", "slot-0", "favorite"));
  CHECK(ui->elementHasClass("partner_menu", "slot-1", "favorite"));

  // The partner evolves into a species that likes the other item: the game publishes the bag again.
  ui->setSetting("bag.slot.0.favorite", true);
  ui->setSetting("bag.slot.1.favorite", false);
  ui->setSetting("bag.revision", 1.0);
  frame(*ui, input);
  CHECK(ui->elementHasClass("partner_menu", "slot-0", "favorite"));
  CHECK_FALSE(ui->elementHasClass("partner_menu", "slot-1", "favorite"));

  // Off, the original's look: no mark.
  ui->setSetting("favorite_food", false);
  frame(*ui, input);
  CHECK_FALSE(ui->elementHasClass("partner_menu", "slot-0", "favorite"));
  ui->setSetting("favorite_food", true);
  frame(*ui, input);
  CHECK(ui->elementHasClass("partner_menu", "slot-0", "favorite"));
}

TEST_CASE("the status page names the partner's favourite food, in both skins, unless the option is "
          "off") {
  Content content;
  content.addOriginalArt();
  auto ui = createUi(content.mount());
  input::InputState input{input::defaultActionMap()};
  REQUIRE(ui->loadDocument("partner_menu", idOf("ui/partner_menu/document")));
  for (const std::string_view skin : {"original", "modern"}) {
    CAPTURE(skin);
    useSkin(*ui, skin);
    ui->setSetting("favorite_food", true);
    ui->setSetting("partner.favorite_food", std::string{"main.item_names.0038"});
    ui->setSetting("partner_menu.start_page", std::string{"status"});
    ui->show("partner_menu", true);
    settle(*ui, input);
    CHECK_FALSE(ui->elementHasClass("partner_menu", "status-food-line", "hidden"));
    CHECK(ui->elementText("partner_menu", "status-food") == "main.item_names.0038");

    // An evolution into a species that likes another food.
    ui->setSetting("partner.favorite_food", std::string{"main.item_names.0041"});
    ui->setSetting("partner.revision", 1.0);
    frame(*ui, input);
    CHECK(ui->elementText("partner_menu", "status-food") == "main.item_names.0041");

    // A species without one.
    ui->setSetting("partner.favorite_food", std::string{});
    ui->setSetting("partner.revision", 2.0);
    frame(*ui, input);
    CHECK(ui->elementText("partner_menu", "status-food") == "None");

    ui->setSetting("favorite_food", false);
    frame(*ui, input);
    CHECK(ui->elementHasClass("partner_menu", "status-food-line", "hidden"));
    ui->hide("partner_menu");
    settle(*ui, input);
  }
}

TEST_CASE("the original egg box grows before its eggs move and shrinks before it closes") {
  // doo2.c: DOO2_openEggBox grows the box out of the screen's middle; DOO2_tickEggInput ignores the
  // eggs until it stands and ends once it has shrunk away (ui.c: 5 frames each way at 20 a second).
  Content content;
  content.addOriginalArt();
  auto ui = createUi(content.mount());
  REQUIRE(ui->loadDocument("egg_box", idOf("ui/egg_box/document")));
  useSkin(*ui, "original");
  ui->setSetting("egg_box_eggs", 4.0);
  input::InputState input{input::defaultActionMap()};
  ui->show("egg_box", true);
  frame(*ui, input);
  CHECK(ui->takeCues().empty());
  CHECK_FALSE(ui->elementHasClass("egg_box", "box-outline", "hidden"));
  CHECK(ui->elementHasClass("egg_box", "panel", "box-animating"));

  // Growing: right and confirm do nothing, and sound nothing (the box's sounds are the game's).
  press(*ui, input, input::keys::Right);
  press(*ui, input, input::keys::Return);
  CHECK(ui->takeCommands().empty());
  CHECK(ui->takeCues().empty());
  settle(*ui, input);
  CHECK(ui->elementHasClass("egg_box", "box-outline", "hidden"));
  CHECK_FALSE(ui->elementHasClass("egg_box", "panel", "box-animating"));

  // Standing: the egg moves and is taken; the box shrinks and only then says it is gone.
  // Up and down move nothing (DOO2_tickEggInput reads left and right only).
  press(*ui, input, input::keys::Down);
  CHECK(ui->takeCommands().empty());
  press(*ui, input, input::keys::Right);
  CHECK(commandNames(*ui) == std::vector<std::string>{"egg_box_move"});
  CHECK(ui->takeCues().empty());
  press(*ui, input, input::keys::Return);
  auto commands = ui->takeCommands();
  REQUIRE(commands.size() == 1);
  CHECK(commands[0].name == "egg_box_choose");
  CHECK(commands[0].value == UiValue{1.0});
  // The game sounds the egg taken; the box sounds its closing (renderUIBoxAnim).
  CHECK(ui->takeCues() == std::vector<UiCue>{UiCue::BoxClose});
  press(*ui, input, input::keys::Return);
  CHECK(ui->takeCommands().empty());
  settle(*ui, input);
  CHECK(commandNames(*ui) == std::vector<std::string>{"egg_box_closed"});
}

TEST_CASE("the modern egg box takes the egg and closes at once") {
  Content content;
  auto ui = createUi(content.mount());
  REQUIRE(ui->loadDocument("egg_box", idOf("ui/egg_box/document")));
  ui->setSetting("ui_skin", std::string{"modern"});
  ui->setSetting("egg_box_eggs", 4.0);
  input::InputState input{input::defaultActionMap()};
  ui->show("egg_box", true);
  frame(*ui, input);
  press(*ui, input, input::keys::Return);
  CHECK(commandNames(*ui) == std::vector<std::string>{"egg_box_choose", "egg_box_closed"});
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
  CHECK(ui->elementText("pause_menu", "prompt-confirm").contains(">Space<"));
  CHECK(ui->elementHasClass("pause_menu", "prompt-confirm", "glyph-key"));
  CHECK(ui->elementText("pause_menu", "prompt-cancel").contains(">Ctrl<"));

  press(*ui, input, input::keys::Down);
  CHECK(ui->focusedElementId("pause_menu") == "open-minimap");
  press(*ui, input, input::keys::Return);
  CHECK(ui->elementText("pause_menu", "title") == "Minimap");
  CHECK(ui->focusedElementId("pause_menu") == "minimap-mode");
  // The old auto reads as on.
  CHECK(ui->elementText("pause_menu", "minimap-mode-value") == "Yes");

  press(*ui, input, input::keys::Return);
  auto commands = ui->takeCommands();
  REQUIRE(commands.size() == 1);
  CHECK(commands[0].name == "minimap");
  CHECK(commands[0].value == UiValue{std::string{"off"}});
  // The label follows what the game reports, not what was asked.
  ui->setSetting("minimap", std::string{"off"});
  CHECK(ui->elementText("pause_menu", "minimap-mode-value") == "No");

  // Right on an option changes it, as confirm does.
  press(*ui, input, input::keys::Down);
  CHECK(ui->focusedElementId("pause_menu") == "minimap-size");
  press(*ui, input, input::keys::Right);
  commands = ui->takeCommands();
  REQUIRE(commands.size() == 1);
  CHECK(commands[0].value == UiValue{std::string{"large"}});

  // Cancel goes back to the entry that opened the page, then closes the menu.
  press(*ui, input, input::keys::LeftCtrl);
  CHECK(ui->elementText("pause_menu", "title") == "Pause");
  CHECK(ui->focusedElementId("pause_menu") == "open-minimap");
  press(*ui, input, input::keys::LeftCtrl);
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

  // Triangle opens the game menu, as on the original's pad.
  press(*ui, input, GamepadButton::North);
  REQUIRE(ui->isVisible("pause_menu"));
  // The original pad's family: its shapes, the cross and the circle.
  CHECK(ui->elementHasClass("pause_menu", "prompt-confirm", "shape-cross"));
  CHECK_FALSE(ui->elementHasClass("pause_menu", "prompt-confirm", "glyph-key"));
  CHECK(ui->elementText("pause_menu", "prompt-confirm").contains("glyph-art"));
  CHECK(ui->elementHasClass("pause_menu", "prompt-cancel", "shape-circle"));

  press(*ui, input, GamepadButton::DpadDown);
  press(*ui, input, GamepadButton::DpadDown);
  CHECK(ui->focusedElementId("pause_menu") == "open-options");
  press(*ui, input, GamepadButton::South);
  CHECK(ui->focusedElementId("pause_menu") == "walk-control");
  // The shoulders switch the options' tabs: the Interface tab is the last, before the first.
  press(*ui, input, GamepadButton::LeftShoulder);
  CHECK(ui->elementHasClass("pause_menu", "tab-interface", "selected"));
  CHECK(ui->focusedElementId("pause_menu") == "ui-skin");

  // The stick navigates too.
  input.handle(
      platform::GamepadAxisMoved{.id = Pad, .axis = platform::GamepadAxis::LeftY, .value = 1.0F});
  frame(*ui, input);
  input.handle(
      platform::GamepadAxisMoved{.id = Pad, .axis = platform::GamepadAxis::LeftY, .value = 0.0F});
  frame(*ui, input);
  CHECK(ui->focusedElementId("pause_menu") == "ui-scale");
  // The options' keys name the pad's: the shoulders for the tabs.
  CHECK(ui->elementHasClass("pause_menu", "hint-tab-next", "glyph-button"));
  press(*ui, input, GamepadButton::DpadRight);
  auto commands = ui->takeCommands();
  REQUIRE(commands.size() == 1);
  CHECK(commands[0].name == "ui_scale");
  CHECK(commands[0].value == UiValue{1.15});

  // Touching the keyboard switches the prompts back.
  input.handle(platform::KeyChanged{.key = input::keys::Q, .down = true, .repeat = false});
  frame(*ui, input);
  CHECK(ui->elementText("pause_menu", "prompt-confirm").contains(">Space<"));
  CHECK(ui->elementHasClass("pause_menu", "prompt-confirm", "glyph-key"));
  CHECK_FALSE(ui->elementHasClass("pause_menu", "prompt-confirm", "shape-cross"));

  press(*ui, input, GamepadButton::East);
  press(*ui, input, GamepadButton::East);
  CHECK_FALSE(ui->isVisible("pause_menu"));
}

TEST_CASE("the pause menu works with the mouse alone") {
  Content content;
  FakeClock clock;
  auto ui = createUi(content.mount(), &clock);
  REQUIRE(ui->loadDocument("pause_menu", idOf("ui/pause_menu/document")));
  ui->setSetting("fullscreen", false);
  input::InputState input{input::defaultActionMap()};

  // The mouse opens the menu from a button the game shows; here the app opens it.
  ui->show("pause_menu", true);
  ui->update();
  CHECK(click(*ui, input, "pause_menu", "open-options"));
  CHECK(ui->elementText("pause_menu", "title") == "Options");
  // A click on a tab shows it.
  CHECK(click(*ui, input, "pause_menu", "tab-graphics"));
  CHECK(ui->elementHasClass("pause_menu", "tab-graphics", "selected"));
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

  // The wheel scrolls a list longer than its box; Back stays under the list either way.
  CHECK(ui->handleEvent(platform::MouseWheelMoved{.x = 0.0F, .y = -20.0F}));
  for (int i = 0; i < 60; ++i) {
    clock.now += Tick;
    frame(*ui, input);
  }
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

namespace {

// Lets the original boxes that follow each other (the bag, then its description; the options, then
// the description and the bag) finish growing or shrinking: up to three of 15 ticks.
void settleBoxes(Ui& ui, input::InputState& input) {
  for (int tick = 0; tick < 60; ++tick) {
    frame(ui, input);
  }
}

// The partner menu's items page with two items: the first one usable where the bag is open, the
// second one not.
void openBag(Ui& ui, input::InputState& input, std::string_view context) {
  useSkin(ui, "original");
  ui.setSetting("bag.slot.count", 2.0);
  for (const int slot : {0, 1}) {
    const std::string prefix = std::format("bag.slot.{}.", slot);
    ui.setSetting(prefix + "empty", false);
    ui.setSetting(prefix + "name", std::string{slot == 0 ? "items.sm_recovery" : "items.meat"});
    ui.setSetting(prefix + "count", 1.0);
    ui.setSetting(prefix + "usable", slot == 0);
  }
  ui.setSetting("partner_menu.start_page", std::string{"items"});
  ui.setSetting("partner_menu.context", std::string{context});
  ui.show("partner_menu", true);
  settleBoxes(ui, input);
}

} // namespace

TEST_CASE("the battle opens the field's bag: its Use throws once the bag has closed") {
  Content content;
  content.addOriginalArt();
  auto ui = createUi(content.mount());
  REQUIRE(ui->loadDocument("partner_menu", idOf("ui/partner_menu/document")));
  REQUIRE(ui->loadDocument("pause_menu", idOf("ui/pause_menu/document")));
  input::InputState input{input::defaultActionMap()};
  openBag(*ui, input, "battle");
  REQUIRE(ui->isVisible("partner_menu"));
  CHECK(ui->elementHasClass("partner_menu", "scrim", "item-screen"));
  CHECK(ui->elementHasClass("partner_menu", "scrim", "battle-bag"));
  CHECK(ui->focusedElementId("partner_menu") == "slot-0");

  // inventory.c: selectOptionMenu: an item the battle cannot use opens the options, and Use does
  // nothing.
  press(*ui, input, input::keys::Right);
  press(*ui, input, input::keys::Return);
  settleBoxes(*ui, input);
  CHECK(ui->focusedElementId("partner_menu") == "opt-use");
  CHECK(ui->elementHasClass("partner_menu", "opt-use", "unavailable"));
  press(*ui, input, input::keys::Return);
  CHECK(ui->takeCommands().empty());
  press(*ui, input, input::keys::LeftCtrl);
  CHECK(ui->focusedElementId("partner_menu") == "slot-1");

  // A usable one: Use closes the boxes and only then asks for the throw (tickInventoryUI's case 5:
  // startThrowingItem once UI_BOX_DATA[0] is closed).
  press(*ui, input, input::keys::Left);
  press(*ui, input, input::keys::Return);
  settleBoxes(*ui, input);
  press(*ui, input, input::keys::Return);
  std::vector<UiCommand> commands = ui->takeCommands();
  CHECK(commands.empty());
  CHECK(ui->isVisible("partner_menu"));
  // The press's own two frames, then one a tick until the throw is asked for. Measured in the
  // original (11_battle_agumon): Use taken between vblanks 28400 and 28404, startThrowingItem at
  // 28445; three boxes of five frames at 20 a second.
  int ticks = 2;
  while (commands.empty() && ticks < 120) {
    frame(*ui, input);
    ++ticks;
    commands = ui->takeCommands();
  }
  MESSAGE("ticks from Use to the throw: ", ticks);
  CHECK(ticks >= 41);
  CHECK(ticks <= 46);
  REQUIRE(commands.size() == 1);
  CHECK(commands[0].name == "bag_use");
  CHECK(commands[0].value == UiValue{0.0});
  CHECK_FALSE(ui->isVisible("partner_menu"));
  // Back to the fight, not to the field's pause menu.
  CHECK_FALSE(ui->isVisible("pause_menu"));

  // Cancel closes it into the fight too.
  openBag(*ui, input, "battle");
  press(*ui, input, input::keys::LeftCtrl);
  settleBoxes(*ui, input);
  CHECK_FALSE(ui->isVisible("partner_menu"));
  CHECK_FALSE(ui->isVisible("pause_menu"));
  CHECK(ui->takeCommands().empty());
}

TEST_CASE("the field's bag uses an item at once and goes back to the pause menu") {
  Content content;
  content.addOriginalArt();
  auto ui = createUi(content.mount());
  REQUIRE(ui->loadDocument("partner_menu", idOf("ui/partner_menu/document")));
  REQUIRE(ui->loadDocument("pause_menu", idOf("ui/pause_menu/document")));
  input::InputState input{input::defaultActionMap()};
  openBag(*ui, input, "");
  CHECK_FALSE(ui->elementHasClass("partner_menu", "scrim", "battle-bag"));
  press(*ui, input, input::keys::Return);
  settleBoxes(*ui, input);
  press(*ui, input, input::keys::Return);
  CHECK(commandNames(*ui) == std::vector<std::string>{"bag_use"});
  CHECK(ui->isVisible("partner_menu"));
  press(*ui, input, input::keys::LeftCtrl);
  settleBoxes(*ui, input);
  CHECK_FALSE(ui->isVisible("partner_menu"));
  CHECK(ui->isVisible("pause_menu"));
}

TEST_CASE("the partner menu's pages stay shown however often the tabs switch and it reopens") {
  // The menu closed from the Tech page used to leave that page shrunk (box-closed) for good: the
  // next menu opened on Status, and the Tech page came up empty under its tabs. Original skin
  // first, so the modern one starts from a page the original look closed.
  Content content;
  content.addOriginalArt();
  auto ui = createUi(content.mount());
  REQUIRE(ui->loadDocument("partner_menu", idOf("ui/partner_menu/document")));
  REQUIRE(ui->loadDocument("pause_menu", idOf("ui/pause_menu/document")));
  input::InputState input{input::defaultActionMap()};
  const auto pageShown = [&](std::string_view page) {
    const std::string id = std::format("page-{}", page);
    return ui->elementShown("partner_menu", id) &&
           !ui->elementHasClass("partner_menu", id, "box-closed") &&
           !ui->elementHasClass("partner_menu", id, "box-animating");
  };
  for (const std::string_view skin : {"original", "modern"}) {
    CAPTURE(skin);
    useSkin(*ui, skin);
    ui->setSetting("partner_menu.start_page", std::string{"status"});
    for (int round = 0; round < 3; ++round) {
      CAPTURE(round);
      ui->show("partner_menu", true);
      settleBoxes(*ui, input);
      REQUIRE(pageShown("status"));
      for (int step = 0; step < 6; ++step) {
        press(*ui, input, input::keys::Right);
        CHECK(pageShown("tech"));
        CHECK_FALSE(pageShown("status"));
        press(*ui, input, input::keys::Left);
        CHECK(pageShown("status"));
      }
      // The mouse on the tabs too: the Status page's portrait reaches up over them and must not
      // take the click.
      CHECK(click(*ui, input, "partner_menu", "tab-tech"));
      frame(*ui, input);
      CHECK(pageShown("tech"));
      CHECK(click(*ui, input, "partner_menu", "tab-status"));
      frame(*ui, input);
      CHECK(pageShown("status"));
      // Leave from the Tech page.
      press(*ui, input, input::keys::Right);
      REQUIRE(pageShown("tech"));
      press(*ui, input, input::keys::LeftCtrl);
      settleBoxes(*ui, input);
      REQUIRE_FALSE(ui->isVisible("partner_menu"));
      ui->hide("pause_menu");
    }
  }
}

TEST_CASE("the HUD's menu button asks the game to open the menu and names the key that does it") {
  Content content;
  auto ui = createUi(content.mount());
  input::InputState input{input::defaultActionMap()};
  REQUIRE(ui->loadDocument("hud", idOf("ui/hud/document")));
  // The button shows with the rest of the HUD (hud.shown).
  ui->setSetting("hud.shown", true);
  ui->show("hud");
  ui->update();
  CHECK(ui->elementText("hud", "prompt-menu").contains(">Tab<"));
  CHECK(click(*ui, input, "hud", "menu-button"));
  CHECK(commandNames(*ui) == std::vector<std::string>{"open_menu"});
  // A gamepad names its own button.
  input.handle(platform::GamepadConnected{
      .id = Pad, .family = platform::GamepadFamily::Xbox, .name = "pad"});
  press(*ui, input, GamepadButton::South);
  CHECK(ui->elementText("hud", "prompt-menu") != "Tab");
}

namespace {

// One field bar of the hud.* contract (docs/architecture/ui.md#estado-del-campo), with its mood as
// the game publishes it (packages/screens: meterMoodOf): low below the middle of its range.
void setFieldMeter(Ui& ui, int index, double value, double minimum, double maximum) {
  const std::string base = "hud.meter." + std::to_string(index) + ".";
  ui.setSetting(base + "value", value);
  ui.setSetting(base + "min", minimum);
  ui.setSetting(base + "max", maximum);
  ui.setSetting(base + "two_tone", true);
  ui.setSetting(base + "mood", std::string{value < (minimum + maximum) / 2 ? "low" : "high"});
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

TEST_CASE("the field clock shows the night and the part of the day the game publishes") {
  Content content;
  auto ui = createUi(content.mount());
  REQUIRE(ui->loadDocument("hud", idOf("ui/hud/document")));
  ui->show("hud");
  // The game decides them (packages/screens: isNightHour, dayPhaseOf); the HUD only draws them.
  for (const auto& [hour, phase] :
       {std::pair{0.0, "night"}, std::pair{6.0, "morning"}, std::pair{9.0, "day"},
        std::pair{15.0, "late"}, std::pair{17.0, "night"}}) {
    CAPTURE(hour);
    const bool night = std::string_view{phase} == "night";
    ui->setSetting("hud.clock.hour", hour);
    ui->setSetting("hud.clock.night", night);
    ui->setSetting("hud.clock.phase", std::string{phase});
    bumpFieldRevision(*ui);
    CHECK(ui->elementHasClass("hud", "field-clock", "night") == night);
    CHECK(ui->elementHasClass("hud", "minimap-phase", std::string{"phase-"} + phase));
  }
}

TEST_CASE("the field clock tells the game time in digits as HH:MM while the option is on") {
  Content content;
  auto ui = createUi(content.mount());
  REQUIRE(ui->loadDocument("hud", idOf("ui/hud/document")));
  ui->show("hud");
  ui->update();
  // On before the game says anything (digital_time, on by default).
  CHECK(ui->elementHasClass("hud", "field-clock", "digital"));
  for (const auto& [hour, minute, text] :
       {std::tuple{0.0, 0.0, "00:00"}, std::tuple{8.0, 54.0, "08:54"},
        std::tuple{13.0, 5.0, "13:05"}, std::tuple{23.0, 59.0, "23:59"}}) {
    CAPTURE(text);
    ui->setSetting("hud.clock.hour", hour);
    ui->setSetting("hud.clock.minute", minute);
    bumpFieldRevision(*ui);
    CHECK(ui->elementText("hud", "field-time") == text);
  }
  ui->setSetting("digital_time", false);
  ui->update();
  CHECK_FALSE(ui->elementHasClass("hud", "field-clock", "digital"));
  // The time still follows the clock, ready for the option to come back on.
  ui->setSetting("hud.clock.minute", 58.0);
  bumpFieldRevision(*ui);
  CHECK(ui->elementText("hud", "field-time") == "23:58");
  ui->setSetting("digital_time", true);
  ui->update();
  CHECK(ui->elementHasClass("hud", "field-clock", "digital"));
}

namespace {

void publishShopJob(Ui& ui, bool shown, double tally, double customer) {
  ui.setSetting("shop_job.shown", shown);
  ui.setSetting("shop_job.tally", tally);
  ui.setSetting("shop_job.goal", 3072.0);
  ui.setSetting("shop_job.better_pay", 1792.0);
  ui.setSetting("shop_job.customer", customer);
  ui.setSetting("shop_job.customers", 20.0);
  const UiValue current = ui.setting("shop_job.revision");
  const double* count = std::get_if<double>(&current);
  ui.setSetting("shop_job.revision", (count == nullptr ? 0.0 : *count) + 1.0);
  ui.update();
}

} // namespace

TEST_CASE("the shop job's bar shows the tally against the join and the customers served") {
  for (const std::string_view skin : {"original", "modern"}) {
    CAPTURE(skin);
    Content content;
    auto ui = createUi(content.mount());
    REQUIRE(ui->loadDocument("hud", idOf("ui/hud/document")));
    ui->setSetting("ui_skin", std::string{skin});
    ui->show("hud");
    ui->update();
    // Hidden until the game says the job runs.
    CHECK_FALSE(ui->elementHasClass("hud", "shop-job", "shown"));
    publishShopJob(*ui, true, 960.0, 7.0);
    CHECK(ui->elementHasClass("hud", "shop-job", "shown"));
    CHECK(ui->elementText("hud", "shop-job-customers") == "Customers 7/20");
    CHECK(ui->elementText("hud", "shop-job-tally") == "960 / 3072 bits");
    CHECK(ui->styleProperty("hud", "shop-job-fill", "width") == "31.25%");
    CHECK(ui->styleProperty("hud", "shop-job-mark-pay", "left") == "58.33%");
    CHECK_FALSE(ui->elementHasClass("hud", "shop-job", "better"));
    // The next mark to reach, on the right.
    CHECK(ui->elementText("hud", "shop-job-next") == "Better pay from 1792");
    publishShopJob(*ui, true, 2000.0, 15.0);
    CHECK(ui->elementHasClass("hud", "shop-job", "better"));
    CHECK_FALSE(ui->elementHasClass("hud", "shop-job", "joins"));
    CHECK(ui->elementText("hud", "shop-job-next") == "Joins the city from 3072");
    // Past the goal the fill stops at the end.
    publishShopJob(*ui, true, 3200.0, 20.0);
    CHECK(ui->elementHasClass("hud", "shop-job", "joins"));
    CHECK(ui->styleProperty("hud", "shop-job-fill", "width") == "100%");
    CHECK(ui->elementText("hud", "shop-job-next") == "Joins the city!");
    publishShopJob(*ui, false, 3200.0, 20.0);
    CHECK_FALSE(ui->elementHasClass("hud", "shop-job", "shown"));
  }
}

namespace {

void setFieldNeeds(Ui& ui, bool hunger, bool sleep, bool toilet, bool tired = false,
                   bool injured = false, bool sick = false) {
  ui.setSetting("hud.need.0.active", hunger);
  ui.setSetting("hud.need.1.active", sleep);
  ui.setSetting("hud.need.2.active", toilet);
  ui.setSetting("hud.need.3.active", tired);
  ui.setSetting("hud.need.4.active", injured);
  ui.setSetting("hud.need.5.active", sick);
  bumpFieldRevision(ui);
}

// The urgency the game ranks the needs with (hud.need.<i>.urgency), in the hud.need list's order:
// hunger, sleep, toilet, tiredness, injury, sickness. Published with the next revision.
void setFieldUrgency(Ui& ui, const std::array<int, 6>& ranks) {
  for (std::size_t index = 0; index < ranks.size(); ++index) {
    ui.setSetting(std::format("hud.need.{}.urgency", index), static_cast<double>(ranks.at(index)));
  }
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
  for (const std::string_view need : {"hunger", "sleep", "toilet", "tired", "injured", "sick"}) {
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
  // The original's bubble: the stacked one (need_bubble_stack) off.
  ui->setSetting("need_bubble_stack", false);
  placePartner(*ui, true);
  setFieldNeeds(*ui, false, false, false);
  CHECK_FALSE(ui->elementHasClass("hud", "field-partner-bubble", "shown"));
  CHECK_FALSE(ui->elementHasClass("hud", "field-partner-bubble", "stack-2"));
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

namespace {

// The needs on the stacked bubble's icons, left to right; empty icons left out.
std::vector<std::string> stackedNeeds(const Ui& ui) {
  std::vector<std::string> needs;
  for (const std::string_view icon :
       {"field-partner-bubble-icon-0", "field-partner-bubble-icon-1", "field-partner-bubble-icon-2",
        "field-partner-bubble-icon-3", "field-partner-bubble-icon-4"}) {
    for (const std::string_view need : {"hunger", "sleep", "toilet", "tired", "injured", "sick"}) {
      if (ui.elementHasClass("hud", icon, need)) {
        needs.emplace_back(need);
      }
    }
  }
  return needs;
}

} // namespace

TEST_CASE(
    "the need bubble shows several needs side by side, most urgent first, unless turned off") {
  // An engine improvement (need_bubble_stack, on unless turned off): the original shows one need at
  // a time, the two most urgent taking turns (partner_impl.c: handleConditionBubble).
  Content content;
  auto ui = createUi(content.mount());
  input::InputState input{input::defaultActionMap()};
  REQUIRE(ui->loadDocument("hud", idOf("ui/hud/document")));
  ui->show("hud");
  placePartner(*ui, true);

  // One need is the original's bubble, its need on the first icon.
  setFieldNeeds(*ui, true, false, false);
  CHECK(bubbleNeed(*ui) == "hunger");
  CHECK_FALSE(ui->elementHasClass("hud", "field-partner-bubble", "stack-2"));
  CHECK(stackedNeeds(*ui) == std::vector<std::string>{"hunger"});

  // Two: one balloon with both, in the original's priority (tiredness before hunger).
  setFieldNeeds(*ui, true, false, false, true);
  CHECK(ui->elementHasClass("hud", "field-partner-bubble", "shown"));
  CHECK(ui->elementHasClass("hud", "field-partner-bubble", "stack-2"));
  CHECK(bubbleNeed(*ui).empty());
  CHECK(stackedNeeds(*ui) == std::vector<std::string>{"tired", "hunger"});
  // They stay together: no turns.
  ui->tick(input, 2.6);
  CHECK(stackedNeeds(*ui) == std::vector<std::string>{"tired", "hunger"});
  setFieldNeeds(*ui, true, false, true);
  CHECK(stackedNeeds(*ui) == std::vector<std::string>{"hunger", "toilet"});

  // Four, all of them; with no urgency published, in the original's priority.
  setFieldNeeds(*ui, true, true, true, true);
  CHECK(ui->elementHasClass("hud", "field-partner-bubble", "stack-4"));
  CHECK_FALSE(ui->elementHasClass("hud", "field-partner-bubble", "stack-3"));
  CHECK(stackedNeeds(*ui) == std::vector<std::string>{"sleep", "tired", "hunger", "toilet"});

  // Six: the five most urgent in one balloon, by the urgency the game ranks them with
  // (hud.need.<i>.urgency, 1 the soonest to do harm); the sixth waits.
  setFieldUrgency(*ui, {3, 5, 1, 6, 4, 2});
  setFieldNeeds(*ui, true, true, true, true, true, true);
  CHECK(ui->elementHasClass("hud", "field-partner-bubble", "stack-5"));
  CHECK_FALSE(ui->elementHasClass("hud", "field-partner-bubble", "stack-4"));
  CHECK(bubbleNeed(*ui).empty());
  CHECK(stackedNeeds(*ui) ==
        std::vector<std::string>{"toilet", "sick", "hunger", "injured", "sleep"});
  // A new order redraws it, with no turns in between.
  setFieldUrgency(*ui, {1, 2, 3, 4, 5, 6});
  bumpFieldRevision(*ui);
  CHECK(stackedNeeds(*ui) ==
        std::vector<std::string>{"hunger", "sleep", "toilet", "tired", "injured"});
  ui->tick(input, 2.6);
  CHECK(stackedNeeds(*ui) ==
        std::vector<std::string>{"hunger", "sleep", "toilet", "tired", "injured"});
  // Needs without a rank go after the ranked ones, in the original's priority among them.
  setFieldUrgency(*ui, {0, 0, 1, 0, 0, 0});
  setFieldNeeds(*ui, true, true, true, true);
  CHECK(stackedNeeds(*ui) == std::vector<std::string>{"toilet", "sleep", "tired", "hunger"});

  // Turned off: only the original's one, whichever of its turns it is on, by its own priority.
  setFieldUrgency(*ui, {1, 2, 3, 4, 5, 6});
  setFieldNeeds(*ui, true, true, true, true, true, true);
  ui->setSetting("need_bubble_stack", false);
  ui->update();
  for (const std::string_view stack : {"stack-2", "stack-3", "stack-4", "stack-5"}) {
    CHECK_FALSE(ui->elementHasClass("hud", "field-partner-bubble", stack));
  }
  CHECK_FALSE(bubbleNeed(*ui).empty());
  CHECK(stackedNeeds(*ui) == std::vector<std::string>{bubbleNeed(*ui)});
  setFieldNeeds(*ui, false, false, false);
  setFieldNeeds(*ui, true, true, true, true, true, true);
  CHECK(bubbleNeed(*ui) == "sleep");
  ui->tick(input, 2.6);
  CHECK(bubbleNeed(*ui) == "tired");
}

TEST_CASE("the need bubble plays the original's frames: growth, the need's animation, burst, gap") {
  // bubble.c: tickConditionBubble counts the bubble's frames at 20 a second, a new bubble every 60
  // (handleConditionBubble: CONDITION_BUBBLE_TIMER) or at 50 for the other need's; the skin draws
  // each frame by the class "frame-<n>".
  Content content;
  auto ui = createUi(content.mount());
  input::InputState input{input::defaultActionMap()};
  REQUIRE(ui->loadDocument("hud", idOf("ui/hud/document")));
  ui->show("hud");
  ui->setSetting("need_bubble_stack", false);
  placePartner(*ui, true);
  const auto onFrame = [&ui](int frame) {
    return ui->elementHasClass("hud", "field-partner-bubble", "frame-" + std::to_string(frame));
  };
  setFieldNeeds(*ui, false, true, false);
  CHECK(onFrame(1));
  ui->tick(input, 0.56);
  CHECK(onFrame(12));
  CHECK_FALSE(onFrame(1));
  // One need alone: past the burst (51) to the gap, then a new bubble on frame 1 at 60.
  ui->tick(input, 2.0);
  CHECK(onFrame(52));
  ui->tick(input, 0.5);
  CHECK(onFrame(2));
  // With another need, its bubble takes over at 50 frames, from frame 1.
  setFieldNeeds(*ui, true, true, false);
  CHECK(bubbleNeed(*ui) == "sleep");
  ui->tick(input, 2.5);
  CHECK(bubbleNeed(*ui) == "hunger");
  CHECK(onFrame(1));
  // The frames stop while the bubble is not drawn.
  placePartner(*ui, false);
  ui->tick(input, 1.0);
  placePartner(*ui, true);
  CHECK(onFrame(1));
}

namespace {

// The evolving bubble's frame the game publishes (hud.partner.evolving), with a new revision.
void setEvolvingFrame(Ui& ui, int frame) {
  ui.setSetting("hud.partner.evolving", static_cast<double>(frame));
  placePartner(ui, true);
}

} // namespace

TEST_CASE("the evolving bubble shows over the partner on the game's frames, over any need") {
  // main/evl.c: tickEvoSequenceLoading makes bubble 7 (bubble.c: CONDITION_ICON_EVOLVING) as the
  // partner starts to evolve; the game steps it and publishes its frame.
  for (const bool stacked : {false, true}) {
    CAPTURE(stacked);
    Content content;
    auto ui = createUi(content.mount());
    input::InputState input{input::defaultActionMap()};
    REQUIRE(ui->loadDocument("hud", idOf("ui/hud/document")));
    ui->show("hud");
    ui->setSetting("need_bubble_stack", stacked);
    placePartner(*ui, true);
    // A need left from before the evolution: the evolving bubble takes the balloon.
    setFieldNeeds(*ui, true, true, false);
    setEvolvingFrame(*ui, 1);
    CHECK(ui->elementHasClass("hud", "field-partner-bubble", "shown"));
    CHECK(ui->elementHasClass("hud", "field-partner-bubble", "evolving"));
    CHECK(ui->elementHasClass("hud", "field-partner-bubble", "frame-1"));
    CHECK(bubbleNeed(*ui).empty());
    CHECK_FALSE(ui->elementHasClass("hud", "field-partner-bubble", "stack-2"));
    CHECK(ui->elementHasClass("hud", "field-partner-bubble-icon-0", "evolving"));
    CHECK_FALSE(ui->elementHasClass("hud", "field-partner-bubble-icon-0", "sleep"));
    CHECK_FALSE(ui->elementHasClass("hud", "field-partner-bubble-icon-1", "hunger"));
    // The game's frames, not the script's count.
    ui->tick(input, 1.0);
    CHECK(ui->elementHasClass("hud", "field-partner-bubble", "frame-1"));
    setEvolvingFrame(*ui, 30);
    CHECK(ui->elementHasClass("hud", "field-partner-bubble", "frame-30"));
    CHECK_FALSE(ui->elementHasClass("hud", "field-partner-bubble", "frame-1"));
    setEvolvingFrame(*ui, 51);
    CHECK(ui->elementHasClass("hud", "field-partner-bubble", "frame-51"));
    // Gone after the burst; the new form has no need.
    setFieldNeeds(*ui, false, false, false);
    setEvolvingFrame(*ui, 0);
    CHECK_FALSE(ui->elementHasClass("hud", "field-partner-bubble", "shown"));
    CHECK_FALSE(ui->elementHasClass("hud", "field-partner-bubble", "evolving"));
    CHECK_FALSE(ui->elementHasClass("hud", "field-partner-bubble-icon-0", "evolving"));
    // A need after it is the need bubble again.
    setFieldNeeds(*ui, true, false, false);
    CHECK(bubbleNeed(*ui) == "hunger");
  }
}

TEST_CASE("the HUD shows the minimap unless it is off or the game does not allow it now") {
  Content content;
  auto ui = createUi(content.mount());
  REQUIRE(ui->loadDocument("hud", idOf("ui/hud/document")));
  ui->show("hud");
  ui->update();
  // Unset, and the old auto, show it with either skin.
  CHECK_FALSE(ui->elementHasClass("hud", "minimap", "hidden"));
  ui->setSetting("minimap", std::string{"off"});
  CHECK(ui->elementHasClass("hud", "minimap", "hidden"));
  ui->setSetting("minimap", std::string{"on"});
  CHECK_FALSE(ui->elementHasClass("hud", "minimap", "hidden"));
  ui->setSetting("minimap", std::string{"auto"});
  CHECK_FALSE(ui->elementHasClass("hud", "minimap", "hidden"));
  // The old boolean setting's false is off too.
  ui->setSetting("minimap", false);
  CHECK(ui->elementHasClass("hud", "minimap", "hidden"));
  ui->setSetting("minimap", std::string{"on"});

  // A scene, a talk, a menu or a minigame (hud.minimap_allowed false) and a battle hide it, and
  // it comes back with the field.
  ui->setSetting("hud.minimap_allowed", false);
  CHECK(ui->elementHasClass("hud", "minimap", "hidden"));
  ui->setSetting("hud.minimap_allowed", true);
  CHECK_FALSE(ui->elementHasClass("hud", "minimap", "hidden"));
  ui->setSetting("battle.active", true);
  CHECK(ui->elementHasClass("hud", "minimap", "hidden"));
  ui->setSetting("battle.active", false);
  CHECK_FALSE(ui->elementHasClass("hud", "minimap", "hidden"));
}

TEST_CASE("the HUD shows the minimap of the current map round the player, north-up") {
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
  CHECK(playerAt.x == doctest::Approx(frameAt.x).epsilon(0.02));
  CHECK(playerAt.y == doctest::Approx(frameAt.y).epsilon(0.02));

  // The disc follows the player: its marker stays in the middle wherever it walks, and the partner
  // shows where it stands from the player, east to the right and south down. The view's radius is
  // the smallest (MinimapMinViewRadius: 800), so 200 units are a quarter of the disc's radius.
  const auto radius = ui->elementRect("hud", "minimap-frame");
  REQUIRE(radius.has_value());
  const float discRadius = (radius->right - radius->left) / 2.0F;
  ui->setMinimapMarkers(MinimapMarker{.x = 100.0F, .z = 150.0F, .facingX = 0.0F, .facingZ = 1.0F},
                        MinimapMarker{.x = 300.0F, .z = 50.0F, .facingX = 0.0F, .facingZ = -1.0F});
  ui->update();
  const auto movedPlayer = ui->elementCenter("hud", "minimap-player");
  const auto partner = ui->elementCenter("hud", "minimap-partner");
  REQUIRE(movedPlayer.has_value());
  REQUIRE(partner.has_value());
  const Ui::Point playerNow = movedPlayer.value_or(Ui::Point{});
  const Ui::Point partnerNow = partner.value_or(Ui::Point{});
  CHECK(playerNow.x == doctest::Approx(frameAt.x).epsilon(0.02));
  CHECK(playerNow.y == doctest::Approx(frameAt.y).epsilon(0.02));
  CHECK(partnerNow.x - frameAt.x == doctest::Approx(discRadius * 200.0F / 800.0F).epsilon(0.05));
  CHECK(frameAt.y - partnerNow.y == doctest::Approx(discRadius * 100.0F / 800.0F).epsilon(0.05));
  CHECK_FALSE(ui->elementHasClass("hud", "minimap-partner", "pinned"));

  // A partner out of view waits on the rim, in its direction.
  ui->setMinimapMarkers(MinimapMarker{.x = 100.0F, .z = 150.0F, .facingX = 0.0F, .facingZ = 1.0F},
                        MinimapMarker{.x = 3100.0F, .z = 150.0F, .facingX = 0.0F, .facingZ = 1.0F});
  ui->update();
  const auto pinned = ui->elementCenter("hud", "minimap-partner");
  REQUIRE(pinned.has_value());
  CHECK(ui->elementHasClass("hud", "minimap-partner", "pinned"));
  CHECK(pinned.value_or(Ui::Point{}).x - frameAt.x ==
        doctest::Approx(discRadius * 0.9F).epsilon(0.05));
  CHECK(pinned.value_or(Ui::Point{}).y == doctest::Approx(frameAt.y).epsilon(0.02));

  // A new map replaces the image.
  grid.columns = 2;
  grid.rows = 4;
  ui->setMinimap(grid, "Casa");
  ui->update();
  CHECK(ui->elementText("hud", "minimap-name") == "Casa");
}

TEST_CASE("the minimap marks the enemies in view round the player, under the player's arrow") {
  for (const std::string_view skin : {"original", "modern"}) {
    CAPTURE(skin);
    Content content;
    auto ui = createUi(content.mount());
    REQUIRE(ui->loadDocument("hud", idOf("ui/hud/document")));
    ui->setSetting("ui_skin", std::string{skin});
    ui->show("hud");
    MinimapGrid grid{
        .columns = 4, .rows = 2, .cells = {}, .cellSize = 100.0F, .originX = 0.0F, .originZ = 0.0F};
    grid.cells.assign(8, MinimapCell::Walkable);
    ui->setMinimap(grid, "Bosque");
    // The view's radius is the smallest (MinimapMinViewRadius: 800): an enemy 400 east of the
    // player is half way to the rim, one 400 north of it half way up, and one 2000 away is out of
    // view.
    const MinimapMarker player{.x = 100.0F, .z = 100.0F, .facingX = 0.0F, .facingZ = -1.0F};
    const std::array<MinimapMarker, 3> enemies{
        MinimapMarker{.x = 500.0F, .z = 100.0F, .facingX = 0.0F, .facingZ = -1.0F},
        MinimapMarker{.x = 100.0F, .z = -300.0F, .facingX = 0.0F, .facingZ = -1.0F},
        MinimapMarker{.x = 2100.0F, .z = 100.0F, .facingX = 0.0F, .facingZ = -1.0F}};
    ui->setMinimapMarkers(player, std::nullopt, enemies);
    ui->update();
    const auto frame = ui->elementRect("hud", "minimap-frame");
    REQUIRE(frame.has_value());
    const float centreX = (frame->left + frame->right) / 2.0F;
    const float centreY = (frame->top + frame->bottom) / 2.0F;
    const float discRadius = (frame->right - frame->left) / 2.0F;
    const auto east = ui->elementCenter("hud", "minimap-enemy-1");
    const auto north = ui->elementCenter("hud", "minimap-enemy-2");
    REQUIRE(east.has_value());
    REQUIRE(north.has_value());
    CHECK(east->x - centreX == doctest::Approx(discRadius * 0.5F).epsilon(0.05));
    CHECK(east->y == doctest::Approx(centreY).epsilon(0.02));
    CHECK(north->x == doctest::Approx(centreX).epsilon(0.02));
    CHECK(centreY - north->y == doctest::Approx(discRadius * 0.5F).epsilon(0.05));
    CHECK(ui->styleProperty("hud", "minimap-enemy-1", "display") == "block");
    // Out of view it is hidden, not pinned to the rim as the partner is.
    CHECK(ui->styleProperty("hud", "minimap-enemy-3", "display") == "none");
    CHECK(ui->elementHasClass("hud", "minimap-enemy-1", "enemy"));
    // A dot, smaller than the player's arrow.
    const auto dot = ui->elementRect("hud", "minimap-enemy-1");
    const auto arrow = ui->elementRect("hud", "minimap-player");
    REQUIRE(dot.has_value());
    REQUIRE(arrow.has_value());
    CHECK(dot->right - dot->left > 0.0F);
    CHECK(dot->right - dot->left < arrow->right - arrow->left);

    // The enemies move with the frames: the east one walks closer, the far one comes into view.
    const std::array<MinimapMarker, 3> moved{
        MinimapMarker{.x = 300.0F, .z = 100.0F, .facingX = 0.0F, .facingZ = -1.0F},
        MinimapMarker{.x = 100.0F, .z = -300.0F, .facingX = 0.0F, .facingZ = -1.0F},
        MinimapMarker{.x = 100.0F, .z = 500.0F, .facingX = 0.0F, .facingZ = -1.0F}};
    ui->setMinimapMarkers(player, std::nullopt, moved);
    ui->update();
    const auto closer = ui->elementCenter("hud", "minimap-enemy-1");
    const auto south = ui->elementCenter("hud", "minimap-enemy-3");
    REQUIRE(closer.has_value());
    REQUIRE(south.has_value());
    CHECK(closer->x - centreX == doctest::Approx(discRadius * 0.25F).epsilon(0.05));
    CHECK(ui->styleProperty("hud", "minimap-enemy-3", "display") == "block");
    CHECK(south->y - centreY == doctest::Approx(discRadius * 0.5F).epsilon(0.05));

    // One beaten or gone, then none (the option off): the dots left over hide.
    ui->setMinimapMarkers(player, std::nullopt, std::span{moved}.first(1));
    ui->update();
    CHECK(ui->styleProperty("hud", "minimap-enemy-1", "display") == "block");
    CHECK(ui->styleProperty("hud", "minimap-enemy-2", "display") == "none");
    CHECK(ui->styleProperty("hud", "minimap-enemy-3", "display") == "none");
    ui->setMinimapMarkers(player, std::nullopt);
    ui->update();
    CHECK(ui->styleProperty("hud", "minimap-enemy-1", "display") == "none");
  }
}

TEST_CASE("the minimap marks the items on the ground in view, under the enemies' dots") {
  for (const std::string_view skin : {"original", "modern"}) {
    CAPTURE(skin);
    Content content;
    auto ui = createUi(content.mount());
    REQUIRE(ui->loadDocument("hud", idOf("ui/hud/document")));
    ui->setSetting("ui_skin", std::string{skin});
    ui->show("hud");
    MinimapGrid grid{
        .columns = 4, .rows = 2, .cells = {}, .cellSize = 100.0F, .originX = 0.0F, .originZ = 0.0F};
    grid.cells.assign(8, MinimapCell::Walkable);
    ui->setMinimap(grid, "Playa");
    // The smallest view (800): an item 400 west of the player is half way to the rim, one 2000
    // away is out of view; an enemy stands on the first item.
    const MinimapMarker player{.x = 1000.0F, .z = 100.0F, .facingX = 0.0F, .facingZ = -1.0F};
    const std::array<MinimapMarker, 2> items{
        MinimapMarker{.x = 600.0F, .z = 100.0F, .facingX = 0.0F, .facingZ = -1.0F},
        MinimapMarker{.x = 3000.0F, .z = 100.0F, .facingX = 0.0F, .facingZ = -1.0F}};
    const std::array<MinimapMarker, 1> enemies{items[0]};
    ui->setMinimapMarkers(player, std::nullopt, enemies, items);
    ui->update();
    const auto frame = ui->elementRect("hud", "minimap-frame");
    REQUIRE(frame.has_value());
    const float centreX = (frame->left + frame->right) / 2.0F;
    const float centreY = (frame->top + frame->bottom) / 2.0F;
    const float discRadius = (frame->right - frame->left) / 2.0F;
    const auto west = ui->elementCenter("hud", "minimap-item-1");
    REQUIRE(west.has_value());
    CHECK(centreX - west->x == doctest::Approx(discRadius * 0.5F).epsilon(0.05));
    CHECK(west->y == doctest::Approx(centreY).epsilon(0.02));
    CHECK(ui->elementHasClass("hud", "minimap-item-1", "item"));
    CHECK_FALSE(ui->elementHasClass("hud", "minimap-item-1", "enemy"));
    CHECK(ui->styleProperty("hud", "minimap-item-1", "display") == "block");
    CHECK(ui->styleProperty("hud", "minimap-item-2", "display") == "none");
    // The enemy's dot is its own element, in its own layer, at the same place.
    const auto enemy = ui->elementCenter("hud", "minimap-enemy-1");
    REQUIRE(enemy.has_value());
    CHECK(enemy->x == doctest::Approx(west->x).epsilon(0.02));
    const auto mark = ui->elementRect("hud", "minimap-item-1");
    const auto arrow = ui->elementRect("hud", "minimap-player");
    REQUIRE(mark.has_value());
    REQUIRE(arrow.has_value());
    CHECK(mark->right - mark->left > 0.0F);
    CHECK(mark->right - mark->left < arrow->right - arrow->left);

    // Picked up (or the option off): the marks hide, and the enemies stay.
    ui->setMinimapMarkers(player, std::nullopt, enemies);
    ui->update();
    CHECK(ui->styleProperty("hud", "minimap-item-1", "display") == "none");
    CHECK(ui->styleProperty("hud", "minimap-enemy-1", "display") == "block");
  }
}

// A UI that shows the texts of a catalog, as the game creates it.
std::unique_ptr<Ui> createUi(const VirtualFileSystem& files, const localization::Catalog& texts) {
  auto ui = Ui::create(UiDesc{.files = &files,
                              .texts = &texts,
                              .width = 1280,
                              .height = 720,
                              .clock = {},
                              .scripts = {},
                              .cursor = {}});
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
  CHECK(ui->elementText("pause_menu", "prompt-confirm").contains(">Espacio<"));

  REQUIRE(texts.setLanguage("en"));
  ui->refreshTexts();
  ui->update();
  CHECK(ui->elementText("said", "label") == "Yes");
  CHECK(ui->elementText("said", "said") == "150%");
  CHECK(ui->elementText("hud", "minimap-name") == "File City &lt;A&gt;");
  CHECK(ui->elementText("pause_menu", "title") == "Pause");
  CHECK(ui->elementText("pause_menu", "prompt-confirm").contains(">Space<"));
  CHECK(ui->elementText("main_menu", "prompt-confirm").contains(">Space<"));
  CHECK(ui->elementText("main_menu", "options") == "Options");
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

TEST_CASE("the main menu opens the options over the title with the keyboard and starts the game") {
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
  CHECK(ui->elementText("main_menu", "play") == "Nueva partida");
  CHECK(ui->elementText("main_menu", "options") == "Opciones");
  CHECK(ui->elementText("main_menu", "quit") == "Salir del juego");

  // Continue is greyed without a save, so down goes to the online versus and then to Options,
  // which asks the title for the pause menu's options page (title_options.hpp) and hides the
  // menu's box meanwhile.
  press(*ui, input, input::keys::Down);
  CHECK(ui->focusedElementId("main_menu") == "versus");
  CHECK(ui->elementText("main_menu", "versus") == "Versus en línea");
  press(*ui, input, input::keys::Down);
  REQUIRE(ui->focusedElementId("main_menu") == "options");
  press(*ui, input, input::keys::Return);
  CHECK(commandNames(*ui) == std::vector<std::string>{"title_options"});
  CHECK(ui->elementHasClass("main_menu", "panel", "hidden"));
  // Back from the options the title shows the menu again, on Options.
  ui->hide("main_menu");
  ui->show("main_menu", true);
  frame(*ui, input);
  CHECK_FALSE(ui->elementHasClass("main_menu", "panel", "hidden"));
  CHECK(ui->focusedElementId("main_menu") == "options");
  // Cancel does not leave the main menu.
  press(*ui, input, input::keys::LeftCtrl);
  CHECK(ui->isVisible("main_menu"));

  press(*ui, input, input::keys::Up);
  press(*ui, input, input::keys::Up);
  REQUIRE(ui->focusedElementId("main_menu") == "play");
  press(*ui, input, input::keys::Return);
  CHECK_FALSE(ui->isVisible("main_menu"));
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
  CHECK(ui->elementText("main_menu", "options") == "Options");
  CHECK(click(*ui, input, "main_menu", "options"));
  CHECK(commandNames(*ui) == std::vector<std::string>{"title_options"});
  ui->hide("main_menu");
  ui->show("main_menu", true);
  frame(*ui, input);
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
  // The game pack's default is the original's bitmap font.
  CHECK(ui->textFont() == TextFont::Original);
  ui->setTextFont(TextFont::Modern);
  ui->update();
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

TEST_CASE("in the script's textboxes a space advances as far as the glyph before it") {
  // The fixture font with a space 2 pixels wide.
  const std::string meta = std::string{fixture::Meta} + R"yaml(- index: 3
  sjis: "8140"
  character: "　"
  ascii: " "
  x: 12
  y: 0
  width: 2
)yaml";
  Content content;
  auto png = assets::encodePng(fixture::atlas());
  REQUIRE(png.has_value());
  content.add("font/main.png",
              std::string_view{reinterpret_cast<const char*>(png->data()), png->size()});
  content.add("font/main_meta.yaml", meta);
  content.add("ui/look/document.rml", R"rml(<rml>
<head>
	<link type="text/rcss" href="/ui/common/base.rcss"/>
	<style>
		div { position: absolute; left: 0; font-size: 33px; }
	</style>
</head>
<body>
	<div id="menu-b" class="original-look" style="top: 0;">B A</div>
	<div id="dialogue-b" class="original-look original-dialogue" style="top: 50px;">B A</div>
	<div id="dialogue-a" class="original-look original-dialogue" style="top: 100px;">A B</div>
</body>
</rml>)rml");
  auto ui = createUi(content.mount());
  REQUIRE(ui->loadDocument("look", idOf("ui/look/document")));
  ui->show("look");
  ui->update();
  REQUIRE(ui->textFont() == TextFont::Original);
  // Elsewhere a space takes its own 2 pixels; in a textbox, the 4 of the B or the 3 of the A
  // before it (three times larger).
  CHECK(widthOf(*ui, "menu-b") == doctest::Approx(27.0F));
  CHECK(widthOf(*ui, "dialogue-b") == doctest::Approx(33.0F));
  CHECK(widthOf(*ui, "dialogue-a") == doctest::Approx(30.0F));
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
