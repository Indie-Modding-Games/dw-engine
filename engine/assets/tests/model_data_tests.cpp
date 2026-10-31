#include <dwe/assets/file_system.hpp>
#include <dwe/assets/model_data.hpp>
#include <dwe/assets/pak.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/test_support/user_assets.hpp>

#include <doctest/doctest.h>
#include <lodepng.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include "glb_builder.hpp"

using dwe::assets::AlphaMode;
using dwe::assets::ChannelPath;
using dwe::assets::Interpolation;
using dwe::assets::ModelData;
using dwe::assets::parseModel;
using dwe::assets::SemiTransparency;
using dwe::assets::testing::GlbBuilder;

namespace {

// Two joints (a root and a child 10 units up), one textured, unlit, double-sided triangle on the
// child, and two clips: one as dwx writes them (ticks, a loop forever, root motion) and a plain one
// with step keyframes.
struct Fixture {
  std::string materials =
      R"([{"name":"skin","pbrMetallicRoughness":{"baseColorTexture":{"index":0}},)"
      R"("alphaMode":"MASK","alphaCutoff":0.25,"doubleSided":true,)"
      R"("extensions":{"KHR_materials_unlit":{}},)"
      R"("extras":{"image":[576,256],"depth":0,"clut":[128,490],"blending":2}}])";
  std::string clipExtras =
      R"({"id":2,"frameCount":4,"hasScale":false,"ticksPerSecond":20,"tickCount":4,)"
      R"("endsInLoop":true,"rootMotionNode":0,"loops":[{"firstTick":1,"lastTick":3,"count":255}],)"
      R"("textureMoves":[],"sounds":[]})";
  std::vector<std::uint16_t> indices{0, 1, 2};
  std::vector<std::uint8_t> joints{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
  std::vector<float> weights{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
  // Hostile variants: accessors with no buffer view, which glTF reads as zeros.
  std::optional<std::size_t> indicesWithoutView;
  bool positionsWithoutView = false;
  bool inverseBindWithoutView = false;

  [[nodiscard]] std::vector<std::byte> glb() const {
    GlbBuilder b;
    const int positions = positionsWithoutView
                              ? b.addViewlessAccessor(GlbBuilder::Float, "VEC3", 3)
                              : b.addFloats({0, 10, 0, 1, 10, 0, 0, 11, 0}, "VEC3", 3,
                                            R"(,"min":[0,10,0],"max":[1,11,0])");
    const int normals = b.addFloats({0, 0, 1, 0, 0, 1, 0, 0, 1}, "VEC3", 3);
    const int uvs = b.addFloats({0, 0, 1, 0, 0, 1}, "VEC2", 2);
    const int colors = b.addFloats({1, 1, 1, 2, 2, 2, 0.5F, 0.5F, 0.5F}, "VEC3", 3);
    const int jointsAccessor = b.addAccessor(std::span<const std::uint8_t>{joints},
                                             GlbBuilder::UnsignedByte, "VEC4", joints.size() / 4);
    const int weightsAccessor = b.addFloats(weights, "VEC4", 4);
    const int indexAccessor =
        indicesWithoutView
            ? b.addViewlessAccessor(GlbBuilder::UnsignedShort, "SCALAR", *indicesWithoutView)
            : b.addAccessor(std::span<const std::uint16_t>{indices}, GlbBuilder::UnsignedShort,
                            "SCALAR", indices.size());
    // Joint 1's bind pose is 10 units up.
    const int inverseBind = inverseBindWithoutView
                                ? b.addViewlessAccessor(GlbBuilder::Float, "MAT4", 2)
                                : b.addFloats({1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0,   0, 1, //
                                               1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -10, 0, 1},
                                              "MAT4", 16);
    const int times =
        b.addFloats({0.0F, 0.05F, 0.1F, 0.15F}, "SCALAR", 1, R"(,"min":[0],"max":[0.15])");
    const int rootMotion = b.addFloats({0, 0, 0, 0, 0, 10, 0, 0, 20, 0, 0, 30}, "VEC3", 3);
    const int turn = b.addFloats(
        {0, 0, 0, 1, 0, 0.7071068F, 0, 0.7071068F, 0, 1, 0, 0, 0, 0.7071068F, 0, -0.7071068F},
        "VEC4", 4);
    const int stepTimes = b.addFloats({0.0F, 0.5F}, "SCALAR", 1, R"(,"min":[0],"max":[0.5])");
    const int stepScale = b.addFloats({1, 1, 1, 2, 2, 2}, "VEC3", 3);
    std::vector<unsigned char> png;
    REQUIRE(lodepng::encode(png, std::vector<unsigned char>{255, 0, 0, 255, 0, 255, 0, 0}, 2, 1,
                            LCT_RGBA, 8) == 0);
    const int image = b.addBytes(png);

    const std::string json = std::format(
        R"({{"scene":0,"scenes":[{{"nodes":[0,2]}}],)"
        R"("nodes":[{{"name":"bone_00","children":[1]}},{{"name":"bone_01","translation":[0,10,0]}},)"
        R"({{"name":"model","mesh":0,"skin":0}}],)"
        R"("skins":[{{"joints":[0,1],"inverseBindMatrices":{}}}],)"
        R"("meshes":[{{"primitives":[{{"attributes":{{"POSITION":{},"NORMAL":{},"TEXCOORD_0":{},"COLOR_0":{},)"
        R"("JOINTS_0":{},"WEIGHTS_0":{}}},"indices":{},"material":0}}]}}],)"
        R"("materials":{},"textures":[{{"source":0}}],"images":[{{"bufferView":{},"mimeType":"image/png"}}],)"
        R"("extensionsUsed":["KHR_materials_unlit"],)"
        R"("animations":[)"
        R"({{"name":"anim_02","extras":{},"samplers":[{{"input":{},"output":{}}},{{"input":{},"output":{}}}],)"
        R"("channels":[{{"sampler":0,"target":{{"node":0,"path":"translation"}}}},)"
        R"({{"sampler":1,"target":{{"node":1,"path":"rotation"}}}}]}},)"
        R"({{"name":"grow","samplers":[{{"input":{},"output":{},"interpolation":"STEP"}}],)"
        R"("channels":[{{"sampler":0,"target":{{"node":1,"path":"scale"}}}}]}}]}})",
        inverseBind, positions, normals, uvs, colors, jointsAccessor, weightsAccessor,
        indexAccessor, materials, image, clipExtras, times, rootMotion, times, turn, stepTimes,
        stepScale);
    return b.build(json);
  }
};

// A PNG whose header says width x height over a 1x1 image: enough for the checks made from the
// header alone.
std::vector<unsigned char> pngClaiming(std::uint32_t width, std::uint32_t height) {
  std::vector<unsigned char> png;
  REQUIRE(lodepng::encode(png, std::vector<unsigned char>{1, 2, 3, 255}, 1, 1, LCT_RGBA, 8) == 0);
  // Signature (8), IHDR length (4), "IHDR" at 12, width at 16, height at 20, CRC of type and data
  // at 29.
  constexpr std::size_t WidthAt = 16;
  constexpr std::size_t HeightAt = 20;
  constexpr std::size_t TypeAt = 12;
  constexpr std::size_t CrcAt = 29;
  const auto put32 = [&png](std::size_t at, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
      png[at + i] = static_cast<unsigned char>((value >> (24U - (8U * i))) & 0xFFU);
    }
  };
  put32(WidthAt, width);
  put32(HeightAt, height);
  put32(CrcAt, lodepng_crc32(&png[TypeAt], CrcAt - TypeAt));
  return png;
}

} // namespace

TEST_CASE("a skinned glb loads with its mesh, material, image, skin and clips") {
  const auto model = parseModel(Fixture{}.glb());
  REQUIRE_MESSAGE(model.has_value(), (model ? "" : model.error().message));

  REQUIRE(model->nodes.size() == 3);
  CHECK(model->nodes[1].name == "bone_01");
  CHECK(model->nodes[1].parent == 0U);
  CHECK_FALSE(model->nodes[2].parent.has_value());
  CHECK(model->nodes[1].rest.translation == glm::vec3{0, 10, 0});
  REQUIRE(model->nodeOrder.size() == 3);
  CHECK(model->nodeOrder[0] == 0);

  REQUIRE(model->primitives.size() == 1);
  const auto& primitive = model->primitives[0];
  CHECK(primitive.indices == std::vector<std::uint32_t>{0, 1, 2});
  CHECK(primitive.hasNormals);
  CHECK(primitive.hasUvs);
  CHECK(primitive.hasColors);
  REQUIRE(primitive.vertices.size() == 3);
  CHECK(primitive.vertices[1].position == glm::vec3{1, 10, 0});
  CHECK(primitive.vertices[1].uv == glm::vec2{1, 0});
  // 128 = 1.0 with a texture, so a colour may pass 1.
  CHECK(primitive.vertices[1].color == glm::vec4{2, 2, 2, 1});
  CHECK(primitive.vertices[2].joints[0] == 1);
  CHECK(primitive.vertices[2].weights == glm::vec4{1, 0, 0, 0});

  REQUIRE(model->materials.size() == 1);
  const auto& material = model->materials[0];
  CHECK(material.alphaMode == AlphaMode::Mask);
  CHECK(material.alphaCutoff == doctest::Approx(0.25F));
  CHECK(material.unlit);
  CHECK(material.doubleSided);
  CHECK(material.blending == SemiTransparency::Subtract);
  CHECK(material.texture == dwe::assets::VramPoint{.x = 576, .y = 256});
  CHECK(material.clut == dwe::assets::VramPoint{.x = 128, .y = 490});
  CHECK(material.image == 0U);
  REQUIRE(model->images.size() == 1);
  CHECK(model->images[0].width == 2);
  CHECK(model->images[0].pixels[4] == std::byte{0});
  CHECK(model->images[0].pixels[5] == std::byte{255});

  REQUIRE(model->skin.has_value());
  const auto skin = model->skin.value_or(dwe::assets::ModelSkin{});
  CHECK(skin.joints == std::vector<std::uint32_t>{0, 1});
  CHECK(skin.inverseBindMatrices[1][3][1] == doctest::Approx(-10.0F));

  REQUIRE(model->clips.size() == 2);
  const auto* clip = model->findClip(2U);
  REQUIRE(clip != nullptr);
  CHECK(clip == model->findClip("anim_02"));
  CHECK(clip->ticksPerSecond == 20);
  CHECK(clip->tickCount == 4);
  CHECK(clip->endsInLoop);
  CHECK(clip->rootMotionNode == 0U);
  CHECK(clip->duration == doctest::Approx(0.15F));
  REQUIRE(clip->loops.size() == 1);
  CHECK_FALSE(clip->loops[0].count.has_value()); // dwx's 255: forever
  REQUIRE(clip->repeat.has_value());
  const auto repeat = clip->repeat.value_or(dwe::assets::ClipRepeat{});
  CHECK(repeat.start == doctest::Approx(0.05F));
  CHECK(repeat.end == doctest::Approx(0.15F));
  CHECK(repeat.gap == doctest::Approx(0.05F));
  REQUIRE(clip->channels.size() == 2);
  CHECK(clip->channels[0].path == ChannelPath::Translation);
  CHECK(clip->channels[1].path == ChannelPath::Rotation);
  CHECK(clip->channels[1].values[1].y == doctest::Approx(0.7071068F));

  const auto* grow = model->findClip("grow");
  REQUIRE(grow != nullptr);
  CHECK_FALSE(grow->id.has_value());
  CHECK_FALSE(grow->repeat.has_value());
  CHECK(grow->channels[0].interpolation == Interpolation::Step);
  CHECK(model->findClip(7U) == nullptr);
}

TEST_CASE("a glb that breaks the model's rules is refused with the reason") {
  SUBCASE("not a glb") {
    const std::vector<std::byte> bytes(16, std::byte{1});
    CHECK_FALSE(parseModel(bytes).has_value());
  }
  SUBCASE("an index past the vertices") {
    Fixture fixture;
    fixture.indices = {0, 1, 3};
    CHECK_FALSE(parseModel(fixture.glb()).has_value());
  }
  SUBCASE("a joint the skin does not have") {
    Fixture fixture;
    fixture.joints[4] = 2;
    const auto model = parseModel(fixture.glb());
    REQUIRE_FALSE(model.has_value());
    CHECK(model.error().message.find("joint") != std::string::npos);
  }
  SUBCASE("weights that do not add up to 1") {
    Fixture fixture;
    fixture.weights[0] = 0.5F;
    const auto model = parseModel(fixture.glb());
    REQUIRE_FALSE(model.has_value());
    CHECK(model.error().message.find("weights") != std::string::npos);
  }
  SUBCASE("a loop past the clip's ticks") {
    Fixture fixture;
    fixture.clipExtras = R"({"id":2,"ticksPerSecond":20,"tickCount":4,"endsInLoop":true,)"
                         R"("loops":[{"firstTick":1,"lastTick":4,"count":255}]})";
    const auto model = parseModel(fixture.glb());
    REQUIRE_FALSE(model.has_value());
    CHECK(model.error().message.find("loop") != std::string::npos);
  }
  SUBCASE("a loop that runs a number of times") {
    Fixture fixture;
    fixture.clipExtras = R"({"id":2,"ticksPerSecond":20,"tickCount":4,"endsInLoop":false,)"
                         R"("loops":[{"firstTick":0,"lastTick":2,"count":3}]})";
    const auto model = parseModel(fixture.glb());
    REQUIRE_MESSAGE(model.has_value(), (model ? "" : model.error().message));
    const auto* clip = model->findClip(2U);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->loops.size() == 1);
    CHECK(clip->loops[0].firstTick == 0);
    CHECK(clip->loops[0].count == 3U);
  }
  SUBCASE("a sound past the clip's ticks") {
    Fixture fixture;
    fixture.clipExtras = R"({"id":2,"ticksPerSecond":20,"tickCount":4,)"
                         R"("sounds":[{"tick":4,"bank":1,"sound":27}]})";
    const auto model = parseModel(fixture.glb());
    REQUIRE_FALSE(model.has_value());
    CHECK(model.error().message.find("sound") != std::string::npos);
  }
  SUBCASE("an unknown semi-transparency mode") {
    Fixture fixture;
    fixture.materials = R"([{"extras":{"blending":4}}])";
    const auto model = parseModel(fixture.glb());
    REQUIRE_FALSE(model.has_value());
    CHECK(model.error().message.find("blending") != std::string::npos);
  }
  SUBCASE("a CLUT that is not a place in VRAM") {
    Fixture fixture;
    fixture.materials = R"([{"extras":{"clut":[128]}}])";
    const auto model = parseModel(fixture.glb());
    REQUIRE_FALSE(model.has_value());
    CHECK(model.error().message.find("clut") != std::string::npos);
  }
}

