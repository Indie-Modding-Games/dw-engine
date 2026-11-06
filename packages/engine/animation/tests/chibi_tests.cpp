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
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

using dwe::animation::AnimationPlayer;
using dwe::animation::BodyPart;
using dwe::animation::BodyRatio;
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

std::string gamePackText(std::string_view path) {
  std::ifstream file{std::filesystem::path{DWE_GAME_PACK_DIR} / path};
  std::stringstream text;
  text << file.rdbuf();
  return text.str();
}

// The game pack's profile of the original models' proportions (models/chibi_profile.yaml).
const dwe::animation::ChibiProfile& gameProfile() {
  static const dwe::animation::ChibiProfile profile = [] {
    const auto read = dwe::animation::parseChibiProfile(gamePackText("models/chibi_profile.yaml"));
    return read ? *read : dwe::animation::ChibiProfile{};
  }();
  return profile;
}

constexpr std::string_view ProfileHeader =
    "# Estilo chibi: las proporciones de los modelos originales (cada especie y el personaje del jugador), que son\n"
    "# la referencia del estilo. Generado por la prueba «the chibi profile is the original models' proportions» de\n"
    "# packages/engine/animation/tests/chibi_tests.cpp sobre el contenido extraído; no se edita a mano. Por forma del cuerpo:\n"
    "# cuántos modelos distintos y, por proporción, su rango (del menor al mayor de los originales), la mediana y\n"
    "# cuántos modelos la tienen. head y torso: tamaño de la cabeza y largo del torso / altura; head_size: tamaño\n"
    "# de la cabeza / largo del torso; head_width: ancho de la cabeza / ancho del tronco; arm, leg, neck, tail,\n"
    "# wing: alcance / largo del torso; hand, foot: tamaño / largo del torso (docs/modulos/engine/animation.md).\n";

// Builds a made-up model, Y up and facing +Z, its bones in the order they are added, each with a
// box (or none) of mesh in model space; the feet's lowest point is on the ground (0).
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
      box(index, boxFrom, boxTo);
    }
    return index;
  }

  // Another box of a bone's mesh.
  void box(std::uint32_t index, glm::vec3 boxFrom, glm::vec3 boxTo) {
    dwe::assets::ModelPrimitive& primitive = model.primitives.front();
    for (int corner = 0; corner < 8; ++corner) {
      const glm::vec3 p{(corner & 1) != 0 ? boxTo.x : boxFrom.x,
                        (corner & 2) != 0 ? boxTo.y : boxFrom.y,
                        (corner & 4) != 0 ? boxTo.z : boxFrom.z};
      primitive.vertices.push_back(
          {.position = p, .joints = {static_cast<std::uint16_t>(index), 0, 0, 0}});
    }
  }
};

void addWalk(ModelData& model, float bodyAt, std::uint32_t swing, std::uint32_t swingBack);

// An upright body's sizes: the hips' height (the legs' length), the torso's length (hips to
// shoulders), the head's height and its width as a share of it, the arms' reach, the trunk's half
// width, the limbs' half thickness and the feet's length.
struct Figure {
  float leg = 85.0F;
  float torso = 75.0F;
  float head = 40.0F;
  float headWidth = 1.0F;
  float arm = 80.0F;
  float half = 30.0F;
  float limb = 5.0F;
  float foot = 28.0F;
};

