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

#include "engine/assets/tests/map_documents.hpp"

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
  sound.useBattleBank(true);
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

  // After the battle the map's bank is back.
  sound.useBattleBank(false);
  sound.playSound(8, 5);
  CHECK(rig.audio.activeVoiceCount() == 3);
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
