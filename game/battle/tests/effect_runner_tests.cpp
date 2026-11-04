#include <dwe/assets/effect_script.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/battle/effect_runner.hpp>
#include <dwe/game/battle/flights.hpp>
#include <dwe/ruleset/random.hpp>
#include <dwe/test_support/user_assets.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
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
  [[nodiscard]] dwe::game::battle::Point
  ground(const dwe::game::battle::EffectVector& at) const override {
    return {.x = at.x, .z = at.z};
  }

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
  // The battle's ground mirrors the console's z, as the game's does.
  [[nodiscard]] dwe::game::battle::Point
  ground(const dwe::game::battle::EffectVector& at) const override {
    return {.x = at.x, .z = -at.z};
  }

private:
  std::int32_t defenderX_;
  std::int32_t size_;
};

constexpr std::int32_t Wide = 4000;
constexpr std::int32_t FarOff = 100000;

// The breath's box check, and a line check in its place (apply_line_hit: from the instance to the
// source, its height at base+0x20).
constexpr std::string_view BoxCheck = R"(- at: "0x0220"
  op: sub
  sub: check_collision
  args:
    extent: "&base+0x0020"
    hit: "&base+0x0000"
)";
constexpr std::string_view LineCheck = R"(- at: "0x0218"
  op: push_address
  address: "base+0x0024"
- at: "0x0220"
  op: sub
  sub: apply_line_hit
  args:
    height: "&base+0x0020"
    hit: "&base+0x0000"
    unused: "&base+0x0024"
)";

// The made-up breath with its instance at the origin and a check box of no size (the instance's
// place at instance+4 and the extent at base+0x20, which the breath leaves unset); with `line`, its
// check is a line to the source instead.
EffectScript placedBreath(bool line = false) {
  std::string checked{ScriptYaml};
  if (line) {
    checked.replace(checked.find(BoxCheck), BoxCheck.size(), LineCheck);
  }
  const std::string script = checked + R"(- at: "0x0014"
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
    REQUIRE(runner.touched().size() == (hits ? 1U : 0U));
    if (hits) {
      // The hit is registered at the instance, at the origin (BTL_applyBoxAttackHit's
      // addAttackObject).
      const dwe::game::battle::EffectTouch& touch = runner.touched().front();
      CHECK(touch.fighter == 1);
      CHECK(touch.at.x == 0);
      CHECK(touch.at.z == 0);
    }
    CHECK((sounds.log ==
           std::vector<Recorded>{{"play", 16}, {"stop", RecordingSounds::HeldVoice}}) == hits);
  }
}

TEST_CASE("touching bodies, a line check hits the fighter on its body toward the source") {
  RecordingSounds sounds;
  EffectRunner runner{sounds};
  // The source stands at x 1000 and the instance at the origin: the line crosses a defender of
  // radius 50 at x 500.
  const StandingPair bodies{500, 50};
  runner.setBodies(bodies);
  runner.touchBodies(true);
  const std::size_t slot = runner.load(placedBreath(true));
  runner.start(slot, 0, 1);
  runner.tick();
  // BTL_applyLineAttackHit registers the hit at BTL_calculateAttackHitPosition: the defender's
  // radius from its root toward the source, at its height.
  CHECK(runner.touched() ==
        std::vector<dwe::game::battle::EffectTouch>{
            {.fighter = 1, .at = {.x = 550, .y = -50, .z = 0}, .onBody = true}});
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
  const std::optional<dwe::game::battle::EffectLanding> landing =
      nearFlights.landing("made_up_breath", 0, 1, std::nullopt);
  REQUIRE(landing.has_value());
  CHECK(landing->frame == 0);
  // The point of the hit on the battle's ground: the box check's instance at the origin.
  CHECK(landing->at == std::optional{dwe::game::battle::Point{.x = 0, .z = 0}});
  // The breath gives up on its third frame without touching anyone.
  CHECK_FALSE(farFlights.landing("made_up_breath", 0, 1, std::nullopt).has_value());
  CHECK_FALSE(nearFlights.landing("no_effect", 0, 1, std::nullopt).has_value());
  CHECK_FALSE(nearFlights.landing("made_up_breath", 1, 1, std::nullopt).has_value());
}