// A made-up upright body of these sizes: bone 0 the entity's root, 1 the body's root, 2 the
// pelvis, 3 the chest, 4 the head, 5-7 and 8-10 the arms (upper arm, forearm, hand), 11-13 and
// 14-16 the legs (thigh, shin, foot), each with a box. It walks: the body's root bobs 4 units and
// the thighs swing 20 degrees, 10 ticks a round; the root moves 10 units a tick.
ModelData biped(const Figure& f) {
  Builder b;
  b.model.skin = dwe::assets::ModelSkin{};
  b.model.primitives.emplace_back();
  const glm::vec3 none{0.0F};
  const float hip = f.leg;
  const float waist = hip + (f.torso / 3.0F);
  const float top = hip + f.torso;
  const float bodyAt = hip + (0.2F * f.torso);
  const auto root = b.bone(std::nullopt, {0, 0, 0}, none, none);
  const auto body = b.bone(root, {0, bodyAt, 0}, none, none);
  const auto pelvis = b.bone(body, {0, bodyAt, 0}, {-0.85F * f.half, hip, -f.half / 2},
                             {0.85F * f.half, waist, f.half / 2});
  const auto chest =
      b.bone(body, {0, bodyAt, 0}, {-f.half, waist, -f.half / 2}, {f.half, top, f.half / 2});
  const float headSide = f.head * f.headWidth / 2.0F;
  b.bone(chest, {0, top, 0}, {-headSide, top, -f.head / 2},
         {headSide, top + f.head, f.head * 0.6F});
  for (const float side : {1.0F, -1.0F}) {
    const float in = side * f.half;
    const float out = side * (f.half + (2 * f.limb));
    const float x = side * (f.half + f.limb);
    const float shoulderY = top - (0.07F * f.torso);
    const float elbowY = shoulderY - (0.44F * f.arm);
    const float wristY = shoulderY - (0.81F * f.arm);
    const auto shoulder = b.bone(chest, {x, shoulderY, 0}, {std::min(in, out), elbowY, -f.limb},
                                 {std::max(in, out), shoulderY, f.limb});
    const auto elbow = b.bone(shoulder, {x, elbowY, 0}, {std::min(in, out), wristY, -f.limb},
                              {std::max(in, out), elbowY, f.limb});
    b.bone(elbow, {x, wristY, 0}, {std::min(in, out), shoulderY - f.arm, -1.6F * f.limb},
           {std::max(in, out), wristY, 1.6F * f.limb});
  }
  for (const float side : {1.0F, -1.0F}) {
    const float x = side * f.half / 2;
    const float low = x - f.limb;
    const float high = x + f.limb;
    const float knee = 0.53F * hip;
    const float ankle = 0.12F * hip;
    const auto thigh = b.bone(pelvis, {x, hip, 0}, {std::min(low, high), knee, -f.limb},
                              {std::max(low, high), hip, f.limb});
    const auto shin = b.bone(thigh, {x, knee, 0}, {std::min(low, high), ankle, -f.limb},
                             {std::max(low, high), knee, f.limb});
    b.bone(shin, {x, ankle, 0}, {std::min(low, high) - 2, 0, -0.3F * f.foot},
           {std::max(low, high) + 2, ankle, 0.7F * f.foot});
  }
  addWalk(b.model, bodyAt, 11, 14);
  return b.model;
}

// The first made-up body: an adult's proportions (a head a fifth of its height, long legs).
ModelData madeUpBiped() {
  return biped({});
}

// A tall, thin adult: a head a tenth of its height, long arms and legs.
ModelData tallThinHumanoid() {
  return biped({.leg = 140,
                .torso = 80,
                .head = 26,
                .headWidth = 0.8F,
                .arm = 105,
                .half = 18,
                .limb = 3.5F,
                .foot = 24});
}

// A body drawn chibi from the start, like the original's: a head half its height, a short torso,
// short arms and legs.
ModelData chibiBiped() {
  return biped({.leg = 25,
                .torso = 30,
                .head = 50,
                .headWidth = 0.9F,
                .arm = 28,
                .half = 20,
                .limb = 4,
                .foot = 16});
}

