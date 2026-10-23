#include <dwe/core/error.hpp>
#include <dwe/game/save/save_file.hpp>
#include <dwe/game/save/slots.hpp>
#include <dwe/game/save/storage.hpp>

#include <doctest/doctest.h>

#include <chrono>
#include <optional>
#include <string>

#include "game_saves.hpp"

namespace {

using dwe::app::GameSaves;
using dwe::app::Playthrough;
using dwe::app::SaveDialog;
using dwe::app::SaveResult;
using dwe::app::SaveStep;
using dwe::game::save::MemorySaveStorage;
using dwe::game::save::SaveFile;
using dwe::game::save::SaveSlots;
using dwe::game::save::SlotId;
using dwe::game::save::SlotKind;

constexpr SlotId Slot1{.kind = SlotKind::Manual, .index = 1};

std::chrono::sys_seconds startedAt(int second) {
  using namespace std::chrono;
  return sys_days{2026y / October / 22} + seconds{second};
}

SaveFile saveOf(std::chrono::sys_seconds started, std::string species) {
  SaveFile file;
  file.header.gameVersion = "0.1.0";
  file.header.rulesetId = "original";
  file.header.created = started;
  file.header.updated = started;
  file.header.summary.species = std::move(species);
  return file;
}

// One manual slot and the playthrough a game saves from it: what it writes is that playthrough's
// save, with the partner it has now.
struct Game {
  MemorySaveStorage storage;
  SaveSlots slots{storage, {.manualSlots = 1, .autosaveSlots = 0}};
  Playthrough playthrough{.home = Slot1, .started = startedAt(0)};
  GameSaves saves{&slots, [this](SlotId slot) -> dwe::core::Result<void> {
                    return slots.save(slot, saveOf(playthrough.started, "now"));
                  }};

  [[nodiscard]] std::string savedSpecies() const {
    auto file = slots.load(Slot1);
    return file ? file->header.summary.species : std::string{};
  }
};

SaveStep saved() {
  return {.dialog = std::nullopt, .result = SaveResult::Saved};
}

SaveStep question(std::string text) {
  return {.dialog = SaveDialog{.text = std::move(text), .file = "slot_01.yaml", .question = true},
          .result = std::nullopt};
}

} // namespace

TEST_CASE("a save in the game writes the playthrough's own slot without asking") {
  Game game;
  REQUIRE(game.slots.save(Slot1, saveOf(startedAt(0), "before")).has_value());
  CHECK(game.saves.save(game.playthrough) == saved());
  CHECK(game.savedSpecies() == "now");
}

TEST_CASE("with no slot or no saves a save in the game writes nothing") {
  Game game;
  game.playthrough.home.reset();
  CHECK(game.saves.save(game.playthrough) ==
        SaveStep{.dialog = std::nullopt, .result = SaveResult::NotSaved});
  GameSaves unsaved{nullptr, [](SlotId) -> dwe::core::Result<void> { return {}; }};
  CHECK(unsaved.save({.home = Slot1, .started = startedAt(0)}) ==
        SaveStep{.dialog = std::nullopt, .result = SaveResult::NotSaved});
  CHECK(game.savedSpecies().empty());
}

TEST_CASE("a slot whose file was deleted outside the game is written again") {
  Game game;
  CHECK(game.saves.save(game.playthrough) == saved());
  CHECK(game.savedSpecies() == "now");
}

TEST_CASE("a damaged slot is written over only when the player says so") {
  Game game;
  game.storage.put("slot_01.yaml", "format: dwe-save\n");
  CHECK(game.saves.save(game.playthrough) == question("save.home_damaged"));
  CHECK(game.saves.answer(false) ==
        SaveStep{.dialog = std::nullopt, .result = SaveResult::NotSaved});
  CHECK(game.savedSpecies().empty());
  CHECK(game.saves.save(game.playthrough) == question("save.home_damaged"));
  CHECK(game.saves.answer(true) == saved());
  CHECK(game.savedSpecies() == "now");
}

TEST_CASE("another playthrough's save, started a second apart, is told apart and asked about") {
  Game game;
  REQUIRE(game.slots.save(Slot1, saveOf(startedAt(1), "other")).has_value());
  CHECK(game.saves.save(game.playthrough) == question("save.home_other"));
  CHECK(game.saves.answer(false).result == SaveResult::NotSaved);
  CHECK(game.savedSpecies() == "other");
  CHECK(game.saves.save(game.playthrough) == question("save.home_other"));
  CHECK(game.saves.answer(true) == saved());
  CHECK(game.savedSpecies() == "now");
  // Once written, the slot is this playthrough's again.
  CHECK(game.saves.save(game.playthrough) == saved());
}

TEST_CASE("a newer game's save is never written over") {
  Game game;
  game.storage.put("slot_01.yaml", "format: dwe-save\nformat_version: 99\n");
  CHECK(
      game.saves.save(game.playthrough) ==
      SaveStep{.dialog =
                   SaveDialog{.text = "save.home_newer", .file = "slot_01.yaml", .question = false},
               .result = SaveResult::NotSaved});
  CHECK(game.saves.answer(true) == SaveStep{});
  CHECK(game.storage.read("slot_01.yaml").value() ==
        std::optional<std::string>{"format: dwe-save\nformat_version: 99\n"});
}

TEST_CASE("a save that fails says so and leaves the slot as it was") {
  Game game;
  REQUIRE(game.slots.save(Slot1, saveOf(startedAt(0), "before")).has_value());
  game.storage.failNextWrite();
  CHECK(game.saves.save(game.playthrough) ==
        SaveStep{.dialog =
                     SaveDialog{.text = "save.failed", .file = "slot_01.yaml", .question = false},
                 .result = SaveResult::Failed});
  CHECK(game.saves.answer(false) == SaveStep{});
  CHECK(game.savedSpecies() == "before");
}