TEST_CASE("with the battle's rolls an effect's random load takes one of the battle's draws") {
  // The original's effect scripts draw from the game's one rand() (efe.c: load_random), so with
  // the original's rolls the effects take their numbers from the battle's generator.
  constexpr std::string_view RandomEffectYaml = R"(move: "made_up_dice"
header:
  sub_effect_count: 1
)";
  constexpr std::string_view RandomScriptYaml = R"(start_script:
- label: "start"
  at: "0x0100"
  op: load_random
- at: "0x0110"
  op: return
init_script:
- label: "init"
  at: "0x0120"
  op: stop
)";
  const auto effect = dwe::core::parseYaml(RandomEffectYaml);
  const auto script = dwe::core::parseYaml(RandomScriptYaml);
  REQUIRE(effect.has_value());
  REQUIRE(script.has_value());
  auto read = dwe::assets::readEffectScript(*effect, *script);
  REQUIRE_MESSAGE(read.has_value(), (read ? std::string{} : read.error().message));

  RecordingSounds sounds;
  EffectRunner runner{sounds};
  dwe::ruleset::Random rolls = dwe::ruleset::Random::original(0x5CF299CBU);
  runner.useRolls(&rolls);
  runner.start(runner.load(*read));
  dwe::ruleset::Random expected = dwe::ruleset::Random::original(0x5CF299CBU);
  static_cast<void>(expected.next());
  CHECK(rolls.state() == expected.state());

  EffectRunner own{sounds};
  dwe::ruleset::Random untouched = dwe::ruleset::Random::original(0x5CF299CBU);
  own.start(own.load(*read));
  CHECK(untouched.state() == dwe::ruleset::Random::original(0x5CF299CBU).state());
}

TEST_CASE("with the battle's particles an effect's attacker particle takes one and its draws") {
  // battle_effect.c: BTL_addSourceEntityParticleFX (static_elect, every frame until it hits).
  constexpr std::string_view SparkEffectYaml = R"(move: "made_up_spark"
header:
  sub_effect_count: 1
)";
  constexpr std::string_view SparkScriptYaml = R"(start_script:
- label: "start"
  at: "0x0100"
  op: spawn
  count: 1
  table: "base+0x0010"
  stride: 28
  ordinal: 0
- at: "0x0102"
  op: push_immediate
  value: 12
- at: "0x0106"
  op: sub
  sub: add_source_particles
  args:
    timer: 12
- at: "0x0108"
  op: return
init_script:
- label: "init"
  at: "0x0120"
  op: stop
data:
- at: "0x0010"
  kind: instances
  count: 1
  stride: 28
  free: true
)";
  const auto effect = dwe::core::parseYaml(SparkEffectYaml);
  const auto script = dwe::core::parseYaml(SparkScriptYaml);
  REQUIRE(effect.has_value());
  REQUIRE(script.has_value());
  auto read = dwe::assets::readEffectScript(*effect, *script);
  REQUIRE_MESSAGE(read.has_value(), (read ? std::string{} : read.error().message));

  RecordingSounds sounds;
  EffectRunner runner{sounds};
  dwe::ruleset::Random rolls = dwe::ruleset::Random::original(0x5CF299CBU);
  dwe::game::battle::HitParticles particles;
  runner.useRolls(&rolls, &particles);
  runner.start(runner.load(*read), 0, 1);
  // The node's draw, then the particle's first tick (12 % 4 == 0): bone and offset.
  dwe::ruleset::Random expected = dwe::ruleset::Random::original(0x5CF299CBU);
  for (int draw = 0; draw < 3; ++draw) {
    static_cast<void>(expected.next());
  }
  CHECK(rolls.state() == expected.state());
  CHECK(particles.fighterParticles() == 1);
  CHECK(particles.fighterParticleSlots()[0]->fighter == 0);

  // Without the original's rolls the particle rolls the particles' own: the battle's are left.
  EffectRunner own{sounds};
  dwe::game::battle::HitParticles ownParticles;
  own.useRolls(nullptr, &ownParticles);
  own.start(own.load(*read), 0, 1);
  CHECK(ownParticles.fighterParticles() == 1);

  // Without the battle's particles nothing is put on the attacker.
  EffectRunner none{sounds};
  dwe::ruleset::Random untouched = dwe::ruleset::Random::original(0x5CF299CBU);
  none.useRolls(&untouched);
  none.start(none.load(*read), 0, 1);
  CHECK(untouched.state() == dwe::ruleset::Random::original(0x5CF299CBU).state());
}

