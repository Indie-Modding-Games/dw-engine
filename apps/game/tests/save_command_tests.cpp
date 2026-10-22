#include <dwe/game/save/slots.hpp>
#include <dwe/game/save/storage.hpp>
#include <dwe/script/script_vm.hpp>

#include <doctest/doctest.h>

#include <memory>
#include <ostream>
#include <string_view>
#include <utility>

#include "save_command.hpp"
#include "session_fixture.hpp"

namespace {

using dwe::app::manualSlotFromName;
using dwe::app::SaveCommand;
using dwe::game::save::MemorySaveStorage;
using dwe::game::save::SlotId;
using dwe::game::save::SlotKind;
using dwe::session::testing::errorOf;
using dwe::session::testing::newGame;
using dwe::session::testing::TestPacks;

// A script VM with the save command bound to `command`.
std::unique_ptr<dwe::script::ScriptVm> scriptsWith(SaveCommand& command) {
    auto scripts = dwe::script::ScriptVm::create();
    REQUIRE_MESSAGE(scripts.has_value(), errorOf(scripts));
    command.bind(**scripts);
    return std::move(*scripts);
}

void run(dwe::script::ScriptVm& scripts, std::string_view source) {
    const auto ran = scripts.runString(source, "save_command_tests");
    REQUIRE_MESSAGE(ran.has_value(), errorOf(ran));
}

bool written(const MemorySaveStorage& storage, std::string_view name) {
    const auto file = storage.read(name);
    REQUIRE_MESSAGE(file.has_value(), errorOf(file));
    return file->has_value();
}

} // namespace

TEST_CASE("the save command names only manual slots") {
    const auto first = manualSlotFromName("slot_01");
    REQUIRE_MESSAGE(first.has_value(), errorOf(first));
    CHECK(*first == SlotId{.kind = SlotKind::Manual, .index = 1});
    const auto withExtension = manualSlotFromName("slot_12.yaml");
    REQUIRE_MESSAGE(withExtension.has_value(), errorOf(withExtension));
    CHECK(withExtension->index == 12);

    CHECK_FALSE(manualSlotFromName("autosave_01").has_value());
    CHECK_FALSE(manualSlotFromName("slot_00").has_value());
    CHECK_FALSE(manualSlotFromName("slot_").has_value());
    CHECK_FALSE(manualSlotFromName("").has_value());
}

TEST_CASE("a script's save command saves the game to its slot between frames") {
    const TestPacks packs;
    MemorySaveStorage storage;
    const auto session = newGame(packs, &storage);
    SaveCommand command;
    const auto scripts = scriptsWith(command);

    run(*scripts, R"(save("slot_03"))");
    CHECK_FALSE(written(storage, "slot_03.yaml"));
    const auto saved = command.apply(*session);
    REQUIRE_MESSAGE(saved.has_value(), errorOf(saved));
    REQUIRE(written(storage, "slot_03.yaml"));

    const auto loaded = newGame(packs, &storage, 3);
    const auto read = loaded->load({.kind = SlotKind::Manual, .index = 3});
    REQUIRE_MESSAGE(read.has_value(), errorOf(read));
    CHECK(loaded->partner().state() == session->partner().state());
}

TEST_CASE("a save command that names no manual slot, or a game that is not saved, fails and writes nothing") {
    const TestPacks packs;
    MemorySaveStorage storage;
    const auto session = newGame(packs, &storage);
    SaveCommand command;
    const auto scripts = scriptsWith(command);

    run(*scripts, R"(save("autosave_01"))");
    CHECK_FALSE(command.apply(*session).has_value());
    CHECK_FALSE(written(storage, "autosave_01.yaml"));
    run(*scripts, R"(save("first"))");
    CHECK_FALSE(command.apply(*session).has_value());

    // The command runs once: the next frame has nothing queued, which is no failure.
    run(*scripts, R"(save("slot_02"))");
    CHECK(command.apply(*session).has_value());
    REQUIRE(written(storage, "slot_02.yaml"));
    storage.put("slot_02.yaml", "kept");
    CHECK(command.apply(*session).has_value());
    CHECK(storage.read("slot_02.yaml").value() == "kept");

    const auto unsaved = newGame(packs, nullptr);
    run(*scripts, R"(save("slot_04"))");
    CHECK_FALSE(command.apply(*unsaved).has_value());
    CHECK_FALSE(written(storage, "slot_04.yaml"));
}
