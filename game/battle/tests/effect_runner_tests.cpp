#include <dwe/assets/effect_script.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/battle/effect_runner.hpp>
#include <dwe/game/battle/flights.hpp>
#include <dwe/test_support/user_assets.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
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

// A made-up effect that draws: its instance starts on the source's bone of the bone list's entry
// for its species, 100 units over the node, and each frame draws a centred sprite there (scale 64,
// texels 0 to 31, colour 50, 50, 80) and its model's object 1 (scale 1, 0.5, 1), which the init
// script coloured 50, 50, 80.
// A made-up cloud: each frame it runs through the targets and sounds 7 on every one it has not hit
// yet, the way Poison Powder does (select_next_target, is_target_unhit, hit_target).
constexpr std::string_view TargetLoopEffectYaml = R"(move: "made_up_cloud"
header:
  sub_effect_count: 1
)";
constexpr std::string_view TargetLoopScriptYaml = R"(start_script:
- label: "start"
  at: "0x0100"
  op: spawn
  count: 1
  table: "base+0x0010"
  stride: 8
  ordinal: 0
- at: "0x0110"
  op: push_address
  address: "base+0x0200"
- at: "0x0120"
  op: push_address
  address: "base+0x0300"
- at: "0x0130"
  op: sub
  sub: set_sub_effect_scripts
  args:
    tick_script: "l_0200"
    render_script: "l_0300"
- at: "0x0140"
  op: return
- label: "l_0200"
  at: "0x0200"
  op: push_address
  address: "base+0x0000"
- at: "0x0210"
  op: sub
  sub: select_next_target
  args:
    index: "&base+0x0000"
- at: "0x0220"
  op: load_immediate
  value: 0
- at: "0x0230"
  op: branch_if
  variable: "base+0x0000"
  width: 4
  comparison: less
  target: "l_0300"
- at: "0x0240"
  op: push_address
  address: "base+0x0000"
- at: "0x0250"
  op: sub
  sub: is_target_unhit
  args:
    out: "&base+0x0000"
- at: "0x0260"
  op: load_immediate
  value: 0
- at: "0x0270"
  op: branch_if
  variable: "base+0x0000"
  width: 4
  comparison: equal
  target: "l_0200"
- at: "0x0280"
  op: push_immediate
  value: 7
- at: "0x0290"
  op: sub
  sub: play_sound
  args:
    sound: 7
- at: "0x02a0"
  op: sub
  sub: hit_target
- at: "0x02b0"
  op: jump
  target: "l_0200"
- label: "l_0300"
  at: "0x0300"
  op: return
init_script:
- label: "init"
  at: "0x0310"
  op: stop
data:
- at: "0x0000"
  kind: variable
  width: 4
  value: 0
- at: "0x0010"
  kind: instances
  count: 1
  stride: 8
  free: true
)";

constexpr std::string_view DrawingEffectYaml = R"(move: "made_up_spark"
header:
  sub_effect_count: 1
uv_anim_count: 2
)";

constexpr std::string_view DrawingScriptYaml = R"(start_script:
- label: "start"
  at: "0x0100"
  op: spawn
  count: 1
  table: "base+0x0010"
  stride: 28
  ordinal: 0
- at: "0x0102"
  op: push_address
  address: "base+0x0060"
- at: "0x0104"
  op: sub
  sub: check_tech_compatibility
  args:
    bone_offsets: "&base+0x0060"
- at: "0x0106"
  op: push_address
  address: "base+0x0120"
- at: "0x0108"
  op: push_address
  address: "base+0x0130"
- at: "0x010a"
  op: sub
  sub: set_sub_effect_scripts
  args:
    tick_script: "l_0120"
    render_script: "l_0130"
- at: "0x010c"
  op: sub
  sub: init_transform
- at: "0x010e"
  op: return
- label: "l_0120"
  at: "0x0120"
  op: return
- label: "l_0130"
  at: "0x0130"
  op: push_address
  address: "instance+0x0004"
- at: "0x0132"
  op: push_address
  address: "base+0x0040"
- at: "0x0134"
  op: push_immediate
  value: 64
- at: "0x0136"
  op: push_immediate
  value: 64
