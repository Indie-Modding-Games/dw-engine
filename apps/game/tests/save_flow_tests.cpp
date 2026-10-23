#include <dwe/game/save/save_file.hpp>
#include <dwe/game/save/settings.hpp>
#include <dwe/game/save/slots.hpp>
#include <dwe/game/save/storage.hpp>
#include <dwe/screens/screen_settings.hpp>
#include <dwe/ui/ui.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>

#include "save_flow.hpp"

namespace {

using dwe::app::PlayStart;
using dwe::app::SaveDialog;
using dwe::app::SaveFlow;
using dwe::app::SaveNames;
using dwe::app::SlotListMode;
using dwe::game::save::MemorySaveStorage;
using dwe::game::save::SaveFile;
using dwe::game::save::SaveSlots;
using dwe::game::save::SlotId;
using dwe::game::save::SlotKind;
using dwe::screens::ScreenSettings;
using dwe::ui::UiValue;

constexpr SlotId Slot1{.kind = SlotKind::Manual, .index = 1};
constexpr SlotId Slot2{.kind = SlotKind::Manual, .index = 2};
constexpr SlotId Slot3{.kind = SlotKind::Manual, .index = 3};
constexpr SlotId Autosave1{.kind = SlotKind::Autosave, .index = 1};

SaveFile saveOf(std::string species) {
  using namespace std::chrono;
  SaveFile file;
  file.header.gameVersion = "0.1.0";
  file.header.rulesetId = "original";
  file.header.created = sys_days{2026y / October / 22};
  file.header.updated = file.header.created;
  file.header.playSeconds = 90;
  file.header.summary.species = std::move(species);
  file.header.summary.map = "twna01";
  return file;
}

UiValue valueOf(const ScreenSettings& settings, std::string_view name) {
  const auto found = std::ranges::find(settings, name, &dwe::screens::ScreenSetting::name);
  return found == settings.end() ? UiValue{} : found->value;
}

SaveNames names() {
  return {.species = [](std::string_view id) { return "names." + std::string{id}; },
          .map = [](std::string_view) { return std::string{}; }};
}

// Three manual slots and one autosave slot: an empty slot 1, a save in slot 2, a damaged slot 3
// with its backup, and an autosave from a newer game.
struct Slots {
  MemorySaveStorage storage;
  SaveSlots slots{storage, {.manualSlots = 3, .autosaveSlots = 1}};

  Slots() {
    REQUIRE(slots.save(Slot2, saveOf("agumon")).has_value());
    REQUIRE(slots.save(Slot3, saveOf("gabumon")).has_value());
    REQUIRE(slots.save(Slot3, saveOf("gabumon")).has_value());
    storage.put("slot_03.yaml", "format: dwe-save\n");
    storage.put("autosave_01.yaml", "format: dwe-save\nformat_version: 99\n");
  }
};

} // namespace

TEST_CASE("without slots a new game is all the title screen offers") {
  SaveFlow flow{nullptr, {}};
  CHECK_FALSE(flow.keepsSaves());
  CHECK(valueOf(flow.titleSettings(), "save.can_continue") == UiValue{false});
}

TEST_CASE("a new game lists the empty slots and is saved to the one chosen") {
  Slots slots;
  SaveFlow flow{&slots.slots, names()};
  CHECK(valueOf(flow.titleSettings(), "save.can_continue") == UiValue{true});
  const ScreenSettings list = flow.open(SlotListMode::NewGame);
  CHECK(valueOf(list, "save.slots") == UiValue{1.0});
  CHECK(valueOf(list, "save.slots.mode") == UiValue{std::string{"new"}});
  CHECK(valueOf(list, "save.slot.0.number") == UiValue{1.0});
  CHECK(valueOf(list, "save.slot.0.state") == UiValue{std::string{"empty"}});

  CHECK_FALSE(flow.choose(1).has_value());
  CHECK(flow.choose(0) ==
        SaveDialog{.text = "save.confirm_new", .file = {}, .question = true, .startOnYes = true});
  CHECK_FALSE(flow.answer(false).has_value());
  CHECK_FALSE(flow.answer(true).has_value());
  REQUIRE(flow.choose(0).has_value());
  CHECK(flow.answer(true) ==
        PlayStart{.load = std::nullopt, .backup = false, .saveTo = Slot1, .choice = std::nullopt});

  CHECK(flow.chooseWithoutSaving().text == "save.slots.no_save_warning");
  CHECK(flow.answer(true) == PlayStart{});
}

