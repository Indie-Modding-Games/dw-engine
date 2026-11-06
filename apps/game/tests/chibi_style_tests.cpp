#include <dwe/animation/animation_player.hpp>
#include <dwe/animation/chibi.hpp>
#include <dwe/animation/clip_sampling.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/model_data.hpp>
#include <dwe/assets/virtual_file_system.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "chibi_style.hpp"
#include "packages/engine/assets/tests/glb_builder.hpp"

namespace dwe::app {
namespace {

using assets::testing::GlbBuilder;

// A made-up humanoid as a mod would bring it, with an adult's proportions: 180 units tall, a head
// an eighth of that, long arms and legs. One box per bone (two for the head), every bone straight
// above or below its parent, so its bind pose is translations only.
struct Box {
  std::uint32_t joint;
  glm::vec3 low;
  glm::vec3 high;
};

struct Bone {
  int parent;
  glm::vec3 at; // in model space
};

std::vector<std::byte> humanoidGlb() {
  // 0 the entity's root, 1 hips, 2 spine, 3 chest, 4 neck, 5 head, 6-8 left arm, 9-11 right arm,
  // 12-14 left leg, 15-17 right leg.
  const std::array<Bone, 18> bones{{
      {-1, {0, 0, 0}},
      {0, {0, 95, 0}},
      {1, {0, 115, 0}},
      {2, {0, 135, 0}},
      {3, {0, 152, 0}},
      {4, {0, 158, 0}},
      {3, {18, 148, 0}},
      {6, {18, 120, 0}},
      {7, {18, 96, 0}},
      {3, {-18, 148, 0}},
      {9, {-18, 120, 0}},
      {10, {-18, 96, 0}},
      {1, {9, 92, 0}},
      {12, {9, 50, 0}},
      {13, {9, 8, 0}},
      {1, {-9, 92, 0}},
      {15, {-9, 50, 0}},
      {16, {-9, 8, 0}},
  }};
  const std::vector<Box> boxes{
      {1, {-15, 85, -9}, {15, 105, 9}},   {2, {-14, 105, -8}, {14, 125, 8}},
      {3, {-16, 125, -9}, {16, 152, 9}},  {4, {-4, 152, -4}, {4, 158, 4}},
      {5, {-9, 158, -10}, {9, 181, 10}},  {5, {-6, 160, 8}, {6, 168, 12}},
      {6, {14, 120, -4}, {22, 148, 4}},   {7, {14, 96, -4}, {22, 120, 4}},
      {8, {14, 84, -3}, {22, 96, 3}},     {9, {-22, 120, -4}, {-14, 148, 4}},
      {10, {-22, 96, -4}, {-14, 120, 4}}, {11, {-22, 84, -3}, {-14, 96, 3}},
      {12, {4, 50, -5}, {14, 92, 5}},     {13, {5, 8, -4}, {13, 50, 4}},
      {14, {5, 0, -4}, {13, 8, 14}},      {15, {-14, 50, -5}, {-4, 92, 5}},
      {16, {-13, 8, -4}, {-5, 50, 4}},    {17, {-13, 0, -4}, {-5, 8, 14}},
  };
  std::vector<float> positions;
  std::vector<float> normals;
  std::vector<std::uint8_t> joints;
  std::vector<float> weights;
  std::vector<std::uint16_t> indices;
  glm::vec3 low{1e9F};
  glm::vec3 high{-1e9F};
  for (const Box& box : boxes) {
    // Six faces of four corners each, so that each face has its own normal.
    for (int axis = 0; axis < 3; ++axis) {
      for (const float side : {-1.0F, 1.0F}) {
        const auto first = static_cast<std::uint16_t>(positions.size() / 3);
        glm::vec3 normal{0.0F};
        normal[axis] = side;
        const int u = (axis + 1) % 3;
        const int v = (axis + 2) % 3;
        for (const auto [a, b] :
             std::array<std::array<int, 2>, 4>{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}}) {
          glm::vec3 corner;
          corner[axis] = side < 0.0F ? box.low[axis] : box.high[axis];
          corner[u] = a == 0 ? box.low[u] : box.high[u];
          corner[v] = b == 0 ? box.low[v] : box.high[v];
          positions.insert(positions.end(), {corner.x, corner.y, corner.z});
          normals.insert(normals.end(), {normal.x, normal.y, normal.z});
          joints.insert(joints.end(), {static_cast<std::uint8_t>(box.joint), 0, 0, 0});
          weights.insert(weights.end(), {1.0F, 0.0F, 0.0F, 0.0F});
          low = glm::min(low, corner);
          high = glm::max(high, corner);
        }
        for (const int corner : {0, 1, 2, 0, 2, 3}) {
          indices.push_back(static_cast<std::uint16_t>(first + corner));
        }
      }
    }
  }
  std::vector<float> inverseBind;
  for (const Bone& bone : bones) {
    const glm::mat4 matrix = glm::translate(glm::mat4{1.0F}, -bone.at);
    for (int column = 0; column < 4; ++column) {
      for (int row = 0; row < 4; ++row) {
        inverseBind.push_back(matrix[column][row]);
      }
    }
  }
  GlbBuilder b;
  const int positionAccessor =
      b.addFloats(positions, "VEC3", 3,
                  std::format(R"(,"min":[{},{},{}],"max":[{},{},{}])", low.x, low.y, low.z, high.x,
                              high.y, high.z));
  const int normalAccessor = b.addFloats(normals, "VEC3", 3);
  const int jointAccessor = b.addAccessor(std::span<const std::uint8_t>{joints},
                                          GlbBuilder::UnsignedByte, "VEC4", joints.size() / 4);
  const int weightAccessor = b.addFloats(weights, "VEC4", 4);
  const int indexAccessor = b.addAccessor(std::span<const std::uint16_t>{indices},
                                          GlbBuilder::UnsignedShort, "SCALAR", indices.size());
  const int inverseBindAccessor = b.addFloats(inverseBind, "MAT4", 16);
  std::string nodes;
  std::string jointList;
  for (std::size_t i = 0; i < bones.size(); ++i) {
    std::string children;
    for (std::size_t c = 0; c < bones.size(); ++c) {
      if (bones[c].parent == static_cast<int>(i)) {
        children += std::format("{}{}", children.empty() ? "" : ",", c);
      }
    }
    const glm::vec3 local = bones[i].parent < 0
                                ? bones[i].at
                                : bones[i].at - bones[static_cast<std::size_t>(bones[i].parent)].at;
    nodes +=
        std::format(R"({{"name":"bone_{:02}","translation":[{},{},{}]{}}},)", i, local.x, local.y,
                    local.z, children.empty() ? "" : std::format(R"(,"children":[{}])", children));
    jointList += std::format("{}{}", i == 0 ? "" : ",", i);
  }
  const std::string json = std::format(
      R"({{"scene":0,"scenes":[{{"nodes":[0,{}]}}],"nodes":[{}{{"name":"model","mesh":0,"skin":0}}],)"
      R"("skins":[{{"joints":[{}],"inverseBindMatrices":{}}}],)"
      R"("meshes":[{{"primitives":[{{"attributes":{{"POSITION":{},"NORMAL":{},"JOINTS_0":{},)"
      R"("WEIGHTS_0":{}}},"indices":{},"material":0}}]}}],)"
      R"("materials":[{{"name":"cloth","pbrMetallicRoughness":{{"baseColorFactor":[0.4,0.5,0.8,1]}}}}]}})",
      bones.size(), nodes, jointList, inverseBindAccessor, positionAccessor, normalAccessor,
      jointAccessor, weightAccessor, indexAccessor);
  return b.build(json);
}

// The original game's packs (a base and a remaster layer, found in the engine's folders) and a
// mod: the mod brings its own humanoid and replaces one of the base's models with it.
struct Packs {
  assets::MemoryFileSystem memory;
  std::unique_ptr<assets::VirtualFileSystem> files;

