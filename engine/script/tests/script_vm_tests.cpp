#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/log.hpp>
#include <dwe/input/input_state.hpp>
#include <dwe/input/keys.hpp>
#include <dwe/localization/catalog.hpp>
#include <dwe/script/script_vm.hpp>

#include <doctest/doctest.h>
#include <sol/sol.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

using namespace dwe;

namespace {

std::unique_ptr<script::ScriptVm> makeVm(const script::ScriptLimits& limits = {}) {
    auto vm = script::ScriptVm::create(limits);
    REQUIRE(vm.has_value());
    return std::move(vm.value());
}

} // namespace

TEST_CASE("scripts run and keep their globals") {
    auto vm = makeVm();
    REQUIRE(vm->runString("counter = 41", "test"));
    REQUIRE(vm->runString("counter = counter + 1", "test"));
    CHECK(vm->lua()["counter"].get<int>() == 42);
}

TEST_CASE("the sandbox has no file, process or module access") {
    auto vm = makeVm();
    for (const std::string_view name : {"io", "os", "package", "require", "dofile", "loadfile", "load", "debug"}) {
        CAPTURE(name);
        CHECK_FALSE(vm->lua()[name].valid());
    }
    CHECK(vm->lua()["string"].get_type() == sol::type::table);
    CHECK(vm->lua()["math"].get_type() == sol::type::table);
}

TEST_CASE("scripts cannot dump bytecode or control the garbage collector") {
    auto vm = makeVm();
    CHECK_FALSE(vm->lua()["string"]["dump"].valid());
    REQUIRE(vm->runString("used = collectgarbage('count')", "test"));
    CHECK(vm->lua()["used"].get<double>() > 0.0);
    for (const char* option : {"stop", "collect", "step", "incremental", "generational"}) {
        CAPTURE(option);
        CHECK_FALSE(vm->runString(std::string{"collectgarbage('"} + option + "')", "test").has_value());
    }
}

TEST_CASE("precompiled chunks are rejected") {
    auto vm = makeVm();
    // A binary chunk starts with the escape character followed by "Lua".
    CHECK_FALSE(vm->runString("\x1bLua", "test").has_value());
}

TEST_CASE("an error is returned, not thrown") {
    auto vm = makeVm();
    auto result = vm->runString("error('broken')", "test");
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("broken") != std::string::npos);
}

TEST_CASE("an error that is not a string, or that breaks print, is still returned") {
    auto vm = makeVm();
    CHECK_FALSE(vm->runString("error(setmetatable({}, { __tostring = function() return 'odd' end }))", "test"));
    CHECK_FALSE(vm->runString("print(setmetatable({}, { __tostring = function() error('broken') end }))", "test"));
    CHECK(vm->runString("print('still', 'working', 1, nil, true)", "test"));
}

TEST_CASE("the engine's reads and calls survive hostile metatables") {
    auto vm = makeVm();
    REQUIRE(vm->runString("marker = setmetatable({}, { __index = function() error('trap') end })"
                          " setmetatable(_G, { __index = function() while true do end end })",
                          "test"));
    CHECK_FALSE(vm->number("marker.x").has_value());
    CHECK_FALSE(vm->number("missing.x").has_value());
    CHECK(vm->call("missing").has_value());
    REQUIRE(vm->runString("rawset(_G, 'tick', function() error(setmetatable({}, {})) end)", "test"));
    auto result = vm->call("tick");
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("not a string") != std::string::npos);
}

TEST_CASE("table.move refuses ranges that would run for too long") {
    auto vm = makeVm();
    CHECK_FALSE(vm->runString("table.move({}, 1, math.maxinteger - 1, 2)", "test").has_value());
    CHECK_FALSE(vm->runString("table.move({}, math.mininteger, 0, 1)", "test").has_value());
    REQUIRE(vm->runString("moved = table.move({1, 2, 3}, 1, 3, 2, {})", "test"));
    REQUIRE(vm->runString("count = #moved + (moved[1] == nil and 0 or 100)", "test"));
    CHECK(vm->lua()["count"].get<int>() == 4);
}

TEST_CASE("a printed line is cut to a bounded size") {
    static std::size_t longest = 0;
    core::addLogSink(
        [](core::LogLevel /*level*/, std::string_view message) { longest = std::max(longest, message.size()); });
    auto vm = makeVm();
    REQUIRE(vm->runString("print(string.rep('x', 1000000))", "test"));
    CHECK(longest > 4096);
    CHECK(longest < 4200);
}

TEST_CASE("an endless loop is stopped by the instruction budget") {
    auto vm = makeVm({.memoryBytes = std::size_t{16} * 1024 * 1024, .instructionsPerCall = 1'000'000});
    auto result = vm->runString("while true do end", "test");
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("too long") != std::string::npos);
}

TEST_CASE("catching the budget error with pcall does not keep a script running") {
    auto vm = makeVm({.memoryBytes = std::size_t{16} * 1024 * 1024, .instructionsPerCall = 1'000'000});
    auto result = vm->runString("local function spin() while true do end end while true do pcall(spin) end", "test");
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("too long") != std::string::npos);
}