// A made-up body on four legs, facing +Z: bone 0 the entity's root, 1 the body's root, 2 the hind
// trunk and 3 the front one, 4-6 and 7-9 the hind legs, 10-12 and 13-15 the front ones (thigh,
// shin, foot), 16 the neck, 17 the head, 18-19 the tail. Long legs (the hips 150 high, the body
// 100 long), a long neck and a small head.
ModelData longLeggedQuadruped() {
  Builder b;
  b.model.skin = dwe::assets::ModelSkin{};
  b.model.primitives.emplace_back();
  const glm::vec3 none{0.0F};
  constexpr float Leg = 150.0F;
  constexpr float Body = 100.0F;
  constexpr float Girth = 36.0F;
  constexpr float Half = 18.0F;
  constexpr float Limb = 5.0F;
  const auto root = b.bone(std::nullopt, {0, 0, 0}, none, none);
  const auto body = b.bone(root, {0, Leg, 0}, none, none);
  const auto hind = b.bone(body, {0, Leg, 0}, {-Half, Leg - (Girth / 2), -Body / 2},
                           {Half, Leg + (Girth / 2), 0});
  const auto front =
      b.bone(body, {0, Leg, 0}, {-Half, Leg - (Girth / 2), 0}, {Half, Leg + (Girth / 2), Body / 2});
  for (const auto [trunk, z] :
       {std::pair{hind, (-Body / 2) + 12}, std::pair{front, (Body / 2) - 12}}) {
    for (const float side : {1.0F, -1.0F}) {
      const float x = side * (Half - Limb);
      const float hip = Leg - (Girth / 2) + 6;
      const float knee = 0.5F * hip;
      const float ankle = 0.1F * hip;
      const auto thigh =
          b.bone(trunk, {x, hip, z}, {x - Limb, knee, z - Limb}, {x + Limb, hip, z + Limb});
      const auto shin =
          b.bone(thigh, {x, knee, z}, {x - Limb, ankle, z - Limb}, {x + Limb, knee, z + Limb});
      b.bone(shin, {x, ankle, z}, {x - Limb - 1, 0, z - Limb},
             {x + Limb + 1, ankle, z + (3 * Limb)});
    }
  }
  const glm::vec3 neckAt{0, Leg + (Girth / 4), Body / 2};
  const glm::vec3 headAt = neckAt + glm::vec3{0, 58, 20};
  const auto neck = b.bone(front, neckAt, neckAt - glm::vec3{7, 4, 7}, headAt + glm::vec3{7, 0, 7});
  const auto head =
      b.bone(neck, headAt, headAt - glm::vec3{12, 8, 10}, headAt + glm::vec3{12, 16, 14});
  b.box(head, headAt + glm::vec3{-6, -6, 14}, headAt + glm::vec3{6, 4, 26});
  const glm::vec3 tailAt{0, Leg + (Girth / 4), -Body / 2};
  const glm::vec3 tailMid = tailAt + glm::vec3{0, 6, -30};
  const auto tail =
      b.bone(hind, tailAt, tailAt + glm::vec3{-4, -4, -30}, tailAt + glm::vec3{4, 8, 0});
  b.bone(tail, tailMid, tailMid + glm::vec3{-3, -3, -30}, tailMid + glm::vec3{3, 6, 0});
  addWalk(b.model, Leg, 4, 10);
  return b.model;
}

void addWalk(ModelData& model, float bodyAt, std::uint32_t swingNode, std::uint32_t swingBackNode) {
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
    bob.emplace_back(0, bodyAt + 4 * std::cos(2 * phase), 0, 0);
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
      {.node = swingNode, .path = ChannelPath::Rotation, .times = times, .values = swing});
  walk.channels.push_back(
      {.node = swingBackNode, .path = ChannelPath::Rotation, .times = times, .values = swingBack});
  model.clips.push_back(walk);
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