  Packs() {
    const std::vector<std::byte> humanoid = humanoidGlb();
    memory.addFile("base/pack.yaml", "id: base\nversion: 0.1.0\nlayer: base\npriority: 0\n");
    // The game pack's profile of the original's proportions.
    std::ifstream profile{std::filesystem::path{DWE_GAME_PACK} / "models/chibi_profile.yaml"};
    std::stringstream text;
    text << profile.rdbuf();
    memory.addFile("base/models/chibi_profile.yaml", text.str());
    memory.addFile("base/digimon/boys/model.glb", humanoid);
    memory.addFile("base/digimon/agum/model.glb", humanoid);
    memory.addFile("remaster/pack.yaml",
                   "id: remaster\nversion: 0.1.0\nlayer: remaster\npriority: 0\n");
    memory.addFile("remaster/digimon/garu/model.glb", humanoid);
    memory.addFile("mod/pack.yaml", "id: hero_mod\nversion: 0.1.0\nlayer: mods\npriority: 0\n");
    memory.addFile("mod/hero_mod/hero/model.glb", humanoid);
    memory.addFile("mod/digimon/agum/model.glb", humanoid);
    const auto at = [this](std::string_view root, assets::PackOrigin origin) {
      return assets::PackLocation{
          .fileSystem = &memory, .root = std::string{root}, .origin = origin};
    };
    auto mounted = assets::VirtualFileSystem::mount(
        std::array{at("base", assets::PackOrigin::Engine),
                   at("remaster", assets::PackOrigin::Engine), at("mod", assets::PackOrigin::Mod)});
    REQUIRE_MESSAGE(mounted.has_value(), mounted.error().message);
    files = std::make_unique<assets::VirtualFileSystem>(std::move(*mounted));
  }
};

assets::AssetId id(std::string_view text) {
  auto parsed = assets::AssetId::parse(text);
  REQUIRE(parsed.has_value());
  return *parsed;
}

} // namespace

