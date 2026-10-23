#include <dwe/assets/effect_script.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/battle/effect_runner.hpp>
#include <dwe/test_support/user_assets.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using dwe::assets::EffectScript;
using dwe::game::battle::EffectRunner;
using dwe::game::battle::EffectSounds;

namespace {

// A made-up technique: it starts with sound 1 and the held sound 0x12, then a sub-effect that
// checks for a hit each frame (sound 16 and the end on a hit) and gives up on frame 3 (sound 15).
constexpr std::string_view EffectYaml = R"(move: "made_up_breath"
header:
  sub_effect_count: 3
)";

constexpr std::string_view ScriptYaml = R"(start_script:
- label: "start"
  at: "0x0100"
  op: spawn
  count: 2
  table: "base+0x0010"
  stride: 8
  ordinal: 0
- at: "0x0110"
  op: push_address
  address: "base+0x0190"
- at: "0x0120"
  op: push_address
  address: "base+0x0250"
- at: "0x0130"
  op: sub
  sub: set_sub_effect_scripts
  args:
    tick_script: "l_0190"
    render_script: "l_0250"
- at: "0x0140"
  op: push_immediate
  value: 1
- at: "0x0150"
  op: sub
  sub: play_sound
  args:
    sound: 1
- at: "0x0160"
  op: push_immediate
  value: 18
- at: "0x0170"
  op: sub
  sub: play_sound
  args:
    sound: 18
- at: "0x0180"
  op: return
- label: "l_0190"
  at: "0x0190"
  op: load_immediate
  value: 3
- at: "0x01a0"
  op: branch_if
  variable: "instance+0x0000"
  width: 4
  comparison: less
  target: "l_0200"
- at: "0x01b0"
  op: push_immediate
  value: 15
- at: "0x01c0"
  op: sub
  sub: play_sound
  args:
    sound: 15
- at: "0x01d0"
  op: load_immediate
  value: -1
- at: "0x01e0"
  op: apply_operator
  variable: "instance+0x0000"
  width: 4
  operator: set
- at: "0x01f0"
  op: return
- label: "l_0200"
  at: "0x0200"
  op: push_address
  address: "base+0x0020"
- at: "0x0210"
  op: push_address
  address: "base+0x0000"
- at: "0x0220"
  op: sub
  sub: check_collision
  args:
    extent: "&base+0x0020"
    hit: "&base+0x0000"
- at: "0x0230"
  op: load_immediate
  value: 0
- at: "0x0240"
  op: branch_if
  variable: "base+0x0000"
  width: 4
  comparison: not_equal
  target: "l_0260"
- label: "l_0250"
  at: "0x0250"
  op: return
- label: "l_0260"
  at: "0x0260"
  op: push_immediate
  value: 16
- at: "0x0270"
  op: sub
  sub: play_sound
  args:
    sound: 16
- at: "0x0280"
  op: load_immediate
  value: -1
- at: "0x0290"
  op: apply_operator
  variable: "instance+0x0000"
  width: 4
  operator: set
- at: "0x02a0"
  op: return
init_script:
- label: "init"
  at: "0x02b0"
  op: stop
data:
- at: "0x0000"
  kind: variable
  width: 4
  value: 0
- at: "0x0010"
  kind: instances
  count: 2
  stride: 8
  free: true
)";

struct Recorded {
  std::string kind;
  std::int64_t value = 0;
  bool operator==(const Recorded&) const = default;
};

class RecordingSounds final : public EffectSounds {
public:
  void play(std::int32_t sound) override { log.push_back({.kind = "play", .value = sound}); }
  std::optional<std::int64_t> playHeld(std::int32_t sound) override {
    log.push_back({.kind = "held", .value = sound});
    return HeldVoice;
  }
  void stop(std::int64_t voice) override { log.push_back({.kind = "stop", .value = voice}); }

  static constexpr std::int64_t HeldVoice = 7;
  std::vector<Recorded> log;
};

EffectScript madeUpScript() {
  const auto effect = dwe::core::parseYaml(EffectYaml);
  const auto script = dwe::core::parseYaml(ScriptYaml);
  REQUIRE(effect.has_value());
  REQUIRE(script.has_value());
  auto read = dwe::assets::readEffectScript(*effect, *script);
  REQUIRE_MESSAGE(read.has_value(), (read ? std::string{} : read.error().message));
  return std::move(*read);
}

std::string fileText(const std::filesystem::path& path) {
  std::ifstream in{path, std::ios::binary};
  return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

std::optional<EffectScript> extractedEffect(const std::filesystem::path& folder) {
  const auto effect = dwe::core::parseYaml(fileText(folder / "effect.yaml"));
  const auto script = dwe::core::parseYaml(fileText(folder / "script.yaml"));
  if (!effect || !script) {
    return std::nullopt;
  }
  auto read = dwe::assets::readEffectScript(*effect, *script);
  if (!read) {
    MESSAGE(folder.string() << ": " << read.error().message);
    return std::nullopt;
  }
  return std::move(*read);
}

} // namespace

