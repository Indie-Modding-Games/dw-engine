#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/input/action_map.hpp>
#include <dwe/input/input_state.hpp>
#include <dwe/input/keys.hpp>
#include <dwe/ui/ui.hpp>

#include <doctest/doctest.h>

#include <array>
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
#include <vector>

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

// The team pack from the source tree, a stand-in for the base pack it depends on, and a pack for the tests' own
// documents, all in memory so a test can edit a file.
struct Content {
    MemoryFileSystem memory;
    std::unique_ptr<VirtualFileSystem> files;

    Content() {
        const std::filesystem::path game = DWE_GAME_PACK;
        for (const auto& entry : std::filesystem::recursive_directory_iterator{game}) {
            if (!entry.is_regular_file()) {
                continue;
            }
            std::ifstream stream{entry.path(), std::ios::binary};
            const std::string bytes{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
            memory.addFile(std::filesystem::path{"packs/game"} / entry.path().lexically_relative(game), bytes);
        }
        memory.addFile("packs/base/pack.yaml", "id: base\nversion: 0.1.0\nlayer: base\npriority: 0\n");
        memory.addFile("packs/tests/pack.yaml", "id: tests\nversion: 0.1.0\nlayer: mods\npriority: 0\n");
    }

    void add(std::string_view path, std::string_view text) {
        memory.addFile(std::filesystem::path{"packs/tests"} / path, text);
    }

    // Mounts the packs; call after adding the tests' files, because mounting lists them.
    VirtualFileSystem& mount() {
        const std::vector<std::filesystem::path> roots{"packs/base", "packs/game", "packs/tests"};
        auto mounted = VirtualFileSystem::mount(memory, roots);
        REQUIRE_MESSAGE(mounted.has_value(), mounted.error().message);
        files = std::make_unique<VirtualFileSystem>(std::move(*mounted));
        return *files;
    }
};

// A clock the tests move by hand.
struct FakeClock {
    double now = 0.0;
};

std::unique_ptr<Ui> createUi(const VirtualFileSystem& files, FakeClock* clock = nullptr, bool hotReload = false,
                             const script::ScriptLimits& limits = {}) {
    UiDesc desc{.files = &files, .width = 1280, .height = 720, .hotReload = hotReload, .clock = {}, .scripts = limits};
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

// Moves the pointer to an element and clicks it, as the platform reports it. Returns whether the UI took the click.
bool click(Ui& ui, input::InputState& input, std::string_view document, std::string_view element) {
    const auto found = ui.elementCenter(document, element);
    REQUIRE_MESSAGE(found.has_value(), element);
    const Ui::Point center = found.value_or(Ui::Point{});
    const platform::Event move = platform::MouseMoved{.x = center.x, .y = center.y};
    const platform::Event down = platform::MouseButtonChanged{.button = platform::MouseButton::Left, .down = true};
    const platform::Event up = platform::MouseButtonChanged{.button = platform::MouseButton::Left, .down = false};
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
	<link type="text/rcss" href="/ui/common/theme.rcss"/>
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
    content.add("ui/one/document.rml", "<rml><head><script>value = 'one'\nfunction tell() ui.send(value) end"
                                       "</script></head><body/></rml>");
    content.add("ui/two/document.rml", "<rml><head><script>value = 'two'\nfunction tell() ui.send(value) end"
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
    ui->setSetting("minimap_visible", true);
    ui->setSetting("minimap_size", std::string{"medium"});
    ui->setSetting("ui_scale", 1.0);
    input::InputState input{input::defaultActionMap()};

    press(*ui, input, input::keys::Tab);
    REQUIRE(ui->isVisible("pause_menu"));
    CHECK(ui->focusedElementId("pause_menu") == "continue");
    CHECK(ui->elementText("pause_menu", "prompt-confirm") == "Intro");
    CHECK(ui->elementText("pause_menu", "prompt-cancel") == "Esc");

    press(*ui, input, input::keys::Down);
    CHECK(ui->focusedElementId("pause_menu") == "open-minimap");
    press(*ui, input, input::keys::Return);
    CHECK(ui->elementText("pause_menu", "title") == "Minimapa");
    CHECK(ui->focusedElementId("pause_menu") == "minimap-visible");
    CHECK(ui->elementText("pause_menu", "minimap-visible-value") == "Sí");

    press(*ui, input, input::keys::Return);
    auto commands = ui->takeCommands();
    REQUIRE(commands.size() == 1);
    CHECK(commands[0].name == "minimap_visible");
    CHECK(commands[0].value == UiValue{false});
    // The label follows what the game reports, not what was asked.
    ui->setSetting("minimap_visible", false);
    CHECK(ui->elementText("pause_menu", "minimap-visible-value") == "No");

    // Right on an option changes it, as confirm does.
    press(*ui, input, input::keys::Down);
    CHECK(ui->focusedElementId("pause_menu") == "minimap-size");
    press(*ui, input, input::keys::Right);
    commands = ui->takeCommands();
    REQUIRE(commands.size() == 1);
    CHECK(commands[0].value == UiValue{std::string{"large"}});

    // Cancel goes back to the entry that opened the page, then closes the menu.
    press(*ui, input, input::keys::Escape);
    CHECK(ui->elementText("pause_menu", "title") == "Pausa");
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
    input.handle(platform::GamepadConnected{.id = Pad, .family = platform::GamepadFamily::PlayStation, .name = "pad"});

    press(*ui, input, GamepadButton::Start);
    REQUIRE(ui->isVisible("pause_menu"));
    CHECK(ui->elementText("pause_menu", "prompt-confirm") == "Cruz");
    CHECK(ui->elementText("pause_menu", "prompt-cancel") == "Círculo");

    press(*ui, input, GamepadButton::DpadDown);
    press(*ui, input, GamepadButton::DpadDown);
    CHECK(ui->focusedElementId("pause_menu") == "open-options");
    press(*ui, input, GamepadButton::South);
    CHECK(ui->focusedElementId("pause_menu") == "display-mode");

    // The stick navigates too.
    input.handle(platform::GamepadAxisMoved{.id = Pad, .axis = platform::GamepadAxis::LeftY, .value = 1.0F});
    frame(*ui, input);
    input.handle(platform::GamepadAxisMoved{.id = Pad, .axis = platform::GamepadAxis::LeftY, .value = 0.0F});
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
    CHECK(ui->elementText("pause_menu", "prompt-confirm") == "Intro");

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
    CHECK(ui->elementText("pause_menu", "title") == "Opciones");
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
    CHECK(ui->elementText("pause_menu", "title") == "Pausa");
    // A click on the background keeps the focus on an entry, so the keyboard goes on from it.
    const std::string before = ui->focusedElementId("pause_menu");
    ui->handleEvent(platform::MouseMoved{.x = 4.0F, .y = 4.0F});
    CHECK(ui->handleEvent(platform::MouseButtonChanged{.button = platform::MouseButton::Left, .down = true}));
    CHECK(ui->handleEvent(platform::MouseButtonChanged{.button = platform::MouseButton::Left, .down = false}));
    frame(*ui, input);
    CHECK(ui->focusedElementId("pause_menu") == before);
    CHECK(ui->elementText("pause_menu", "title") == "Pausa");
    // Nor does the pointer position the window reports after a resize, which the player did not move.
    const auto quit = ui->elementCenter("pause_menu", "quit");
    REQUIRE(quit.has_value());
    const Ui::Point quitAt = quit.value_or(Ui::Point{});
    ui->handleEvent(platform::WindowResized{.width = 1280, .height = 720});
    CHECK(ui->handleEvent(platform::MouseMoved{.x = quitAt.x, .y = quitAt.y}));
    frame(*ui, input);
    CHECK(ui->focusedElementId("pause_menu") == before);
    // A right click backs out, as cancel does.
    CHECK(ui->handleEvent(platform::MouseButtonChanged{.button = platform::MouseButton::Right, .down = true}));
    CHECK(ui->handleEvent(platform::MouseButtonChanged{.button = platform::MouseButton::Right, .down = false}));
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
    CHECK_FALSE(ui->handleEvent(platform::MouseButtonChanged{.button = platform::MouseButton::Left, .down = true}));
    CHECK_FALSE(ui->handleEvent(platform::MouseButtonChanged{.button = platform::MouseButton::Left, .down = false}));
}

TEST_CASE("the HUD shows the minimap of the current map and its markers") {
    Content content;
    auto ui = createUi(content.mount());
    REQUIRE(ui->loadDocument("hud", idOf("ui/hud/document")));
    ui->show("hud");

    MinimapGrid grid{.columns = 4, .rows = 2, .cells = {}, .cellSize = 100.0F, .originX = 0.0F, .originZ = 0.0F};
    grid.cells.assign(8, MinimapCell::Walkable);
    ui->setMinimap(grid, "Ciudad <Archivo>");
    ui->setMinimapMarkers(MinimapMarker{.x = 200.0F, .z = 100.0F, .facingX = 1.0F, .facingZ = 0.0F}, std::nullopt);
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

    // A quarter of the way along the map and three quarters down it, the marker is there in the frame too.
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
    // The player is 100 units left of and 50 below the centre, the partner 150 right of and 50 above it.
    CHECK(playerNow.x < frameCentre.x - 1.0F);
    CHECK(playerNow.y > frameCentre.y + 1.0F);
    CHECK(partnerNow.x > frameCentre.x + 1.0F);
    CHECK(partnerNow.y < frameCentre.y - 1.0F);
    CHECK((frameCentre.x - playerNow.x) == doctest::Approx((partnerNow.x - frameCentre.x) * 2.0F / 3.0F).epsilon(0.05));
    CHECK((playerNow.y - frameCentre.y) == doctest::Approx(frameCentre.y - partnerNow.y).epsilon(0.05));

    // A new map replaces the image.
    grid.columns = 2;
    grid.rows = 4;
    ui->setMinimap(grid, "Casa");
    ui->update();
    CHECK(ui->elementText("hud", "minimap-name") == "Casa");
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

    content.add("ui/test/logic.lua", R"(function on_show(document) document:get("label"):set_text("edited") end)");
    ui->reloadChanged();
    CHECK(ui->elementText("test", "label") == "shown 1 &lt;&amp;&gt;"); // checked at most twice a second
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
    content.add("ui/other/document.rml",
                "<rml><head><script>function on_show() shown = (shown or 0) + 1 end</script></head><body/></rml>");
    auto ui = createUi(content.mount(), nullptr, false, script::ScriptLimits{.instructionsPerCall = 200'000});
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
    // Every API function that returns text or an element is called until Lua has no memory left for the result.
    // Lua unwinds with longjmp, so a C++ string alive at that moment would leak, and LeakSanitizer would say so.
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
end
</script></head><body><p id="label-with-a-long-id-that-does-not-fit-in-a-short-string" title="a title long enough to live on the heap, not in the string">a text long enough to live on the heap, not inside the string</p></body></rml>)rml");
    auto ui = createUi(
        content.mount(), nullptr, false,
        script::ScriptLimits{.memoryBytes = std::size_t{4} * 1024 * 1024, .instructionsPerCall = 1'000'000'000});
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
    CHECK(large.value_or(Ui::Point{}).x == doctest::Approx(small.value_or(Ui::Point{}).x * 2.0F).epsilon(0.01));
    CHECK(large.value_or(Ui::Point{}).y == doctest::Approx(small.value_or(Ui::Point{}).y * 2.0F).epsilon(0.01));

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
    // The hand on the mouse trembles: the pointer stays on the same entry, so the focus stays where it is.
    ui->handleEvent(platform::MouseMoved{.x = firstAt.x + 1.0F, .y = firstAt.y});
    CHECK(ui->focusedElementId("test") == "second");
    // Onto another entry and back: the focus follows.
    const auto second = ui->elementCenter("test", "second");
    REQUIRE(second.has_value());
    ui->handleEvent(platform::MouseMoved{.x = second.value_or(Ui::Point{}).x, .y = second.value_or(Ui::Point{}).y});
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
    ui->handleEvent(platform::MouseMoved{.x = first.value_or(Ui::Point{}).x, .y = first.value_or(Ui::Point{}).y});
    CHECK(ui->handleEvent(platform::MouseButtonChanged{.button = platform::MouseButton::Left, .down = true}));
    CHECK_FALSE(ui->handleEvent(platform::FocusLost{}));
    // The release never comes; the next press and release are a whole new click, and the lost one sent nothing.
    CHECK(ui->takeCommands().empty());
    ui->handleEvent(platform::MouseMoved{.x = first.value_or(Ui::Point{}).x, .y = first.value_or(Ui::Point{}).y});
    CHECK(ui->handleEvent(platform::MouseButtonChanged{.button = platform::MouseButton::Left, .down = true}));
    CHECK(ui->handleEvent(platform::MouseButtonChanged{.button = platform::MouseButton::Left, .down = false}));
    const auto commands = ui->takeCommands();
    REQUIRE(commands.size() == 1);
    CHECK(commands[0].value == UiValue{std::string{"first"}});
}

TEST_CASE("hot reload keeps the old document when the edit is broken, and keeps its place") {
    Content content;
    content.add("ui/test/document.rml", TestDocument);
    content.add("ui/test/logic.lua", TestLogic);
    content.add("ui/top/document.rml", "<rml><head><link type='text/rcss' href='/ui/common/theme.rcss'/></head>"
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

} // namespace dwe::ui