TEST_CASE("the chibi style's modes read and write by name, external models by default") {
  for (const ChibiMode mode : {ChibiMode::Off, ChibiMode::External}) {
    CHECK(parseChibiMode(chibiModeName(mode)) == mode);
  }
  CHECK(startChibiMode(std::nullopt) == ChibiMode::External);
  // The old "all" (the original's models reshaped too) reads as external models.
  CHECK(startChibiMode(std::string{"all"}) == ChibiMode::External);
  CHECK(startChibiMode(std::string{"sideways"}) == ChibiMode::External);
  // The development mode (--chibi) is never saved as such.
  CHECK(chibiModeName(ChibiMode::EveryModel) == "external");
  CHECK_FALSE(drawsChibi(ChibiMode::External, true));
  CHECK(drawsChibi(ChibiMode::External, false));
  CHECK(drawsChibi(ChibiMode::EveryModel, true));
  CHECK_FALSE(drawsChibi(ChibiMode::Off, false));
}

TEST_CASE("a mod's model is drawn chibi by default, the original game's models are not") {
  Packs packs;
  const assets::VirtualFileSystem& files = *packs.files;
  CHECK(isOriginalModel(files, id("digimon/boys/model")));
  CHECK(isOriginalModel(files, id("digimon/garu/model")));
  CHECK_FALSE(isOriginalModel(files, id("hero_mod/hero/model")));
  // A mod's replacement of an original model is the mod's.
  CHECK_FALSE(isOriginalModel(files, id("digimon/agum/model")));

  auto model = assets::loadModel(files, id("hero_mod/hero/model"));
  REQUIRE_MESSAGE(model.has_value(), model.error().message);
  const ChibiStyleData style = loadChibiStyleData(files);
  REQUIRE(style.profile.find(animation::BodyShape::Biped) != nullptr);
  const animation::ChibiRig rig = chibiRigOf(id("hero_mod/hero/model"), *model, style);
  MESSAGE(chibiDecision(id("hero_mod/hero/model"), rig));
  // The detector: an adult's proportions are not the original's, so it is reshaped.
  CHECK_FALSE(rig.alreadyChibi);
  REQUIRE(rig.active);
  CHECK(rig.classification.shape == animation::BodyShape::Biped);
  CHECK(rig.classification.legs == 2);
  CHECK(rig.classification.arms == 2);
  CHECK(rig.classification.head == std::optional<std::uint32_t>{5});
  // A small head (an eighth of the height) grows a lot; drawn, its share of the height is within
  // the original's range.
  CHECK(rig.proportions.head > 1.8F);
  const auto& head = rig.reference[animation::BodyRatio::Head];
  REQUIRE(head.has_value());
  CHECK_FALSE(head->contains(*rig.measure[animation::BodyRatio::Head]));
  CHECK(head->contains(*rig.drawn[animation::BodyRatio::Head]));

  // Drawn chibi: the head is bigger, the hips lower (shorter legs), the feet still on the floor.
  std::vector<assets::NodeTransform> rest(model->nodes.size());
  animation::restPose(*model, rest);
  std::vector<glm::mat4> world(model->nodes.size());
  std::vector<glm::mat4> original(model->skin->joints.size());
  animation::computeJointMatrices(*model, rest, world, original);
  std::vector<glm::mat4> chibi(original.size());
  animation::computeChibiJointMatrices(*model, rig, rest, original, world, chibi);
  CHECK(animation::lowestPoint(*model, chibi) == doctest::Approx(0.0F).epsilon(1e-3));
  const auto hips = [&](std::span<const glm::mat4> joints) {
    return (joints[1] * glm::vec4{0.0F, 95.0F, 0.0F, 1.0F}).y;
  };
  CHECK(hips(chibi) < 0.7F * hips(original));
  const float headSize = glm::length(glm::vec3{chibi[5][1]});
  CHECK(headSize == doctest::Approx(rig.proportions.head));
}

} // namespace dwe::app
