#include <dwe/game/field/field.hpp>
#include <dwe/game/save/slots.hpp>
#include <dwe/game/save/storage.hpp>
#include <dwe/scene/world.hpp>
#include <dwe/script/script_vm.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <ostream>
#include <string_view>
#include <utility>
#include <vector>

#include "packages/game/field/tests/fixture.hpp"
#include "save_command.hpp"
#include "session_fixture.hpp"

namespace {

using dwe::app::manualSlotFromName;
using dwe::app::quickSaveSlot;
using dwe::app::SaveCommand;
using dwe::game::save::MemorySaveStorage;
using dwe::game::save::SlotId;
using dwe::game::save::SlotInfo;
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

// One slot of a quick save's list: a manual or autosave slot, empty or holding a game.
SlotInfo slotOf(SlotKind kind, std::int32_t index, SlotInfo::State state = SlotInfo::State::Empty) {
  return SlotInfo{.slot = SlotId{.kind = kind, .index = index},
                  .state = state,
                  .header = std::nullopt,
                  .problem = {},
                  .hasBackup = false};
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

TEST_CASE(
    "a save command that names no manual slot, or a game that is not saved, fails and writes nothing") {
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

TEST_CASE("a save command waits while a care scene holds the field and saves once it ends") {
  const TestPacks packs;
  MemorySaveStorage storage;
  const auto session = newGame(packs, &storage);
  dwe::scene::World world;
  dwe::game::field::Field field(world, dwe::game::field::testing::originalRules());
  const auto map = dwe::game::field::testing::roomMap("town");
  REQUIRE(field.enterMap(map, 0).has_value());
  session->attachField(field);
  SaveCommand command;
  const auto scripts = scriptsWith(command);

  // Asked for during the partner's pile scene, the save stays queued while the scene plays.
  field.holdForCare(true);
  run(*scripts, R"(save("slot_05"))");
  constexpr std::int32_t SceneTicks = 120;
  for (std::int32_t tick = 0; tick < SceneTicks; ++tick) {
    field.tick(static_cast<float>(dwe::session::testing::Tick));
    session->tick(dwe::session::testing::Tick);
    const auto waited = command.apply(*session);
    REQUIRE_MESSAGE(waited.has_value(), errorOf(waited));
  }
  CHECK(command.pending());
  CHECK_FALSE(written(storage, "slot_05.yaml"));

  // The scene's end lets the next frame write it, once.
  field.holdForCare(false);
  session->tick(dwe::session::testing::Tick);
  const auto saved = command.apply(*session);
  REQUIRE_MESSAGE(saved.has_value(), errorOf(saved));
  CHECK_FALSE(command.pending());
  CHECK(written(storage, "slot_05.yaml"));
  session->detachField();
}

TEST_CASE("the quick save writes the playthrough's home slot when it has one") {
  const std::optional<SlotId> home = SlotId{.kind = SlotKind::Manual, .index = 3};
  const std::vector<SlotInfo> listed{
      slotOf(SlotKind::Manual, 1),
      slotOf(SlotKind::Manual, 2, SlotInfo::State::Saved),
  };
  CHECK(quickSaveSlot(home, listed) == home);
}

TEST_CASE("the quick save takes the first empty manual slot for a game that never saved") {
  const std::vector<SlotInfo> listed{
      slotOf(SlotKind::Manual, 1, SlotInfo::State::Saved),
      slotOf(SlotKind::Manual, 2, SlotInfo::State::Damaged),
      slotOf(SlotKind::Manual, 3),
      slotOf(SlotKind::Autosave, 1, SlotInfo::State::Saved),
  };
  // A damaged slot holds a game, as the original's memory card: the quick save goes to the next
  // free one, and never to an autosave's.
  CHECK(quickSaveSlot(std::nullopt, listed) == SlotId{.kind = SlotKind::Manual, .index = 3});
}

TEST_CASE("the quick save finds nowhere when every manual slot holds a game") {
  const std::vector<SlotInfo> listed{
      slotOf(SlotKind::Manual, 1, SlotInfo::State::Saved),
      slotOf(SlotKind::Manual, 2, SlotInfo::State::Newer),
  };
  CHECK_FALSE(quickSaveSlot(std::nullopt, listed).has_value());
}

TEST_CASE("a quick save from a new game writes the first slot and keeps it as its home") {
  const TestPacks packs;
  MemorySaveStorage storage;
  const auto session = newGame(packs, &storage);

  // A new game has no home: the quick save takes the first empty manual slot...
  REQUIRE(session->slots() != nullptr);
  CHECK(quickSaveSlot(session->homeSlot(), session->slots()->list()) ==
        SlotId{.kind = SlotKind::Manual, .index = 1});

  // ...and from its first write on, every quick save returns to that same slot.
  const auto first = session->save({.kind = SlotKind::Manual, .index = 1});
  REQUIRE_MESSAGE(first.has_value(), errorOf(first));
  CHECK(quickSaveSlot(session->homeSlot(), session->slots()->list()) ==
        SlotId{.kind = SlotKind::Manual, .index = 1});
  CHECK(written(storage, "slot_01.yaml"));
}