- at: "0x0138"
  op: push_immediate
  value: 0
- at: "0x013a"
  op: push_immediate
  value: 0
- at: "0x013c"
  op: push_immediate
  value: 31
- at: "0x013e"
  op: push_immediate
  value: 31
- at: "0x0140"
  op: sub
  sub: render_centered_sprite
  args:
    position: "&instance+0x0004"
    color: "&base+0x0040"
    scale_x: 64
    scale_y: 64
    u_offset: 0
    v_offset: 0
    width: 31
    height: 31
- at: "0x0142"
  op: push_immediate
  value: 1
- at: "0x0144"
  op: push_address
  address: "base+0x0050"
- at: "0x0146"
  op: sub
  sub: draw_tmd
  args:
    object_index: 1
    scale: "&base+0x0050"
- at: "0x0148"
  op: return
init_script:
- label: "init"
  at: "0x0150"
  op: push_immediate
  value: 1
- at: "0x0152"
  op: push_address
  address: "base+0x0040"
- at: "0x0154"
  op: sub
  sub: set_model_object_color
  args:
    object_index: 1
    color: "&base+0x0040"
- at: "0x0156"
  op: stop
data:
- at: "0x0010"
  kind: instances
  count: 1
  stride: 28
  free: true
- at: "0x0040"
  kind: color
  values: [50, 50, 80]
- at: "0x0050"
  kind: vector
  values: [4096, 2048, 4096]
- at: "0x0060"
  kind: bone_offsets
  entries:
  - digimon: "palm"
    bone: 0
    offset: [0, 0, 0]
  - digimon: "agum"
    bone: 0
    offset: [0, -100, 0]
)";

struct Recorded {
  std::string kind;
  std::int64_t value = 0;
  bool operator==(const Recorded&) const = default;
};