TEST_CASE("a bone's matrix adds up its chain in the console's fixed point, as the original does") {
  using dwe::game::battle::BonePose;
  // Agumon's bones 1 to 4 holding Spit Fire's breath (clip 46 from its frame 10), read from the
  // original's PositionData; the matrices calculateBoneMatrix gave for bone 4 at two places.
  const std::vector<BonePose> chain{
      {.rotation = {.x = 0, .y = -1024, .z = 0}, .location = {.x = 0, .y = -110, .z = -50}},
      {.rotation = {.x = 0, .y = 0, .z = -1702}, .location = {.x = 5, .y = 0, .z = 0}},
      {.rotation = {.x = 0, .y = 0, .z = 255}, .location = {.x = 34, .y = 0, .z = 0}},
      {.rotation = {.x = 0, .y = 0, .z = -1334}, .location = {.x = 50, .y = 0, .z = 0}}};
  dwe::game::battle::EffectBody place;
  place.location = {.x = 278, .y = 0, .z = 3113};
  place.rotationY = 44;
  const dwe::game::battle::EffectMatrix facingDown = dwe::game::battle::boneMatrix(place, chain);
  // Exact maths puts it at (270.96, -167.05, 3008.70).
  CHECK(facingDown.t == dwe::game::battle::EffectVector{.x = 269, .y = -168, .z = 3006});
  CHECK(facingDown.m[0] == std::array{-118, -250, -4087});
  CHECK(facingDown.m[1] == std::array{3695, -1769, 0});
  CHECK(facingDown.m[2] == std::array{-1764, -3688, 276});

  place.location = {.x = 421, .y = 0, .z = 3418};
  place.rotationY = 871;
  const dwe::game::battle::EffectMatrix facingLeft = dwe::game::battle::boneMatrix(place, chain);
  CHECK(facingLeft.t == dwe::game::battle::EffectVector{.x = 317, .y = -168, .z = 3392});
  CHECK(facingLeft.m[0] == std::array{-1720, -3595, -953});
  CHECK(facingLeft.m[2] == std::array{-411, -861, 3984});
}

