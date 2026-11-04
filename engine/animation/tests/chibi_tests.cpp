#include <dwe/animation/animation_player.hpp>
#include <dwe/animation/chibi.hpp>
#include <dwe/animation/clip_sampling.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/pak.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/test_support/user_assets.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

using dwe::animation::AnimationPlayer;
using dwe::animation::BodyPart;
using dwe::animation::BodyShape;
using dwe::animation::buildChibiRig;
using dwe::animation::ChibiOverride;
using dwe::animation::ChibiRig;
using dwe::animation::classifyChibi;
using dwe::animation::computeChibiJointMatrices;
using dwe::animation::computeJointMatrices;
using dwe::animation::lowestPoint;
using dwe::assets::ModelData;
using dwe::assets::NodeTransform;

namespace {

// A made-up upright body, Y up and facing +Z: bone 0 the entity's root, 1 the body's root at hip
// height (100), 2 the pelvis, 3 the chest, 4 the head, 5-7 and 8-10 the arms (upper arm, forearm,
// hand), 11-13 and 14-16 the legs (thigh, shin, foot). Every bone with a mesh has a small box
// below or above its joint, so the feet's lowest point is on the ground (0).
struct Builder {
  ModelData model;

  std::uint32_t bone(std::optional<std::uint32_t> parent, glm::vec3 at, glm::vec3 boxFrom,
                     glm::vec3 boxTo) {
    const auto index = static_cast<std::uint32_t>(model.nodes.size());
    const glm::vec3 parentAt =
        parent ? glm::vec3{glm::inverse(model.skin->inverseBindMatrices[*parent])[3]}
               : glm::vec3{0.0F};
    model.nodes.push_back({.name = std::format("bone_{:02}", index),
                           .parent = parent,
                           .rest = {.translation = at - parentAt}});
    model.nodeOrder.push_back(index);
    model.skin->joints.push_back(index);
    model.skin->inverseBindMatrices.push_back(glm::translate(glm::mat4{1.0F}, -at));
    if (boxFrom != boxTo) {
      dwe::assets::ModelPrimitive& primitive = model.primitives.front();
      for (int corner = 0; corner < 8; ++corner) {
        const glm::vec3 p{(corner & 1) != 0 ? boxTo.x : boxFrom.x,
                          (corner & 2) != 0 ? boxTo.y : boxFrom.y,
                          (corner & 4) != 0 ? boxTo.z : boxFrom.z};
        primitive.vertices.push_back(
            {.position = p, .joints = {static_cast<std::uint16_t>(index), 0, 0, 0}});
      }
    }
    return index;
  }
};

ModelData madeUpBiped() {
  Builder b;
  b.model.skin = dwe::assets::ModelSkin{};
  b.model.primitives.emplace_back();
  const glm::vec3 none{0.0F};
  const auto root = b.bone(std::nullopt, {0, 0, 0}, none, none);
  const auto body = b.bone(root, {0, 100, 0}, none, none);
  const auto pelvis = b.bone(body, {0, 100, 0}, {-25, 85, -15}, {25, 110, 15});
  const auto chest = b.bone(body, {0, 100, 0}, {-30, 110, -15}, {30, 160, 15});
  b.bone(chest, {0, 160, 0}, {-20, 160, -20}, {20, 200, 25});
  for (const float side : {1.0F, -1.0F}) {
    const auto shoulder =
        b.bone(chest, {side * 35, 155, 0}, {side * 30, 120, -6}, {side * 40, 155, 6});
    const auto elbow =
        b.bone(shoulder, {side * 35, 120, 0}, {side * 30, 90, -6}, {side * 40, 120, 6});
    b.bone(elbow, {side * 35, 90, 0}, {side * 30, 75, -8}, {side * 40, 90, 8});
  }
  for (const float side : {1.0F, -1.0F}) {
    const auto hip = b.bone(pelvis, {side * 15, 85, 0}, {side * 10, 45, -7}, {side * 20, 85, 7});
    const auto knee = b.bone(hip, {side * 15, 45, 0}, {side * 10, 10, -7}, {side * 20, 45, 7});
    b.bone(knee, {side * 15, 10, 0}, {side * 8, 0, -8}, {side * 22, 10, 20});
  }
  // A walk: the body's root bobs 4 units and the thighs swing 20 degrees, 10 ticks a round; the
  // root moves 10 units a tick.
  constexpr float TicksPerSecond = 20.0F;
  dwe::assets::AnimationClip walk;
  walk.name = "anim_02";
  walk.id = 2;
  walk.ticksPerSecond = 20;
  walk.tickCount = 11;
  walk.frameCount = 11;
  walk.duration = 10 / TicksPerSecond;
  walk.loops = {{.firstTick = 0, .lastTick = 10, .count = std::nullopt}};
  walk.endsInLoop = true;
  walk.repeat =
      dwe::assets::ClipRepeat{.start = 0, .end = 10 / TicksPerSecond, .gap = 1 / TicksPerSecond};
  walk.rootMotionNode = 0;
  std::vector<float> times;
  std::vector<glm::vec4> moved;
  std::vector<glm::vec4> bob;
  std::vector<glm::vec4> swing;
  std::vector<glm::vec4> swingBack;
  for (int t = 0; t <= 10; ++t) {
    const float phase = static_cast<float>(t) / 10.0F * 2.0F * 3.14159265F;
    times.push_back(static_cast<float>(t) / TicksPerSecond);
    moved.emplace_back(0, 0, 10 * t, 0);
    bob.emplace_back(0, 100 + 4 * std::cos(2 * phase), 0, 0);
    const glm::quat q = glm::angleAxis(glm::radians(20.0F * std::sin(phase)), glm::vec3{1, 0, 0});
    swing.emplace_back(q.x, q.y, q.z, q.w);
    swingBack.emplace_back(-q.x, q.y, q.z, q.w);
  }
  using dwe::assets::ChannelPath;
  walk.channels.push_back(
      {.node = 0, .path = ChannelPath::Translation, .times = times, .values = moved});
  walk.channels.push_back(
      {.node = 1, .path = ChannelPath::Translation, .times = times, .values = bob});
  walk.channels.push_back(
      {.node = 11, .path = ChannelPath::Rotation, .times = times, .values = swing});
  walk.channels.push_back(
      {.node = 14, .path = ChannelPath::Rotation, .times = times, .values = swingBack});
  b.model.clips.push_back(walk);
  return b.model;
}

struct Poses {
  std::vector<glm::mat4> original;
  std::vector<glm::mat4> chibi;
};

Poses jointsOf(const ModelData& model, const ChibiRig& rig, std::span<const NodeTransform> pose) {
  std::vector<glm::mat4> world(model.nodes.size());
  Poses out{.original = std::vector<glm::mat4>(model.skin->joints.size()),
            .chibi = std::vector<glm::mat4>(model.skin->joints.size())};
  computeJointMatrices(model, pose, world, out.original);
  computeChibiJointMatrices(model, rig, pose, out.original, world, out.chibi);
  return out;
}

// The height of a joint's own vertices once skinned.
float heightOf(const ModelData& model, std::span<const glm::mat4> joints, std::uint32_t joint) {
  float low = 1e9F;
  float high = -1e9F;
  for (const auto& vertex : model.primitives.front().vertices) {
    if (vertex.joints[0] == joint) {
      const float y = (joints[joint] * glm::vec4{vertex.position, 1.0F}).y;
      low = std::min(low, y);
      high = std::max(high, y);
    }
  }
  return high - low;
}

} // namespace