TEST_CASE("a clip keeps the sounds its stream plays, in tick order") {
  Fixture fixture;
  fixture.clipExtras =
      R"({"id":2,"ticksPerSecond":20,"tickCount":4,"endsInLoop":true,)"
      R"("loops":[{"firstTick":1,"lastTick":3,"count":255}],)"
      R"("sounds":[{"tick":3,"bank":4,"sound":53},{"tick":2,"bank":1,"sound":27}]})";
  const auto model = parseModel(fixture.glb());
  REQUIRE_MESSAGE(model.has_value(), (model ? "" : model.error().message));
  const auto* clip = model->findClip(2U);
  REQUIRE(clip != nullptr);
  CHECK(clip->sounds == std::vector<dwe::assets::ClipSound>{{.tick = 2, .bank = 1, .sound = 27},
                                                            {.tick = 3, .bank = 4, .sound = 53}});
}

TEST_CASE("a hostile glb cannot make the reader allocate without bound") {
  SUBCASE("indices with no buffer view, four billion of them") {
    Fixture fixture;
    fixture.indicesWithoutView = std::size_t{4'000'000'000};
    const auto model = parseModel(fixture.glb());
    REQUIRE_FALSE(model.has_value());
    CHECK(model.error().message.find("indices") != std::string::npos);
  }
  SUBCASE("indices with no buffer view, a few") {
    Fixture fixture;
    fixture.indicesWithoutView = 3;
    const auto model = parseModel(fixture.glb());
    REQUIRE_FALSE(model.has_value());
    CHECK(model.error().message.find("buffer view") != std::string::npos);
  }
  SUBCASE("positions with no buffer view") {
    Fixture fixture;
    fixture.positionsWithoutView = true;
    const auto model = parseModel(fixture.glb());
    REQUIRE_FALSE(model.has_value());
    CHECK(model.error().message.find("buffer view") != std::string::npos);
  }
  SUBCASE("inverse bind matrices with no buffer view") {
    Fixture fixture;
    fixture.inverseBindWithoutView = true;
    const auto model = parseModel(fixture.glb());
    REQUIRE_FALSE(model.has_value());
    CHECK(model.error().message.find("buffer view") != std::string::npos);
  }
  SUBCASE("more indices than a model may have, across primitives") {
    // Five primitives share one index buffer of 1048575 byte indices: each is within the limit,
    // together they are past it.
    GlbBuilder b;
    const int positions =
        b.addFloats({0, 0, 0, 1, 0, 0, 0, 1, 0}, "VEC3", 3, R"(,"min":[0,0,0],"max":[1,1,0])");
    const std::vector<std::uint8_t> zeros((dwe::assets::MaxModelIndices / 4) - 1, 0);
    const int indices = b.addAccessor(std::span<const std::uint8_t>{zeros},
                                      GlbBuilder::UnsignedByte, "SCALAR", zeros.size());
    std::string primitives;
    for (int i = 0; i < 5; ++i) {
      primitives += std::format(R"({}{{"attributes":{{"POSITION":{}}},"indices":{}}})",
                                i == 0 ? "" : ",", positions, indices);
    }
    const auto model = parseModel(b.build(
        std::format(R"({{"nodes":[{{"mesh":0}}],"meshes":[{{"primitives":[{}]}}]}})", primitives)));
    REQUIRE_FALSE(model.has_value());
    CHECK(model.error().message.find("more than") != std::string::npos);
  }
  SUBCASE("more image pixels than a model may have") {
    // Two images whose headers say 4096x2049: the budget is spent before either is decoded.
    GlbBuilder b;
    const int first = b.addBytes(pngClaiming(4096, 2049));
    const int second = b.addBytes(pngClaiming(4096, 2049));
    const auto model = parseModel(b.build(std::format(
        R"({{"images":[{{"bufferView":{},"mimeType":"image/png"}},{{"bufferView":{},"mimeType":"image/png"}}]}})",
        first, second)));
    REQUIRE_FALSE(model.has_value());
    CHECK(model.error().message.find("pixels") != std::string::npos);
  }
  SUBCASE("a node transform that is not a finite number") {
    GlbBuilder b;
    const auto model = parseModel(b.build(R"({"nodes":[{"scale":[1,1e39,1]}]})"));
    REQUIRE_FALSE(model.has_value());
  }
}

TEST_CASE("loadModel reads a model by asset id") {
  dwe::assets::MemoryFileSystem files;
  files.addFile("base/pack.yaml", std::string_view{"id: base\nversion: 1.0.0\nlayer: base\n"});
  files.addFile("base/digimon/test/model.glb", Fixture{}.glb());
  const std::vector<std::filesystem::path> roots{"base"};
  const auto vfs = dwe::assets::VirtualFileSystem::mount(files, roots);
  REQUIRE(vfs.has_value());
  const auto id = dwe::assets::AssetId::parse("digimon/test/model");
  REQUIRE(id.has_value());
  const auto model = dwe::assets::loadModel(*vfs, *id);
  REQUIRE(model.has_value());
  CHECK(model->clips.size() == 2);
  const auto missing = dwe::assets::AssetId::parse("digimon/none/model");
  CHECK_FALSE(dwe::assets::loadModel(*vfs, *missing).has_value());
}

// Runs only when DWE_CONTENT names a content folder (scripts/dev.sh mounts it); the CI has none.
// Every extracted .glb must load, and the clips as dwx writes them must keep the invariants the
// animation player relies on.
TEST_CASE("the user's disc, extracted: every model loads") {
  const std::optional<std::filesystem::path> mountedContent = dwe::test_support::userContent();
  if (!mountedContent) {
    MESSAGE("DWE_CONTENT is not set: skipping the test on the extracted content");
    return;
  }
  const std::filesystem::path& content = *mountedContent;
  const dwe::assets::DiskFileSystem disk;
  const auto packs = dwe::assets::findPacks(disk, content);
  REQUIRE(packs.has_value());
  const auto opened = dwe::assets::openPacks(disk, packs->packs, dwe::assets::PackOrigin::Engine);
  REQUIRE_MESSAGE(opened.has_value(), (opened ? "" : opened.error().message));
  const auto vfs = dwe::assets::VirtualFileSystem::mount(opened->locations);
  REQUIRE_MESSAGE(vfs.has_value(), (vfs ? "" : vfs.error().message));

  std::size_t models = 0;
  std::size_t clips = 0;
  std::size_t looping = 0;
  for (const auto& id : vfs->ids()) {
    if (vfs->resolve(id)->file.extension() != ".glb") {
      continue;
    }
    const auto model = dwe::assets::loadModel(*vfs, id);
    REQUIRE_MESSAGE(model.has_value(), (model ? "" : model.error().message));
    ++models;
    CHECK_MESSAGE(!model->primitives.empty(), id.str());
    for (const auto& primitive : model->primitives) {
      CHECK(primitive.material < model->materials.size());
    }
    for (const auto& material : model->materials) {
      CHECK((!material.image || *material.image < model->images.size()));
    }
    for (const auto& clip : model->clips) {
      ++clips;
      INFO(id.str(), " ", clip.name);
      CHECK(clip.id.has_value());
      CHECK(clip.ticksPerSecond == 20);
      CHECK(clip.name == std::format("anim_{:02}", clip.id.value_or(0)));
      for (const auto& channel : clip.channels) {
        CHECK(channel.times.size() == clip.tickCount);
        CHECK(channel.node < model->nodes.size());
      }
      if (clip.endsInLoop) {
        ++looping;
        CHECK(clip.repeat.has_value());
      }
    }
  }
  MESSAGE(std::format("{} models, {} clips, {} of them loop forever", models, clips, looping));
  CHECK(models > 0);
  const auto player = dwe::assets::AssetId::parse("digimon/boys/model");
  const auto tamer = dwe::assets::loadModel(*vfs, *player);
  REQUIRE(tamer.has_value());
  for (const std::uint32_t clip : {0U, 2U, 3U}) {
    CHECK(tamer->findClip(clip) != nullptr);
  }
}
