#include <dwe/game/save/slots.hpp>
#include <dwe/game/save/storage.hpp>

#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "slot_description.hpp"

namespace {

using dwe::app::describeSlot;
using dwe::game::save::MemorySaveStorage;
using dwe::game::save::SaveSlots;
using dwe::game::save::SlotInfo;

std::vector<SlotInfo> listOf(MemorySaveStorage& storage) {
  const SaveSlots slots{storage, {.manualSlots = 2}};
  return slots.list();
}

} // namespace

TEST_CASE("the slot list says a newer game's save cannot be read instead of calling it empty") {
  MemorySaveStorage storage;
  storage.put("slot_01.yaml", "format: dwe-save\nformat_version: 99\n");
  const std::vector<SlotInfo> slots = listOf(storage);
  REQUIRE(slots.size() == 2);
  REQUIRE(slots[0].state == SlotInfo::State::Newer);
  const std::string line = describeSlot(slots[0]);
  CHECK(line.starts_with("slot_01.yaml: "));
  CHECK(line != "slot_01.yaml: empty");
  CHECK(line == slots[0].problem);
  CHECK(describeSlot(slots[1]) == "slot_02.yaml: empty");
}

TEST_CASE("the slot list gives a damaged save's problem") {
  MemorySaveStorage storage;
  storage.put("slot_01.yaml", "not a save at all: [");
  const std::vector<SlotInfo> slots = listOf(storage);
  REQUIRE(slots[0].state == SlotInfo::State::Damaged);
  CHECK(describeSlot(slots[0]) == slots[0].problem);
}