TEST_CASE("chibi: a made-up upright body is a biped with two arms, two legs and a head") {
  const ModelData model = madeUpBiped();
  const auto found = classifyChibi(model);
  CHECK(found.shape == BodyShape::Biped);
  CHECK(found.legs == 2);
  CHECK(found.arms == 2);
  CHECK(found.head == std::optional<std::uint32_t>{4});
  CHECK(found.parts[2] == BodyPart::Trunk);
  CHECK(found.parts[3] == BodyPart::Trunk);
  CHECK(found.parts[6] == BodyPart::Arm);
  CHECK(found.parts[12] == BodyPart::Leg);
}

TEST_CASE("chibi: the head grows, the limbs shrink and the feet stay on the ground") {
  const ModelData model = madeUpBiped();
  const ChibiRig rig = buildChibiRig(model);
  REQUIRE(rig.active);
  std::vector<NodeTransform> rest(model.nodes.size());
  dwe::animation::restPose(model, rest);
  const Poses at = jointsOf(model, rig, rest);
  CHECK(lowestPoint(model, at.chibi) == doctest::Approx(lowestPoint(model, at.original)));
  CHECK(heightOf(model, at.chibi, 4) ==
        doctest::Approx(heightOf(model, at.original, 4) * rig.proportions.head));
  CHECK(heightOf(model, at.chibi, 12) ==
        doctest::Approx(heightOf(model, at.original, 12) * rig.proportions.leg));
  CHECK(heightOf(model, at.chibi, 3) ==
        doctest::Approx(heightOf(model, at.original, 3) * rig.proportions.torso));
  // The whole model is shorter than the original but the head is a bigger share of it.
  const auto top = [&](std::span<const glm::mat4> joints) {
    float high = -1e9F;
    for (const auto& vertex : model.primitives.front().vertices) {
      high = std::max(high, (joints[vertex.joints[0]] * glm::vec4{vertex.position, 1.0F}).y);
    }
    return high;
  };
  CHECK(heightOf(model, at.chibi, 4) / top(at.chibi) >
        1.5F * heightOf(model, at.original, 4) / top(at.original));
}

