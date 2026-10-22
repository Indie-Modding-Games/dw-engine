#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/game/save/save_file.hpp>
#include <dwe/game/save/settings.hpp>
#include <dwe/game/save/slots.hpp>
#include <dwe/game/save/storage.hpp>

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <format>
#include <ostream>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

using namespace dwe::game::save;

template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
  return result ? std::string{} : result.error().message;
}

SaveFile fileLabelled(std::string label, std::chrono::sys_seconds updated) {
  SaveFile file;
  file.header = {.gameVersion = "0.1.0",
                 .rulesetId = "original",
                 .created = updated,
                 .updated = updated,
                 .playSeconds = 60,
                 .label = std::move(label),
                 .summary = {},
                 .autosaveReasons = {}};
  return file;
}

std::chrono::sys_seconds at(int hour) {
  using namespace std::chrono;
  return sys_days{2026y / October / 20} + hours{hour};
}

// A folder under the system's temporary folder, removed with everything in it at the end.
class TempFolder {
public:
  TempFolder() {
    std::random_device random;
    path_ = std::filesystem::temp_directory_path() /
            std::format("dwe_save_tests_{:08x}{:08x}", random(), random());
  }
  ~TempFolder() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }
  TempFolder(const TempFolder&) = delete;
  TempFolder& operator=(const TempFolder&) = delete;
  TempFolder(TempFolder&&) = delete;
  TempFolder& operator=(TempFolder&&) = delete;

  [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
  std::filesystem::path path_;
};

constexpr std::string_view Settings = R"(manual_slots: 3
autosave_slots: 2
)";

} // namespace

TEST_CASE("the disk storage replaces a file and keeps the old one as backup") {
  const TempFolder folder;
  DiskSaveStorage storage{folder.path() / "saves"};
  CHECK_FALSE(storage.read("slot_01.yaml").value().has_value());
  REQUIRE(storage.write("slot_01.yaml", "first\n", "slot_01.yaml.bak"));
  CHECK(storage.read("slot_01.yaml").value() == "first\n");
  CHECK_FALSE(storage.read("slot_01.yaml.bak").value().has_value());
  REQUIRE(storage.write("slot_01.yaml", "second\n", "slot_01.yaml.bak"));
  CHECK(storage.read("slot_01.yaml").value() == "second\n");
  CHECK(storage.read("slot_01.yaml.bak").value() == "first\n");
  CHECK_FALSE(std::filesystem::exists(folder.path() / "saves" / "slot_01.yaml.tmp"));

  CHECK(errorOf(storage.read("../escape")).contains("not a save file name"));
  CHECK_FALSE(storage.write(".hidden", "x", "slot_01.yaml.bak"));
  CHECK_FALSE(storage.write("slot_01.yaml", "x", "a/b"));
}

TEST_CASE("the save settings are read and checked") {
  const auto settings = parseSaveSettings(Settings);
  REQUIRE_MESSAGE(settings, errorOf(settings));
  CHECK(settings->manualSlots == 3);
  CHECK(settings->autosaveSlots == 2);

  CHECK(errorOf(parseSaveSettings("manual_slots: 0\nautosave_slots: 1\n"))
            .contains("manual_slots must be 1 to 99"));
  CHECK(errorOf(parseSaveSettings("manual_slots: 1\nautosave_slots: 100\n"))
            .contains("autosave_slots must be 0 to 99"));
  CHECK(errorOf(parseSaveSettings("manual_slots: 1\n")).contains("autosave_slots"));
  CHECK(errorOf(parseSaveSettings("manual_slots: 1\nautosave_slots: 1\nextra: 1\n"))
            .contains("extra"));
}

TEST_CASE("the game pack's save settings: 15 slots and 3 autosave slots") {
  const dwe::assets::DiskFileSystem disk;
  // The game pack depends on base; a stand-in manifest lets it mount alone.
  dwe::assets::MemoryFileSystem memory;
  memory.addFile("base/pack.yaml", "id: base\nversion: 0.1.0\nlayer: base\n");
  for (const std::string_view file : {"pack.yaml", "save/settings.yaml"}) {
    const auto bytes = disk.readFile(std::filesystem::path{DWE_GAME_PACK} / file);
    REQUIRE(bytes);
    memory.addFile(std::filesystem::path{"game"} / file, *bytes);
  }
  const std::vector<std::filesystem::path> packs{"base", "game"};
  const auto vfs = dwe::assets::VirtualFileSystem::mount(memory, packs);
  REQUIRE_MESSAGE(vfs, errorOf(vfs));
  const auto settings = loadSaveSettings(*vfs);
  REQUIRE_MESSAGE(settings, errorOf(settings));
  CHECK(settings->manualSlots == 15);
  CHECK(settings->autosaveSlots == 3);
}

