#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/game/battle/effect_runner.hpp>
#include <dwe/game/battle/events.hpp>
#include <dwe/game/battle/moves.hpp>
#include <dwe/game/story/story_ports.hpp>
#include <dwe/session/battle_effects.hpp>
#include <dwe/test_support/user_assets.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "packages/game/battle/tests/fixture.hpp"

namespace {

using dwe::game::battle::BattleEvents;
using dwe::game::battle::EffectBodies;
using dwe::game::battle::EffectBody;
using dwe::game::battle::EffectDraw;
using dwe::game::battle::EffectMatrix;
using dwe::game::battle::EventKind;
using dwe::game::battle::FighterId;
using dwe::game::battle::MoveRef;

// The move of the test catalog the effects below play as.
constexpr std::string_view Move = "shot";

// A made-up spark: one instance in its pool, which draws a sprite every frame and never ends.
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
  op: push_address
  address: "base+0x0120"
- at: "0x0104"
  op: push_address
  address: "base+0x0130"
- at: "0x0106"
  op: sub
  sub: set_sub_effect_scripts
  args:
    tick_script: "l_0120"
    render_script: "l_0130"
- at: "0x0108"
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
  values: [50, 50, 80]
)";

class Silence final : public dwe::game::story::SoundPort {
public:
  void playSound(std::int64_t /*slot*/, std::int64_t /*sound*/) override {}
};

// The partner and enemy 1, 1000 units apart on the console's x and facing each other; their nodes
// are their places.
class Duel final : public EffectBodies {
public:
  explicit Duel(std::string species) : species_(std::move(species)) {}

  [[nodiscard]] std::optional<EffectBody> body(FighterId fighter) const override {
    if (fighter > 1) {
      return std::nullopt;
    }
    return EffectBody{.species = species_,
                      .location = {.x = fighter == 0 ? 0 : Distance, .y = 0, .z = 0},
                      .rotationY = fighter == 0 ? FacingPlusX : FacingMinusX,
                      .radius = Radius,
                      .height = Height,
                      .standing = true};
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

private:
  static constexpr std::int32_t Distance = 1000;
  static constexpr std::int32_t FacingPlusX = 3072;
  static constexpr std::int32_t FacingMinusX = 1024;
  static constexpr std::int32_t Radius = 100;
  static constexpr std::int32_t Height = 300;

  std::string species_;
};

// A pack with one effect, as effect/shot/; not movable, the mounted pack points into it.
class EffectPack {
public:
  EffectPack(std::string_view effect, std::string_view script) {
    memory_.addFile("base/pack.yaml", "id: base\nversion: 0.1.0\nlayer: base\n");
    memory_.addFile(std::format("base/effect/{}/effect.yaml", Move), effect);
    memory_.addFile(std::format("base/effect/{}/script.yaml", Move), script);
    const std::array<dwe::assets::PackLocation, 1> packs{
        dwe::assets::PackLocation{.fileSystem = &memory_,
                                  .root = "base",
                                  .fillsGaps = false,
                                  .origin = dwe::assets::PackOrigin::Engine}};
    auto vfs = dwe::assets::VirtualFileSystem::mount(packs);
    REQUIRE_MESSAGE(vfs.has_value(), (vfs ? std::string{} : vfs.error().message));
    files_ = std::make_unique<dwe::assets::VirtualFileSystem>(std::move(*vfs));
  }
  EffectPack(const EffectPack&) = delete;
  EffectPack& operator=(const EffectPack&) = delete;
  EffectPack(EffectPack&&) = delete;
  EffectPack& operator=(EffectPack&&) = delete;
  ~EffectPack() = default;