TEST_CASE("chibi: walking, the feet stay on the ground and the animation and root motion are the "
          "original's") {
  const ModelData model = madeUpBiped();
  const ChibiRig rig = buildChibiRig(model);
  AnimationPlayer player{model};
  REQUIRE(player.play(2U));
  std::vector<glm::mat4> world(model.nodes.size());
  std::vector<glm::mat4> chibi(model.skin->joints.size());
  float worst = 0.0F;
  glm::vec3 moved{0.0F};
  for (int frame = 0; frame < 120; ++frame) {
    moved += player.advance(1.0F / 60.0F);
    computeChibiJointMatrices(model, rig, player.pose(), player.jointMatrices(), world, chibi);
    for (const glm::mat4& joint : chibi) {
      REQUIRE(std::isfinite(joint[3][0] + joint[3][1] + joint[3][2]));
    }
    worst = std::max(
        worst, std::abs(lowestPoint(model, chibi) - lowestPoint(model, player.jointMatrices())));
  }
  // The lowest point is the original's on every frame.
  CHECK(worst < 0.01F);
  // The style draws only: the player's pose, matrices and root motion are untouched.
  AnimationPlayer plain{model};
  REQUIRE(plain.play(2U));
  glm::vec3 plainMoved{0.0F};
  for (int frame = 0; frame < 120; ++frame) {
    plainMoved += plain.advance(1.0F / 60.0F);
  }
  CHECK(plainMoved == moved);
  CHECK(std::ranges::equal(plain.jointMatrices(), player.jointMatrices()));
}