TEST_CASE("slots are listed, saved, loaded and backed up") {
  MemorySaveStorage storage;
  SaveSlots slots{storage, parseSaveSettings(Settings).value()};
  REQUIRE(slots.slots().size() == 5);
  CHECK(slots.slots()[3] == SlotId{.kind = SlotKind::Autosave, .index = 1});
  CHECK(slotFileName({.kind = SlotKind::Manual, .index = 2}) == "slot_02.yaml");
  CHECK(slotFileName({.kind = SlotKind::Autosave, .index = 1}) == "autosave_01.yaml");

  const SlotId second{.kind = SlotKind::Manual, .index = 2};
  REQUIRE(slots.save(second, fileLabelled("first", at(8))));
  REQUIRE(slots.save(second, fileLabelled("second", at(9))));
  storage.put("slot_03.yaml", "format: dwe-save\nformat_version: 1\n");

  const auto infos = slots.list();
  REQUIRE(infos.size() == 5);
  CHECK(infos[0].state == SlotInfo::State::Empty);
  CHECK(infos[1].state == SlotInfo::State::Saved);
  CHECK(infos[1].header.value_or(SaveHeader{}).label == "second");
  CHECK(infos[1].hasBackup);
  CHECK(infos[2].state == SlotInfo::State::Damaged);
  CHECK(infos[2].problem.starts_with("slot_03.yaml: save file: "));

  CHECK(slots.load(second).value().header.label == "second");
  CHECK(slots.loadBackup(second).value().header.label == "first");
  CHECK(errorOf(slots.load({.kind = SlotKind::Manual, .index = 1})) == "slot_01.yaml is empty");
  CHECK(errorOf(slots.save({.kind = SlotKind::Manual, .index = 4}, fileLabelled("x", at(8)))) ==
        "there is no save slot 4");
  CHECK_FALSE(slots.load({.kind = SlotKind::Autosave, .index = 3}));
}

TEST_CASE("the list fills the summary of saves that have none with the game's reader") {
  MemorySaveStorage storage;
  SaveSlots slots{storage, parseSaveSettings(Settings).value()};
  const SlotId first{.kind = SlotKind::Manual, .index = 1};
  const SlotId second{.kind = SlotKind::Manual, .index = 2};
  REQUIRE(slots.save(first, fileLabelled("old", at(8))));
  SaveFile summarized = fileLabelled("new", at(9));
  summarized.header.summary = {
      .slot = "slot_02", .tamer = "", .species = "base:sp_02", .nickname = "", .map = ""};
  REQUIRE(slots.save(second, summarized));

  CHECK(slots.list()[0].header.value_or(SaveHeader{}).summary == SaveSummary{});
  slots.setSummaryReader([](const SaveFile& file) {
    return SaveSummary{
        .slot = "", .tamer = "", .species = file.header.label, .nickname = "", .map = "read"};
  });
  const auto infos = slots.list();
  CHECK(infos[0].header.value_or(SaveHeader{}).summary.species == "old");
  CHECK(infos[0].header.value_or(SaveHeader{}).summary.map == "read");
  CHECK(infos[1].header.value_or(SaveHeader{}).summary == summarized.header.summary);
  // Loading gives the file as saved: the summary is only for the list.
  CHECK(slots.load(first).value().header.summary == SaveSummary{});
}

TEST_CASE("a failed save leaves the slot as it was") {
  MemorySaveStorage storage;
  SaveSlots slots{storage, parseSaveSettings(Settings).value()};
  const SlotId first{.kind = SlotKind::Manual, .index = 1};
  REQUIRE(slots.save(first, fileLabelled("kept", at(8))));
  storage.failNextWrite();
  CHECK_FALSE(slots.save(first, fileLabelled("lost", at(9))));
  CHECK(slots.load(first).value().header.label == "kept");
  CHECK_FALSE(slots.list()[0].hasBackup);
}

TEST_CASE("the autosave rotates over its slots") {
  MemorySaveStorage storage;
  SaveSlots slots{storage, parseSaveSettings(Settings).value()};

  const SlotId first{.kind = SlotKind::Autosave, .index = 1};
  const SlotId second{.kind = SlotKind::Autosave, .index = 2};
  CHECK(slots.autosave(fileLabelled("a", at(8))).value() == first);
  CHECK(slots.autosave(fileLabelled("b", at(9))).value() == second);
  CHECK(slots.autosave(fileLabelled("c", at(10))).value() == first);
  CHECK(slots.autosave(fileLabelled("d", at(11))).value() == second);
  storage.put("autosave_02.yaml", "damaged");
  CHECK(slots.autosave(fileLabelled("e", at(12))).value() == second);
  CHECK(slots.load(second).value().header.label == "e");
  CHECK(slots.load(first).value().header.label == "c");

  SaveSlots off{storage, SaveSettings{}};
  CHECK(errorOf(off.autosave(fileLabelled("x", at(8)))).contains("off"));
}