namespace {

// A made-up technique whose start puts a particle emitter of `kind` on its instance: strength 300
// (start offset 4800, speed 592, slowing 16), a colour and 3 frames.
EffectScript emitterScript(int kind) {
  constexpr std::string_view EmitterEffectYaml = R"(move: "made_up_charge"
header:
  sub_effect_count: 1
)";
  const std::string scriptYaml = std::string{R"(start_script:
- label: "start"
  at: "0x0100"
  op: spawn
  count: 1
  table: "base+0x0010"
  stride: 28
  ordinal: 0
- at: "0x0102"
  op: push_address
  address: "base+0x0120"
- at: "0x0104"
  op: push_address
  address: "base+0x0122"
- at: "0x0106"
  op: sub
  sub: set_sub_effect_scripts
  args:
    tick_script: "l_0120"
    render_script: "l_0122"
- at: "0x0108"
  op: sub
  sub: init_transform
- at: "0x010a"
  op: push_immediate
  value: )"} + std::to_string(kind) +
                                 R"(
- at: "0x010c"
  op: push_immediate
  value: 300
- at: "0x010e"
  op: push_address
  address: "base+0x0040"
- at: "0x0110"
  op: push_immediate
  value: 3
- at: "0x0112"
  op: sub
  sub: add_particle_emitter
  args:
    type: 0
    strength: 300
    color: "&base+0x0040"
    frames: 3
- at: "0x0114"
  op: return
- label: "l_0120"
  at: "0x0120"
  op: return
- label: "l_0122"
  at: "0x0122"
  op: return
init_script:
- label: "init"
  at: "0x0150"
  op: stop
data:
- at: "0x0010"
  kind: instances
  count: 1
  stride: 28
  free: true
- at: "0x0040"
  kind: color
  values: [300, 128, 64]
)";
  const auto effect = dwe::core::parseYaml(EmitterEffectYaml);
  const auto script = dwe::core::parseYaml(scriptYaml);
  REQUIRE(effect.has_value());
  REQUIRE(script.has_value());
  auto read = dwe::assets::readEffectScript(*effect, *script);
  REQUIRE_MESSAGE(read.has_value(), (read ? std::string{} : read.error().message));
  return std::move(*read);
}

std::int64_t lengthOf(const dwe::game::battle::EffectVector& v) {
  return std::llround(std::sqrt(static_cast<double>((static_cast<std::int64_t>(v.x) * v.x) +
                                                    (static_cast<std::int64_t>(v.y) * v.y) +
                                                    (static_cast<std::int64_t>(v.z) * v.z))));
}

} // namespace

TEST_CASE("a particle emitter sends one particle a frame toward its instance, drawing the "
          "battle's rand() three times for each") {
  using dwe::game::battle::EffectStreak;
  RecordingSounds sounds;
  EffectRunner runner{sounds};
  dwe::ruleset::Random rolls = dwe::ruleset::Random::original(0x5CF299CBU);
  runner.useRolls(&rolls);
  runner.start(runner.load(emitterScript(0)), 0, 1);
  dwe::ruleset::Random expected = dwe::ruleset::Random::original(0x5CF299CBU);

  std::vector<std::vector<EffectStreak>> frames;
  for (int frame = 0; frame < 4; ++frame) {
    runner.tick();
    runner.render();
    std::vector<EffectStreak> streaks;
    for (const auto& draw : runner.draws()) {
      if (const auto* streak = std::get_if<EffectStreak>(&draw.shape)) {
        streaks.push_back(*streak);
      }
    }
    frames.push_back(std::move(streaks));
    if (frame < 3) {
      for (int roll = 0; roll < 3; ++roll) {
        static_cast<void>(expected.next());
      }
    }
    CHECK(rolls.state() == expected.state());
  }
  // One particle more each frame; on its third frame the emitter ends and draws nothing.
  REQUIRE(frames.size() == 4);
  CHECK(frames[0].size() == 1);
  CHECK(frames[1].size() == 2);
  CHECK(frames[2].empty());
  CHECK(frames[3].empty());
  const EffectStreak& first = frames[0][0];
  // Colour bytes: 300 keeps 44.
  CHECK(first.color == dwe::game::battle::EffectVector{44, 128, 64});
  CHECK(first.glows);
  // After a frame it is 4208 of 4800 away: 14 sixteenths of the start offset's 300 units.
  CHECK(lengthOf(first.from) >= 260);
  CHECK(lengthOf(first.from) <= 264);
  // The next frame it is 4208 - 576 = 3632 away: 12 sixteenths.
  CHECK(lengthOf(frames[1][0].from) >= 223);
  CHECK(lengthOf(frames[1][0].from) <= 227);
}