TEST_CASE("an effect plays its sounds on the frames its scripts say") {
  RecordingSounds sounds;
  EffectRunner runner{sounds};
  const std::size_t slot = runner.load(madeUpScript());
  CHECK(runner.start(slot) == 0);
  CHECK(sounds.log == std::vector<Recorded>{{"play", 1}, {"held", 0x12}});
  CHECK(runner.running(slot));

  sounds.log.clear();
  runner.tick(); // frame 1
  runner.tick(); // frame 2
  CHECK(sounds.log.empty());
  runner.tick(); // frame 3: gives up, and the held sound of its sub-effect stops
  CHECK(sounds.log == std::vector<Recorded>{{"play", 15}, {"stop", RecordingSounds::HeldVoice}});
  CHECK_FALSE(runner.running(slot));
  sounds.log.clear();
  runner.tick();
  CHECK(sounds.log.empty());
}

TEST_CASE("an effect's hit check answers with the battle's hit") {
  RecordingSounds sounds;
  EffectRunner runner{sounds};
  const std::size_t slot = runner.load(madeUpScript());
  runner.start(slot);
  runner.tick();
  sounds.log.clear();
  runner.noteHit(slot);
  runner.tick(); // frame 2: the check sees the hit
  CHECK(sounds.log == std::vector<Recorded>{{"play", 16}, {"stop", RecordingSounds::HeldVoice}});
  CHECK_FALSE(runner.running(slot));

  // A new start runs on the instance the first one freed, without the old hit.
  sounds.log.clear();
  CHECK(runner.start(slot) == 0);
  runner.tick();
  runner.tick();
  CHECK(sounds.log == std::vector<Recorded>{{"play", 1}, {"held", 0x12}});
}

TEST_CASE("a stopped effect plays nothing more and lets go of its held sound") {
  RecordingSounds sounds;
  EffectRunner runner{sounds};
  const std::size_t slot = runner.load(madeUpScript());
  const auto section = runner.start(slot);
  REQUIRE(section.has_value());
  sounds.log.clear();
  runner.stop(slot, section.value_or(0));
  runner.tick();
  CHECK(sounds.log == std::vector<Recorded>{{"stop", RecordingSounds::HeldVoice}});
  runner.tick();
  runner.tick();
  runner.tick();
  CHECK(sounds.log.size() == 1);

  runner.start(slot);
  sounds.log.clear();
  runner.stopAll();
  CHECK(sounds.log == std::vector<Recorded>{{"stop", RecordingSounds::HeldVoice}});
}

TEST_CASE("every extracted effect reads, and Pepper Breath sounds its breath, shot and hit") {
  const std::optional<std::filesystem::path> content = dwe::test_support::userContent();
  if (!content) {
    MESSAGE("DWE_CONTENT is not set: skipping the test on extracted content");
    return;
  }
  const std::filesystem::path effects = *content / "base" / "effect";
  REQUIRE(std::filesystem::is_directory(effects));
  std::size_t read = 0;
  for (const auto& entry : std::filesystem::directory_iterator{effects}) {
    CHECK_MESSAGE(extractedEffect(entry.path()).has_value(), entry.path().string());
    ++read;
  }
  CHECK(read > 0);

  // Pepper Breath: the breath at once, the fireball on frame 20, the hit once the battle hits.
  constexpr int ShotFrame = 20;
  constexpr int HitFrame = 24;
  constexpr int Frames = 40;
  auto pepperBreath = extractedEffect(effects / "pepper_breath");
  if (!pepperBreath) {
    FAIL_CHECK("pepper_breath does not read");
    return;
  }
  RecordingSounds sounds;
  EffectRunner runner{sounds};
  const std::size_t slot = runner.load(std::move(*pepperBreath));
  runner.start(slot);
  CHECK(sounds.log == std::vector<Recorded>{{"play", 1}});
  sounds.log.clear();
  std::vector<std::pair<int, std::int64_t>> played;
  for (int frame = 1; frame <= Frames; ++frame) {
    if (frame == HitFrame) {
      runner.noteHit(slot);
    }
    runner.tick();
    for (const Recorded& sound : sounds.log) {
      played.emplace_back(frame, sound.value);
    }
    sounds.log.clear();
  }
  REQUIRE(played.size() >= 2);
  CHECK(played[0] == std::pair<int, std::int64_t>{ShotFrame, 14});
  CHECK(std::ranges::find(played, std::pair<int, std::int64_t>{HitFrame, 15}) != played.end());
}
