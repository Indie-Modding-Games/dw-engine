#include <dwe/formats/model/animation.hpp>
#include <dwe/formats/model/gltf.hpp>
#include <dwe/formats/model/tmd.hpp>

#include <cgltf.h>
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <memory>
#include <string_view>
#include <vector>

#include "model_fixtures.hpp"

using namespace dwe::formats::model;
using namespace dwe::formats::model::testing;

namespace {

const std::vector<SkeletonBone> sampleSkeleton{{.objIndex = -1, .parentIndex = -1},
                                               {.objIndex = -1, .parentIndex = 0},
                                               {.objIndex = 0, .parentIndex = 1},
                                               {.objIndex = 1, .parentIndex = 2}};

// Stand-ins for the PNGs: the writer embeds them as they are.
constexpr std::array<std::byte, 4> FirstRowImage{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
constexpr std::array<std::byte, 3> SecondRowImage{std::byte{5}, std::byte{6}, std::byte{7}};

ModelTexture sampleTexture() {
    return {.pixelX = 320,
            .pixelY = 0,
            .clutX = 0,
            .clutY = 488,
            .width = 256,
            .height = 128,
            .pngPerPaletteRow = {FirstRowImage, SecondRowImage}};
}

std::vector<Animation> sampleAnimations() {
    const auto withLoop = buildSampleAnimationWithLoop();
    const auto forever = buildSampleAnimationForever();
    auto first = simulateAnimation(withLoop, 0, SampleBoneCount);
    auto second = simulateAnimation(forever, 2, SampleBoneCount);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    return {std::move(*first), std::move(*second)};
}

struct CgltfDeleter {
    void operator()(cgltf_data* data) const { cgltf_free(data); }
};
using ParsedGlb = std::unique_ptr<cgltf_data, CgltfDeleter>;

// Reads a GLB back with cgltf and checks it against the specification's rules that cgltf knows.
ParsedGlb parse(const std::vector<std::byte>& glb) {
    const cgltf_options options{};
    cgltf_data* data = nullptr;
    REQUIRE(cgltf_parse(&options, glb.data(), glb.size(), &data) == cgltf_result_success);
    ParsedGlb parsed(data);
    REQUIRE(cgltf_load_buffers(&options, data, nullptr) == cgltf_result_success);
    REQUIRE(cgltf_validate(data) == cgltf_result_success);
    return parsed;
}

const cgltf_accessor* attribute(const cgltf_primitive& primitive, cgltf_attribute_type type) {
    for (std::size_t i = 0; i < primitive.attributes_count; ++i) {
        if (primitive.attributes[i].type == type) {
            return primitive.attributes[i].data;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("a model is written as a GLB that reads back with its nodes, mesh, skin and animations") {
    const auto tmd = readTmd(buildSampleTmd());
    REQUIRE(tmd.has_value());
    const auto animations = sampleAnimations();
    const auto glb = writeModelGlb(*tmd, sampleSkeleton, animations, sampleTexture());
    REQUIRE(glb.has_value());
    const auto parsed = parse(*glb);
    const cgltf_data& data = *parsed;

    CHECK(data.nodes_count == sampleSkeleton.size() + 1);
    CHECK(std::string_view(data.nodes[2].name) == "bone_02");
    CHECK(data.nodes[2].parent == &data.nodes[1]);
    REQUIRE(data.skins_count == 1);
    CHECK(data.skins[0].joints_count == sampleSkeleton.size());
    CHECK(data.skins[0].skeleton == &data.nodes[0]);
    CHECK(data.skins[0].inverse_bind_matrices->count == sampleSkeleton.size());

    // One primitive per material: textured gouraud with palette row 1, textured flat double-sided with row 0, unlit
    // flat untextured, lit untextured with a colour per vertex. The line is not drawn.
    REQUIRE(data.meshes_count == 1);
    const cgltf_mesh& mesh = data.meshes[0];
    REQUIRE(mesh.primitives_count == 4);
    CHECK(data.materials_count == 4);
    CHECK(data.images_count == 2);
    std::size_t vertices = 0;
    std::size_t indices = 0;
    for (std::size_t i = 0; i < mesh.primitives_count; ++i) {
        const cgltf_primitive& primitive = mesh.primitives[i];
        const cgltf_accessor* position = attribute(primitive, cgltf_attribute_type_position);
        REQUIRE(position != nullptr);
        REQUIRE(position->has_min);
        REQUIRE(position->has_max);
        for (std::size_t v = 0; v < position->count; ++v) {
            std::array<float, 3> value{};
            REQUIRE(cgltf_accessor_read_float(position, v, value.data(), 3));
            for (std::size_t c = 0; c < 3; ++c) {
                CHECK(value[c] >= position->min[c]);
                CHECK(value[c] <= position->max[c]);
            }
        }
        CHECK(attribute(primitive, cgltf_attribute_type_joints)->count == position->count);
        CHECK(attribute(primitive, cgltf_attribute_type_weights)->count == position->count);
        vertices += position->count;
        indices += primitive.indices->count;
    }
    CHECK(vertices == 3 + 4 + 4 + 3);
    CHECK(indices == 3 * (1 + 2 + 2 + 1));

    REQUIRE(data.animations_count == 2);
    const cgltf_animation& first = data.animations[0];
    CHECK(std::string_view(first.name) == "anim_00");
    // Root motion, bone 1's rotation and location, bone 2's scale, bone 3's rotation; the rest keep their pose.
    CHECK(first.channels_count == 5);
    const cgltf_accessor* times = first.samplers[0].input;
    CHECK(times->count == 13);
    CHECK(times->min[0] == 0.0F);
    CHECK(times->max[0] == doctest::Approx(12.0 / 20.0));
    REQUIRE(first.extras.data != nullptr);
    CHECK(std::string_view(first.extras.data).find(R"("loops":[{"firstTick":5,"lastTick":7,"count":2}])") !=
          std::string_view::npos);
    CHECK(std::string_view(first.extras.data).find(R"("sounds":[{"tick":1,"bank":4,"sound":5})") !=
          std::string_view::npos);
    // Every bone that differs from the first animation's pose: bone 1 on location and rotation, bone 2 on scale and
    // location, bone 3 on rotation and location.
    const cgltf_animation& second = data.animations[1];
    CHECK(std::string_view(second.name) == "anim_02");
    CHECK(second.channels_count == 6);
    CHECK(second.samplers[0].input->max[0] == doctest::Approx(3.0 / 20.0));
}

TEST_CASE("without animations the model rests at the bind pose, turned to Y up") {
    const auto tmd = readTmd(buildSampleTmd());
    REQUIRE(tmd.has_value());
    const auto glb = writeModelGlb(*tmd, sampleSkeleton, {}, std::nullopt);
    REQUIRE(glb.has_value());
    const auto parsed = parse(*glb);
    const cgltf_data& data = *parsed;
    CHECK(data.animations_count == 0);
    CHECK(data.images_count == 0);

    std::array<float, 3> min{1e9F, 1e9F, 1e9F};
    std::array<float, 3> max{-1e9F, -1e9F, -1e9F};
    for (std::size_t i = 0; i < data.meshes[0].primitives_count; ++i) {
        const cgltf_primitive& primitive = data.meshes[0].primitives[i];
        CHECK(attribute(primitive, cgltf_attribute_type_texcoord) == nullptr);
        const cgltf_accessor* position = attribute(primitive, cgltf_attribute_type_position);
        for (std::size_t c = 0; c < 3; ++c) {
            min[c] = std::min(min[c], position->min[c]);
            max[c] = std::max(max[c], position->max[c]);
        }
    }
    // Object 0 spans x 0..100 and y -100..0 (down is negative Y on the console), object 1 x and y 0..50.
    CHECK(min == std::array<float, 3>{0.0F, -50.0F, 0.0F});
    CHECK(max == std::array<float, 3>{100.0F, 100.0F, 0.0F});
}

TEST_CASE("writing the same model twice gives the same bytes") {
    const auto tmd = readTmd(buildSampleTmd());
    REQUIRE(tmd.has_value());
    const auto animations = sampleAnimations();
    const auto once = writeModelGlb(*tmd, sampleSkeleton, animations, sampleTexture());
    const auto twice = writeModelGlb(*tmd, sampleSkeleton, animations, sampleTexture());
    REQUIRE(once.has_value());
    REQUIRE(twice.has_value());
    CHECK(*once == *twice);
}

TEST_CASE("a model that does not fit its skeleton, animations or texture is not written") {
    const auto tmd = readTmd(buildSampleTmd());
    REQUIRE(tmd.has_value());
    const auto animations = sampleAnimations();

    auto badSkeleton = sampleSkeleton;
    badSkeleton[3].objIndex = 2;
    CHECK_FALSE(writeModelGlb(*tmd, badSkeleton, {}, std::nullopt).has_value());

    const std::vector<SkeletonBone> shorter(sampleSkeleton.begin(), sampleSkeleton.end() - 1);
    CHECK_FALSE(writeModelGlb(*tmd, shorter, animations, std::nullopt).has_value());

    auto oneRow = sampleTexture();
    oneRow.pngPerPaletteRow.pop_back();
    CHECK_FALSE(writeModelGlb(*tmd, sampleSkeleton, {}, oneRow).has_value());

    auto otherClut = sampleTexture();
    otherClut.clutX = 16;
    CHECK_FALSE(writeModelGlb(*tmd, sampleSkeleton, {}, otherClut).has_value());

    auto noSize = sampleTexture();
    noSize.width = 0;
    CHECK_FALSE(writeModelGlb(*tmd, sampleSkeleton, {}, noSize).has_value());
}