class RecordingSounds final : public EffectSounds {
public:
  void play(std::size_t /*slot*/, std::int32_t sound) override {
    log.push_back({.kind = "play", .value = sound});
  }
  std::optional<std::int64_t> playHeld(std::size_t /*slot*/, std::int32_t sound) override {
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

EffectScript drawingScript() {
  const auto effect = dwe::core::parseYaml(DrawingEffectYaml);
  const auto script = dwe::core::parseYaml(DrawingScriptYaml);
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

namespace {

using dwe::game::battle::EffectBodies;
using dwe::game::battle::EffectBody;
using dwe::game::battle::EffectDraw;
using dwe::game::battle::EffectMatrix;
using dwe::game::battle::EffectModel;
using dwe::game::battle::EffectSprite;
using dwe::game::battle::FighterId;

// Two fighters 1000 units apart on the console's x, facing each other: the attacker at the origin
// faces +x (rotation.vy 3072), the target faces -x (1024). Their nodes are their places.
class TwoFighters final : public EffectBodies {
public:
  explicit TwoFighters(std::string species) : species_(std::move(species)) {}

  [[nodiscard]] std::optional<EffectBody> body(FighterId fighter) const override {
    if (fighter > 1) {
      return std::nullopt;
    }
    EffectBody out{.species = species_,
                   .location = {.x = fighter == 0 ? 0 : Distance, .y = 0, .z = 0},
                   .rotationY = fighter == 0 ? FacingPlusX : FacingMinusX,
                   .radius = Radius,
                   .height = Height,
                   .standing = true};
    return out;
  }
  [[nodiscard]] std::optional<EffectMatrix> node(FighterId fighter,
                                                 std::int32_t /*index*/) const override {
    const std::optional<EffectBody> place = body(fighter);
    if (!place) {
      return std::nullopt;
    }
    EffectMatrix out;
    out.t = place->location;
    out.t.y = -Height / 2;
    return out;
  }
  [[nodiscard]] std::vector<FighterId> fighters() const override { return {0, 1}; }

  static constexpr std::int32_t Distance = 1000;
  static constexpr std::int32_t FacingPlusX = 3072;
  static constexpr std::int32_t FacingMinusX = 1024;
  static constexpr std::int32_t Radius = 100;
  static constexpr std::int32_t Height = 300;

private:
  std::string species_;
};

// Runs an effect from fighter 0 at fighter 1 for some frames, keeping what each frame drew.
std::vector<std::vector<EffectDraw>> drawsOf(EffectScript script, const EffectBodies& bodies,
                                             int frames) {
  RecordingSounds sounds;
  EffectRunner runner{sounds};
  runner.setBodies(bodies);
  const std::size_t slot = runner.load(std::move(script));
  runner.start(slot, 0, 1);
  std::vector<std::vector<EffectDraw>> out;
  for (int frame = 0; frame < frames; ++frame) {
    runner.tick();
    runner.render();
    out.push_back(runner.draws());
  }
  return out;
}

} // namespace

TEST_CASE("Spit Fire's trail of sprites flies from the attacker toward its target") {
  const std::optional<std::filesystem::path> content = dwe::test_support::userContent();
  if (!content) {
    MESSAGE("DWE_CONTENT is not set: skipping the test on extracted content");
    return;
  }
  auto script = extractedEffect(*content / "base" / "effect" / "spit_fire");
  REQUIRE(script.has_value());
  const TwoFighters bodies{"agum"};
  constexpr int Frames = 30;
  const auto frames = drawsOf(std::move(*script), bodies, Frames);
  std::vector<std::int32_t> fronts;
  for (const std::vector<EffectDraw>& drawn : frames) {
    std::optional<std::int32_t> front;
    for (const EffectDraw& draw : drawn) {
      const auto* sprite = std::get_if<EffectSprite>(&draw.shape);
      REQUIRE(sprite != nullptr);
      CHECK(sprite->blend == dwe::game::battle::EffectBlend::Add);
      CHECK(sprite->width == 32);
      front = std::max(front.value_or(sprite->position.x), sprite->position.x);
    }
    if (front) {
      fronts.push_back(*front);
    }
  }
  REQUIRE(fronts.size() > 4);
  // It moves 35 units a frame along the attacker's facing.
  CHECK(fronts.back() > fronts.front());
  CHECK(fronts.back() <= TwoFighters::Distance + TwoFighters::Distance / 2);
}

TEST_CASE("Sonic Jab and Poison Claw draw their model's object at the attacker") {
  const std::optional<std::filesystem::path> content = dwe::test_support::userContent();
  if (!content) {
    MESSAGE("DWE_CONTENT is not set: skipping the test on extracted content");
    return;
  }
  for (const auto& [move, species] : {std::pair{"sonic_jab", "agum"}, {"poison_claw", "palm"}}) {
    CAPTURE(move);
    auto script = extractedEffect(*content / "base" / "effect" / move);
    REQUIRE(script.has_value());
    const TwoFighters bodies{species};
    constexpr int Frames = 20;
    std::size_t models = 0;
    for (const std::vector<EffectDraw>& drawn : drawsOf(std::move(*script), bodies, Frames)) {
      for (const EffectDraw& draw : drawn) {
        if (const auto* model = std::get_if<EffectModel>(&draw.shape)) {
          CHECK(model->object == 1);
          CHECK(model->color == dwe::game::battle::EffectVector{60, 60, 60});
          ++models;
        }
      }
    }
    CHECK(models > 0);
  }
}

TEST_CASE("an effect's bone list decodes by species") {
  const EffectScript script = drawingScript();
  CHECK(script.modelObjects == 2);
  const auto found = script.boneOffsets.find(0x60);
  REQUIRE(found != script.boneOffsets.end());
  REQUIRE(found->second.size() == 2);
  CHECK(found->second[1] ==
        dwe::assets::EffectBoneOffset{.species = "agum", .bone = 0, .offset = {0, -100, 0}});
}

TEST_CASE("a render script's sprite and model come out as draws on the source's bone") {
  const TwoFighters bodies{"agum"};
  const auto frames = drawsOf(drawingScript(), bodies, 2);
  REQUIRE(frames.size() == 2);
  REQUIRE(frames[1].size() == 2);
  // The node is 150 units up (y -150); the entry's offset lifts it 100 more.
  const dwe::game::battle::EffectVector at{0, -TwoFighters::Height / 2 - 100, 0};
  const auto* sprite = std::get_if<EffectSprite>(&frames[1][0].shape);
  REQUIRE(sprite != nullptr);
  CHECK(sprite->position == at);
  CHECK(sprite->color == dwe::game::battle::EffectVector{50, 50, 80});
  CHECK(sprite->scaleX == 64);
  CHECK(sprite->width == 32);
  CHECK(sprite->height == 32);
  CHECK(sprite->pivotX == 15);
  CHECK(sprite->blend == dwe::game::battle::EffectBlend::Add);
  const auto* model = std::get_if<EffectModel>(&frames[1][1].shape);
  REQUIRE(model != nullptr);
  CHECK(model->object == 1);
  CHECK(model->color == dwe::game::battle::EffectVector{50, 50, 80});
  CHECK(model->transform.t == at);
  // The source faces +x (rotation.vy 3072): RotMatrix's columns scaled by 1, 0.5, 1.
  CHECK(model->transform.m[0] == std::array<std::int32_t, 3>{0, 0, -4096});
  CHECK(model->transform.m[1] == std::array<std::int32_t, 3>{0, 2048, 0});
  CHECK(model->transform.m[2] == std::array<std::int32_t, 3>{4096, 0, 0});
}

TEST_CASE("an effect whose bone list lacks the source's species draws nothing") {
  const TwoFighters bodies{"gabu"};
  const auto frames = drawsOf(drawingScript(), bodies, 2);
  CHECK(std::ranges::all_of(frames, [](const auto& drawn) { return drawn.empty(); }));
}

TEST_CASE("a target loop sounds once on each target it hits, not every frame") {
  const auto effect = dwe::core::parseYaml(TargetLoopEffectYaml);
  const auto script = dwe::core::parseYaml(TargetLoopScriptYaml);
  REQUIRE(effect.has_value());
  REQUIRE(script.has_value());
  auto read = dwe::assets::readEffectScript(*effect, *script);
  REQUIRE_MESSAGE(read.has_value(), (read ? std::string{} : read.error().message));
  const TwoFighters bodies{"agum"};
  RecordingSounds sounds;
  EffectRunner runner{sounds};
  runner.setBodies(bodies);
  const std::size_t slot = runner.load(std::move(*read));
  runner.start(slot, 0, 1);
  for (int frame = 0; frame < 4; ++frame) {
    runner.tick();
  }
  CHECK(sounds.log == std::vector<Recorded>{{"play", 7}});
}

TEST_CASE("every extracted effect runs and draws on two fighters") {
  const std::optional<std::filesystem::path> content = dwe::test_support::userContent();
  if (!content) {
    MESSAGE("DWE_CONTENT is not set: skipping the test on extracted content");
    return;
  }
  constexpr int Frames = 60;
  constexpr int HitFrame = 30;
  std::size_t drawing = 0;
  std::size_t effects = 0;
  for (const auto& entry : std::filesystem::directory_iterator{*content / "base" / "effect"}) {
    auto script = extractedEffect(entry.path());
    if (!script) {
      continue;
    }
    // The species of the bone list's first entry, so the technique is the source's.
    std::string species = "agum";
    if (!script->boneOffsets.empty() && !script->boneOffsets.begin()->second.empty()) {
      species = script->boneOffsets.begin()->second.front().species;
    }
    const TwoFighters bodies{species};
    RecordingSounds sounds;
    EffectRunner runner{sounds};
    runner.setBodies(bodies);
    const std::size_t slot = runner.load(std::move(*script));
    runner.start(slot, 0, 1);
    bool drew = false;
    for (int frame = 0; frame < Frames; ++frame) {
      if (frame == HitFrame) {
        runner.noteHit(slot);
      }
      runner.tick();
      runner.render();
      drew = drew || !runner.draws().empty();
    }
    drawing += drew ? 1 : 0;
    ++effects;
  }
  MESSAGE(drawing << " of " << effects << " effects draw");
  CHECK(drawing > effects / 2);
}

namespace {

// The attacker and the defender standing on the console's x: the defender at `defenderX` with a
// square `size` wide and tall, the attacker at 1000 with one 100 wide. The made-up breath's
// instance stays at the origin.
class StandingPair final : public dwe::game::battle::EffectBodies {
public:
  StandingPair(std::int32_t defenderX, std::int32_t size) : defenderX_(defenderX), size_(size) {}

  [[nodiscard]] std::optional<dwe::game::battle::EffectBody>
  body(dwe::game::battle::FighterId fighter) const override {
    if (fighter > 1) {
      return std::nullopt;
    }
    return dwe::game::battle::EffectBody{
        .species = "hero",
        .location = {.x = fighter == 0 ? 1000 : defenderX_, .y = 0, .z = 0},
        .rotationY = 0,
        .radius = fighter == 0 ? 100 : size_,
        .height = fighter == 0 ? 100 : size_,
        .standing = true};
  }
  [[nodiscard]] std::optional<dwe::game::battle::EffectMatrix>
  node(dwe::game::battle::FighterId /*fighter*/, std::int32_t /*index*/) const override {
    return std::nullopt;
  }
  [[nodiscard]] std::vector<dwe::game::battle::FighterId> fighters() const override {
    return {0, 1};
  }

private:
  std::int32_t defenderX_;
  std::int32_t size_;
};

constexpr std::int32_t Wide = 4000;
constexpr std::int32_t FarOff = 100000;

// The made-up breath with its instance at the origin and a check box of no size (the instance's
// place at instance+4 and the extent at base+0x20, which the breath leaves unset).
EffectScript placedBreath() {
  const std::string script = std::string{ScriptYaml} + R"(- at: "0x0014"
  kind: variable
  width: 4
  value: 0
- at: "0x001c"
  kind: variable
  width: 4
  value: 0
- at: "0x0020"
  kind: variable
  width: 4
  value: 0
- at: "0x0024"
  kind: variable
  width: 4
  value: 0
- at: "0x0028"
  kind: variable
  width: 4
  value: 0
)";
  const auto effect = dwe::core::parseYaml(EffectYaml);
  const auto parsed = dwe::core::parseYaml(script);
  REQUIRE(effect.has_value());
  REQUIRE(parsed.has_value());
  auto read = dwe::assets::readEffectScript(*effect, *parsed);
  REQUIRE_MESSAGE(read.has_value(), (read ? std::string{} : read.error().message));
  return std::move(*read);
}

} // namespace

TEST_CASE("touching bodies, an effect's box check hits a fighter in reach and not one far off") {
  for (const auto& [defenderX, hits] : {std::pair{0, true}, std::pair{FarOff, false}}) {
    RecordingSounds sounds;
    EffectRunner runner{sounds};
    const StandingPair bodies{defenderX, Wide};
    runner.setBodies(bodies);
    runner.touchBodies(true);
    const std::size_t slot = runner.load(placedBreath());
    runner.start(slot, 0, 1);
    sounds.log.clear();
    runner.tick();
    // aabb.c: findAABBHitEntity: the fighter's box around the instance, the attacker never itself.
    CHECK(runner.touched() == (hits ? std::vector<dwe::game::battle::FighterId>{1}
                                    : std::vector<dwe::game::battle::FighterId>{}));
    CHECK((sounds.log ==
           std::vector<Recorded>{{"play", 16}, {"stop", RecordingSounds::HeldVoice}}) == hits);
  }
}

TEST_CASE("a scripted flight lands on the frame the move's effect touches the defender") {
  const auto source = [](std::string_view move) -> std::optional<EffectScript> {
    if (move != "made_up_breath") {
      return std::nullopt;
    }
    return placedBreath();
  };
  const StandingPair near{0, Wide};
  const StandingPair far{FarOff, Wide};
  const dwe::game::battle::ScriptedFlights nearFlights{source, near};
  const dwe::game::battle::ScriptedFlights farFlights{source, far};
  CHECK(nearFlights.landingFrame("made_up_breath", 0, 1, std::nullopt) == std::optional{0});
  // The breath gives up on its third frame without touching anyone.
  CHECK_FALSE(farFlights.landingFrame("made_up_breath", 0, 1, std::nullopt).has_value());
  CHECK_FALSE(nearFlights.landingFrame("no_effect", 0, 1, std::nullopt).has_value());
  CHECK_FALSE(nearFlights.landingFrame("made_up_breath", 1, 1, std::nullopt).has_value());
}