TEST_CASE("continue lists the saves and says why one cannot be loaded") {
  Slots slots;
  SaveFlow flow{&slots.slots, names()};
  const ScreenSettings list = flow.open(SlotListMode::Continue);
  CHECK(valueOf(list, "save.slots") == UiValue{3.0});
  CHECK(valueOf(list, "save.slot.0.number") == UiValue{2.0});
  CHECK(valueOf(list, "save.slot.0.species_key") == UiValue{std::string{"names.agumon"}});
  CHECK(valueOf(list, "save.slot.0.map") == UiValue{std::string{"twna01"}});
  CHECK(valueOf(list, "save.slot.0.updated") == UiValue{std::string{"2026-10-22"}});
  CHECK(valueOf(list, "save.slot.1.state") == UiValue{std::string{"damaged"}});
  CHECK(valueOf(list, "save.slot.2.kind") == UiValue{std::string{"autosave"}});
  CHECK(valueOf(list, "save.slot.2.state") == UiValue{std::string{"newer"}});

  CHECK(flow.choose(0) ==
        SaveDialog{
            .text = "save.confirm_continue", .file = {}, .question = true, .startOnYes = false});
  CHECK(flow.answer(true) ==
        PlayStart{.load = Slot2, .backup = false, .saveTo = std::nullopt, .choice = std::nullopt});

  CHECK(flow.choose(1) == SaveDialog{.text = "save.damaged_use_backup",
                                     .file = "slot_03.yaml",
                                     .question = true,
                                     .startOnYes = false});
  CHECK(flow.answer(true) ==
        PlayStart{.load = Slot3, .backup = true, .saveTo = std::nullopt, .choice = std::nullopt});

  CHECK(flow.choose(2) == SaveDialog{.text = "save.too_new",
                                     .file = "autosave_01.yaml",
                                     .question = false,
                                     .startOnYes = false});
  CHECK_FALSE(flow.answer(true).has_value());
}

TEST_CASE("a damaged save with no backup is only a notice") {
  MemorySaveStorage storage;
  SaveSlots slots{storage, {.manualSlots = 1, .autosaveSlots = 0}};
  storage.put("slot_01.yaml", "format: dwe-save\n");
  SaveFlow flow{&slots, {}};
  CHECK(valueOf(flow.open(SlotListMode::NewGame), "save.slots") == UiValue{0.0});
  static_cast<void>(flow.open(SlotListMode::Continue));
  CHECK(flow.choose(0) == SaveDialog{.text = "save.damaged_no_backup",
                                     .file = "slot_01.yaml",
                                     .question = false,
                                     .startOnYes = false});
  CHECK_FALSE(flow.answer(true).has_value());
}

TEST_CASE("a game that did not start says whether the start or the save failed, and the file") {
  const PlayStart inFirst{
      .load = std::nullopt, .backup = false, .saveTo = Slot1, .choice = std::nullopt};
  CHECK(SaveFlow::startFailed(inFirst, true) ==
        SaveDialog{
            .text = "save.failed", .file = "slot_01.yaml", .question = false, .startOnYes = false});
  CHECK(
      SaveFlow::startFailed(inFirst, false) ==
      SaveDialog{.text = "save.start_failed", .file = {}, .question = false, .startOnYes = false});
  CHECK(SaveFlow::startFailed({}, false).text == "save.start_failed");
  CHECK(SaveFlow::startFailed(
            {.load = Slot3, .backup = true, .saveTo = std::nullopt, .choice = std::nullopt}, false)
            .file == "slot_03.yaml.bak");
  CHECK(SaveFlow::startFailed(
            {.load = Autosave1, .backup = false, .saveTo = std::nullopt, .choice = std::nullopt},
            false) == SaveDialog{.text = "save.load_failed",
                                 .file = "autosave_01.yaml",
                                 .question = false,
                                 .startOnYes = false});
}