TEST_CASE("a particle emitter of a line kind draws streaks, and its instance's end stops it") {
  using dwe::game::battle::EffectStreak;
  RecordingSounds sounds;
  EffectRunner runner{sounds};
  const std::size_t slot = runner.load(emitterScript(1));
  const std::optional<std::size_t> section = runner.start(slot, 0, 1);
  REQUIRE(section.has_value());
  runner.tick();
  runner.render();
  REQUIRE(runner.draws().size() == 1);
  const auto* streak = std::get_if<EffectStreak>(&runner.draws()[0].shape);
  REQUIRE(streak != nullptr);
  CHECK_FALSE(streak->glows);
  // Kind 1: from 16 / 16 to 4 / 16 of the particle's distance.
  CHECK(lengthOf(streak->from) > lengthOf(streak->to) * 3);
  runner.stop(slot, *section);
  runner.tick();
  runner.render();
  CHECK(runner.draws().empty());
}

namespace {

// A made-up technique whose start puts a cloud at a point.
EffectScript cloudScript() {
  constexpr std::string_view CloudEffectYaml = R"(move: "made_up_stomp"
header:
  sub_effect_count: 1
)";
  constexpr std::string_view CloudScriptYaml = R"(start_script:
- label: "start"
  at: "0x0100"
  op: push_address
  address: "base+0x0040"
- at: "0x0102"
  op: sub
  sub: add_cloud
  args:
    position: "&base+0x0040"
- at: "0x0104"
  op: return
init_script:
- label: "init"
  at: "0x0150"
  op: stop
data:
- at: "0x0040"
  kind: vector
  values: [120, -300, 900]
)";
  const auto effect = dwe::core::parseYaml(CloudEffectYaml);
  const auto script = dwe::core::parseYaml(CloudScriptYaml);
  REQUIRE(effect.has_value());
  REQUIRE(script.has_value());
  auto read = dwe::assets::readEffectScript(*effect, *script);
  REQUIRE_MESSAGE(read.has_value(), (read ? std::string{} : read.error().message));
  return std::move(*read);
}

} // namespace

TEST_CASE("a cloud rises from the ground under its point for 14 frames, growing and fading") {
  using dwe::game::battle::EffectCloud;
  RecordingSounds sounds;
  EffectRunner runner{sounds};
  runner.start(runner.load(cloudScript()), 0, 1);
  std::vector<EffectCloud> clouds;
  for (int frame = 0; frame < 16; ++frame) {
    runner.tick();
    runner.render();
    for (const auto& draw : runner.draws()) {
      if (const auto* cloud = std::get_if<EffectCloud>(&draw.shape)) {
        clouds.push_back(*cloud);
      }
    }
  }
  // Added by the start, a frame older on each tick, gone on its 14th: frames 1 to 13 are drawn.
  REQUIRE(clouds.size() == 13);
  CHECK(clouds.front() ==
        EffectCloud{
            .position = {.x = 120, .y = -54, .z = 900}, .u = 0, .shade = 0x80, .scale = 0x2ee0});
  CHECK(clouds.back() ==
        EffectCloud{
            .position = {.x = 120, .y = -99, .z = 900}, .u = 0x60, .shade = 0x40, .scale = 0x6400});
}

namespace {

// A made-up technique whose start puts a 4-frame flash of mode `mode` at a point: size 0 to 4096,
// colour (200, 100, 0) to (200, 100, 0).
EffectScript flashScript(int mode) {
  constexpr std::string_view FlashEffectYaml = R"(move: "made_up_glare"
header:
  sub_effect_count: 1
)";
  const std::string scriptYaml = std::string{R"(start_script:
- label: "start"
  at: "0x0100"
  op: push_immediate
  value: )"} + std::to_string(mode) +
                                 R"(
- at: "0x0102"
  op: push_address
  address: "base+0x0040"
- at: "0x0104"
  op: push_immediate
  value: 33
- at: "0x0106"
  op: push_immediate
  value: 4
- at: "0x0108"
  op: push_immediate
  value: 0
- at: "0x010a"
  op: push_immediate
  value: 4096
- at: "0x010c"
  op: push_address
  address: "base+0x0050"
- at: "0x010e"
  op: push_address
  address: "base+0x0050"
- at: "0x0110"
  op: sub
  sub: create_flash
  args:
    mode: )" + std::to_string(mode) +
                                 R"(
    position: "&base+0x0040"
    fixed_depth: 33
    duration: 4
    scale_start: 0
    scale_end: 4096
    color_start: "&base+0x0050"
    color_end: "&base+0x0050"
