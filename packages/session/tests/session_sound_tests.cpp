#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/audio/audio.hpp>
#include <dwe/audio/output.hpp>
#include <dwe/core/log.hpp>
#include <dwe/session/session_sound.hpp>

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "packages/engine/assets/tests/map_documents.hpp"

using namespace dwe;

namespace {

// A mono 16-bit WAV of a few frames at 11025 Hz.
std::vector<std::byte> shortWav() {
  std::vector<std::byte> bytes;
  const auto put = [&](std::uint32_t value, int size) {
    for (int i = 0; i < size; ++i) {
      bytes.push_back(static_cast<std::byte>((value >> (8 * i)) & 0xFFU));
    }
  };
  const auto tag = [&](std::string_view text) {
    for (const char c : text) {
      bytes.push_back(static_cast<std::byte>(c));
    }
  };
  tag("RIFF");
  put(4 + 24 + 8 + 16, 4);
  tag("WAVEfmt ");
  put(16, 4);
  put(1, 2);
  put(1, 2);
  put(11025, 4);
  put(22050, 4);
  put(2, 2);
  put(16, 2);
  tag("data");
  put(16, 4);
  for (int i = 0; i < 8; ++i) {
    put(500, 2);
  }
  return bytes;
}

// A bank whose only sound has the given value.
std::string bankMeta(int value) {
  return std::string{R"(vab: {master_volume: 127}
programs:
- program: 0
  master_volume: 127
  pan: 64
  tones:
  - {tone: 0, center: 60, shift: 0, volume: 127, pan: 64, sample: 1, adsr: "80ff 5fd5"}
samples:
- {sample: 1, file: "sample_001.wav", rate: 11025}
sounds:
- {value: )"} +
         std::to_string(value) + ", program: 0, note: 60, tones: [0]}\n";
}

void addBank(assets::MemoryFileSystem& memory, const std::string& folder, int value) {
  memory.addFile("pack/sound/" + folder + "/bank_meta.yaml", bankMeta(value));
  memory.addFile("pack/sound/" + folder + "/sample_001.wav", shortWav());
}

// Fills the pack with the map table, the system bank and the bank of sound_id 9, and mounts it.
assets::VirtualFileSystem mountedPack(assets::MemoryFileSystem& memory) {
  memory.addFile("pack/pack.yaml", "id: base\nversion: 1.0.0\nlayer: base\n");
  // A pack that is not the engine's own names the maps it adds after itself and leaves their
  // numbers to the engine.
  std::string maps{assets::testing::FixtureTable};
  for (const std::string_view id : {"twna01", "room08", "map_119"}) {
    const std::string quoted = std::format("\"{}\"", id);
    maps.replace(maps.find(quoted), quoted.size(), std::format("\"base:{}\"", id));
  }
  for (const std::string_view index : {"  index: 104\n", "  index: 118\n", "  index: 119\n"}) {
    maps.erase(maps.find(index), index.size());
  }
  memory.addFile("pack/data/maps.yaml", maps);
  addBank(memory, "ss", 3);
  addBank(memory, "esall/009", 5);
  addBank(memory, "sb", 34);
  const std::vector<std::filesystem::path> roots{"pack"};
  return assets::VirtualFileSystem::mount(memory, roots).value();
}

struct SoundRig {
  assets::MemoryFileSystem memory;
  assets::VirtualFileSystem files{mountedPack(memory)};
  audio::Audio audio{std::make_unique<audio::NullOutput>()};
};

} // namespace

TEST_CASE("the story's slots play the system bank and the bank of the map") {
  SoundRig rig;
  session::SessionSound sound{rig.audio, rig.files};

  sound.playSound(0, 3);
  CHECK(rig.audio.activeVoiceCount() == 1);
  // No map yet, no second system bank in the packs, no such slot, no such sound: all silent.
  sound.playSound(8, 5);
  sound.playSound(1, 3);
  sound.playSound(4, 3);
  sound.playSound(0, 4);
  sound.playSound(0, -1);
  CHECK(rig.audio.activeVoiceCount() == 1);

  // twna01's sound_id is 9.
  sound.enterMap("base:twna01");
  sound.playSound(8, 5);
  CHECK(rig.audio.activeVoiceCount() == 2);
  // room08's is 7, which the packs do not have.
  sound.enterMap("base:room08");
  sound.playSound(8, 5);
  CHECK(rig.audio.activeVoiceCount() == 2);
}

TEST_CASE("a battle holds the battle bank in slot 8, and held sounds keep a handle") {
  SoundRig rig;
  session::SessionSound sound{rig.audio, rig.files};
  sound.enterMap("base:twna01");
  sound.startBattleBank();
  // The map's sound 5 is not in the battle bank; the battle's 34 is.
  sound.playSound(8, 5);
  CHECK(rig.audio.activeVoiceCount() == 0);
  sound.playSound(8, 34);
  CHECK(rig.audio.activeVoiceCount() == 1);

  const std::optional<std::int64_t> held = sound.playHeldSound(8, 34);
  REQUIRE(held.has_value());
  CHECK(rig.audio.activeVoiceCount() == 2);
  CHECK_FALSE(sound.playHeldSound(8, 5).has_value());
  sound.stopHeldSound(held.value_or(0));
  sound.stopHeldSound(held.value_or(0)); // a second stop finds nothing to stop

  // After a won battle the map's bank is back.
  sound.endBattleBank(true);
  sound.playSound(8, 5);
  CHECK(rig.audio.activeVoiceCount() == 3);
}

TEST_CASE("a won battle gives slot 8 back to the map's bank; a fled one keeps the battle's") {
  SoundRig rig;
  addBank(rig.memory, "esall/021", 1);
  const assets::VirtualFileSystem files =
      assets::VirtualFileSystem::mount(rig.memory, std::vector<std::filesystem::path>{"pack"})
          .value();
  session::SessionSound sound{rig.audio, files};
  sound.enterMap("base:twna01");
  // Routine 51's bank (0x15) in slot 8: the map's sound 5 is not in it.
  sound.useSceneBank(0x15);
  sound.playSound(8, 5);
  CHECK(rig.audio.activeVoiceCount() == 0);

  // battle_main.c: BTL_handleVictorySequence: loadMapSounds2(getMapSoundId) over the scene's.
  sound.startBattleBank();
  sound.endBattleBank(true);
  sound.playSound(8, 5);
  CHECK(rig.audio.activeVoiceCount() == 1);

  // A fled battle reloads nothing: the battle bank stays until a map loads.
  sound.startBattleBank();
  sound.endBattleBank(false);
  sound.playSound(8, 5);
  sound.playSound(8, 34);
  CHECK(rig.audio.activeVoiceCount() == 2);
  sound.enterMap("base:twna01");
  sound.playSound(8, 5);
  CHECK(rig.audio.activeVoiceCount() == 3);
  // A scene's bank replaces it too.
  sound.startBattleBank();
  sound.endBattleBank(false);
  sound.useSceneBank(std::nullopt);
  sound.playSound(8, 5);
  CHECK(rig.audio.activeVoiceCount() == 4);
}

namespace {

// How many warnings the log has had.
std::size_t warningCount() {
  static std::size_t count = 0;
  static const bool added = [] {
    core::addLogSink([](core::LogLevel level, std::string_view /*message*/) {
      count += level == core::LogLevel::Warning ? 1 : 0;
    });
    return true;
  }();
  static_cast<void>(added);
  return count;
}

} // namespace

TEST_CASE("a sound or a bank that fails warns once, across maps too") {
  SoundRig rig;
  session::SessionSound sound{rig.audio, rig.files};
  sound.enterMap("base:room08");
  const std::size_t before = warningCount();
  sound.playSound(1, 3);
  sound.playSound(1, 3);
  sound.playSound(0, 4);
  sound.playSound(0, 4);
  sound.playSound(4, 3);
  sound.playSound(4, 3);
  CHECK(warningCount() == before + 3);

  sound.enterMap("base:twna01");
  sound.playSound(1, 3);
  sound.playSound(0, 4);
  CHECK(warningCount() == before + 3);
}

namespace {

// A species bank (sound/vlall or sound/vball) that the given species use, as dwx writes used_by.
void addSpeciesBank(assets::MemoryFileSystem& memory, const std::string& folder, int value,
                    std::string_view users, bool fallback = false) {
  std::string meta = bankMeta(value) + "used_by:\n  types:\n" + std::string{users};
  if (fallback) {
    meta += "  fallback: \"every type outside 1-65\"\n";
  }
  memory.addFile("pack/sound/" + folder + "/bank_meta.yaml", meta);
  memory.addFile("pack/sound/" + folder + "/sample_001.wav", shortWav());
}

assets::VirtualFileSystem speciesPack(assets::MemoryFileSystem& memory) {
  addSpeciesBank(memory, "vlall/001", 49, "  - {type: 3, id: \"agum\"}\n");
  addSpeciesBank(memory, "vlall/015", 7, "  - {type: 14, id: \"monz\"}\n", true);
  addSpeciesBank(memory, "vball/002", 50,
                 "  - {type: 3, id: \"agum\"}\n  - {type: 117, id: \"weag\"}\n");
  addSpeciesBank(memory, "vball/005", 51, "  - {type: 17, id: \"gabu\"}\n");
  return mountedPack(memory);
}

} // namespace

TEST_CASE("slots 3 and 4 hold the partner's species banks, 5 to 7 the last battle's enemies'") {
  assets::MemoryFileSystem memory;
  const assets::VirtualFileSystem files = speciesPack(memory);
  audio::Audio audio{std::make_unique<audio::NullOutput>()};
  session::SessionSound sound{audio, files};
  std::string partner = "agum";
  sound.attachPartner([&partner] { return partner; });

  // sound.c: loadPartnerSounds: Agumon's voices (vlall 1) in slot 3, its own bank (vball 2) in 4.
  sound.playSound(3, 49);
  CHECK(audio.activeVoiceCount() == 1);
  sound.playSound(4, 50);
  CHECK(audio.activeVoiceCount() == 2);
  // A species no vlall entry names takes the fallback entry; it has no vball entry here.
  partner = "weag";
  sound.playSound(3, 7);
  CHECK(audio.activeVoiceCount() == 3);
  sound.playSound(4, 50);
  CHECK(audio.activeVoiceCount() == 4);

  // No battle yet: slots 5 to 7 are silent. A battle with two enemies fills 5 and 6.
  sound.playSound(5, 51);
  CHECK(audio.activeVoiceCount() == 4);
  const std::vector<std::string> enemies{"gabu", "agum"};
  sound.useEnemyBanks(enemies);
  sound.playSound(5, 51);
  sound.playSound(6, 50);
  sound.playSound(7, 50);
  CHECK(audio.activeVoiceCount() == 6);
  // They stay after the battle and across maps, and a battle with one enemy changes only slot 5.
  sound.enterMap("base:twna01");
  const std::vector<std::string> one{"agum"};
  sound.useEnemyBanks(one);
  sound.playSound(5, 50);
  sound.playSound(6, 50);
  CHECK(audio.activeVoiceCount() == 8);
}

TEST_CASE("a slot from 5 to 7 takes the vball entry of a species number") {
  assets::MemoryFileSystem memory;
  const assets::VirtualFileSystem files = speciesPack(memory);
  audio::Audio audio{std::make_unique<audio::NullOutput>()};
  session::SessionSound sound{audio, files};
  // script_interp.c:1845: routine 51: readVBALLSection(5, 0x73); here number 17's entry.
  sound.useSpeciesBank(5, 17);
  sound.playSound(5, 51);
  CHECK(audio.activeVoiceCount() == 1);
  // A number no entry names leaves the slot silent; slot 4 is the partner's and stays out.
  sound.useSpeciesBank(5, 99);
  sound.playSound(5, 51);
  sound.useSpeciesBank(4, 17);
  sound.playSound(4, 51);
  CHECK(audio.activeVoiceCount() == 1);
}

TEST_CASE("a map the table did not have when it was read is looked up again") {
  SoundRig rig;
  session::SessionSound sound{rig.audio, rig.files};
  sound.enterMap("base:twna01");
  // A map editor adds a map (here: renames twna01, whose sound_id is 9) and the game reloads.
  const std::vector<std::byte> bytes =
      rig.files.read(assets::AssetId::parse("data/maps").value()).value();
  std::string maps(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  const std::string_view old = "\"base:twna01\"";
  maps.replace(maps.find(old), old.size(), "\"base:twna02\"");
  rig.memory.addFile("pack/data/maps.yaml", maps);
  sound.enterMap("base:twna02");
  sound.playSound(8, 5);
  CHECK(rig.audio.activeVoiceCount() == 1);
}