TEST_CASE("chibi: an override turns a species off, or sets its shape, head, parts and factors") {
  const auto read = dwe::animation::parseChibiOverrides(R"(species:
  abcd: {off: true}
  efgh:
    shape: quadruped
    head: 4
    parts: {5: wing, 11: tail}
    scale: {head: 2, leg: 0.5}
)");
  REQUIRE_MESSAGE(read.has_value(), (read ? "" : read.error().message));
  CHECK(read->at("abcd").off);
  const auto& efgh = read->at("efgh");
  CHECK(efgh.shape == std::optional{BodyShape::Quadruped});
  CHECK(efgh.head == std::optional<std::uint32_t>{4});
  CHECK(efgh.parts.at(5) == BodyPart::Wing);
  CHECK(efgh.scale.at("leg") == 0.5F);

  const ModelData model = madeUpBiped();
  CHECK_FALSE(buildChibiRig(model, &read->at("abcd")).active);
  const ChibiRig rig = buildChibiRig(model, &efgh);
  CHECK(rig.classification.shape == BodyShape::Quadruped);
  CHECK(rig.classification.parts[6] == BodyPart::Wing);
  CHECK(rig.classification.parts[12] == BodyPart::Tail);
  CHECK(rig.proportions.head == 2.0F);

  for (const char* bad :
       {"species: {abcd: {shape: blob}}", "species: {abcd: {scale: {head: 9}}}",
        "species: {abcd: {parts: {x: leg}}}", "kinds: {}", "species: {abcd: {colour: red}}"}) {
    CAPTURE(bad);
    CHECK_FALSE(dwe::animation::parseChibiOverrides(bad).has_value());
  }
}

TEST_CASE("chibi: the game pack's overrides parse") {
  std::ifstream file{std::filesystem::path{DWE_GAME_PACK_DIR} / "models/chibi.yaml"};
  REQUIRE(file.good());
  std::stringstream text;
  text << file.rdbuf();
  const auto read = dwe::animation::parseChibiOverrides(text.str());
  CHECK_MESSAGE(read.has_value(), (read ? "" : read.error().message));
}

