#include <dwe/core/error.hpp>
#include <dwe/game/save/save_file.hpp>
#include <dwe/game/save/slots.hpp>
#include <dwe/game/save/storage.hpp>
#include <dwe/session/battle_registry.hpp>

#include <doctest/doctest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "game_saves.hpp"
#include "save_machine_flow.hpp"

namespace {

using dwe::app::GameSaves;
using dwe::app::Playthrough;
using dwe::app::SaveMachineFacts;
using dwe::app::SaveMachineFlow;
using dwe::app::SaveMachineView;
using dwe::app::SaveResult;
using dwe::game::save::MemorySaveStorage;
using dwe::game::save::SaveFile;
using dwe::game::save::SaveSlots;
using dwe::game::save::SlotId;
using dwe::game::save::SlotKind;
using dwe::session::BattleRegistry;
using dwe::session::RegisteredPartner;

constexpr SlotId Slot1{.kind = SlotKind::Manual, .index = 1};
constexpr std::size_t Yes = 0;
constexpr std::size_t No = 1;

std::chrono::sys_seconds started() {
  using namespace std::chrono;
  return sys_days{2026y / October / 30};
}

RegisteredPartner agumon() {
  RegisteredPartner partner{.name = "AA",
                            .species = "agumon",
                            .hp = 800,
                            .mp = 600,
                            .offense = 80,
                            .defense = 60,
                            .speed = 50,
                            .brains = 40,
                            .discipline = 50};
  partner.techniques.at(0) = "spit_fire";
  return partner;
}

// A playthrough with its slot, the registry and the machine; the save writes the slot unless told
// to fail, and counts what it wrote.
struct Machine {
  MemorySaveStorage storage;
  SaveSlots slots{storage, {.manualSlots = 1, .autosaveSlots = 0}};
  BattleRegistry registry;
  bool failWrites = false;
  int writes = 0;
  SaveMachineFlow flow{GameSaves{&slots,
                                 [this](SlotId slot) -> dwe::core::Result<void> {
                                   if (failWrites) {
                                     return dwe::core::fail("the disk is full");
                                   }
                                   ++writes;
                                   SaveFile file;
                                   file.header.gameVersion = "0.1.0";
                                   file.header.rulesetId = "original";
                                   file.header.created = started();
                                   file.header.updated = started();
                                   return slots.save(slot, file);
                                 }},
                       registry, [] { return dwe::core::Result<RegisteredPartner>{agumon()}; }};

  void open(bool hasSlot = true, bool belowRookie = false) {
    flow.open({.hasSlot = hasSlot,
               .belowRookie = belowRookie,
               .playthrough = {.home = Slot1, .started = started()}});
  }
};

} // namespace

TEST_CASE("the save machine registers the partner in the slot chosen and saves") {
  Machine machine;
  machine.open();
  REQUIRE(machine.flow.view() == SaveMachineView::Register);
  machine.flow.choose(Yes);
  REQUIRE(machine.flow.view() == SaveMachineView::Slots);
  machine.flow.choose(4);
  REQUIRE(machine.flow.view() == SaveMachineView::ConfirmRegister);
  CHECK(machine.flow.slot() == 4);
  // No goes back to the list, cancel to the first box (tickMainMenu 0x3E).
  machine.flow.choose(No);
  CHECK(machine.flow.view() == SaveMachineView::Slots);
  machine.flow.cancel();
  CHECK(machine.flow.view() == SaveMachineView::Register);
  machine.flow.choose(Yes);
  machine.flow.choose(4);
  machine.flow.choose(Yes);
  CHECK_FALSE(machine.flow.active());
  CHECK(machine.flow.takeResult() == SaveResult::Saved);
  CHECK(machine.writes == 1);
  REQUIRE(machine.registry.at(4).has_value());
  CHECK(*machine.registry.at(4) == agumon());
  // The list reaches 39 slots: the last of the 40 is the ending's.
  machine.open();
  machine.flow.choose(Yes);
  machine.flow.choose(BattleRegistry::MachineSlots);
  CHECK(machine.flow.view() == SaveMachineView::Slots);
}