- at: "0x0112"
  op: return
init_script:
- label: "init"
  at: "0x0150"
  op: stop
data:
- at: "0x0040"
  kind: vector
  values: [120, -300, 900]
- at: "0x0050"
  kind: color
  values: [200, 100, 0]
)";
  const auto effect = dwe::core::parseYaml(FlashEffectYaml);
  const auto script = dwe::core::parseYaml(scriptYaml);
  REQUIRE(effect.has_value());
  REQUIRE(script.has_value());
  auto read = dwe::assets::readEffectScript(*effect, *script);
  REQUIRE_MESSAGE(read.has_value(), (read ? std::string{} : read.error().message));
  return std::move(*read);
}

std::vector<dwe::game::battle::EffectFlash> flashFrames(int mode, int frames) {
  RecordingSounds sounds;
  EffectRunner runner{sounds};
  runner.start(runner.load(flashScript(mode)), 0, 1);
  std::vector<dwe::game::battle::EffectFlash> flashes;
  for (int frame = 0; frame < frames; ++frame) {
    runner.tick();
    runner.render();
    for (const auto& draw : runner.draws()) {
      if (const auto* flash = std::get_if<dwe::game::battle::EffectFlash>(&draw.shape)) {
        flashes.push_back(*flash);
      }
    }
  }
  return flashes;
}

} // namespace

TEST_CASE("a flash grows and fades over its frames on the light sheet's quarter of its mode") {
  using dwe::game::battle::EffectBlend;
  const std::vector<dwe::game::battle::EffectFlash> flashes = flashFrames(0, 6);
  // Made by the start and a frame older on each tick, it ends on its 4th: frames 1 to 3 draw.
  REQUIRE(flashes.size() == 3);
  CHECK(flashes[0].position == dwe::game::battle::EffectVector{120, -300, 900});
  CHECK(flashes[0].size == 1024);
  CHECK(flashes[2].size == 3072);
  CHECK(flashes[0].u == 0x40);
  CHECK(flashes[0].blend == EffectBlend::Add);
  CHECK(flashes[0].fixedDepth == 33);
  // The fade: _sin(lerp(0x80, 0x14)) = _sin(101) is about 0.95 on frame 1, about 0.62 on frame 3.
  CHECK(flashes[0].color.x >= 187);
  CHECK(flashes[0].color.x <= 190);
  CHECK(flashes[0].color.z == 0);
  CHECK(flashes[2].color.x < flashes[1].color.x);
  CHECK(flashes[1].color.x < flashes[0].color.x);
  // The swell: grows over the first 0x17 frames.
  CHECK(flashes[0].bump > 1);
  CHECK(flashes[1].bump > flashes[0].bump);
}

TEST_CASE("a flash of mode 1 takes the sheet's other quarter, and one of mode 0x20 subtracts") {
  CHECK(flashFrames(1, 2).at(0).u == 0);
  const std::vector<dwe::game::battle::EffectFlash> dark = flashFrames(0x20, 2);
  REQUIRE(dark.size() == 2);
  CHECK(dark[0].u == 0x40);
  CHECK(dark[0].blend == dwe::game::battle::EffectBlend::Subtract);
  // An unknown mode makes nothing.
  CHECK(flashFrames(2, 2).empty());
}