// Runs only when DWE_CONTENT names a content folder: the test species are found as the body they
// are, and on every frame of their idle and walk the chibi's lowest point stays near the
// original's.
TEST_CASE(
    "the user's disc, extracted: chibi detection and feet on the ground for the test species") {
  const std::optional<std::filesystem::path> content = dwe::test_support::userContent();
  if (!content) {
    MESSAGE("DWE_CONTENT is not set: skipping the test on the extracted content");
    return;
  }
  const dwe::assets::DiskFileSystem disk;
  auto packs = dwe::assets::findPacks(disk, *content);
  REQUIRE(packs.has_value());
  const auto opened = dwe::assets::openPacks(disk, packs->packs, dwe::assets::PackOrigin::Engine);
  REQUIRE(opened.has_value());
  const auto vfs = dwe::assets::VirtualFileSystem::mount(opened->locations);
  REQUIRE(vfs.has_value());

  struct Expected {
    const char* species;
    BodyShape shape;
    std::uint32_t head;
  };
  // The player (boys), Agumon, Angemon, Andromon, Garurumon, Birdramon and Seadramon.
  for (const Expected& expected :
       {Expected{"boys", BodyShape::Biped, 3}, Expected{"agum", BodyShape::Biped, 3},
        Expected{"ange", BodyShape::Biped, 3}, Expected{"andr", BodyShape::Biped, 3},
        Expected{"garu", BodyShape::Quadruped, 4}, Expected{"bird", BodyShape::Winged, 4},
        Expected{"sead", BodyShape::Serpent, 3}}) {
    const std::string species = expected.species;
    CAPTURE(species);
    const auto model = dwe::assets::loadModel(
        *vfs, *dwe::assets::AssetId::parse(std::format("digimon/{}/model", expected.species)));
    REQUIRE(model.has_value());
    const ChibiRig rig = buildChibiRig(*model);
    REQUIRE(rig.active);
    std::string parts;
    for (const BodyPart part : rig.classification.parts) {
      parts += std::format(" {}", dwe::animation::nameOf(part));
    }
    MESSAGE(std::format("{}:{} | head {} hand {} arm {} leg {} tail {} wing {}{}", species, parts,
                        rig.proportions.head, rig.proportions.hand, rig.proportions.arm,
                        rig.proportions.leg, rig.proportions.tail, rig.proportions.wing,
                        rig.lift ? std::format(" | flies, lift {:.1f}", *rig.lift) : ""));
    CHECK(rig.classification.shape == expected.shape);
    CHECK(rig.classification.head == std::optional{expected.head});
    std::vector<NodeTransform> rest(model->nodes.size());
    dwe::animation::restPose(*model, rest);
    const Poses at = jointsOf(*model, rig, rest);
    const float height = [&] {
      float high = -1e9F;
      for (const auto& primitive : model->primitives) {
        for (const auto& vertex : primitive.vertices) {
          high = std::max(high, vertex.position.y);
        }
      }
      return high - lowestPoint(*model, at.original);
    }();
    // At rest nothing goes lower than the original's lowest point (an arm may come 1 unit closer).
    CHECK(lowestPoint(*model, at.chibi) > lowestPoint(*model, at.original) - 1.5F);
    const auto groundOf = [&](std::span<const glm::mat4> joints) {
      float low = 1e9F;
      for (const auto& point : rig.ground) {
        low = std::min(low, (joints[point.joint] * glm::vec4{point.position, 1.0F}).y);
      }
      return low;
    };
    for (const std::uint32_t clip : {0U, 2U}) {
      CAPTURE(clip);
      AnimationPlayer player{*model};
      if (!player.play(clip)) {
        continue;
      }
      std::vector<glm::mat4> world(model->nodes.size());
      std::vector<glm::mat4> chibi(model->skin->joints.size());
      float worst = 0.0F;
      float lowest = 1e9F;
      float originalLowest = 1e9F;
      for (int frame = 0; frame < 120; ++frame) {
        std::ignore = player.advance(1.0F / 60.0F);
        computeChibiJointMatrices(*model, rig, player.pose(), player.jointMatrices(), world, chibi);
        if (!rig.lift) {
          worst = std::max(worst, std::abs(groundOf(chibi) - groundOf(player.jointMatrices())));
        }
        lowest = std::min(lowest, lowestPoint(*model, chibi));
        originalLowest = std::min(originalLowest, lowestPoint(*model, player.jointMatrices()));
      }
      // The feet are as high as the original's on every frame, and nothing goes lower than the
      // original's lowest.
      CHECK(worst < 1e-3F * height);
      CHECK(lowest > originalLowest - (0.02F * height));
    }
  }

  // Every species, with the game pack's overrides: a summary of what the detection finds, for the
  // log. Every override names a species there is, and the ones that fix a part do.
  std::ifstream file{std::filesystem::path{DWE_GAME_PACK_DIR} / "models/chibi.yaml"};
  std::stringstream text;
  text << file.rdbuf();
  const auto overrides = dwe::animation::parseChibiOverrides(text.str());
  REQUIRE(overrides.has_value());
  for (const auto& [species, override] : *overrides) {
    CAPTURE(species);
    CHECK(std::filesystem::exists(*content / "base/digimon" / species / "model.glb"));
  }
  std::string summary;
  for (const auto& entry : std::filesystem::directory_iterator{*content / "base/digimon"}) {
    const std::string species = entry.path().filename().string();
    const auto model = dwe::assets::loadModel(
        *vfs, *dwe::assets::AssetId::parse(std::format("digimon/{}/model", species)));
    if (!model) {
      continue;
    }
    const auto override = overrides->find(species);
    const ChibiOverride* given = override != overrides->end() ? &override->second : nullptr;
    const auto found = classifyChibi(*model, given);
    if (species == "etem") {
      CHECK(found.head == std::optional<std::uint32_t>{4});
    }
    if (species == "jiji") {
      CHECK(found.arms == 2);
      CHECK(found.wings == 0);
    }
    summary += std::format("{} {} legs {} arms {} wings {} tails {} head {}\n", species,
                           dwe::animation::nameOf(found.shape), found.legs, found.arms, found.wings,
                           found.tails, found.head ? int(*found.head) : -1);
  }
  MESSAGE(summary);
}