  [[nodiscard]] const dwe::assets::VirtualFileSystem& files() const { return *files_; }

private:
  dwe::assets::MemoryFileSystem memory_;
  std::unique_ptr<dwe::assets::VirtualFileSystem> files_;
};

std::string readText(const std::filesystem::path& path) {
  std::ifstream in{path};
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

// The attacks that start and the ones that land, by frame.
struct Script {
  std::map<int, std::vector<std::pair<FighterId, FighterId>>> starts; // attacker, target
  std::map<int, std::vector<FighterId>> landings;                     // whose attack landed
};

Script attacks(std::map<int, std::vector<std::pair<FighterId, FighterId>>> starts,
               std::map<int, std::vector<FighterId>> landings = {}) {
  return {.starts = std::move(starts), .landings = std::move(landings)};
}

// How many sprites each effect slot drew over the frames (the slots load in the order the fighters
// first attack, so the first fighter's is the lowest).
std::map<std::size_t, std::size_t> drawsBySlot(const dwe::assets::VirtualFileSystem& files,
                                               const EffectBodies& bodies, const Script& script,
                                               int frames) {
  Silence sound;
  dwe::session::BattleEffects effects{files, sound};
  effects.setBodies(bodies);
  const MoveRef move = battle_test::refOf(battle_test::testCatalog(), Move);
  std::map<std::size_t, std::size_t> counts;
  for (int frame = 0; frame < frames; ++frame) {
    BattleEvents events;
    if (const auto found = script.starts.find(frame); found != script.starts.end()) {
      for (const auto& [attacker, target] : found->second) {
        dwe::game::battle::BattleEvent start;
        start.kind = EventKind::AttackStarted;
        start.fighter = attacker;
        start.other = target;
        start.move = move;
        events.push_back(start);
      }
    }
    if (const auto found = script.landings.find(frame); found != script.landings.end()) {
      for (const FighterId attacker : found->second) {
        dwe::game::battle::BattleEvent landed;
        landed.kind = EventKind::AttackLanded;
        landed.fighter = attacker == 0 ? FighterId{1} : FighterId{0};
        landed.other = attacker;
        events.push_back(landed);
      }
    }
    effects.frame(events, battle_test::testCatalog());
    for (const EffectDraw& draw : effects.draws()) {
      ++counts[draw.slot];
    }
  }
  return counts;
}

// The sprites the first fighter's effect drew: those of the lowest slot.
std::size_t firstOf(const std::map<std::size_t, std::size_t>& counts) {
  return counts.empty() ? 0 : counts.begin()->second;
}

// An extracted effect of the user's content, as the pack's effect/shot/; nothing without content.
std::optional<std::pair<std::string, std::string>> extracted(std::string_view move) {
  const std::optional<std::filesystem::path> content = dwe::test_support::userContent();
  if (!content) {
    return std::nullopt;
  }
  const std::filesystem::path folder = *content / "base" / "effect" / move;
  return std::pair{readText(folder / "effect.yaml"), readText(folder / "script.yaml")};
}

} // namespace

TEST_CASE("two fighters using the same move each run their own copy of its effect") {
  // btl.c: loadBattleData loads an effect for each move of each fighter (effectSlot[j]): the
  // spark's pool has one instance, so a shared copy would draw one spark, not one per fighter.
  const EffectPack pack{SparkEffectYaml, SparkScriptYaml};
  const Duel bodies{"agum"};
  constexpr int Frames = 5;
  const auto counts = drawsBySlot(pack.files(), bodies, attacks({{0, {{0, 1}, {1, 0}}}}), Frames);
  CHECK(counts == std::map<std::size_t, std::size_t>{{0, Frames}, {1, Frames}});
}

TEST_CASE("another fighter's hit or start does not cut an effect of the same move") {
  // A fighter's hit lands on its own effect only (BTL_applyBoxAttackHit answers the effect that
  // checked), and a start clears only the starting fighter's: with one copy shared, Spit Fire lost
  // its projectile and Prominence Beam ended early when the enemy's same move started and landed
  // while they ran.
  constexpr int Frames = 100;
  for (const std::string_view move : {"spit_fire", "prominence_beam"}) {
    CAPTURE(move);
    const auto files = extracted(move);
    if (!files) {
      MESSAGE("DWE_CONTENT is not set: skipping the test on extracted content");
      return;
    }
    const EffectPack pack{files->first, files->second};
    const Duel bodies{"tyra"};
    const std::size_t alone =
        firstOf(drawsBySlot(pack.files(), bodies, attacks({{0, {{0, 1}}}}), Frames));
    REQUIRE(alone > 0);
    const auto both = drawsBySlot(pack.files(), bodies,
                                  attacks({{0, {{0, 1}}}, {12, {{1, 0}}}}, {{13, {1}}}), Frames);
    CHECK(firstOf(both) == alone);
  }
}

TEST_CASE("two Blaze Blasts at once both draw in full") {
  constexpr int Frames = 150;
  const auto files = extracted("blaze_blast");
  if (!files) {
    MESSAGE("DWE_CONTENT is not set: skipping the test on extracted content");
    return;
  }
  const EffectPack pack{files->first, files->second};
  const Duel bodies{"tyra"};
  const std::size_t first =
      firstOf(drawsBySlot(pack.files(), bodies, attacks({{0, {{0, 1}}}}), Frames));
  const std::size_t second =
      firstOf(drawsBySlot(pack.files(), bodies, attacks({{8, {{1, 0}}}}), Frames));
  REQUIRE(first > 0);
  REQUIRE(second > 0);
  const auto both =
      drawsBySlot(pack.files(), bodies, attacks({{0, {{0, 1}}}, {8, {{1, 0}}}}), Frames);
  CHECK(both == std::map<std::size_t, std::size_t>{{0, first}, {1, second}});
}