TEST_CASE("the save machine without registering asks to save, and no closes it") {
  Machine machine;
  machine.open();
  machine.flow.choose(No);
  REQUIRE(machine.flow.view() == SaveMachineView::SaveQuestion);
  // The save's question takes no cancel (tickMainMenu 0x45).
  machine.flow.cancel();
  CHECK(machine.flow.view() == SaveMachineView::SaveQuestion);
  machine.flow.choose(No);
  CHECK_FALSE(machine.flow.active());
  CHECK(machine.writes == 0);

  machine.open();
  machine.flow.cancel();
  REQUIRE(machine.flow.view() == SaveMachineView::SaveQuestion);
  machine.flow.choose(Yes);
  CHECK_FALSE(machine.flow.active());
  CHECK(machine.flow.takeResult() == SaveResult::Saved);
  CHECK(machine.writes == 1);
  CHECK_FALSE(machine.registry.at(0).has_value());
}

TEST_CASE("the ending's save asks to save at once, and without a slot asks nothing") {
  // main_menu.c: gameClearSave: TARGET_MENU 0x45, or -1 with no card or slot.
  Machine machine;
  machine.flow.openSaveQuestion(
      {.hasSlot = true, .belowRookie = true, .playthrough = {.home = Slot1, .started = started()}});
  REQUIRE(machine.flow.view() == SaveMachineView::SaveQuestion);
  machine.flow.choose(Yes);
  CHECK_FALSE(machine.flow.active());
  CHECK(machine.flow.takeResult() == SaveResult::Saved);
  CHECK(machine.writes == 1);

  machine.flow.openSaveQuestion({.hasSlot = true,
                                 .belowRookie = false,
                                 .playthrough = {.home = Slot1, .started = started()}});
  machine.flow.choose(No);
  CHECK_FALSE(machine.flow.active());
  CHECK(machine.writes == 1);

  machine.flow.openSaveQuestion({.hasSlot = false,
                                 .belowRookie = false,
                                 .playthrough = {.home = std::nullopt, .started = started()}});
  CHECK_FALSE(machine.flow.active());
  CHECK(machine.writes == 1);
}

TEST_CASE("the save machine's notices: below Rookie, and a playthrough with no slot") {
  Machine machine;
  machine.open(true, true);
  REQUIRE(machine.flow.view() == SaveMachineView::InTraining);
  machine.flow.choose(Yes);
  CHECK(machine.flow.view() == SaveMachineView::SaveQuestion);

  machine.flow.choose(No);
  CHECK_FALSE(machine.flow.active());

  machine.open(false, true);
  REQUIRE(machine.flow.view() == SaveMachineView::NoSlot);
  // It closes by itself after 0x79 of the original's frames (tickMainMenu 0x44).
  for (std::int32_t frame = 1; frame < SaveMachineFlow::NoSlotFrames; ++frame) {
    machine.flow.tick();
  }
  CHECK(machine.flow.view() == SaveMachineView::NoSlot);
  machine.flow.tick();
  CHECK_FALSE(machine.flow.active());
  machine.open(false, false);
  machine.flow.cancel();
  CHECK_FALSE(machine.flow.active());
}

TEST_CASE("a registration whose save fails is undone") {
  Machine machine;
  REQUIRE(machine.registry.registerAt(2, RegisteredPartner{.name = "BB", .species = "gabumon"})
              .has_value());
  machine.failWrites = true;
  machine.open();
  machine.flow.choose(Yes);
  machine.flow.choose(2);
  machine.flow.choose(Yes);
  REQUIRE(machine.flow.view() == SaveMachineView::SaveDialog);
  REQUIRE(machine.flow.dialog().has_value());
  CHECK(machine.flow.dialog()->text == "save.failed");
  CHECK(machine.flow.takeResult() == SaveResult::Failed);
  REQUIRE(machine.registry.at(2).has_value());
  CHECK(machine.registry.at(2)->name == "BB");
  machine.flow.choose(Yes);
  CHECK_FALSE(machine.flow.active());

  machine.open();
  machine.flow.choose(Yes);
  machine.flow.choose(5);
  machine.flow.choose(Yes);
  CHECK_FALSE(machine.registry.at(5).has_value());
}