TEST_CASE("chibi: a tall, thin humanoid: the head grows, the legs shrink and the feet stay on "
          "the ground") {
  const ModelData model = tallThinHumanoid();
  const ChibiRig rig = buildChibiRig(model, gameProfile());
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

namespace {

// The ratios of a measure, own and drawn, with the range, for the log.
std::string describe(const ChibiRig& rig) {
  std::string text = std::format("{} already chibi {} active {} | factors head {:.2f} torso {:.2f} "
                                 "arm {:.2f} leg {:.2f} hand {:.2f} foot {:.2f} tail {:.2f}\n",
                                 dwe::animation::nameOf(rig.classification.shape), rig.alreadyChibi,
                                 rig.active, rig.proportions.head, rig.proportions.torso,
                                 rig.proportions.arm, rig.proportions.leg, rig.proportions.hand,
                                 rig.proportions.foot, rig.proportions.tail);
  for (const auto ratio : dwe::animation::BodyRatios) {
    const auto own = rig.measure[ratio];
    const auto& range = rig.reference[ratio];
    if (own && range) {
      text += std::format("  {:10} {:.3f} -> {:.3f} in [{:.3f} {:.3f}] median {:.3f}{}\n",
                          dwe::animation::nameOf(ratio), *own, rig.drawn[ratio].value_or(0.0F),
                          range->low, range->high, range->median,
                          rig.outside[static_cast<std::size_t>(ratio)] ? " (was outside)" : "");
    }
  }
  return text;
}

// How far a ratio is from a range, in its own scale (0 inside).
float distanceTo(float value, const dwe::animation::ChibiRange& range) {
  return value < range.low    ? std::log(range.low / value)
         : value > range.high ? std::log(value / range.high)
                              : 0.0F;
}

// Walks the model for two seconds drawn with the rig: the worst distance between the feet's
// lowest point and the original's, and whether the animation's pose and root motion are the
// original's.
float walkGroundError(const ModelData& model, const ChibiRig& rig) {
  AnimationPlayer player{model};
  REQUIRE(player.play(2U));
  std::vector<glm::mat4> world(model.nodes.size());
  std::vector<glm::mat4> chibi(model.skin->joints.size());
  float worst = 0.0F;
  for (int frame = 0; frame < 120; ++frame) {
    std::ignore = player.advance(1.0F / 60.0F);
    computeChibiJointMatrices(model, rig, player.pose(), player.jointMatrices(), world, chibi);
    worst = std::max(
        worst, std::abs(lowestPoint(model, chibi) - lowestPoint(model, player.jointMatrices())));
  }
  return worst;
}

} // namespace

TEST_CASE("chibi: the original models' profile has a range for each body shape's main ratios") {
  const auto& profile = gameProfile();
  const auto* biped = profile.find(BodyShape::Biped);
  REQUIRE(biped != nullptr);
  for (const auto ratio :
       {BodyRatio::Head, BodyRatio::Torso, BodyRatio::HeadSize, BodyRatio::HeadWidth,
        BodyRatio::Arm, BodyRatio::Leg, BodyRatio::Hand, BodyRatio::Foot}) {
    CAPTURE(dwe::animation::nameOf(ratio));
    CHECK((*biped)[ratio].has_value());
  }
  // The original's bipeds are chibi: the head is well over a third of the height.
  CHECK((*biped)[BodyRatio::Head]->median > 0.4F);
  const auto* quadruped = profile.find(BodyShape::Quadruped);
  REQUIRE(quadruped != nullptr);
  for (const auto ratio :
       {BodyRatio::Head, BodyRatio::Torso, BodyRatio::HeadSize, BodyRatio::Leg, BodyRatio::Foot}) {
    CAPTURE(dwe::animation::nameOf(ratio));
    CHECK((*quadruped)[ratio].has_value());
  }
}

TEST_CASE("chibi: an external model already within the original's range is left exactly as it is") {
  const ModelData model = chibiBiped();
  const ChibiRig rig = buildChibiRig(model, gameProfile());
  MESSAGE(describe(rig));
  CHECK(rig.classification.shape == BodyShape::Biped);
  CHECK(rig.alreadyChibi);
  CHECK_FALSE(rig.active);
  CHECK(std::ranges::none_of(rig.outside, std::identity{}));
  // Walking, the matrices drawn are the pose's own, bit for bit.
  AnimationPlayer player{model};
  REQUIRE(player.play(2U));
  std::vector<glm::mat4> world(model.nodes.size());
  std::vector<glm::mat4> drawn(model.skin->joints.size());
  for (int frame = 0; frame < 60; ++frame) {
    std::ignore = player.advance(1.0F / 60.0F);
    computeChibiJointMatrices(model, rig, player.pose(), player.jointMatrices(), world, drawn);
    REQUIRE(std::memcmp(drawn.data(), player.jointMatrices().data(),
                        drawn.size() * sizeof(glm::mat4)) == 0);
  }
}

TEST_CASE("chibi: external models outside the original's range are brought into it, the feet on "
          "the ground") {
  struct Case {
    const char* name;
    ModelData model;
    BodyShape shape;
  };
  for (const Case& test :
       {Case{"tall thin humanoid", tallThinHumanoid(), BodyShape::Biped},
        Case{"long-legged quadruped", longLeggedQuadruped(), BodyShape::Quadruped}}) {
    CAPTURE(test.name);
    const ChibiRig rig = buildChibiRig(test.model, gameProfile());
    MESSAGE(std::format("{}: {}", test.name, describe(rig)));
    REQUIRE(rig.classification.shape == test.shape);
    CHECK_FALSE(rig.alreadyChibi);
    REQUIRE(rig.active);
    // A small head grows and long legs shorten.
    CHECK(rig.proportions.head > 1.2F);
    CHECK(rig.proportions.leg < 1.0F);
    // Every ratio that was out of range ends in it, or (when a factor reached its bound) at least
    // much nearer.
    for (const auto ratio : dwe::animation::BodyRatios) {
      const auto own = rig.measure[ratio];
      const auto drawn = rig.drawn[ratio];
      const auto& range = rig.reference[ratio];
      if (!own || !drawn || !range) {
        continue;
      }
      CAPTURE(dwe::animation::nameOf(ratio));
      const float before = distanceTo(*own, *range);
      const float after = distanceTo(*drawn, *range);
      CHECK(after <= std::max(0.05F, 0.5F * before));
    }
    // Walking, the feet stay on the ground on every frame.
    CHECK(walkGroundError(test.model, rig) < 0.01F);
  }
}

TEST_CASE("chibi: the detector's edge: a model just outside the range changes only a little") {
  const ModelData model = madeUpBiped();
  const auto found = classifyChibi(model);
  const auto own = dwe::animation::measureBody(model, found, dwe::animation::restJoints(model));
  // A profile made around the model's own ratios: it is chibi as it is.
  dwe::animation::ChibiProfile profile;
  auto& shape = profile.shapes[BodyShape::Biped];
  for (std::size_t r = 0; r < dwe::animation::BodyRatioCount; ++r) {
    if (own.ratios[r]) {
      shape.ranges[r] = dwe::animation::ChibiRange{.low = *own.ratios[r] * 0.9F,
                                                   .median = *own.ratios[r],
                                                   .high = *own.ratios[r] * 1.1F,
                                                   .samples = 10};
    }
  }
  CHECK(buildChibiRig(model, profile).alreadyChibi);
  // Its head just under the range: the head grows by about as little.
  const auto head = static_cast<std::size_t>(BodyRatio::HeadSize);
  const float value = *own.ratios[head];
  shape.ranges[head] = dwe::animation::ChibiRange{
      .low = value * 1.01F, .median = value * 1.5F, .high = value * 2.0F, .samples = 10};
  const ChibiRig near = buildChibiRig(model, profile);
  CHECK_FALSE(near.alreadyChibi);
  CHECK(near.active);
  CHECK(near.outside[head]);
  CHECK(near.proportions.head > 1.0F);
  CHECK(near.proportions.head < 1.05F);
  // Far under it (half the low end and less), its width too: toward the middle.
  shape.ranges[head] = dwe::animation::ChibiRange{
      .low = value * 2.0F, .median = value * 2.5F, .high = value * 3.0F, .samples = 10};
  const auto width = static_cast<std::size_t>(BodyRatio::HeadWidth);
  const float wide = *own.ratios[width];
  shape.ranges[width] = dwe::animation::ChibiRange{
      .low = wide * 2.0F, .median = wide * 2.5F, .high = wide * 3.0F, .samples = 10};
  const ChibiRig far = buildChibiRig(model, profile);
  CHECK(*far.drawn[BodyRatio::HeadSize] > value * 2.2F);
}

TEST_CASE("chibi: a profile is made from measured models, written and read back") {
  std::vector<std::pair<BodyShape, dwe::animation::BodyMeasure>> models;
  for (int i = 0; i <= 10; ++i) {
    dwe::animation::BodyMeasure measure;
    measure.ratios[static_cast<std::size_t>(BodyRatio::Head)] =
        0.3F + (0.03F * static_cast<float>(i));
    if (i < 2) {
      measure.ratios[static_cast<std::size_t>(BodyRatio::Tail)] = 1.0F;
    }
    models.emplace_back(BodyShape::Biped, measure);
  }
  models.emplace_back(BodyShape::Unknown, dwe::animation::BodyMeasure{});
  const auto profile = dwe::animation::profileOf(models);
  REQUIRE(profile.shapes.size() == 1);
  const auto& biped = *profile.find(BodyShape::Biped);
  CHECK(biped.models == 11);
  REQUIRE(biped[BodyRatio::Head].has_value());
  CHECK(biped[BodyRatio::Head]->low == doctest::Approx(0.3F));
  CHECK(biped[BodyRatio::Head]->median == doctest::Approx(0.45F));
  CHECK(biped[BodyRatio::Head]->high == doctest::Approx(0.6F));
  CHECK(biped[BodyRatio::Head]->samples == 11);
  // Two models are too few for a range.
  CHECK_FALSE(biped[BodyRatio::Tail].has_value());

  const std::string written = dwe::animation::writeChibiProfile(profile);
  const auto read = dwe::animation::parseChibiProfile(written);
  REQUIRE_MESSAGE(read.has_value(), (read ? "" : read.error().message));
  CHECK(dwe::animation::writeChibiProfile(*read) == written);
  for (const char* bad : {"shapes: {blob: {models: 1}}", "shapes: {biped: {nose: {low: 1}}}",
                          "shapes: {biped: {head: {low: 0.5, median: 0.4, high: 0.6}}}",
                          "shapes: {biped: {head: {low: 0.5}}}", "ranges: {}"}) {
    CAPTURE(bad);
    CHECK_FALSE(dwe::animation::parseChibiProfile(bad).has_value());
  }
}

TEST_CASE("chibi: the game pack's profile parses") {
  const auto read = dwe::animation::parseChibiProfile(gamePackText("models/chibi_profile.yaml"));
  CHECK_MESSAGE(read.has_value(), (read ? "" : read.error().message));
}

TEST_CASE("chibi: walking, the feet stay on the ground and the animation and root motion are the "
          "original's") {
  const ModelData model = madeUpBiped();
  const ChibiRig rig = buildChibiRig(model, gameProfile());
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
  CHECK_FALSE(buildChibiRig(model, gameProfile(), &read->at("abcd")).active);
  const ChibiRig rig = buildChibiRig(model, gameProfile(), &efgh);
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

namespace {

struct Content {
  std::filesystem::path folder;
  // The packs read through it: it outlives the VFS.
  std::unique_ptr<dwe::assets::DiskFileSystem> disk;
  std::unique_ptr<dwe::assets::VirtualFileSystem> vfs;
};

std::optional<Content> mountedContent() {
  const std::optional<std::filesystem::path> content = dwe::test_support::userContent();
  if (!content) {
    return std::nullopt;
  }
  auto disk = std::make_unique<dwe::assets::DiskFileSystem>();
  auto packs = dwe::assets::findPacks(*disk, *content);
  REQUIRE(packs.has_value());
  const auto opened = dwe::assets::openPacks(*disk, packs->packs, dwe::assets::PackOrigin::Engine);
  REQUIRE(opened.has_value());
  auto vfs = dwe::assets::VirtualFileSystem::mount(opened->locations);
  REQUIRE(vfs.has_value());
  return Content{.folder = *content,
                 .disk = std::move(disk),
                 .vfs = std::make_unique<dwe::assets::VirtualFileSystem>(std::move(*vfs))};
}

dwe::animation::ChibiOverrides gameOverrides() {
  const auto overrides = dwe::animation::parseChibiOverrides(gamePackText("models/chibi.yaml"));
  REQUIRE(overrides.has_value());
  return *overrides;
}

} // namespace

// Runs only when DWE_CONTENT names a content folder: every original model (each species and the
// player's character) is measured, and the profile they make is the game pack's
// (models/chibi_profile.yaml). With DWE_CHIBI_PROFILE_OUT set it writes that file instead (how
// the profile is made: `DWE_CONTENT=<content> DWE_CHIBI_PROFILE_OUT=<game pack>/models/
// chibi_profile.yaml dwe_animation_tests -tc='*chibi profile*'`).
TEST_CASE("the user's disc, extracted: the chibi profile is the original models' proportions") {
  const auto content = mountedContent();
  if (!content) {
    MESSAGE("DWE_CONTENT is not set: skipping the test on the extracted content");
    return;
  }
  const auto overrides = gameOverrides();
  std::vector<std::pair<BodyShape, dwe::animation::BodyMeasure>> measured;
  std::vector<std::string> names;
  std::string table;
  std::vector<std::filesystem::path> folders;
  for (const auto& entry : std::filesystem::directory_iterator{content->folder / "base/digimon"}) {
    folders.push_back(entry.path());
  }
  std::ranges::sort(folders);
  for (const auto& folder : folders) {
    const std::string species = folder.filename().string();
    const auto model = dwe::assets::loadModel(
        *content->vfs, *dwe::assets::AssetId::parse(std::format("digimon/{}/model", species)));
    if (!model || !model->skin) {
      continue;
    }
    const auto override = overrides.find(species);
    const ChibiOverride* given = override != overrides.end() ? &override->second : nullptr;
    // A species turned off is a round body (a head and a body in one piece): no shape to compare.
    if (given != nullptr && given->off) {
      continue;
    }
    const auto found = classifyChibi(*model, given);
    if (found.shape == BodyShape::Unknown) {
      continue;
    }
    const auto measure =
        dwe::animation::measureBody(*model, found, dwe::animation::restJoints(*model));
    // An enemy's model is often a species' own: a model counts once.
    const bool again = std::ranges::any_of(measured, [&](const auto& seen) {
      return seen.first == found.shape && seen.second.ratios == measure.ratios;
    });
    if (again) {
      continue;
    }
    measured.emplace_back(found.shape, measure);
    names.push_back(species);
    table += std::format("{} {}", species, dwe::animation::nameOf(found.shape));
    for (const auto ratio : dwe::animation::BodyRatios) {
      if (const auto value = measure[ratio]) {
        table += std::format(" {} {:.3f}", dwe::animation::nameOf(ratio), *value);
      }
    }
    table += "\n";
  }
  MESSAGE(table);
  const auto profile = dwe::animation::profileOf(measured);
  CHECK(profile.find(BodyShape::Biped) != nullptr);
  CHECK(profile.find(BodyShape::Quadruped) != nullptr);
  if (const char* out = std::getenv("DWE_CHIBI_PROFILE_OUT")) {
    std::ofstream file{out};
    file << ProfileHeader << dwe::animation::writeChibiProfile(profile);
    MESSAGE(std::format("wrote {}", out));
    return;
  }
  const auto stored = dwe::animation::parseChibiProfile(gamePackText("models/chibi_profile.yaml"));
  REQUIRE(stored.has_value());
  CHECK(dwe::animation::writeChibiProfile(*stored) == dwe::animation::writeChibiProfile(profile));
  // The detector finds every original model already chibi (the few it cannot measure, without a
  // trunk, it leaves alone too): drawn as it is.
  for (const std::string& species : names) {
    CAPTURE(species);
    const auto model = dwe::assets::loadModel(
        *content->vfs, *dwe::assets::AssetId::parse(std::format("digimon/{}/model", species)));
    const auto override = overrides.find(species);
    const ChibiRig rig =
        buildChibiRig(*model, *stored, override != overrides.end() ? &override->second : nullptr);
    const bool hasRatios = std::ranges::any_of(rig.measure.ratios,
                                               [](const auto& value) { return value.has_value(); });
    CHECK(rig.alreadyChibi == hasRatios);
    CHECK_FALSE(rig.active);
  }
}

// Runs only when DWE_CONTENT names a content folder: the test species are found as the body they
// are, and reshaped as a model from elsewhere would be (against a profile made for it: a head a
// third bigger, limbs, hands and feet a fifth shorter), on every frame of their idle and walk the
// chibi's lowest point stays near the original's.
TEST_CASE(
    "the user's disc, extracted: chibi detection and feet on the ground for the test species") {
  const auto content = mountedContent();
  if (!content) {
    MESSAGE("DWE_CONTENT is not set: skipping the test on the extracted content");
    return;
  }
  const auto& vfs = content->vfs;

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
    CHECK_FALSE(buildChibiRig(*model, gameProfile()).active);
    const auto found = classifyChibi(*model);
    const auto own = dwe::animation::measureBody(*model, found, dwe::animation::restJoints(*model));
    dwe::animation::ChibiProfile reshaped;
    auto& ranges = reshaped.shapes[found.shape];
    for (const auto ratio : dwe::animation::BodyRatios) {
      const auto r = static_cast<std::size_t>(ratio);
      if (own.ratios[r]) {
        const bool head = ratio == BodyRatio::HeadSize || ratio == BodyRatio::HeadWidth;
        const float to = *own.ratios[r] * (head ? 1.3F : 0.8F);
        ranges.ranges[r] = dwe::animation::ChibiRange{.low = to, .median = to, .high = to};
      }
    }
    const ChibiRig rig = buildChibiRig(*model, reshaped);
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
    CHECK(std::filesystem::exists(content->folder / "base/digimon" / species / "model.glb"));
  }
  std::string summary;
  for (const auto& entry : std::filesystem::directory_iterator{content->folder / "base/digimon"}) {
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