TEST_CASE("coroutines share the instruction budget") {
    auto vm = makeVm({.memoryBytes = std::size_t{16} * 1024 * 1024, .instructionsPerCall = 1'000'000});
    auto result =
        vm->runString("local ok, message = coroutine.resume(coroutine.create(function() while true do end end))"
                      " error(message)",
                      "test");
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("too long") != std::string::npos);
}

TEST_CASE("each call gets a fresh budget") {
    auto vm = makeVm({.memoryBytes = std::size_t{16} * 1024 * 1024, .instructionsPerCall = 1'000'000});
    REQUIRE(vm->runString("function work() local n = 0 for i = 1, 20000 do n = n + i end end", "test"));
    for (int call = 0; call < 200; ++call) {
        REQUIRE(vm->call("work"));
    }
}

TEST_CASE("a script cannot allocate past its memory limit") {
    auto vm = makeVm({.memoryBytes = std::size_t{4} * 1024 * 1024, .instructionsPerCall = 100'000'000});
    auto result = vm->runString("local t = {} for i = 1, 10000000 do t[i] = i end", "test");
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("calling a missing function does nothing, and arguments reach the script") {
    auto vm = makeVm();
    CHECK(vm->call("missing").has_value());
    REQUIRE(vm->runString("function tick(dt) total = (total or 0) + dt end", "test"));
    const std::array<double, 1> step{0.5};
    REQUIRE(vm->call("tick", step));
    REQUIRE(vm->call("tick", step));
    CHECK(vm->lua()["total"].get<double>() == doctest::Approx(1.0));
}

TEST_CASE("the input API reports actions") {
    input::InputState input{input::defaultActionMap()};
    auto vm = makeVm();
    vm->bindInput(input);
    input.handle(platform::KeyChanged{.key = input::keys::D, .down = true, .repeat = false});
    REQUIRE(vm->runString("x, y = input.vector('move_left', 'move_right', 'move_up', 'move_down')", "test"));
    CHECK(vm->lua()["x"].get<double>() == doctest::Approx(1.0));
    REQUIRE(vm->runString("held = input.down('move_right'); device = input.device()", "test"));
    CHECK(vm->lua()["held"].get<bool>());
    CHECK(vm->lua()["device"].get<std::string>() == "keyboard_mouse");
}

TEST_CASE("the texts API gives the text of a key in the player's language") {
    assets::MemoryFileSystem memory;
    memory.addFile("game/pack.yaml", "id: game\nversion: 1.0.0\nlayer: base\n");
    memory.addFile("game/localization/en/menu.yaml", "items: \"{count} of {total}\"\nonly_english: Only\n");
    memory.addFile("game/localization/es/menu.yaml", "items: \"{count} de {total}\"\n");
    auto files = assets::VirtualFileSystem::mount(memory, std::vector<std::filesystem::path>{"game"});
    REQUIRE(files.has_value());
    auto catalog = localization::Catalog::load(*files, "es");
    REQUIRE(catalog.has_value());
    auto vm = makeVm();
    vm->bindTexts(*catalog);
    REQUIRE(vm->runString("a = t('menu.items', {count = 3, total = 'siete', [1] = 'ignored'})", "test"));
    REQUIRE(vm->runString("b = t('menu.only_english'); c = t('menu.none'); d = texts.language()", "test"));
    CHECK(vm->lua()["a"].get<std::string>() == "3 de siete");
    CHECK(vm->lua()["b"].get<std::string>() == "Only");
    CHECK(vm->lua()["c"].get<std::string>() == "menu.none");
    CHECK(vm->lua()["d"].get<std::string>() == "es");
    REQUIRE(catalog->setLanguage("en"));
    REQUIRE(vm->runString("a = t('menu.items', {count = 1.5, total = 2})", "test"));
    CHECK(vm->lua()["a"].get<std::string>() == "1.5 of 2");
}

TEST_CASE("an edited file is reloaded, and a broken edit keeps the previous state") {
    const auto path = std::filesystem::temp_directory_path() / "dwe_reload_test.lua";
    std::ofstream{path} << "value = 1";
    auto vm = makeVm();
    REQUIRE(vm->runFile(path));

    std::ofstream{path} << "value = 2";
    std::filesystem::last_write_time(path, std::filesystem::last_write_time(path) + std::chrono::seconds{1});
    vm->reloadChanged();
    CHECK(vm->lua()["value"].get<int>() == 2);

    std::ofstream{path} << "value = 3 +";
    std::filesystem::last_write_time(path, std::filesystem::last_write_time(path) + std::chrono::seconds{1});
    vm->reloadChanged();
    CHECK(vm->lua()["value"].get<int>() == 2);
    std::filesystem::remove(path);
}

TEST_CASE("numbers are read by dotted path") {
    auto vm = makeVm();
    REQUIRE(vm->runString("speed = 3; marker = { pos = { x = 1.5 } }; name = 'agu'", "test"));
    CHECK(vm->number("speed") == 3.0);
    CHECK(vm->number("marker.pos.x") == 1.5);
    CHECK_FALSE(vm->number("marker.pos.y").has_value());
    CHECK_FALSE(vm->number("name").has_value());
    CHECK_FALSE(vm->number("speed.x").has_value());
}
